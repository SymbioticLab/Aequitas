#include "homa_channel.h"

#include <algorithm>
#include <assert.h>
#include <cstddef>
#include <iostream>
#include <math.h>

#include "homa_host.h"
#include "../coresim/agg_channel.h"
#include "../coresim/event.h"
#include "../coresim/flow.h"
#include "../coresim/node.h"
#include "../coresim/nic.h"
#include "../coresim/packet.h"
#include "../coresim/queue.h"
#include "../coresim/topology.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern DCExpParams params;
extern Topology* topology;
extern std::vector<uint32_t> num_timeouts;
//extern uint32_t num_outstanding_packets;
extern uint32_t pkt_total_count;

bool FlowComparatorHoma::operator() (Flow *a, Flow *b) {
    if (a->size == b->size) {
        return a->start_time > b->start_time;
    } else {
        return a->size > b->size;
    }
}

HomaChannel::HomaChannel(uint32_t id, Host *s, Host *d, uint32_t priority, AggChannel *agg_channel)
    : Channel(id, s, d, priority, agg_channel) {
        record_freq = 0;
        curr_unscheduled_prio_levels = 0;
    }

HomaChannel::~HomaChannel() {}

void HomaChannel::add_to_channel(Flow *flow) {
    //std::cout << "add_to_channel[" << id << "]: end_seq_no = " << end_seq_no << std::endl;
    sender_flows.push(flow);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == flow->id)) {
        std::cout << "Flow[" << flow->id << "] added to Channel[" << id << "]" << std::endl;
    }
    send_pkts();
}

int HomaChannel::send_pkts() {
    while (!sender_flows.empty()) {
        Flow *flow = sender_flows.top();
        sender_flows.pop();
        if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == flow->id)) {
            std::cout << "Flow[" << flow->id << "] popped from sender flow queue" << std::endl;
        }
        flow->send_pending_data();
    }
    return 0;
}

//void HomaChannel::increment_active_flows() {
//    num_active_flows++;
//}
//
//void HomaChannel::decrement_active_flows() {
//    assert(num_active_flows > 0);
//    num_active_flows--;
//}

void HomaChannel::insert_active_flow(Flow *flow) {
    active_flows.insert(flow);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == flow->id)) {
        //std::cout << "insert active flow[" << flow->id << "], num active flows = " << active_flows.size() << std::endl;
        std::cout << "insert active flow[" << flow->id << "], num active flows = " << active_flows.size();
        std::cout << "; { ";
        for (const auto &f : active_flows) {
            std::cout << f->id << " ";
        }
        std::cout << "}" << std::endl;
    }
}

// Note: Homa discard flow state once the last grant packet is sent (original paper, S3.8)
void HomaChannel::remove_active_flow(Flow *flow) {
    active_flows.erase(flow);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == flow->id)) {
        //std::cout << "remove active flow[" << flow->id << "], num active flows = " << active_flows.size() << std::endl;
        std::cout << "remove active flow[" << flow->id << "], num active flows = " << active_flows.size();
        std::cout << "; { ";
        for (const auto &f : active_flows) {
            std::cout << f->id << " ";
        }
        std::cout << "}" << std::endl;
    }
}

struct FlowCompator2 {
    bool operator() (Flow *a, Flow *b) {
        if (a->size == b->size) {
            return a->start_time < b->start_time;
        } else {
            return a->size < b->size;
        }
    }
} fc;

int HomaChannel::calculate_scheduled_priority(Flow *flow) {
    uint32_t num_avail_scheduled_prio_levels = num_hw_prio_levels - curr_unscheduled_prio_levels;
    std::vector<Flow *> active_flow_vec;
    for (const auto &f : active_flows) {
        active_flow_vec.push_back(f);
    }
    std::sort(active_flow_vec.begin(), active_flow_vec.end(), fc);

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == flow->id)) {
        std::cout << "calculate_scheduled_priority: num active flows = " << active_flow_vec.size() << "; num_avail_sche_prio_levels = " << num_avail_scheduled_prio_levels << std::endl;
    }

    int num_active_flows = active_flow_vec.size();
    if (num_active_flows <= num_avail_scheduled_prio_levels) {
        int prio = num_avail_scheduled_prio_levels - 1;
        for (int i = num_active_flows - 1; i >= 0; i--) {
            if (active_flow_vec[i]->id == flow->id) {
                return prio;
            }
            prio--;
        }
        std::cout << "num_active_flows = " << num_active_flows << "; prio = " << prio << "; num_avail_sche_prio_levels = " << num_avail_scheduled_prio_levels << std::endl;
        assert(false);
    } else {
        for (size_t i = 0; i < num_avail_scheduled_prio_levels; i++) {
            if (active_flow_vec[i]->id == flow->id) {
                return i;
            }
        }    
    }

    return -1;        // return -1 means no grant prio assigned
}

void HomaChannel::get_unscheduled_offsets(std::vector<uint32_t> &vec) {
    vec = curr_unscheduled_offsets;
}

void HomaChannel::calculate_unscheduled_offsets() {
    curr_unscheduled_offsets.clear();
    double unscheduled_bytes = 0, scheduled_bytes = 0;
    for (const auto &s : sampled_unscheduled_flow_size) {
        unscheduled_bytes += s;
    }
    for (const auto &s : sampled_scheduled_flow_size) {
        scheduled_bytes += s;
    }

    double unscheduled_pctg = unscheduled_bytes/(unscheduled_bytes + scheduled_bytes);
    curr_unscheduled_prio_levels = unscheduled_pctg * num_hw_prio_levels;
    if (curr_unscheduled_prio_levels == 0) {
        curr_unscheduled_prio_levels = 1;   // at least assign 1 level
    } else if (curr_unscheduled_prio_levels == num_hw_prio_levels) {
        curr_unscheduled_prio_levels = num_hw_prio_levels - 1;  // leave 1 level for schedued data
    }
    uint32_t bytes_per_level = unscheduled_bytes / curr_unscheduled_prio_levels;

    std::sort(sampled_unscheduled_flow_size.begin(), sampled_unscheduled_flow_size.end());
    std::sort(sampled_scheduled_flow_size.begin(), sampled_scheduled_flow_size.end());
    uint32_t count = 0;
    //for (const auto &s : sampled_unscheduled_flow_size) {
    for (size_t i = 0; i < sampled_unscheduled_flow_size.size(); i++) {
        count += sampled_unscheduled_flow_size[i];
        if (count >= bytes_per_level) {
            curr_unscheduled_offsets.push_back(sampled_unscheduled_flow_size[i]);
            count = 0;
            if (curr_unscheduled_offsets.size() == curr_unscheduled_prio_levels) {
                break;
            }
        }
    }
    if (curr_unscheduled_offsets.size() < curr_unscheduled_prio_levels) {
        curr_unscheduled_offsets.push_back(sampled_unscheduled_flow_size[sampled_scheduled_flow_size.size() - 1]);  // shouldn't need the last offset anyway
    }

    sampled_unscheduled_flow_size.clear();
    sampled_scheduled_flow_size.clear();
    sampled_unscheduled_flows.clear();
    sampled_scheduled_flows.clear();

}

void HomaChannel::record_flow_size(Flow* flow, bool scheduled) {
    if (scheduled) {
        if (sampled_scheduled_flows.find(flow) == sampled_scheduled_flows.end()) {  // new flow
            sampled_scheduled_flows.insert(flow);
            sampled_scheduled_flow_size.push_back(flow->size);
            record_freq++;
        }
    } else {
        if (sampled_unscheduled_flows.find(flow) == sampled_unscheduled_flows.end()) {  // new flow
            sampled_unscheduled_flows.insert(flow);
            sampled_unscheduled_flow_size.push_back(flow->size);
            record_freq++;
        }
    }
    if (record_freq == params.homa_sampling_freq) {
        calculate_unscheduled_offsets();
        record_freq = 0;
    }
}

void HomaChannel::add_to_grant_waitlist(Flow *flow) {
    if (waitlist_set.find(flow->id) != waitlist_set.end()) {
        return;
    }
    grant_waitlist.push_back(flow);
    waitlist_set.insert(flow->id);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == flow->id)) {
        std::cout << "At time: " << get_current_time() << ", Flow[" << flow->id << "] added to Channel[" << id << "]'s grant waitlist, waitlist size = " << grant_waitlist.size() << std::endl;
    }
}

void HomaChannel::handle_flow_from_waitlist() {
    while (!grant_waitlist.empty()) {
        Flow *flow = grant_waitlist.front();
        grant_waitlist.pop_front();
        waitlist_set.erase(flow->id);
        insert_active_flow(flow);
        int prio = calculate_scheduled_priority(flow);
        if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == flow->id)) {
            std::cout << "At time: " << get_current_time() << ", Channel[" << id << "] handles Flow[" << flow->id << "] from waitlist, waitlist size = " << grant_waitlist.size() << ", grant prio = " << prio << std::endl;
        }
        if (prio == -1) {
            remove_active_flow(flow);
            grant_waitlist.push_back(flow);
            waitlist_set.insert(flow->id);
            return;
        } else {
            flow->resend_grant();
        }
    }
}
