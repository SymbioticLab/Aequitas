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
        this->constant_early_start = 2;
        this->constant_x = 0.2;
        this->max_num_active_flows = 2 * this->constant_k;  // allow flow states (active_flows) up to 2 * k (according to PDQ paper)
        this->dampening_time_window = 10;   // in unit of us; TODO: pass in as config param; I haven't seen anywhere in the PDQ paper talking about the value they use
        this->time_accept_last_flow = params.first_flow_start_time;
        this->rate_capacity = rate;
        this->time_since_last_rate_control = params.first_flow_start_time;
        this->time_since_last_rcp_update = params.first_flow_start_time;
        this->curr_rcp_fs_rate = 0;
        this->prev_rcp_fs_rate = 0;
        this->bytes_since_last_rcp_update = 0;
        this->rtt_moving_avg = 0;
        this->num_rtts_to_store = 10;   // store 10 RTT values for now
        this->rtt_measures = std::vector<double> (num_rtts_to_store); 
        this->next_rtt_idx = 0;
        this->sum_rtts = 0;
        this->rtt_counts = 0;
}

// For now, flow control packets can never be dropped
void PDQQueue::enque(Packet *packet) {
    packet->hop_count++;        // hop_count starts from -1
    p_arrivals += 1;
    b_arrivals += packet->size;
    if (bytes_in_queue + packet->size <= limit_bytes) {
        packets.push_back(packet);
        bytes_in_queue += packet->size;
        bytes_since_last_rcp_update += packet->size;
    } else {
        pkt_drop++;
        drop(packet);
    }
    packet->enque_queue_size = b_arrivals;

    update_rtt_moving_avg(packet);      // gets updated when an ACK (or SYN ACK) passed thru
    update_RCP_fair_share_rate();       // update fs rate roughly roughly once per RTT
    perform_rate_control(packet);       // perform rate control roughly once per 2 RTTs
    perform_flow_control(packet);       // shouldn't matter if we do in enque() or dequeu()
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
    //std::cout << "PUPU adding Flow[" << packet->flow->id << "] to list" << std::endl;
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
    //std::cout << "removing least critical flow[" << least_critical_flow->id << "]" << std::endl;
}

bool PDQQueue::more_critical(Flow *a, Flow *b) {
    return flow_comp(a, b);
}

void PDQQueue::update_rtt_moving_avg(Packet *packet) {
    if (packet->type == ACK_PACKET || packet->type == SYN_ACK_PACKET) {
        if (rtt_counts < num_rtts_to_store) {
            sum_rtts += packet->measured_rtt;
            rtt_counts++;
            rtt_moving_avg = sum_rtts / rtt_counts;
            rtt_measures[next_rtt_idx] = packet->measured_rtt;
        } else {    // stop increment rtt_counts
            sum_rtts -= rtt_measures[next_rtt_idx];
            rtt_measures[next_rtt_idx] = packet->measured_rtt;
            sum_rtts += packet->measured_rtt;
            rtt_moving_avg = sum_rtts / num_rtts_to_store;
        }

        next_rtt_idx++;
        if (next_rtt_idx == num_rtts_to_store) {
            next_rtt_idx = 0;
        }
        if (params.debug_event_info) {
            std::cout << "At PDQ Queue[" << unique_id << "], RTT moving avg = " << rtt_moving_avg << std::endl;
        }
    }
}

// Note in original RCP, # of flows is estimated by "C/R(t-T)". But PDQ improves this by directly calculating the actual # of active flows
// So we will follow what PDQ does, and the expression should be: R(t) = R(t-T) + (T/d0 * (alpha * (C - y(t)) - beta * q(t)/d0) / num_flows
void PDQQueue::update_RCP_fair_share_rate() {
    // don't update it too frequently; T is supposed to be less than or equal to rtt_moving_avg in RCP
    double T = get_current_time() - time_since_last_rcp_update;
    if (T < 0.9 * rtt_moving_avg || rtt_moving_avg == 0) {     // don't update fs if rtt_moving_avg hasn't been updated
        return;
    }

    double alpha = 0.1, beta = 1.0;
    std::cout << "PUPU: T = " << T << "; rtt_moving_avg = " << rtt_moving_avg << std::endl;
    assert(T <= rtt_moving_avg);
    if (T > rtt_moving_avg) {   // try this in case the assertion fails
        T = rtt_moving_avg;     // can we do this?
    }
    assert(rtt_moving_avg != 0);
    double input_traffic_rate = bytes_since_last_rcp_update * 8.0 / T;
    curr_rcp_fs_rate = prev_rcp_fs_rate
        + (T / rtt_moving_avg * (alpha * (rate - input_traffic_rate) - beta * bytes_since_last_rcp_update * 8.0 / rtt_moving_avg)) / active_flows.size();
    prev_rcp_fs_rate = curr_rcp_fs_rate;
    bytes_since_last_rcp_update = 0;
    time_since_last_rcp_update = get_current_time();
}

// "Algorithm 2" ("Early Start")
double PDQQueue::calculate_available_bandwidth(Packet *packet) {
    uint32_t count = 0;
    double allocation = 0;
    for (const auto &f : active_flows) {
        if (f.second->id == packet->flow->id) {
            break;
        }
        if (f.second->sw_flow_state.expected_trans_time / f.second->sw_flow_state.measured_rtt < constant_early_start
            && count < constant_early_start) {
            count += f.second->sw_flow_state.expected_trans_time / f.second->sw_flow_state.measured_rtt;
        } else {
            allocation += f.second->sw_flow_state.rate;
        }
        if (allocation >= rate_capacity) {
            return 0;
        }
    }
    return rate_capacity - allocation;
}

// They really ask for the flow index in the list... OK I'm not gonna give up the map implementation
// Note: this function assumes/asserts packet->flow must be in the list
uint32_t PDQQueue::find_flow_index(Packet *packet) {
    uint32_t idx = 0, count = 0;
    for (const auto &f : active_flows) {
        if (f.second->id == packet->flow->id) {
            count++;
            break;
        }
        idx++;
    }
    
    assert(count > 0);
    return idx;
}

void PDQQueue::perform_flow_control(Packet *packet) {
    if (packet->type == NORMAL_PACKET) {        // "Algorithm 1"
        if (packet->paused && packet->pause_sw_id != unique_id) {
            remove_flow_from_list(packet);
            return;
        }

        if (active_flows.count(packet->flow->id) == 0) {
            Flow *least_critical_flow = active_flows_pq.top();
            if (active_flows.size() < max_num_active_flows
                || more_critical(packet->flow, least_critical_flow)) {
                packet->flow->sw_flow_state.rate = 0;
                if (active_flows.size() > constant_k) {
                    remove_least_critical_flow();
                }
                add_flow_to_list(packet);       // adding should be performed before removing; otherwise it may remove the newly added flow
            } else {
                packet->allocated_rate = curr_rcp_fs_rate;
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
        if (packet->paused && packet->pause_sw_id != unique_id) {
            remove_flow_from_list(packet);
        }

        if (packet->paused) {
            packet->allocated_rate = 0;
        }

        // "Suppressed Probing"
        if (active_flows.count(packet->flow->id) > 0) {
            packet->flow->sw_flow_state.paused = packet->paused;
            packet->flow->sw_flow_state.pause_sw_id = packet->pause_sw_id;
            packet->inter_probing_time = std::max(packet->inter_probing_time, constant_x * find_flow_index(packet));
            packet->flow->sw_flow_state.rate = packet->allocated_rate;
        }
    } else if (packet->type == FIN_PACKET) {
        // remove flow from list
        remove_flow_from_list(packet);
    }
}

void PDQQueue::perform_rate_control(Packet *packet) {
    // update every 2 RTT according to PDQ paper
    if (get_current_time() - time_since_last_rate_control < 1.9 * rtt_moving_avg) { 
        return;
    }
    rate_capacity = std::max((double) 0, rate - bytes_in_queue * 8.0 / (2 * packet->flow->sw_flow_state.measured_rtt));
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

void PDQQueue::drop(Packet *packet) {
    packet->flow->pkt_drop++;
    if (packet->seq_no < packet->flow->size) {
        packet->flow->data_pkt_drop++;
    }

    if (packet->type == SYN_PACKET) {
        assert(false);
    } else if (packet->type == SYN_ACK_PACKET) {
        assert(false);
    } else if (packet->type == ACK_PACKET) {
        packet->flow->ack_pkt_drop++;
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
    for (const auto &f : active_flows) {
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
