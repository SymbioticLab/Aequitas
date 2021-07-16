#include "d3_queue.h"

#include <assert.h>
#include <iostream>
#include <stdlib.h>
#include <math.h>

#include "../coresim/event.h"
#include "../coresim/flow.h"
#include "../coresim/packet.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event* ev);
extern uint32_t dead_packets;
extern DCExpParams params;


/* D3Queues */
D3Queue::D3Queue(uint32_t id, double rate, uint32_t limit_bytes, int location)
    : Queue(id, rate, limit_bytes, location) {
        this->demand_counter = 0;
        this->allocation_counter = 0;
        this->base_rate = 0.1 * rate;   // double check on this
    }

//D3Queue::~D3Queue() {};

void D3Queue::enque(Packet *packet) {
    packet->hop_count++;        // hop_count starts from -1
    Queue::enque(packet);
}

Packet *D3Queue::deque(double deque_time) {
    if (bytes_in_queue > 0) {
        Packet *p = packets.front();
        packets.pop_front();
        bytes_in_queue -= p->size;
        p_departures += 1;
        b_departures += p->size;

        // calculate allocated rate (a_{t+1}) for this packet
        if (p->type == SYN_PACKET || p->type == NORMAL_PACKET) {    // only do this for SYN and DATA pkts
            allocate_rate(p);
        }
        return p;
    }
    return NULL;
}

// rtt ~ 300 us in original D3 paper?
// D3Queue::allocate_rate() follows "Snippet 1" in the original D3 paper
void D3Queue::allocate_rate(Packet *packet) {
    double rate_to_allocate = 0;
    double left_capacity = 0;
    double fair_share = 0;
    if (packet->type == SYN_PACKET) {   // if new flow joins; assuming no duplicated SYN pkts
        num_active_flows++;
    }
    allocation_counter -= packet->prev_allocated_rate;
    demand_counter = demand_counter - packet->prev_desired_rate + packet->desired_rate;
    left_capacity = rate - allocation_counter;
    fair_share = (rate - demand_counter) / num_active_flows;
    assert(fair_share > 0);
    std::cout << std::setprecision(2) << std::fixed;
    std::cout << "At D3 Queue[" << unique_id << "]:" << std::endl;
    std::cout << "allocate rate for Packet[" << packet->unique_id << "] from Flow["<< packet->flow->id << "]; type = " << packet->type << " at Queue[" << unique_id << "]" << std::endl;
    std::cout << "num_active_flows = " << num_active_flows << "; prev allocated = " << packet->prev_allocated_rate/1e9
        << "; prev desired = " << packet->prev_desired_rate/1e9 << "; desired = " << packet->desired_rate/1e9 << std::endl;
    std::cout << "allocation_counter = " << allocation_counter/1e9
        << "; demand_counter = " << demand_counter/1e9 << "; left_capacity = " << left_capacity/1e9 << "; fair_share = " << fair_share/1e9 << std::endl;

    if (left_capacity > packet->desired_rate) {
        rate_to_allocate = packet->desired_rate + fair_share;
    } else {
        rate_to_allocate = left_capacity;
    }
    rate_to_allocate = std::max(rate_to_allocate, base_rate);
    allocation_counter += rate_to_allocate;
    std::cout << "rate_to_allocate = " << rate_to_allocate/1e9 << " Gbps; desired_rate = " << packet->desired_rate/1e9 << " Gbps." << std::endl;
    std::cout << std::setprecision(15) << std::fixed;

    packet->curr_rates_per_hop.push_back(rate_to_allocate);
}

double D3Queue::get_transmission_delay(Packet *packet) {
    // NOTE: Assume the packet has been enqueud when D3Queue::get_transmission_delay() is called.
    double td;
    if (packet->type == ACK_PACKET || packet->type == SYN_ACK_PACKET) {
        td = rate;  // Let's forward the ACK with full line rate since the paper doesn't specifically discuss how ACK pkt's rate is assigned
    } else {
        td = packet->size * 8.0 / packet->curr_rates_per_hop[packet->hop_count];
    }
    return td;
}

void D3Queue::drop(Packet *packet) {
    packet->flow->pkt_drop++;
    if (packet->seq_no < packet->flow->size) {
        packet->flow->data_pkt_drop++;
    }

    if (packet->type == SYN_PACKET) {
        assert(false);  //TODO: impl syn pkt retransmission if this ever happens
    }
    if (packet->type == SYN_ACK_PACKET) {
        assert(false);  //TODO: impl syn_ack pkt retransmission if this ever happens
    }
    if (packet->type == ACK_PACKET) {
        packet->flow->ack_pkt_drop++;
    }
    if (location != 0 && packet->type == NORMAL_PACKET) {
        dead_packets += 1;
    }

    delete packet;
}

/*  // PDQ may need to re-implement this 
void Queue::preempt_current_transmission() {
    assert(false);
}
*/
