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
    if (!this->has_ddl) {
        assert(this->deadline == 0);
    }
    this->has_sent_probe_this_rtt = false;
    //this->paused = false;
    this->pause_sw_id = 0;
    this->measured_rtt = 0;
    this->inter_probing_time = 0;
}

// Since PDQ will still send out a probe pkt (its Rs is initalized to 0) after receiving the SYN_ACK pkt,
// we will skip the syn pkt (SYN in PDQ doesn't ask for rate) and directly send out the first probe pkt.
// Otherwise this is an unfair comparison with D3 especially when there are a lot of short flows in the workload.
void PDQFlow::start_flow() {
    send_probe_pkt();
}

// When allocated_rate becomes 0 (i.e., the flow is paused by the switch), PDQ sends out a PROBE packet every Is (inter-probing time) RTTs to request new rate info;
// A PROBE packet is a DATA packet with no payload.
Packet *PDQFlow::send_probe_pkt() {
    assert(allocated_rate == 0);
    Packet *p = new Packet(
            get_current_time(),
            this,
            0,
            flow_priority,
            0,
            src,
            dst
            );
    this->total_pkt_sent++;
    p->start_ts = get_current_time();
    
    // assign scheduling header
    p->is_probe = true;
    p->deadline = deadline;
    p->expected_trans_time = get_expected_trans_time();
    p->inter_probing_time = inter_probing_time;
    p->measured_rtt = measured_rtt;

    Queue *next_hop = topology->get_next_hop(p, src->queue);
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop));      // adding a pd since we skip the source queue
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << src->id << "] sends out Probe Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
    }
    return p;
}

// PDQQueue will not process SYN pkt, but we need to throw it into the network to be able to measure the rtt used in the first probe packet
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
// TODO: switch remove the flow when receiving FIN pkt.
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
    if (params.early_termination) {
        // TODO: implement early termination in PDQ
    }

    // if we have already sent the PROBE packet during this RTT, return early and don't bother with sending another one
    if (allocated_rate == 0 && has_sent_probe_this_rtt) {
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

    Queue *next_hop = topology->get_next_hop(p, src->queue);
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + delay, p, next_hop));      // adding a pd since we skip the source queue
    double td = pkt_size * 8.0 / allocated_rate;    // rate limited with the rate assigned by the routers
    RateLimitingEvent *event = new RateLimitingEvent(get_current_time() + td, this);
    add_to_event_queue(event);
    rate_limit_event = event;

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << src->id << "] sends out Packet[" << p->unique_id << "] from flow[" << id << "] at time: " << get_current_time() + delay << std::endl;
    }
    return p;
}

void PDQFlow::send_pending_data() {
    assert(false);    
}

uint32_t PDQFlow::send_pkts() {
    assert(false);
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

    // record measured RTT if it's an ACK/SYN_ACK
    if (p->type == ACK_PACKET || p->type == SYN_ACK_PACKET) {
        measured_rtt = get_current_time() - p->start_ts;
    }

    if (p->type == ACK_PACKET) {                // TODO
        /*
        Ack *a = dynamic_cast<Ack *>(p);
        receive_ack_d3(a, a->seq_no, a->sack_list);
        */
    }
    else if(p->type == NORMAL_PACKET) {         // TODO
        //receive_data_pkt(p);
    } else if (p->type == SYN_PACKET) {
        receive_syn_pkt(p);
    } else if (p->type == SYN_ACK_PACKET) {     // TODO
        receive_syn_ack_pkt(p);
    } else {
        assert(false);
    }

    delete p;
}

void PDQFlow::receive_syn_pkt(Packet *syn_pkt) {
    // basically calling send_ack_d3(), but send a SynAck pkt instead of a normal Ack.
    ////Packet *p = new SynAck(this, 0, hdr_size, dst, src);  //Acks are dst->src
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
    send_probe_pkt();
}

void PDQFlow::receive_fin_pkt(Packet *p) {
    assert(finished);
    // nothing to do here. 'num_active_flows' are decremented when FIN packet traverse thru the network
}



