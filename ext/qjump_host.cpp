#include "qjump_host.h"

#include <cstdio>

#include "factory.h"
#include "../coresim/agg_channel.h"
#include "../coresim/channel.h"
#include "../coresim/event.h"
#include "../coresim/queue.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern DCExpParams params;

// From Qjump's paper: network epoch = 2 * num_hosts' x (max_packet_size / rate) + cumulative_processing_delay
// where num_hosts' = num_hosts / tput_factor_i, i = prio level idx
// According to the paper, for each priority level we use a different epoch value based on the tput factor.
// This means within the same prio level, packets are sent via epoch[priority]; across prio levels the transport is independent
QjumpHost::QjumpHost(uint32_t id, double rate, uint32_t queue_type, uint32_t host_type)
        : Host(id, rate, queue_type, QJUMP_HOST) {
    for (int i = 0; i < params.num_qos_level; i++) {
        double epoch = 2 * ((double)params.num_hosts / params.qjump_tput_factor[i]) * (params.mss * 8.0 / params.bandwidth) + (double)params.qjump_cumulative_pd / 1e6;
        this->network_epoch.push_back(epoch);
        //std::cout << "Qjump epoch["<< i << "] = " << epoch * 1e6 << " us." << std::endl;
        if (params.debug_event_info) {
            std::cout << "Qjump epoch["<< i << "] = " << epoch * 1e6 << " us." << std::endl;
        }

        this->busy.push_back(false);
    }
    //this->network_epoch = 2 * params.num_hosts * (params.mss * 8.0 / params.bandwidth) + (double)params.qjump_cumulative_pd / 1e6;
    this->agg_channel_count = 0;
    this->agg_channel_idx.resize(params.num_qos_level, 0);
    this->WF_counters.resize(params.num_qos_level, 0);
    this->agg_channels.resize(params.num_qos_level);

    //kick_off_epoch_events();
}

QjumpHost::~QjumpHost() {
}

void QjumpHost::set_agg_channels(AggChannel *agg_channel) {
    int prio = agg_channel->priority;
    agg_channels[prio].push_back(agg_channel);
    agg_channel_count++;
}


void QjumpHost::increment_agg_channel_idx(uint32_t priority) {
    agg_channel_idx[priority]++;
    if (agg_channel_idx[priority] == agg_channels[priority].size()) {
        agg_channel_idx[priority] = 0;
    }
}

void QjumpHost::start_next_epoch(uint32_t priority) {
    if (busy[priority]) {
        return;
    } else {
        add_to_event_queue(new QjumpEpochEvent(get_current_time(), this, priority));
        busy[priority] = true;
    }
}

void QjumpHost::kick_off_epoch_events() {
    for (uint32_t i = 0; i < params.num_qos_level; i++) {
        add_to_event_queue(new QjumpEpochEvent(params.first_flow_start_time + network_epoch[i], this, i));
    }
}

void QjumpHost::send_next_pkt(uint32_t priority) {
    int pkt_sent = 0;
    int num_channels_in_agg = params.multiplex_constant;
    // Since Qjump epoch is per QoS level, we only need simple arbitration (RR) within each QoS level
    //std::cout << "At time = " << get_current_time() << ", at Host[" << id << "], qjump host handles send_next_pkt at prio_level = " << priority << "; agg_channel_count = " << agg_channel_count << std::endl;
    while (!pkt_sent) {
        AggChannel *next_agg_channel = agg_channels[priority][agg_channel_idx[priority]];
        Channel *next_channel = next_agg_channel->pick_next_channel_RR();
        pkt_sent = next_channel->send_pkts();
        num_channels_in_agg--;
        if (pkt_sent > 0) { // have just sent a pkt
            increment_agg_channel_idx(priority);    // RR among all the channels within same priority
        } else if (pkt_sent == 0 && num_channels_in_agg == 0) { // no pkt sent in the current agg channel (agg_channels are grouped by prio)
            increment_agg_channel_idx(priority);    // reset to the prev agg channel that sent a pkt
            //busy[priority] = false;                   // put host to sleep
            break;
        }
        if (params.debug_event_info) {
            std::cout << "QjumpHost[" << id << "] picks Channel[" << next_channel->id << "], pkt_sent = " << pkt_sent << std::endl;
        }
    }
    add_to_event_queue(new QjumpEpochEvent(get_current_time() + network_epoch[priority], this, priority));
    //std::cout << "At time: " << get_current_time() << ", Host[" << id << "] schedule next QjumpEpochEvent for Priority["
    //    << priority << "] at time = " << get_current_time() + network_epoch[priority] << std::endl;
}

/*
void QjumpHost::send_next_pkt(uint32_t priority) {
    int pkt_sent = 0;
    int num_channels_in_agg = params.multiplex_constant;
    // Since Qjump epoch is per QoS level, we only need simple arbitration (RR) within each QoS level
    while (!pkt_sent) {
        AggChannel *next_agg_channel = agg_channels[priority][agg_channel_idx[priority]];
        Channel *next_channel = next_agg_channel->pick_next_channel_RR();
        pkt_sent = next_channel->send_pkts();
        num_channels_in_agg--;
        if (pkt_sent > 0) { // have just sent a pkt
            increment_agg_channel_idx(priority);    // RR among all the channels within same priority
        } else if (pkt_sent == 0 && num_channels_in_agg == 0) { // no pkt sent in the current agg channel (agg_channels are grouped by prio)
            increment_agg_channel_idx(priority);    // reset to the prev agg channel that sent a pkt
            busy[priority] = false;                   // put host to sleep
            return;
        }
        if (params.debug_event_info) {
            std::cout << "QjumpHost[" << id << "] picks Channel[" << next_channel->id << "], pkt_sent = " << pkt_sent << std::endl;
        }
    }
}
*/
