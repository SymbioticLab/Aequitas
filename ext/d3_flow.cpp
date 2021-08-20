#include "d3_flow.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <assert.h>
#include <iomanip>

#include "../coresim/event.h"
#include "../coresim/flow.h"
#include "../coresim/packet.h"
#include "../coresim/queue.h"
#include "../coresim/topology.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern DCExpParams params;
extern Topology* topology;
extern uint32_t num_outstanding_packets;
extern uint32_t max_outstanding_packets;
extern uint32_t duplicated_packets_received;
extern uint32_t num_early_termination;

D3Flow::D3Flow(uint32_t id, double start_time, uint32_t size, Host *s,
                         Host *d, uint32_t flow_priority)
    : Flow(id, start_time, size, s, d, flow_priority) {
    // D3Flow does not use conventional congestion control; all CC related OPs (inc/dec cwnd; timeout events, etc) are removed
    this->cwnd_mss = params.initial_cwnd;
    this->has_sent_rrq_this_rtt = false;
    this->assigned_base_rate = false;
}

// send out a SYN packet once flow starts; cannot send data pkts until receiving the SYN ACK pkt (with allocated rate)
void D3Flow::start_flow() {
    run_priority = flow_priority;   // necessary to print latencies for all qos levels in the output log
    send_syn_pkt();
}

// once the ACK of the last data pkt is received, send a FIN packet to return the flow's desired & allocated rate to all the D3queues on its path.
// Note we mark flow completion when ACK of the last data pkt is received (as usual) instead of after the FIN packet is processed or the ACK of it is received (and this is no ACK to the FIN pkt)
// like other header-only packet, FIN can not be dropped since its size is set to 0.
void D3Flow::send_fin_pkt() {
    Packet *p = new Fin(
            get_current_time(),
            prev_desired_rate,
            allocated_rate,
            this,
            0,  // made it 0 size so it can't be dropped 
            src,
            dst
            );
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << src->id << "] sends out Fin Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
    }
}

// send out the SYN (first "Rate Request packet" in D3)
// later RRQ pkts are "piggybacked in data pkts" so only need the first one
// send_syn_pkt() is called right after start_flow() so it's going to be ahead of all other data pkts
// SYN, SYN_ACK, FIN, and header-only DATA pkt (in this case, it is treated as a RRQ or Rate Request packet) all has 0 size so they can never be dropped
//// (Revoked) Update: ACK to DATA RRQ also has size 0 and can never be dropped
void D3Flow::send_syn_pkt() {
    double desired_rate = calculate_desired_rate();
    Packet *p = new Syn(
            get_current_time(),
            desired_rate,
            this,
            ////hdr_size,
            0,  // made it 0 size so it can't be dropped 
            src,
            dst
            );
    p->prev_allocated_rate = 0;
    p->prev_desired_rate = 0;
    prev_desired_rate = desired_rate;       // sender host updates past info
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << src->id << "] sends out Syn Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
    }
}

// calculate current desired rate based on remaining flow_size and remaining deadline
// rate is in the unit of 'bps'
// non-deadline flows return rate = 0
double D3Flow::calculate_desired_rate() {
    double curr_desired_rate = 0;
    if (has_ddl) {
        if (get_remaining_deadline() <= 0) {
            curr_desired_rate = 0;  // give up desired_rate if deadline has somehow already been missed (not very likley based on our setting)
        } else {
            curr_desired_rate = get_remaining_flow_size() * 8.0 / get_remaining_deadline(); // TODO: make this formula better
            //std::cout << "remaining flow size = " << get_remaining_flow_size() << "; remianing deadline = " << get_remaining_deadline() << std::endl;
            //std::cout << "curr_desired_rate = " << curr_desired_rate << std::endl;
        }
    }   // non-deadline flows require no desired_rate (desired_rate=0)
    return curr_desired_rate;
}

void D3Flow::send_next_pkt() {
    if (terminated) {
        return;
    }
    /*
    if (params.early_termination && has_ddl) {
        // D3's flow quenching check (D3 paper Section 6.1.3)
        // (1) desired_rate exceeds uplink capacity (2) the deadline has already expired
        if (calculate_desired_rate() > params.bandwidth || get_remaining_deadline() < 0) {
            terminated = true;
            num_early_termination++;
            send_fin_pkt();
            if (params.debug_event_info) {
                std::cout << "Terminate Flow[" << id << "]: desired_rate = " << calculate_desired_rate() / 1e9 << "; remaining_deadline = " << get_remaining_deadline() << std::endl;
            }
            return;
        }
    }
    */

    // if we have already sent the only header-only RRQ during this RTT, return early and don't bother with sending anything else
    if (assigned_base_rate && has_sent_rrq_this_rtt) {
        return;
    }

    // send pkt one at a time with allocated_rate
    uint64_t seq = next_seq_no;
    uint32_t window = cwnd_mss * mss;       // cwnd_mss is fixed at params.init_cwnd; its value is actually DC as long as it's >= 1
    if (
        (seq + mss <= last_unacked_seq + window) &&
        ((seq + mss <= size) || (seq != size && (size - seq < mss)))
    ) {
        Packet *p = send_with_delay(seq, 1e-12);    // actually the tiny delay is not needed here since pkts are sent one at a time; let's just stay consistent

        if (p->size != 0) {     // Only update seq_no if we sent a normal data pkt, not a header-only RRQ (with base rate)
            if (seq + mss < size) {
                next_seq_no = seq + mss;
                seq += mss;
            } else {
                next_seq_no = size;
                seq = size;
            }
        }

        //if (retx_event == NULL) {
        //    set_timeout(get_current_time() + retx_timeout);
        //}
    }
}

void D3Flow::send_pending_data() {
    assert(false);    
}

uint32_t D3Flow::send_pkts() {
    assert(false);
}

Packet *D3Flow::send_with_delay(uint64_t seq, double delay) {
    Packet *p = NULL;
    uint32_t pkt_size;
    if (seq + mss > this->size) {
        pkt_size = this->size - seq + hdr_size;
    } else {
        pkt_size = mss + hdr_size;
    }

    // if assigned base rate, only send out a header-only RRQ during this RTT
    if (assigned_base_rate) {
        pkt_size = 0;                   // remove payload
        cancel_rate_limit_event();      // since now we are not allowed to send more data pkts during this RTT
    }

    p = new Packet(
            get_current_time(),
            this,
            seq,
            flow_priority,
            pkt_size,
            src,
            dst
            );
    this->total_pkt_sent++;
    p->start_ts = get_current_time();

    // piggyback RRQ info in the first data pkt in current RTT
    // Note at this time we have already received the SYN_ACK packet
    if (!has_sent_rrq_this_rtt) {
        has_sent_rrq_this_rtt = true;
        p->data_pkt_with_rrq = true;
        p->has_rrq = true;
        p->prev_desired_rate = prev_desired_rate;
        p->prev_allocated_rate = allocated_rate;
        double desired_rate = calculate_desired_rate();     // calculate desired rate at the beginning of every RTT
        p->desired_rate = desired_rate;     
        prev_desired_rate = desired_rate;       // sender host updates past info
    }

    Queue *next_hop = topology->get_next_hop(p, src->queue);
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + delay, p, next_hop));      // adding a pd since we skip the source queue
    if (!assigned_base_rate) {
        double td = pkt_size * 8.0 / allocated_rate;    // rate limited with the rate assigned by the routers
        RateLimitingEvent *event = new RateLimitingEvent(get_current_time() + td, this);
        add_to_event_queue(event);
        rate_limit_event = event;
    }

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        if (assigned_base_rate) {
            std::cout << "Host[" << src->id << "] sends out header-only RRQ Packet[" << p->unique_id << "] from flow[" << id << "] at time: " << get_current_time() + delay << std::endl;
        } else {
            std::cout << "Host[" << src->id << "] sends out Packet[" << p->unique_id << "] from flow[" << id << "] at time: " << get_current_time() + delay << std::endl;
        }
    }
    return p;
}

void D3Flow::receive(Packet *p) {
    // call receive_fin_pkt() early since at this point flow->finished has been marked true
    if (p->type == FIN_PACKET) {
        receive_fin_pkt(p);
    }
    if (finished || terminated) {
        delete p;
        return;
    }

    if (p->type == ACK_PACKET) {
        Ack *a = dynamic_cast<Ack *>(p);
        receive_ack_d3(a, a->seq_no, a->sack_list);
    }
    else if(p->type == NORMAL_PACKET) {
        receive_data_pkt(p);
    } else if (p->type == SYN_PACKET) {
        receive_syn_pkt(p);
    } else if (p->type == SYN_ACK_PACKET) {
        receive_syn_ack_pkt(p);
    } else {
        assert(false);
    }

    delete p;
}

void D3Flow::receive_data_pkt(Packet* p) {
    received_count++;
    total_queuing_time += p->total_queuing_delay;
    if (last_data_pkt_receive_time != 0) {
        double inter_pkt_spacing = get_current_time() - last_data_pkt_receive_time;
        total_inter_pkt_spacing += inter_pkt_spacing;
    }
    last_data_pkt_receive_time = get_current_time();

    if (p->has_rrq && p->size == 0) {  // for header-only data pkt whose payload is removed
        if (num_outstanding_packets > 0) {
            num_outstanding_packets--;
        }
        // Since we don't update 'max_seq_no_recv' here, 'recv_till' won't change.
        // so calling send_ack_d3() will just ask for the correct missing data pkt
    } else {
        if (received.count(p->seq_no) == 0) {
            received[p->seq_no] = true;
            if (num_outstanding_packets >= ((p->size - hdr_size) / (mss)))
                num_outstanding_packets -= ((p->size - hdr_size) / (mss));
            else
                num_outstanding_packets = 0;
            received_bytes += (p->size - hdr_size);
        } else {
            duplicated_packets_received += 1;
        }
        if (p->seq_no > max_seq_no_recv) {
            max_seq_no_recv = p->seq_no;
        }
    }
    // Determing which ack to send
    uint64_t s = recv_till;
    bool in_sequence = true;
    std::vector<uint64_t> sack_list;
    while (s <= max_seq_no_recv) {
        if (received.count(s) > 0) {
            if (in_sequence) {
                if (recv_till + mss > this->size) {
                    recv_till = this->size;
                } else {
                    recv_till += mss;
                }
            } else {
                sack_list.push_back(s);
            }
        } else {
            in_sequence = false;
        }
        s += mss;
    }

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Flow[" << id << "] receive_data_pkt: max_seq_no_recv = " << max_seq_no_recv << "; data pkt seq no = " << p->seq_no
            << "; recv_till (seq for next ACK) = " << recv_till << std::endl;
    }
    //std::cout << "Flow[" << id << "] receive_data_pkt: received_count = " << received_count << "; received_bytes = " << received_bytes << std::endl;
    send_ack_d3(recv_till, sack_list, p->start_ts, p); // Cumulative Ack
}

void D3Flow::receive_syn_pkt(Packet *syn_pkt) {
    // basically calling send_ack_d3(), but send a SynAck pkt instead of a normal Ack.
    ////Packet *p = new SynAck(this, 0, hdr_size, dst, src);  //Acks are dst->src
    Packet *p = new SynAck(this, 0, 0, dst, src);  //Acks are dst->src; made its size=0
    p->allocated_rate = *std::min_element(syn_pkt->curr_rates_per_hop.begin(), syn_pkt->curr_rates_per_hop.end());
    p->marked_base_rate = syn_pkt->marked_base_rate;

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << std::setprecision(2) << std::fixed;
        std::cout << "Receiving SYN packet["<< syn_pkt->unique_id << "] from Flow[" << id << "]. allocated rate assigned = " << p->allocated_rate / 1e9;
        if (p->marked_base_rate) {
            std::cout << " (marked with base rate)";
        }
        std::cout << "; sending out SYN_ACK packet[" << p->unique_id << "]" << std::endl;
        std::cout << "curr_rates_per_hop:";
        for (const auto &x: syn_pkt->curr_rates_per_hop) {
            std::cout << x / 1e9 << " ";
        }
        std::cout << std::endl;
        std::cout << std::setprecision(15) << std::fixed;
    }

    p->pf_priority = 0; // DC; D3 does not have notion of priority.
    Queue *next_hop = topology->get_next_hop(p, dst->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // skip src queue, include dst queue; add a pd delay
    add_to_event_queue(event);
}

void D3Flow::receive_syn_ack_pkt(Packet *p) {
    // update assigned rate for the current RTT
    allocated_rate = p->allocated_rate;     // rate assigned by router from last RTT
    if (p->marked_base_rate) {
        assigned_base_rate = true;
    } else {
        assigned_base_rate = false;
    }
    send_next_pkt();
}

void D3Flow::receive_fin_pkt(Packet *p) {
    assert(finished || terminated);
    // nothing to do here. 'num_active_flows' are decremented when FIN packet traverse thru the network
}

// D3's version of send_ack(), which takes an addition input parameter (data_pkt) to send the allocated_rate back to the source via ACK pkt
// Note the original paper sends back the entire allocation vector. Here I simply calculate the min rate of the vector first at the receiver,
// and send it back directly to the sender
void D3Flow::send_ack_d3(uint64_t seq, std::vector<uint64_t> sack_list, double pkt_start_ts, Packet *data_pkt) {
    Packet *p = new Ack(this, seq, sack_list, hdr_size, dst, src);  //Acks are dst->src
    if (data_pkt->data_pkt_with_rrq) {
        // log the min of all allocated rates in this RTT and send back via ACK pkt
        p->allocated_rate = *std::min_element(data_pkt->curr_rates_per_hop.begin(), data_pkt->curr_rates_per_hop.end());
        p->ack_pkt_with_rrq = true;
        p->marked_base_rate = data_pkt->marked_base_rate;
        ////p->size = 0;        // make this type of ACK not able to be dropped
        if (data_pkt->size == 0) {  // this implies it is a "header-only" packet
            p->ack_to_rrq_no_payload = true;
        }

        if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
            std::cout << std::setprecision(2) << std::fixed;
            std::cout << "Received DATA packet["<< data_pkt->unique_id << "] with RRQ from Flow[" << id << "]. allocated rate assigned = " << p->allocated_rate / 1e9;
            if (p->marked_base_rate) {
                std::cout << " (marked with base rate)";
            }
            std::cout << "; sending out ACK packet[" << p->unique_id << "]" << std::endl;
            std::cout << "curr_rates_per_hop:";
            for (const auto &x: data_pkt->curr_rates_per_hop) {
                std::cout << x / 1e9 << " ";
            }
            std::cout << std::endl;
            std::cout << std::setprecision(15) << std::fixed;
        }
    }

    p->pf_priority = 0; // D3 does not have notion of priority. so whatever
    p->start_ts = pkt_start_ts; // carry the orig packet's start_ts back to the sender for RTT measurement
    //std::cout << "Flow[" << id << "] with priority " << run_priority << " send ack: " << seq << std::endl;
    Queue *next_hop = topology->get_next_hop(p, dst->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // skip src queue, include dst queue; add a pd delay
    ////PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, dst->queue);  // orig version (include src queue, skip dst queue)
    add_to_event_queue(event);
}

void D3Flow::receive_ack_d3(Ack *ack_pkt, uint64_t ack, std::vector<uint64_t> sack_list) {
    // update current assigned rate if received the ACK to the data pkt with RRQ (piggybacked in the first data pkt of every RTT)
    if (ack_pkt->ack_pkt_with_rrq) {
        // receive the allocated rate to use for the current RTT at the sender
        allocated_rate = ack_pkt->allocated_rate;   // rate assigned by router from last RTT
        has_sent_rrq_this_rtt = false;      
        if (ack_pkt->marked_base_rate) {
            assigned_base_rate = true;
        } else {
            assigned_base_rate = false;
        }
        if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
            std::cout << std::setprecision(2) << std::fixed;
            std::cout << "Flow[" << id << "] at Host[" << src->id << "] received ACK packet[" << ack_pkt->unique_id << "] with RRQ. allocated rate = " << allocated_rate /1e9 << std::endl;
            std::cout << std::setprecision(15) << std::fixed;
        }
        if (params.early_termination && has_ddl) {
            // D3's flow quenching check (D3 paper Section 6.1.3)
            // (1) desired_rate exceeds uplink capacity (2) the deadline has already expired
            if (calculate_desired_rate() > params.bandwidth || get_remaining_deadline() < 0) {
                terminated = true;
                num_early_termination++;
                send_fin_pkt();
                if (params.debug_event_info) {
                    std::cout << "Terminate Flow[" << id << "]: desired_rate = " << calculate_desired_rate() / 1e9 << "; remaining_deadline = " << get_remaining_deadline() << std::endl;
                }
                return;
            }
        }
    }

    // On timeouts; next_seq_no is updated to the last_unacked_seq;
    // In such cases, the ack can be greater than next_seq_no; update it
    if (next_seq_no < ack) {
        next_seq_no = ack;
    }

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Flow[" << id << "] at Host[" << src->id << "] received ACK packet[" << ack_pkt->unique_id
            << "]; ack = " << ack << ", next_seq_no = " << next_seq_no << ", last_unacked_seq = " << last_unacked_seq << std::endl;
    }

    // New ack!
    if (ack > last_unacked_seq) {
        // Update the last unacked seq
        last_unacked_seq = ack;

        //increase_cwnd();  // Don't involve normal CC behavior

        // Send the remaining data
        send_next_pkt();

        // Update the retx timer
        /*
        if (retx_event != nullptr) { // Try to move
            cancel_retx_event();
            if (last_unacked_seq < size) {
                // Move the timeout to last_unacked_seq
                double timeout = get_current_time() + retx_timeout;
                set_timeout(timeout);
            }
        }
        */
    } else if (ack == last_unacked_seq && ack_pkt->ack_to_rrq_no_payload) {  // in D3 : when the data pkt's payload gets removed, we need to resend the last packet
        if (params.enable_flow_lookup && params.flow_lookup_id == id) {
            std::cout << "Ready to resend last data pkt: ack = " << ack << "; last_unacked_seq = " << last_unacked_seq << "; size = " << size << std::endl;
        }
        send_next_pkt();
    }

    // Yiwen: since we don't implement FIN packet for other work, I will still use the last DATA ACK to mark the completion of a flow for D3 as a courtesy
    // Here the FIN pkt can never be dropped so its rate return will always be done.
    if (ack == size && !finished) {
        finished = true;
        received.clear();
        finish_time = get_current_time();
        flow_completion_time = finish_time - start_time;
        FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
        add_to_event_queue(ev);

        // cancel current RateLimitingEvent if there is any
        cancel_rate_limit_event();

        // send out a FIN pkt to return the desired and allocated rate of this flow during the last RTT to the routers(queues) on its path
        send_fin_pkt();
    }
}

void D3Flow::set_timeout(double time) {
    assert(false);
    if (last_unacked_seq < size) {
        RetxTimeoutEvent *ev = new RetxTimeoutEvent(time, this);
        add_to_event_queue(ev);
        retx_event = ev;
    }
}

void D3Flow::handle_timeout() {
    assert(false);
    next_seq_no = last_unacked_seq;
    send_next_pkt();
    set_timeout(get_current_time() + retx_timeout);
}
