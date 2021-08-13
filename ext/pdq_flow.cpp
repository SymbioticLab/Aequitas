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
    this->paused = false;
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
void PDQFlow::send_probe_pkt() {
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
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << src->id << "] sends out Syn Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
    }
}

void PDQFlow::receive_syn_ack_pkt(Packet *p) {
    send_probe_pkt();
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
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << src->id << "] sends out Fin Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
    }
}





