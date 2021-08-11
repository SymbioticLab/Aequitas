#include "pdq_queue.h"

#include <assert.h>
#include <iostream>
#include <stdlib.h>
#include <math.h>
#include <iomanip>

#include "../coresim/event.h"
#include "../coresim/flow.h"
#include "../coresim/packet.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event* ev);
extern uint32_t dead_packets;
extern DCExpParams params;

/* PDQQueues */
PDQQueue::PDQQueue(uint32_t id, double rate, uint32_t limit_bytes, int location)
    : Queue(id, rate, limit_bytes, location) {
        // allow flow states (active_flows) up to 2k (according to PDQ paper)
        active_flows = std::vector<Flow *>(2000);       // this is going to be a ring buffer
}

// For now, flow control packets can never be dropped
void PDQQueue::enque(Packet *packet) {
    packet->hop_count++;        // hop_count starts from -1
    p_arrivals += 1;
    b_arrivals += packet->size;
    if (bytes_in_queue + packet->size <= limit_bytes) {
        packets.push_back(packet);
        bytes_in_queue += packet->size;
    } else {
        pkt_drop++;
        drop(packet);
    }
    packet->enque_queue_size = b_arrivals;

    add_flow_to_list(packet);   
    perform_flow_control(packet);       // shouldn't matter if we do in enque() or dequeu()

    // TODO: perform_rate_control()
}

// TODO: even in the case of dropping a data pkt, should process its rrq packet info first
Packet *PDQQueue::deque(double deque_time) {
    if (!packets.empty()) {
        Packet *p = packets.front();
        packets.pop_front();
        bytes_in_queue -= p->size;
        p_departures += 1;
        b_departures += p->size;
        return p;
    }
    return NULL;
}

void PDQQueue::add_flow_to_list(Packet *packet) {
}

void PDQQueue::perform_flow_control(Packet *packet) {
}

void PDQQueue::perform_rate_control(Packet *packet) {
}

// TODO: double check delay values for flow control packets
double PDQQueue::get_transmission_delay(Packet *packet) {
    double td;
    if (packet->has_rrq && packet->size == 0) {      // add back hdr_size when handling hdr_only RRQ packets (their packet->size is set to 0 to avoid dropping)
        ////|| packet->ack_pkt_with_rrq) {         // ACK to DATA RRQ is also made 0 size 
        td = params.hdr_size * 8.0 / rate;
    } else {    // D3 router forwards other packet normally
        td = packet->size * 8.0 / rate;
    }
    return td;
}

// TODO: handle assertions for PDQ
void PDQQueue::drop(Packet *packet) {
    packet->flow->pkt_drop++;
    if (packet->seq_no < packet->flow->size) {
        packet->flow->data_pkt_drop++;
    }

    if (packet->type == SYN_PACKET) {
        assert(false);
    }
    if (packet->type == SYN_ACK_PACKET) {
        assert(false);
    }
    if (packet->type == ACK_PACKET) {
        packet->flow->ack_pkt_drop++;
        if (packet->ack_pkt_with_rrq) {
            assert(false);          // need to handle it if failed
        }
    }
    if (location != 0 && packet->type == NORMAL_PACKET) {
        dead_packets += 1;
    }

    // if it's a DATA rrq whose payload gets removed due to being dropped by the queue, don't delete the packet. We'll still need to receive it (the rrq part).
    if (packet->type == NORMAL_PACKET && packet->size == 0) {
        return;
    }

    delete packet;
}

