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

extern std::vector<double> D3_allocation_counter_per_queue;
extern std::vector<uint32_t> D3_num_allocations_per_queue;

/* D3Queues */
D3Queue::D3Queue(uint32_t id, double rate, uint32_t limit_bytes, int location)
    : Queue(id, rate, limit_bytes, location) {
        this->demand_counter = 0;
        this->allocation_counter = 0;
        this->base_rate = 0;        // base_rate is a very small value that can only send out a header-only pkt;
        // We set the value of base_rate used in the rate allocation algo to be set to 0 to prevent allocation_counter > rate
    }

//D3Queue::~D3Queue() {};

void D3Queue::enque(Packet *packet) {
    packet->hop_count++;        // hop_count starts from -1
    p_arrivals += 1;
    b_arrivals += packet->size;
    if (bytes_in_queue + packet->size <= limit_bytes) {
        packets.push_back(packet);
        bytes_in_queue += packet->size;
    } else {
        // We will still keep the header (with 0 size) if the packet contains rate request
        if (packet->has_rrq && packet->size != 0) {
            assert(packet->type == NORMAL_PACKET);
            packet->size = 0;
            packets.push_back(packet);
        }

        pkt_drop++;
        drop(packet);       // drop the payload; also count as 'drop'
    }
    packet->enque_queue_size = b_arrivals;
}

Packet *D3Queue::deque(double deque_time) {
    // since in D3 some packets has 0 size (e.g., syn, syn_ack, empty data pkts), we can't check with bytes_in_queue
    //if (bytes_in_queue > 0) {
    if (!packets.empty()) {
        Packet *p = packets.front();
        packets.pop_front();
        bytes_in_queue -= p->size;
        p_departures += 1;
        b_departures += p->size;

        // calculate allocated rate (a_{t+1}) if the packet is an RRQ (SYN/FIN/DATA_pkt_with_rrq)
        if (p->has_rrq) {
            allocate_rate(p);       // Note: one can also allocate rate inside enque(), which doesn't affect performance (verified)
        }
        return p;
    }
    return NULL;
}

// rtt ~ 300 us in original D3 paper?
// D3Queue::allocate_rate() follows "Snippet 1" in the original D3 paper
void D3Queue::allocate_rate(Packet *packet) {
    assert(packet->type != ACK_PACKET && packet->type != SYN_ACK_PACKET);
    double rate_to_allocate = 0;
    double left_capacity = 0;
    double fair_share = 0;
    if (packet->type == SYN_PACKET) {   // if new flow joins; assuming no duplicated SYN pkts
        num_active_flows++;
    }
    allocation_counter -= packet->prev_allocated_rate;
    demand_counter = demand_counter - packet->prev_desired_rate + packet->desired_rate;
    left_capacity = rate - allocation_counter;

    // Sometimes some counter will be negative but very close to 0. Let's treat it as within the normal error range instead of a bug
    //assert(allocation_counter >= 0 && demand_counter >= 0 && left_capacity >= 0);
    //if (allocation_counter < 0 || demand_counter < 0 || left_capacity < 0) {
    //    std::cout << "PUPU" << std::endl;
    //}

    if (packet->type == FIN_PACKET) {   // for FIN packet, exit the algorithm after returning past info & decrementing flow counter
        num_active_flows--;
        if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == packet->flow->id)) {
            std::cout << "At D3 Queue[" << unique_id << "]: handling FIN packet[" << packet->unique_id << "] from Flow[" << packet->flow->id << "]" << std::endl;
            std::cout << std::setprecision(2) << std::fixed;
            std::cout << "num_active_flows = " << num_active_flows << "; prev allocated = " << packet->prev_allocated_rate/1e9
                << "; prev desired = " << packet->prev_desired_rate/1e9 << "; desired = " << packet->desired_rate/1e9 << std::endl;
            std::cout << "allocation_counter = " << allocation_counter/1e9
                << "; demand_counter = " << demand_counter/1e9 << "; left_capacity = " << left_capacity/1e9 << std::endl;
            std::cout << std::setprecision(15) << std::fixed;
        }
        return;
    }

    fair_share = (rate - demand_counter) / num_active_flows;
    if (fair_share < 0) {   // happens when demand_counter is > rate
        fair_share = 0;
    }
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == packet->flow->id)) {
        std::cout << std::setprecision(2) << std::fixed;
        std::cout << "At D3 Queue[" << unique_id << "]:" << std::endl;
        std::cout << "allocate rate for Packet[" << packet->unique_id << "]{" << packet->seq_no << "} from Flow["<< packet->flow->id << "]; type = "
            << packet->type << "; size = " << packet->size << " at Queue[" << unique_id << "]" << std::endl;
        std::cout << "num_active_flows = " << num_active_flows << "; prev allocated = " << packet->prev_allocated_rate/1e9
            << "; prev desired = " << packet->prev_desired_rate/1e9 << "; desired = " << packet->desired_rate/1e9 << std::endl;
        std::cout << "allocation_counter = " << allocation_counter/1e9
            << "; demand_counter = " << demand_counter/1e9 << "; left_capacity = " << left_capacity/1e9 << "; fair_share = " << fair_share/1e9 << std::endl;
    }

    if (left_capacity > packet->desired_rate) {
        rate_to_allocate = packet->desired_rate + fair_share;
    } else {
        rate_to_allocate = left_capacity;   // when desired_rate can't be satisfied, do in a greedy way (FCFS)
    }
    rate_to_allocate = std::max(rate_to_allocate, base_rate);
    if (rate_to_allocate == base_rate) {    // this happens when 'rate_to_allocate' = 0
        packet->marked_base_rate = true;    
        if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == packet->flow->id)) {
            std::cout << "assign packet[" << packet->unique_id << "] from Flow[" << packet->flow->id << "] base rate" << std::endl;
        }
    } // Yiwen: set base_rate value to be 0 to prevent allocation_counter > rate; otherwise left_capacity becomes negative in next RTT

    allocation_counter += rate_to_allocate;

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == packet->flow->id)) {
        std::cout << "rate_to_allocate = " << rate_to_allocate/1e9 << " Gbps; allocation counter = " << allocation_counter/1e9 << " Gbps." << std::endl;
        std::cout << std::setprecision(15) << std::fixed;
    }
    // logging
    if (params.big_switch && params.traffic_pattern == 1) {
        D3_allocation_counter_per_queue[unique_id] += (allocation_counter/1e9);
        D3_num_allocations_per_queue[unique_id]++;
        if (allocation_counter/1e9 > 1000) {     // watch for unusual allocation counter value
            std::cout << "PUPU: allocation counter = " << allocation_counter/1e9 << std::endl;
            std::cout << std::setprecision(2) << std::fixed;
            std::cout << "At D3 Queue[" << unique_id << "]:" << std::endl;
            std::cout << "allocate rate for Packet[" << packet->unique_id << "] from Flow["<< packet->flow->id << "]; type = " << packet->type << " at Queue[" << unique_id << "]" << std::endl;
            std::cout << "num_active_flows = " << num_active_flows << "; prev allocated = " << packet->prev_allocated_rate/1e9
                << "; prev desired = " << packet->prev_desired_rate/1e9 << "; desired = " << packet->desired_rate/1e9 << std::endl;
            std::cout << "; demand_counter = " << demand_counter/1e9 << "; left_capacity = " << left_capacity/1e9 << "; fair_share = " << fair_share/1e9 << std::endl;
            std::cout << "rate_to_allocate = " << rate_to_allocate/1e9 << " Gbps; allocation counter = " << allocation_counter/1e9 << " Gbps." << std::endl;
            std::cout << std::setprecision(15) << std::fixed;
        }
    }

    packet->curr_rates_per_hop.push_back(rate_to_allocate); 
}

double D3Queue::get_transmission_delay(Packet *packet) {
    double td;
    if (packet->has_rrq && packet->size == 0) {      // add back hdr_size when handling hdr_only RRQ packets (their packet->size is set to 0 to avoid dropping)
        ////|| packet->ack_pkt_with_rrq) {         // ACK to DATA RRQ is also made 0 size 
        td = params.hdr_size * 8.0 / rate;
    } else {    // D3 router forwards other packet normally
        td = packet->size * 8.0 / rate;
    }
    return td;
}

void D3Queue::drop(Packet *packet) {
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

