#include "pdq_flow.h"

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

PDQFlow::PDQFlow(uint32_t id, double start_time, uint32_t size, Host *s,
                         Host *d, uint32_t flow_priority)
    : Flow(id, start_time, size, s, d, flow_priority) {
    // PDQFlow does not use conventional congestion control; all CC related OPs (inc/dec cwnd; timeout events, etc) are removed
    this->cwnd_mss = params.initial_cwnd;
    ////this->has_sent_probe_this_rtt = false;
    //this->paused = false;
    this->has_sent_probe = false;
    this->time_sent_last_probe = params.first_flow_start_time;
    this->pause_sw_id = 0;
    this->measured_rtt = 0;
    this->inter_probing_time = 0;
    this->probing_event = NULL;
}

// Since PDQ will still send out a probe pkt (its Rs is initalized to 0) after receiving the SYN_ACK pkt,
// we will skip the syn pkt (SYN in PDQ doesn't ask for rate) and directly send out the first probe pkt.
// Otherwise this is an unfair comparison with D3 especially when there are a lot of short flows in the workload.
void PDQFlow::start_flow() {
    run_priority = flow_priority;   // necessary to print latencies for all qos levels in the output log
    if (!this->has_ddl) {
        assert(this->deadline == 0);        // we don't check in constructor b/c by then the ddl has not been set yet
    }
    // also set deadline in sw flow state
    sw_flow_state.deadline = deadline;
    
    send_syn_pkt();
}

// When allocated_rate becomes 0 (i.e., the flow is paused by the switch), PDQ sends out a PROBE packet every Is (inter-probing time) RTTs to request new rate info;
// A PROBE packet is a DATA packet with no payload.
Packet *PDQFlow::send_probe_pkt() {
    ////assert(allocated_rate == 0 && !has_sent_probe_this_rtt);
    ////has_sent_probe_this_rtt = true;
    has_sent_probe = true;
    time_sent_last_probe = get_current_time();
    assert(allocated_rate == 0);

    Packet *p = new Packet(
            get_current_time(),
            this,
            next_seq_no,
            flow_priority,
            0,
            src,
            dst
            );
    this->total_pkt_sent++;
    p->start_ts = get_current_time();
    
    // assign scheduling header
    p->is_probe = true;
    p->allocated_rate = params.bandwidth;   // on packet departure, rate in the schuduling header is always set to max sending rate (PDQ paper S3.1)
    p->deadline = deadline;
    p->expected_trans_time = get_expected_trans_time();
    p->inter_probing_time = inter_probing_time;
    p->measured_rtt = measured_rtt;

    Queue *next_hop = topology->get_next_hop(p, src->queue);
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop));      // adding a pd since we skip the source queue

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << src->id << "] sends out Probe Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
        std::cout << "PUPU measured RTT = " << p->measured_rtt * 1e6 << " us" << std::endl;
    }
    return p;
}

void PDQFlow::send_syn_pkt() {
    Packet *p = new Syn(
            get_current_time(),
            0,
            this,
            0,
            src,
            dst
            );
    p->start_ts = get_current_time();

    Queue *next_hop = topology->get_next_hop(p, src->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << src->id << "] sends out Syn Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
    }
}

// PDQ sends an FIN packet when flow is finished the same way we did in D3.
void PDQFlow::send_fin_pkt() {
    Packet *p = new Fin(
            get_current_time(),
            0,
            0,
            this,
            0,
            src,
            dst
            );
    p->start_ts = get_current_time();

    Queue *next_hop = topology->get_next_hop(p, src->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << src->id << "] sends out Fin Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
    }
}

void PDQFlow::send_next_pkt() {
    if (terminated) {
        return;
    }
    if (params.early_termination && has_ddl) {      // early termination terminates flows that cannot meet deadlines; it should not apply to non-ddl flows
        // Early Termination (PDQ paper S3.1)
        // (1) The deadline is past
        // (2) The remaining flow transmission time is larger than the time to deadline
        // (3) The flow is paused, and the time to deadline is smaller an RTT
        if (get_current_time() > start_time + deadline / 1e6 ||
            get_expected_trans_time() > get_remaining_deadline() ||
            (allocated_rate == 0 && get_remaining_deadline() < measured_rtt)) {
            terminated = true;
            num_early_termination++;
            send_fin_pkt();
            return;
        }
    }

    ////// if we have already sent the PROBE packet during this RTT, return early and don't bother with sending another one
    ////if (allocated_rate == 0 && has_sent_probe_this_rtt) {
    ////    return;
    ////}
    if (allocated_rate == 0) { 
        if (has_sent_probe || probing_event) {   // if we have already sent a probe (or scheduled a probe), return early
            return;             // we can do this because probe pkts and their acks never get dropped
        } else {
            if (get_current_time() - time_sent_last_probe < inter_probing_time * measured_rtt) {
                // send out a probe in the future
                PDQProbingEvent *event = new PDQProbingEvent(time_sent_last_probe + inter_probing_time * measured_rtt, this);
                add_to_event_queue(event);
                probing_event = event;
                return;
            }   // else move on, and we will directly send out a probe when reaching 'send_with_delay()'
        }
    }

    // send pkt one at a time with allocated_rate
    uint64_t seq = next_seq_no;
    uint32_t window = cwnd_mss * mss;       // cwnd_mss is fixed at params.init_cwnd; its value is actually DC as long as it's >= 1
    if (
        (seq + mss <= last_unacked_seq + window) &&
        ((seq + mss <= size) || (seq != size && (size - seq < mss)))
    ) {
        Packet *p = send_with_delay(seq, 1e-12);    // actually the tiny delay is not needed here since pkts are sent one at a time; let's just stay consistent

        if (!p->is_probe) {     // Only update seq_no if we sent a normal data pkt, not a probe packet
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

Packet *PDQFlow::send_with_delay(uint64_t seq, double delay) {
    // if assigned rate is zero (paused), send out a probe packet and return
    if (allocated_rate == 0) {
        cancel_rate_limit_event();      // since now we are not allowed to send more data pkts during this RTT
        return send_probe_pkt();
    }

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

    // assign scheduling header
    p->is_probe = false;
    p->allocated_rate = params.bandwidth;   // on packet departure, rate in the schuduling header is always set to max sending rate (PDQ paper S3.1)
    p->deadline = deadline;
    p->expected_trans_time = get_expected_trans_time();
    p->inter_probing_time = inter_probing_time;
    p->measured_rtt = measured_rtt;

    Queue *next_hop = topology->get_next_hop(p, src->queue);
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + delay, p, next_hop));      // adding a pd since we skip the source queue
    double td = pkt_size * 8.0 / allocated_rate;    // rate limited with the rate assigned by the routers
    RateLimitingEvent *event = new RateLimitingEvent(get_current_time() + td, this);
    add_to_event_queue(event);
    rate_limit_event = event;

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << src->id << "] sends out Packet[" << p->unique_id << "] from flow[" << id << "] at time: " << get_current_time() + delay << std::endl;
        std::cout << "PUPU measured RTT = " << p->measured_rtt * 1e6 << " us" << std::endl;
    }
    return p;
}

void PDQFlow::send_pending_data() {
    assert(false);    
}

uint32_t PDQFlow::send_pkts() {
    assert(false);
}

void PDQFlow::send_ack_pdq(uint64_t seq, std::vector<uint64_t> sack_list, double pkt_start_ts, Packet *data_pkt) {
    Packet *p = new Ack(this, seq, sack_list, hdr_size, dst, src);  //Acks are dst->src

    // receive copies the scueduling header from each data pkt to its corresponding ACK
    // in PDQ, each data pkt carries scheduling header, so we should perform the copying every time
    p->allocated_rate = data_pkt->allocated_rate;
    p->paused = data_pkt->paused;
    p->pause_sw_id = data_pkt->pause_sw_id;
    p->deadline = data_pkt->deadline;
    p->expected_trans_time = data_pkt->expected_trans_time;
    p->inter_probing_time = data_pkt->inter_probing_time;
    if (data_pkt->is_probe) {
        p->ack_to_probe = true;
    }

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << std::setprecision(2) << std::fixed;
        std::cout << "Received DATA packet["<< data_pkt->unique_id << "] from Flow[" << id << "]. allocated rate assigned = " << p->allocated_rate / 1e9;
        if (data_pkt->paused) {
            std::cout << " (paused by switch[" << data_pkt->pause_sw_id << "])";
        }
        std::cout << "; sending out ACK packet[" << p->unique_id << "]" << std::endl;
        std::cout << std::setprecision(15) << std::fixed;
    }

    p->pf_priority = 0; // DC
    p->start_ts = pkt_start_ts; // carry the orig packet's start_ts back to the sender for RTT measurement
    //std::cout << "Flow[" << id << "] with priority " << run_priority << " send ack: " << seq << std::endl;
    Queue *next_hop = topology->get_next_hop(p, dst->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // skip src queue, include dst queue; add a pd delay
    ////PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, dst->queue);  // orig version (include src queue, skip dst queue)
    add_to_event_queue(event);
}

void PDQFlow::receive(Packet *p) {
    // call receive_fin_pkt() early since at this point flow->finished has been marked true
    if (p->type == FIN_PACKET) {
        receive_fin_pkt(p);
    }
    if (finished || terminated) {
        delete p;
        return;
    }

    // update sender info based on receiving an acknowledgement pkt
    // note the syn_ack packet only brings back the rtt info in PDQ
    if (p->type == ACK_PACKET || p->type == SYN_ACK_PACKET) {
        measured_rtt = get_current_time() - p->start_ts;
        allocated_rate = p->allocated_rate;
        pause_sw_id = p->pause_sw_id;
        inter_probing_time = p->inter_probing_time;
        // 'expected_trans_time' is obtained by calling get_expected_trans_time() anytime it's needed
        if (p->type == ACK_PACKET && (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id))) {
            std::cout << std::setprecision(2) << std::fixed;
            std::cout << "Flow[" << id << "] at Host[" << src->id << "] received ACK packet[" << p->unique_id << "]. allocated rate = " << p->allocated_rate /1e9 << std::endl;
            std::cout << std::setprecision(15) << std::fixed;
        }
    }

    if (p->type == ACK_PACKET) {
        Ack *a = dynamic_cast<Ack *>(p);
        receive_ack_pdq(a, a->seq_no, a->sack_list);
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

void PDQFlow::receive_syn_pkt(Packet *syn_pkt) {
    Packet *p = new SynAck(this, 0, 0, dst, src);  //Acks are dst->src; made its size=0
    p->marked_base_rate = syn_pkt->marked_base_rate;
    p->start_ts = syn_pkt->start_ts;     // record syn pkt's start time so the sender can calculate RTT when receiving this syn ack

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Receiving SYN packet["<< syn_pkt->unique_id << "] from Flow[" << id << "]; sending out SYN_ACK packet[" << p->unique_id << "]" << std::endl;
    }

    Queue *next_hop = topology->get_next_hop(p, dst->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // skip src queue, include dst queue; add a pd delay
    add_to_event_queue(event);
}

void PDQFlow::receive_syn_ack_pkt(Packet *p) {
    send_next_pkt();
}

void PDQFlow::receive_data_pkt(Packet* p) {
    received_count++;
    total_queuing_time += p->total_queuing_delay;
    if (last_data_pkt_receive_time != 0) {
        double inter_pkt_spacing = get_current_time() - last_data_pkt_receive_time;
        total_inter_pkt_spacing += inter_pkt_spacing;
    }
    last_data_pkt_receive_time = get_current_time();

    if (p->is_probe) {  
        if (num_outstanding_packets > 0) {
            num_outstanding_packets--;
        }
        // for probe pkts, don't update 'max_seq_no_recv' such that 'recv_till' won't change
        // so calling send_ack_pdq() will just ask for the correct missing data pkt
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
    send_ack_pdq(recv_till, sack_list, p->start_ts, p); // Cumulative Ack
}

void PDQFlow::receive_ack_pdq(Ack *ack_pkt, uint64_t ack, std::vector<uint64_t> sack_list) {
    ////if (ack_pkt->ack_to_probe) {
    ////    has_sent_probe_this_rtt = false;
    ////}

    if (ack_pkt->ack_to_probe) {
        has_sent_probe = false;
        cancel_probing_event();
    }

    // On timeouts; next_seq_no is updated to the last_unacked_seq;
    // In such cases, the ack can be greater than next_seq_no; update it
    if (next_seq_no < ack) {
        next_seq_no = ack;
    }

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Flow[" << id << "] at Host[" << src->id << "] received ACK packet[" << ack_pkt->unique_id
            << "]; ack = " << ack << ", next_seq_no = " << next_seq_no << ", last_unacked_seq = " << last_unacked_seq
            << ", ack_to_probe = " << ack_pkt->ack_to_probe << std::endl;
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
    } else if (ack == last_unacked_seq && ack_pkt->ack_to_probe) {  // resend the last packet when receiving the ack to a probe packet
        if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
            std::cout << "Ready to resend last data pkt: ack = " << ack << "; last_unacked_seq = " << last_unacked_seq << "; size = " << size << std::endl;
        }
        send_next_pkt();
    }

    // same treatment for fin/flow_completion as in D3
    if (ack == size && !finished) {
        finished = true;
        received.clear();
        finish_time = get_current_time();
        flow_completion_time = finish_time - start_time;
        FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
        add_to_event_queue(ev);

        // cancel current RateLimitingEvent/ProbingEvent if there is any
        cancel_rate_limit_event();
        cancel_probing_event();

        // send out a FIN pkt to return the desired and allocated rate of this flow during the last RTT to the routers(queues) on its path
        send_fin_pkt();
    }
}

void PDQFlow::receive_fin_pkt(Packet *p) {
    assert(finished || terminated);
}

void PDQFlow::cancel_probing_event() {
    if (probing_event) {
        probing_event->cancelled = true;
    }
    probing_event = NULL;
}
