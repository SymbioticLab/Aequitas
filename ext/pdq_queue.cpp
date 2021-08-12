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

bool MoreCritical::operator() (Flow *a, Flow *b) {
    if (a->has_ddl && b->has_ddl) {
        if (a->deadline == b->deadline) {
            return a->get_expected_trans_time() < b->get_expected_trans_time();
        } else {
            return a->deadline < b->deadline;
        }
    } else if (!a->has_ddl && !b->has_ddl) {
        return a->get_expected_trans_time() < b->get_expected_trans_time();
    } else {    // when one has ddl and the other does not
        return a->has_ddl;
    }
}

/* PDQQueues */
PDQQueue::PDQQueue(uint32_t id, double rate, uint32_t limit_bytes, int location)
    : Queue(id, rate, limit_bytes, location) {
        this->constant_k = 100;   // set to 100 for now; TODO: set as a configuration parameter
        this->max_num_active_flows = 2 * this->constant_k;  // allow flow states (active_flows) up to 2 * k (according to PDQ paper)
        this->dampening_time_window = 10;   // in unit of us; TODO: pass in as config param; I haven't seen anywhere in the PDQ paper talking about the value they use
        this->time_accept_last_flow = 0;
        this->constant_early_start = 2;
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

    perform_flow_control(packet);       // shouldn't matter if we do in enque() or dequeu()

    // TODO: perform_rate_control()
}

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
    active_flows[packet->flow->id] = packet->flow;
    active_flows_pq.push(packet->flow);
}

void PDQQueue::remove_flow_from_list(Packet *packet) {
    // mark flow as removed_from_pq so that the priority queue can ignore it
    // it's fine to share the same removed_from_pq variable across all switches due to how PDQ works
    packet->flow->sw_flow_state.removed_from_pq = true;
    active_flows.erase(packet->flow->id);
}

void PDQQueue::remove_least_critical_flow() {
    //active_flows.erase(packet->flow->id);
    Flow *least_critical_flow = active_flows_pq.top();
    while (least_critical_flow->sw_flow_state.removed_from_pq) {
        active_flows_pq.pop();
        least_critical_flow = active_flows_pq.top();
    }
    least_critical_flow->sw_flow_state.removed_from_pq = true;    // not necessary to do so here; just to be consistent
    active_flows_pq.pop();
    active_flows.erase(least_critical_flow->id);
}

bool PDQQueue::more_critical(Flow *a, Flow *b) {
    return flow_comp(a, b);
}

// TODO1: finish the impl
// TODO2: perform this calc every RTT; call it everytime it needs the rate for now
double PDQQueue::calculate_RCP_fair_share_rate() {
    return rate;    
}

// "Algorithm 2" ("Early Start")
double PDQQueue::calculate_available_bandwidth(Packet *packet) {
    uint32_t X = 0;
    double A = 0;
    for (const auto & f: active_flows) {
        if (f.second->id == packet->flow->id) {
            break;
        }
        if (f.second->sw_flow_state.expected_trans_time / f.second->sw_flow_state.measured_rtt < constant_early_start
            && X < constant_early_start) {
            X += f.second->sw_flow_state.expected_trans_time / f.second->sw_flow_state.measured_rtt;
        } else {
            A += f.second->sw_flow_state.rate;
        }
        if (A >= rate) {
            return 0;
        }
    }
    return rate - A;
}

// TODO: check whether a flow is finished
// TODO: measure RTT at PDQ host
void PDQQueue::perform_flow_control(Packet *packet) {
    if (packet->type == NORMAL_PACKET) {        // "Algorithm 1"
        if (packet->paused) {
            remove_flow_from_list(packet);
            return;
        }

        if (active_flows.count(packet->flow->id) == 0) {
            Flow *least_critical_flow = active_flows_pq.top();
            if (active_flows.size() < max_num_active_flows
                || more_critical(packet->flow, least_critical_flow)) {
                add_flow_to_list(packet);
                packet->flow->sw_flow_state.rate = 0;
                if (active_flows.size() > constant_k) {
                    remove_least_critical_flow();
                }
            } else {
                //TODO impl RCP fair share rate
                double rcp_fs_rate = calculate_RCP_fair_share_rate();
                packet->allocated_rate = rcp_fs_rate;
                if (packet->allocated_rate == 0) {
                    packet->paused = true;
                    packet->pause_sw_id = unique_id;
                }
                return;
            }
        }

        // if we reach here, we have added a flow into the list
        assert(active_flows.count(packet->flow->id) > 0);
        active_flows[packet->flow->id]->sw_flow_state.deadline = packet->deadline;
        active_flows[packet->flow->id]->sw_flow_state.expected_trans_time = packet->expected_trans_time;
        active_flows[packet->flow->id]->sw_flow_state.measured_rtt = packet->measured_rtt;

        double rate_to_allocate = std::min(calculate_available_bandwidth(packet), packet->allocated_rate);
        if (rate_to_allocate > 0) {
            if (packet->flow->sw_flow_state.paused == true
                && (get_current_time() - time_accept_last_flow) * 1e6 < dampening_time_window) {
                // "Dampening"
                packet->paused = true;
                packet->pause_sw_id = unique_id;
                packet->flow->sw_flow_state.paused = true;
                packet->flow->sw_flow_state.pause_sw_id = unique_id;
            } else {
                packet->paused = false;
                packet->allocated_rate = rate_to_allocate;
            }
        } else {
            packet->paused = true;
            packet->pause_sw_id = unique_id;
            packet->flow->sw_flow_state.paused = true;
            packet->flow->sw_flow_state.pause_sw_id = unique_id;
        }

    } else if (packet->type == ACK_PACKET) {    // "Algorithm 3"
        //TODO
    }
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

// find_least_critical_flow() in a map before moving to the priority queue impl; not currently in use.
// The most critical flow has the smallest ddl; break tie by smaller expected transmission time.
// Thus, the least critical flow is:
// (1) the non-ddl flow with largest expected transmission time, or
// (2) if all flows in the list has ddl, pick the one with the largest ddl; break tie again
//     by largest expected transmission time.
/*
Flow *PDQQueue::find_least_critical_flow() {
    double largest_ddl = 0;
    Flow *least_critical_flow = nullptr;
    bool found_non_ddl_flow = false;
    for (const auto & f : active_flows) {
        if (found_non_ddl_flow) {
            // in this casa, only compare among non-ddl flows
            if (!f.secodn->has_ddl) {
                if (f.second->get_expected_trans_time() > least_critical_flow->get_expected_trans_time()) {
                    least_critical_flow = f.second;
                }
            }
        } else {
            if (!f.second->has_ddl) {
                least_critical_flow = f.second;
                found_non_ddl_flow = true;
            } else {
                if (f.second->deadline > largest_ddl) {
                    least_critical_flow = f.second;
                    largest_ddl = f.second->deadline;
                } else if (f.second->deadline == largest_ddl) {
                    if (f.second->get_expected_trans_time() > least_critical_flow->get_expected_trans_time()) {
                        least_critical_flow = f.second;
                    }
                }
            }
        }
    }
    return least_critical_flow;
}
*/
