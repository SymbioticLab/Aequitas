#include "pfabric_flow.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

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

PFabricFlow::PFabricFlow(uint32_t id, double start_time, uint32_t size, Host *s,
                         Host *d, uint32_t flow_priority)
        : Flow(id, start_time, size, s, d, flow_priority) {
    ssthresh = 100000;
    count_ack_additive_increase = 0;
    priority_thresholds = {4096, 8192, 16384, 32768, 65536, 131072, 262144};
}

uint32_t PFabricFlow::get_pfabric_priority(uint64_t seq) {
    if (params.pfabric_priority_type == "size") {
        uint32_t priority_unlimited = size - last_unacked_seq - scoreboard_sack_bytes;
        if (!params.pfabric_limited_priority) {
            return priority_unlimited;
        } else {
            for (uint32_t i = 0; i < priority_thresholds.size(); i++) {
                if (priority_unlimited <= priority_thresholds[i]) {
                    return i;
                }
            }
            return priority_thresholds.size();
        }
    } else if (params.pfabric_priority_type == "qos") {
        return flow_priority;
    } else {
        return 1;
    }
}

void PFabricFlow::start_flow() {
    run_priority = flow_priority;
    send_pending_data();
}

// Copied from AequitasFlow::send_pkts() which modifies the original YAPS'
// Flow::send_pending_data()
void PFabricFlow::send_pending_data() {
    uint32_t pkts_sent = 0;
    uint64_t seq = next_seq_no;
    uint32_t window = cwnd_mss * mss + scoreboard_sack_bytes;

    while (
        (seq + mss <= last_unacked_seq + window) &&
        ((seq + mss <= size) || (seq != size && (size - seq < mss)))
    ) {
        send_with_delay(seq, 1e-12 * (pkts_sent + 1));    // inc by 1 ps each pkt
        pkts_sent++;

        if (seq + mss < size) {
            next_seq_no = seq + mss;
            seq += mss;
        } else {
            next_seq_no = size;
            seq = size;
        }

        if (retx_event == NULL) {
            set_timeout(get_current_time() + retx_timeout);
        }
    }
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Flow[" << id << "] sends " << pkts_sent << " pkts." << std::endl;
    }
}

uint32_t PFabricFlow::send_pkts() {
    std::cout << "PFabricFlow::send_pkts() should be never called."
        << std::endl;
    abort();
}

// Copied from AequitasFlow::send_with_delay() with changes to setting priority
// send with some tiny delay so that pkts from the same batch of a flow is easier to be distinguished by the event comparator
// also starts from first-hop switch queue, and all the way thru dst queue (instead of src queue -> sw queue but skip dst queue in previous design)
Packet *PFabricFlow::send_with_delay(uint64_t seq, double delay) {
    Packet *p = NULL;

    uint32_t pkt_size;
    if (seq + mss > this->size) {
        pkt_size = this->size - seq + hdr_size;
    } else {
        pkt_size = mss + hdr_size;
    }

    uint32_t priority = get_pfabric_priority(seq);
    p = new Packet(
            get_current_time(),
            this,
            seq,
            priority,
            pkt_size,
            src,
            dst
            );
    this->total_pkt_sent++;
    p->start_ts = get_current_time();

    Queue *next_hop = topology->get_next_hop(p, src->queue);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "sending out Packet[" << p->unique_id << "] at time: " << get_current_time() + delay << " (base=" << get_current_time() << "; delay=" << delay << ")" << std::endl;
    }
    //PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + delay, p, next_hop);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);

    return p;
}

// Copied from AequitasFlow::send() with changes to setting priority.
// Original Flwo::send; include src queue but no dst queue; not currently in use
Packet *PFabricFlow::send(uint64_t seq) {
    Packet *p = NULL;

    uint32_t pkt_size;
    if (seq + mss > this->size) {
        pkt_size = this->size - seq + hdr_size;
    } else {
        pkt_size = mss + hdr_size;
    }

    uint32_t priority = get_pfabric_priority(seq);
    p = new Packet(
            get_current_time(),
            this,
            seq,
            priority,
            pkt_size,
            src,
            dst
            );
    this->total_pkt_sent++;
    p->start_ts = get_current_time();

    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, src->queue);
    add_to_event_queue(event);
    return p;
}

// Copied from AequitasFlow::send_ack() with changes to setting priority.
void PFabricFlow::send_ack(uint64_t seq, std::vector<uint64_t> sack_list, double pkt_start_ts) {
    Packet *p = new Ack(this, seq, sack_list, hdr_size, dst, src);  //Acks are dst->src
    p->pf_priority = 0; // Use highest priority for ACK packets.
    ////p->pf_priority = flow_priority; // stick to flow's assigned priority instead
    p->start_ts = pkt_start_ts; // carry the orig packet's start_ts back to the sender for RTT measurement
    //std::cout << "Flow[" << id << "] with priority " << run_priority << " send ack: " << seq << std::endl;
    Queue *next_hop = topology->get_next_hop(p, dst->queue);
    ////PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, next_hop);  // skip src queue, include dst queue
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // skip src queue, include dst queue; add a pd delay
    ////PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, dst->queue);  // orig version (include src queue, skip dst queue)
    add_to_event_queue(event);
}

// Copied from Flow::receive_ack() and changed to flow-based congestion control.
void PFabricFlow::receive_ack(uint64_t ack, std::vector<uint64_t> sack_list,
                              double pkt_start_ts, uint32_t priority,
                              uint32_t num_hops) {
    //this->scoreboard_sack_bytes = sack_list.size() * mss;
    this->scoreboard_sack_bytes = 0;  // sack_list is non-empty. Manually set sack_bytes to 0 for now since we don't support SACK yet.

    // On timeouts; next_seq_no is updated to the last_unacked_seq;
    // In such cases, the ack can be greater than next_seq_no; update it
    if (next_seq_no < ack) {
        next_seq_no = ack;
    }

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Flow[" << id << "] at Host[" << src->id << "] received ACK packet"
            << "; ack = " << ack << ", next_seq_no = " << next_seq_no << ", last_unacked_seq = " << last_unacked_seq << std::endl;
    }

    // New ack!
    if (ack > last_unacked_seq) {
        // Update the last unacked seq
        last_unacked_seq = ack;

        // Adjust cwnd
        increase_cwnd();

        // Send the remaining data
        send_pending_data();

        // Update the retx timer
        if (retx_event != nullptr) { // Try to move
            cancel_retx_event();
            if (last_unacked_seq < size) {
                // Move the timeout to last_unacked_seq
                double timeout = get_current_time() + retx_timeout;
                set_timeout(timeout);
            }
        }

    }

    if (ack == size && !finished) {
        finished = true;
        received.clear();
        finish_time = get_current_time();
        flow_completion_time = finish_time - start_time;
        FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
        add_to_event_queue(ev);
    }
}

// From https://github.com/NetSys/simulator/blob/master/ext/pfabricflow.cpp
void PFabricFlow::increase_cwnd() {
    if (cwnd_mss < ssthresh) { // slow start
        cwnd_mss += 1;
    }
    else { // additive increase
        if (++count_ack_additive_increase >= cwnd_mss) {
            count_ack_additive_increase = 0;
            cwnd_mss += 1;
        }
    }
    // Check if we exceed max_cwnd
    if (cwnd_mss > max_cwnd) {
        cwnd_mss = max_cwnd;
    }
}

// From https://github.com/NetSys/simulator/blob/master/ext/pfabricflow.cpp
void PFabricFlow::handle_timeout() {
    ssthresh = cwnd_mss / 2;
    if (ssthresh < 2) {
        ssthresh = 2;
    }
    Flow::handle_timeout();
}

