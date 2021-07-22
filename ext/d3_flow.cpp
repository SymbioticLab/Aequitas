#include "d3_flow.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <assert.h>

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

D3Flow::D3Flow(uint32_t id, double start_time, uint32_t size, Host *s,
                         Host *d, uint32_t flow_priority)
    : Flow(id, start_time, size, s, d, flow_priority) {
    // D3Flow does not use conventional congestion control; all CC related OPs (inc/dec cwnd; timeout events, etc) are removed
    this->cwnd_mss = params.max_cwnd;
}

void D3Flow::start_flow() {
    run_priority = flow_priority;
    send_syn_pkt();
    //send_pending_data();      // can't send data pkts yet; only after received the syn_ack pkt
}

// TODO: for D3Flow, make syn pkt not counting hdr_size (or maybe not; tes
// send out the SYN (first "Rate Request packet" in D3)
// later RRQ pkts are "piggybacked in data pkts" so only need the first one
// send_syn_pkt() is called right after start_flow() so it's going to be ahead of all other data pkts
// SYN, SYN_ACK, FIN, and header-only DATA pkt (in this case, it is treated as a RRQ or Rate Request packet) all has 0 size so they can never be dropped
void D3Flow::send_fin_pkt() {
    Packet *p = new Fin(
            get_current_time(),
            prev_desired_rate,
            prev_allocated_rate,
            this,
            0,  // made it 0 size so it can't be dropped 
            src,
            dst
            );
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // adding a pd since we skip the source queue
    //PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, src->queue);
    add_to_event_queue(event);
    if (params.debug_event_info) {
        std::cout << "Host[" << src->id << "] sends out Fin Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
    }
}

// once the ACK of the last data pkt is received, send a FIN packet to return the flow's desired & allocated rate to all the D3queues on its path.
// Note we mark flow completion when ACK of the last data pkt is received (as usual) instead of after the FIN packet is processed or the ACK of it is received (and this is no ACK to the FIN pkt)
// like other header-only packet, FIN can not be dropped since its size is set to 0.
void D3Flow::send_syn_pkt() {
    Packet *p = new Syn(
            get_current_time(),
            calculate_desired_rate(),
            this,
            ////hdr_size,
            0,  // made it 0 size so it can't be dropped 
            src,
            dst
            );
    // at this point, prev_desired_rate & prev_allocated_rate should both be 0
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // adding a pd since we skip the source queue
    //PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, src->queue);
    add_to_event_queue(event);
    if (params.debug_event_info) {
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

// Flow::send_pending_data()
void D3Flow::send_pending_data() {
    uint32_t pkts_sent = 0;
    uint64_t seq = next_seq_no;
    uint32_t window = cwnd_mss * mss + scoreboard_sack_bytes;

    while (
        (seq + mss <= last_unacked_seq + window) &&
        ((seq + mss <= size) || (seq != size && (size - seq < mss)))
    ) {
        send_with_delay(seq, 1e-9 * (pkts_sent + 1));    // inc by 1 ns each pkt
        pkts_sent++;

        if (seq + mss < size) {
            next_seq_no = seq + mss;
            seq += mss;
        } else {
            next_seq_no = size;
            seq = size;
        }

    }
    if (params.debug_event_info) {
        std::cout << "Flow[" << id << "] sends " << pkts_sent << " pkts." << std::endl;
    }
}

uint32_t D3Flow::send_pkts() {
    std::cout << "D3Flow::send_pkts() should be never called."
        << std::endl;
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

    p->prev_desired_rate = prev_desired_rate;
    p->prev_allocated_rate = prev_allocated_rate;
    p->desired_rate = calculate_desired_rate();

    Queue *next_hop = topology->get_next_hop(p, src->queue);
    if (params.debug_event_info) {
        std::cout << "Host[" << src->id << "] sends out Packet[" << p->unique_id << "] from flow[" << id << "] at time: " << get_current_time() + delay << " (base=" << get_current_time() << "; delay=" << delay << ")" << std::endl;
    }
    //PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + delay, p, next_hop);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);

    return p;
}

void D3Flow::receive(Packet *p) {
    if (finished) {
        delete p;
        return;
    }

    if (p->type == ACK_PACKET || p->type == SYN_ACK_PACKET) {
        prev_desired_rate = p->desired_rate;
        prev_allocated_rate = p->prev_allocated_rate;
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
    } else if (p->type == FIN_PACKET) {
        receive_fin_pkt(p);
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

    if (received.count(p->seq_no) == 0) {
        received[p->seq_no] = true;
        if(num_outstanding_packets >= ((p->size - hdr_size) / (mss)))
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

    //std::cout << "Flow[" << id << "] receive_data_pkt: received_count = " << received_count << "; received_bytes = " << received_bytes << std::endl;
    send_ack_d3(recv_till, sack_list, p->start_ts, p); // Cumulative Ack
}

void D3Flow::receive_syn_pkt(Packet *syn_pkt) {
    // basically calling send_ack_d3(), but send a SynAck pkt instead of a normal Ack.
    ////Packet *p = new SynAck(this, 0, hdr_size, dst, src);  //Acks are dst->src
    std::cout << "PUPU receiving syn pkt" << std::endl;
    Packet *p = new SynAck(this, 0, 0, dst, src);  //Acks are dst->src; made its size=0
    p->prev_allocated_rate = *std::min_element(syn_pkt->curr_rates_per_hop.begin(), syn_pkt->curr_rates_per_hop.end());
    p->desired_rate = syn_pkt->desired_rate;

    p->pf_priority = 0; // D3 does not have notion of priority. so whatever
    Queue *next_hop = topology->get_next_hop(p, dst->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // skip src queue, include dst queue; add a pd delay
    add_to_event_queue(event);
}

void D3Flow::receive_syn_ack_pkt(Packet *p) {
    send_pending_data();
}

void D3Flow::receive_fin_pkt(Packet *p) {
    // nothing to do here since we decide to mark flow completion early at the receive of the last data ACK
    assert(finished);
}

// D3's version of send_ack(), which takes an addition input parameter (data_pkt) to send the allocated_rate back to the source via ACK pkt
void D3Flow::send_ack_d3(uint64_t seq, std::vector<uint64_t> sack_list, double pkt_start_ts, Packet *data_pkt) {
    // log the min of all allocated rates in this RTT and send back via ACK pkt
    Packet *p = new Ack(this, seq, sack_list, hdr_size, dst, src);  //Acks are dst->src
    p->prev_allocated_rate = *std::min_element(data_pkt->curr_rates_per_hop.begin(), data_pkt->curr_rates_per_hop.end());
    p->desired_rate = data_pkt->desired_rate;

    p->pf_priority = 0; // D3 does not have notion of priority. so whatever
    p->start_ts = pkt_start_ts; // carry the orig packet's start_ts back to the sender for RTT measurement
    //std::cout << "Flow[" << id << "] with priority " << run_priority << " send ack: " << seq << std::endl;
    Queue *next_hop = topology->get_next_hop(p, dst->queue);
    ////PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, next_hop);  // skip src queue, include dst queue
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // skip src queue, include dst queue; add a pd delay
    ////PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, dst->queue);  // orig version (include src queue, skip dst queue)
    add_to_event_queue(event);
}


// pass in extra ACK pkt pointer to be able to decrement num_active_flows
void D3Flow::receive_ack_d3(Ack *ack_pkt, uint64_t ack, std::vector<uint64_t> sack_list) {
    //this->scoreboard_sack_bytes = sack_list.size() * mss;
    this->scoreboard_sack_bytes = 0;  // sack_list is non-empty. Manually set sack_bytes to 0 for now since we don't support SACK yet.

    // On timeouts; next_seq_no is updated to the last_unacked_seq;
    // In such cases, the ack can be greater than next_seq_no; update it
    if (next_seq_no < ack) {
        next_seq_no = ack;
    }

    // New ack!
    if (ack > last_unacked_seq) {
        // Update the last unacked seq
        last_unacked_seq = ack;

        //increase_cwnd();  // Don't involve normal CC behavior

        // Send the remaining data
        send_pending_data();

        /*  no timeout events
        // Update the retx timer
        if (retx_event != nullptr) { // Try to move
            cancel_retx_event();
            if (last_unacked_seq < size) {
                // Move the timeout to last_unacked_seq
                double timeout = get_current_time() + retx_timeout;
                set_timeout(timeout);
            }
        }
        */
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

        // decrement num_active_flows
        for (int i = 0; i < ack_pkt->traversed_queues.size(); i++) {
            ack_pkt->traversed_queues[i]->num_active_flows--;
        }

        // send out a FIN pkt to return the desired and allocated rate of this flow during the last RTT to the routers(queues) on its path
        send_fin_pkt();
    }
}

