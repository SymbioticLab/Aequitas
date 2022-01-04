#include "homa_channel.h"

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

HomaChannel::HomaChannel(uint32_t id, Host *s, Host *d, uint32_t priority, AggChannel *agg_channel)
    : Channel(id, s, d, priority, agg_channel) {
        overcommitment_degree = num_hw_prio_levels;
        record_freq = 0;
        curr_unscheduled_prio_levels = 0;
    }

HomaChannel::~HomaChannel() {}

// TODO: decide flow's start & end sequence only when the flow is selcted to transmit
void HomaChannel::add_to_channel(Flow *flow) {
    //flow->start_seq_no = end_seq_no;    
    //end_seq_no += flow->size;
    //outstanding_flows.push_back(flow);    // for RPC boundary, tie flow to pkt, easy handling of flow_finish, etc.
    //flow->end_seq_no = end_seq_no;
    //std::cout << "add_to_channel[" << id << "]: end_seq_no = " << end_seq_no << std::endl;
    sender_flows.push(flow);
    if (params.debug_event_info) {
        std::cout << "Flow[" << flow->id << "] added to Channel[" << id << "]" << std::endl;
    }
    send_pkts();
}

// Allow at maximum overcommit_degree num of flows to send pkts at the same time. Flows handling transmission by themselves like pFabric
int HomaChannel::send_pkts() {
    while (!sender_flows.empty() && num_active_flows < overcommitment_degree) {
        Flow *flow = sender_flows.top();
        sender_flows.pop();
        num_active_flows++;
        flow->send_pending_data();
    }
}

//void HomaChannel::increment_active_flows() {
//    num_active_flows++;
//}
//
//void HomaChannel::decrement_active_flows() {
//    assert(num_active_flows > 0);
//    num_active_flows--;
//}

void HomaChannel::insert_active_flows(Flow *flow) {
    active_flows.insert(flow);
}

// Note: Homa discard flow state once the last grant packet is sent (original paper, S3.8)
void HomaChannel::remove_active_flows(Flow *flow) {
    active_flows.erase(flow);
}

//int HomaChannel::count_active_flows() {
//    return active_flows.size();
//}


struct FlowCompator2 {
    bool operator() (Flow *a, Flow *b) {
        if (a->size == b->size) {
            return a->start_time < b->start_time;
        } else {
            return a->size < b->size;
        }
    }
} fc;

// TODO: check range of avail prio levels after the impl of unscheduled prio
int HomaChannel::calculate_scheduled_priority(Flow *flow) {
    std::vector<Flow *> active_flow_vec;
    for (const auto &f : active_flows) {
        active_flow_vec.push_back(f);
    }
    std::sort(active_flow_vec.begin(), active_flow_vec.end(), fc);

    int num_active_flows = active_flow_vec.size();
    if (num_active_flows <= num_hw_prio_levels) {
        int prio = num_hw_prio_levels - 1;
        for (size_t i = num_active_flows - 1; i >= 0; i--) {
            if (active_flow_vec[i]->id == flow->id) {
                return prio;
            }
            prio--;
        }
        assert(false);
    } else {
        for (size_t i = 0; i < num_hw_prio_levels; i++) {
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
        }
    }
    if (curr_unscheduled_offsets.size() < curr_unscheduled_prio_levels) {
        curr_unscheduled_offsets.push_back(sampled_unscheduled_flow_size[sampled_scheduled_flow_size.size() - 1]);  // shouldn't need the last offset anyway
    }
    std::cout << "curr_unscheduled_offsets: ";
    for (const auto &x : curr_unscheduled_offsets) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
    assert(curr_unscheduled_offsets.size() == curr_unscheduled_prio_levels);        // TODO: remove
    //for (int i = 0; i < curr_unscheduled_prio_levels; i++) {
    //    int ith_idx = (double) (i + 1) / curr_unscheduled_prio_levels * unscheduled_bytes;
    //    curr_unscheduled_offsets.push_back(sampled_unscheduled_flow_size[ith_idx]);
    //}

    sampled_unscheduled_flow_size.clear();
    sampled_scheduled_flow_size.clear();
    sampled_unscheduled_flows.clear();
    sampled_scheduled_flows.clear();

}

void HomaChannel::record_flow_size(Flow* flow, bool scheduled) {
    if (scheduled) {
        if (sampled_scheduled_flows.find(flow) != sampled_scheduled_flows.end()) {  // new flow
            sampled_scheduled_flow_size.push_back(flow->size);
            record_freq++;
        }
    } else {
        if (sampled_unscheduled_flows.find(flow) != sampled_unscheduled_flows.end()) {  // new flow
            sampled_unscheduled_flow_size.push_back(flow->size);
            record_freq++;
        }
    }
    if (record_freq == sampling_freq) {
        calculate_unscheduled_offsets();
        record_freq = 0;
    })
}

// Homa dealing with packet loss (orig paper S3.7)
//TODO
void HomaChannel::set_timeout(double time) {
    /*
    if (last_unacked_seq < end_seq_no) {
        ChannelRetxTimeoutEvent *ev = new ChannelRetxTimeoutEvent(time, this);
        add_to_event_queue(ev);
        retx_event = ev;
    }
    */
}

//TODO
void HomaChannel::handle_timeout() {
    /*
    num_timeouts[priority]++;
    next_seq_no = last_unacked_seq;
    send_pkts();
    set_timeout(get_current_time() + retx_timeout);
    */
}
