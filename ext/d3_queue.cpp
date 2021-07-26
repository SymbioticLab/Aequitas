#include "d3_queue.h"

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


/* D3Queues */
D3Queue::D3Queue(uint32_t id, double rate, uint32_t limit_bytes, int location)
    : Queue(id, rate, limit_bytes, location) {
        this->demand_counter = 0;
        this->allocation_counter = 0;
        this->base_rate = 0;        // base_rate is a very small value that can only send out a header-only pkt;
        // We set the value of base_rate used in the rate allocation algo to be set to 0 to prevent allocation_counter > rate
        this->real_base_rate = 0.1 * rate;   // D3queue will use this rate to when a pkt gets assigned "base_rate"
    }

//D3Queue::~D3Queue() {};

void D3Queue::enque(Packet *packet) {
    packet->hop_count++;        // hop_count starts from -1
    Queue::enque(packet);
}

// TODO: make D3's deque a batch deque
Packet *D3Queue::deque(double deque_time) {
    // since in D3 some packets has 0 size (e.g., syn, syn_ack, empty data pkts), we can't check with bytes_in_queue
    //if (bytes_in_queue > 0) {
    if (!packets.empty()) {
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
    double temp_demand_counter = demand_counter - packet->prev_desired_rate + packet->desired_rate;
    left_capacity = rate - allocation_counter;
    assert(left_capacity >= 0);
    if (temp_demand_counter < rate) {   // only update the demand_counter when its new value will not exceed line rate
        demand_counter = temp_demand_counter;
    } else {    // otherwise its desired_rate can't be satisfied, and we only grant it "base_rate" and instead forward the packet as a header-only RRQ (rate request) packet
        packet->marked_base_rate = true;
        rate_to_allocate = base_rate;       // case 1 for assigning base rate
    }
    fair_share = (rate - demand_counter) / num_active_flows;
    assert(fair_share > 0);
    if (params.debug_event_info) {
        std::cout << std::setprecision(2) << std::fixed;
        std::cout << "At D3 Queue[" << unique_id << "]:" << std::endl;
        std::cout << "allocate rate for Packet[" << packet->unique_id << "] from Flow["<< packet->flow->id << "]; type = " << packet->type << " at Queue[" << unique_id << "]" << std::endl;
        std::cout << "num_active_flows = " << num_active_flows << "; prev allocated = " << packet->prev_allocated_rate/1e9
            << "; prev desired = " << packet->prev_desired_rate/1e9 << "; desired = " << packet->desired_rate/1e9 << std::endl;
        std::cout << "allocation_counter = " << allocation_counter/1e9
            << "; demand_counter = " << demand_counter/1e9 << "; left_capacity = " << left_capacity/1e9 << "; fair_share = " << fair_share/1e9 << std::endl;
    }

    if (left_capacity > packet->desired_rate) {
        rate_to_allocate = packet->desired_rate + fair_share;
    } else {
        rate_to_allocate = left_capacity;
    }
    rate_to_allocate = std::max(rate_to_allocate, base_rate);
    if (rate_to_allocate == base_rate) {
        packet->marked_base_rate = true;    // case 2 for assigning base rate; this happens when 'rate_to_allocate' = 0 (because 'base_rate' is set to 0)
    }
    // Yiwen: set base_rate value to be 0 to prevent allocation_counter > rate; otherwise left_capacity becomes negative in next RTT
    allocation_counter += rate_to_allocate;
    if (params.debug_event_info) {
        std::cout << "rate_to_allocate = " << rate_to_allocate/1e9 << " Gbps; desired_rate = " << packet->desired_rate/1e9 << " Gbps." << std::endl;
        std::cout << std::setprecision(15) << std::fixed;
    }

    if (packet->marked_base_rate) {
        rate_to_allocate = real_base_rate;  // 'real_base_rate' is set to be a small value so that the header-only packet can be sent out
        if (packet->type == NORMAL_PACKET) {    // for DATA packet, remove its data payload and make it a header-only packet (so it becomes a RRQ packet)
            packet->size = 0;   // remove payload; seq_no remains the same
        }
    }
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
        assert(false);  //TODO: impl syn pkt retransmission if this ever happens; -> made syn pkt zero byte so it can't be dropped
    }
    if (packet->type == SYN_ACK_PACKET) {
        assert(false);  //TODO: impl syn_ack pkt retransmission if this ever happens -> made it zero byte so it can't be dropped
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
