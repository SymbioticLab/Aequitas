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

// From Qjump's paper: network epoch = worse-case e2e delay <= num_hosts x (max_packet_size / rate) + cumulative_processing_delay
QjumpHost::QjumpHost(uint32_t id, double rate, uint32_t queue_type, uint32_t host_type)
        : Host(id, rate, queue_type, QJUMP_HOST) {
    this->network_epoch = params.num_hosts * (params.mss * 8.0 / params.bandwidth) + params.qjump_cumulative_pd;    //TODO: double check this
    this->busy = false;
    this->agg_channel_count = 0;
    this->prio_idx = 0;
    this->agg_channel_idx.resize(params.num_qos_level, 0);
    this->WF_counters.resize(params.num_qos_level, 0);
    this->agg_channels.resize(params.num_qos_level);
    if (params.debug_event_info) {
        std::cout << "Qjump epoch = " << network_epoch << " us." << std::endl;
    }
}

QjumpHost::~QjumpHost() {
}

void QjumpHost::set_agg_channels(AggChannel *agg_channel) {
    int prio = agg_channel->priority;
    agg_channels[prio].push_back(agg_channel);
    agg_channel_count++;
}

void QjumpHost::increment_prio_idx() {
    prio_idx++;
    if (prio_idx == params.num_qos_level) {
        prio_idx = 0;
    }
}

void QjumpHost::increment_WF_counters() {
    WF_counters[prio_idx]++;
    if (WF_counters[prio_idx] == params.weights[prio_idx]) {
        WF_counters[prio_idx] = 0;
    }
}

void QjumpHost::increment_agg_channel_idx() {
    agg_channel_idx[prio_idx]++;
    if (agg_channel_idx[prio_idx] == agg_channels[prio_idx].size()) {
        agg_channel_idx[prio_idx] = 0;
    }
}

void QjumpHost::start_next_epoch() {
    if (busy) {
        return;
    } else {
        add_to_event_queue(new QjumpEpochEvent(get_current_time(), this));
        busy = true;
    }
}

// Use RR for now when arbitrate among Channels under an AggChannel
void QjumpHost::send_next_pkt() {
    int pkt_sent = 0;
    int num_channels_in_agg = params.multiplex_constant;
    int num_total_channels = agg_channel_count * params.multiplex_constant;
    //std::cout << "num_channels_in_agg = " << num_channels_in_agg << std::endl;
    //std::cout << "num_total_channels = " << num_total_channels << std::endl;
    while (!pkt_sent) {
        AggChannel *next_agg_channel = agg_channels[prio_idx][agg_channel_idx[prio_idx]];
        Channel *next_channel = next_agg_channel->pick_next_channel_RR();
        pkt_sent = next_channel->nic_send_next_pkt();
        num_channels_in_agg--;
        num_total_channels--;
        if (pkt_sent > 0) { // have just sent a pkt
            // need to increment agg_channel_idx before incrementing prio_idx
            increment_agg_channel_idx();    // RR among all the channels within same priority
            increment_prio_idx();           // RR among all priority levels
            /*
            if (params.nic_use_WF) {            
                increment_WF_counters();        // WF among all priority levels
                if (WF_counters[prio_idx] == 0) {
                    increment_prio_idx();
                }
            } else {
                increment_prio_idx();           // RR among all priority levels
            }
            */
        } else if (pkt_sent == 0 && num_channels_in_agg == 0) { // no pkt sent in the current agg channel (agg_channels are grouped by prio)
            if (num_total_channels != 0) {  // no pkt in the current agg_channels[prio_idx], but other prio levels may have one
                increment_agg_channel_idx();    // reset to the prev agg channel that sent a pkt
                increment_prio_idx();           // try the next priority
            } else {    // currently no pkt need to be served at the host
                increment_agg_channel_idx();    // reset to the prev agg channel that sent a pkt
                increment_prio_idx();           // reset to the prev priority that sent a pkt
                busy = false;                   // put host to sleep
                if (params.debug_event_info) {
                    std::cout << "At Host[" << id << "], put host to sleep." << std::endl;
                }
                return;
            }
            num_channels_in_agg = params.multiplex_constant;
        }
        //std::cout << "pick Channel[" << next_channel->id << "], pkt_sent = " << pkt_sent << std::endl;
        //std::cout << "num_channels_in_agg = " << num_channels_in_agg << "; num_total_channels = " << num_total_channels << std::endl;
    }
}
