#include "nic.h"

#include "agg_channel.h"
#include "channel.h"
#include "event.h"
#include "node.h"
#include "queue.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern DCExpParams params;

NIC::NIC(Host *s, double rate) {
    this->src = s;
    this->busy = false;
    this->rate = rate;
    this->agg_channel_count = 0;
    this->prio_idx = 0;
    this->agg_channel_idx.resize(params.num_qos_level, 0);
    this->WF_counters.resize(params.num_qos_level, 0);
    this->agg_channels.resize(params.num_qos_level);
    //this->ack_queue = new HostEgressQueue(src->id+100, rate, 0);
}

NIC::~NIC() {
    //delete ack_queue;
}

void NIC::set_agg_channels(AggChannel *agg_channel) {
    int prio = agg_channel->priority;
    agg_channels[prio].push_back(agg_channel);
    agg_channel_count++;
}

void NIC::increment_prio_idx() {
    prio_idx++;
    if (prio_idx == params.num_qos_level) {
        prio_idx = 0;
    }
}

void NIC::increment_WF_counters() {
    WF_counters[prio_idx]++;
    if (WF_counters[prio_idx] == params.weights[prio_idx]) {
        WF_counters[prio_idx] = 0;
    }
}

void NIC::increment_agg_channel_idx() {
    agg_channel_idx[prio_idx]++;
    if (agg_channel_idx[prio_idx] == agg_channels[prio_idx].size()) {
        agg_channel_idx[prio_idx] = 0;
    }
}

// Channel let nic knows it has pkts to send
void NIC::add_to_nic(Channel *channel) {
    //std::cout << "Host[" << src->id << "] adding Channel[" << channel->id << "] to NIC" << std::endl;
    pending_channels.push_back(channel);
    start_nic();
}

// Wake up nic if it's not busy working already
void NIC::start_nic() {
    if (busy) {
        return;
    } else {
        add_to_event_queue(new NICProcessingEvent(get_current_time(), this));
        busy = true;
    }
}

// send next set of pkts based for the next pending channel
void NIC::send_pkts() {
    uint32_t pkt_sent = 0;
    while (!pending_channels.empty()) {
        Channel *next_channel = pending_channels.front();
        pkt_sent = send_next_pkt(next_channel);
        //std::cout << "Host[" << src->id << "]: Channel[" << next_channel->id << "] send " << pkt_sent << " pkt." << std::endl;
        if (pkt_sent == 0) {
            pending_channels.pop_front();        
        } else {
            break;
        }
    }
    if (pkt_sent == 0) {
        busy = false;
    }
}

uint32_t NIC::send_next_pkt(Channel *channel) {
    uint32_t pkt_sent = 0;
    pkt_sent = channel->nic_send_next_pkt();
    return pkt_sent;
}

// Use RR for now when arbitrate among Channels under an AggChannel
/*
void NIC::send_next_pkt() {
    int pkt_sent = 0;
    int num_channels_in_agg = params.multiplex_constant;
    int num_total_channels = agg_channel_count * params.multiplex_constant;
    //std::cout << "num_channels_in_agg = " << num_channels_in_agg << std::endl;
    //std::cout << "num_total_channels = " << num_total_channels << std::endl;
    while (!pkt_sent) {
        Channel *next_channel;
        Channel *last_active_channel = NULL;
        //while (!last_active_channels.empty()) {
        for (uint32_t i = 0; i < last_active_channels.size(); i++) {
            last_active_channel = last_active_channels[i];
            //std::cout << "PUPU Host[" << src->id << "] last active channel is Channel[" << last_active_channel->id << "], its curr_flow_done is " << last_active_channel->curr_flow_done << std::endl;
            if (!last_active_channel->curr_flow_done) {
                next_channel = last_active_channel;
                pkt_sent = next_channel->nic_send_next_pkt();
                //std::cout << "PUPU0: Host[" << src->id << "] picking Channel[" << next_channel->id << "] sending " << pkt_sent << " pkt." << std::endl;
                if (pkt_sent > 0) {
                    if (last_active_channel->curr_flow_done) {
                        last_active_channels.erase(last_active_channels.begin() + i);
                    }
                    break;
                }
            }
        }
        if (pkt_sent > 0) {
            break;
        }
        //if (last_active_channel && !last_active_channel->curr_flow_done) {
        //    next_channel = last_active_channel;
        //    pkt_sent = next_channel->nic_send_next_pkt();
        //    std::cout << "PUPU0: Host[" << src->id << "] picking Channel[" << next_channel->id << "]" << std::endl;
        //    assert(pkt_sent > 0);
        //    continue;
        //}
        AggChannel *next_agg_channel = agg_channels[prio_idx][agg_channel_idx[prio_idx]];
        next_channel = next_agg_channel->pick_next_channel_RR();
        pkt_sent = next_channel->nic_send_next_pkt();
        num_channels_in_agg--;
        num_total_channels--;
        if (pkt_sent > 0) { // have just sent a pkt
            last_active_channel = next_channel;
            //std::cout << "PUPU1: Host[" << src->id << "] picking Channel[" << next_channel->id << "]; setting last_active_channel to Channel[" << last_active_channel->id << "]" << std::endl;
            //if (!last_active_channel->curr_flow_done) {
            //    std::cout << "PUPU1: Host[" << src->id << "] Channel[" << next_channel->id << "] not done sending all pkts yet." << std::endl;
            //}
            // need to increment agg_channel_idx before incrementing prio_idx
            increment_agg_channel_idx();    // RR among all the channels within same priority
            if (params.nic_use_WF) {            
                increment_WF_counters();        // WF among all priority levels
                if (WF_counters[prio_idx] == 0) {
                    increment_prio_idx();
                }
            } else {
                increment_prio_idx();           // RR among all priority levels
            }
        } else if (pkt_sent == 0 && num_channels_in_agg == 0) { // no pkt sent in the current agg channel (agg_channels are grouped by prio)
            if (num_total_channels != 0) {  // no pkt in the current agg_channels[prio_idx], but other prio levels may have one
                increment_agg_channel_idx();    // reset to the prev agg channel that sent a pkt
                increment_prio_idx();           // try the next priority
            } else {    // currently no pkt need to be served at the host
                increment_agg_channel_idx();    // reset to the prev agg channel that sent a pkt
                increment_prio_idx();           // reset to the prev priority that sent a pkt
                busy = false;                   // put nic to sleep
                if (params.debug_event_info) {
                    std::cout << "At Host[" << src->id << "], put NIC to sleep." << std::endl;
                }
                return;
            }
            num_channels_in_agg = params.multiplex_constant;
        }
        //std::cout << "pick Channel[" << next_channel->id << "], pkt_sent = " << pkt_sent << std::endl;
        //std::cout << "num_channels_in_agg = " << num_channels_in_agg << "; num_total_channels = " << num_total_channels << std::endl;
    }

    //while (!pkt_sent) {
    //    AggChannel *next_agg_channel = agg_channels[prio_idx][agg_channel_idx[prio_idx]];
    //    Channel *next_channel = next_agg_channel->pick_next_channel_RR();
    //    pkt_sent = next_channel->nic_send_next_pkt();
    //    num_channels_in_agg--;
    //    num_total_channels--;
    //    if (pkt_sent > 0) { // have just sent a pkt
    //        // need to increment agg_channel_idx before incrementing prio_idx
    //        increment_agg_channel_idx();    // RR among all the channels within same priority
    //        if (params.nic_use_WF) {            
    //            increment_WF_counters();        // WF among all priority levels
    //            if (WF_counters[prio_idx] == 0) {
    //                increment_prio_idx();
    //            }
    //        } else {
    //            increment_prio_idx();           // RR among all priority levels
    //        }
    //    } else if (pkt_sent == 0 && num_channels_in_agg == 0) { // no pkt sent in the current agg channel (agg_channels are grouped by prio)
    //        if (num_total_channels != 0) {  // no pkt in the current agg_channels[prio_idx], but other prio levels may have one
    //            increment_agg_channel_idx();    // reset to the prev agg channel that sent a pkt
    //            increment_prio_idx();           // try the next priority
    //        } else {    // currently no pkt need to be served at the host
    //            increment_agg_channel_idx();    // reset to the prev agg channel that sent a pkt
    //            increment_prio_idx();           // reset to the prev priority that sent a pkt
    //            busy = false;                   // put nic to sleep
    //            if (params.debug_event_info) {
    //                std::cout << "At Host[" << src->id << "], put NIC to sleep." << std::endl;
    //            }
    //            return;
    //        }
    //        num_channels_in_agg = params.multiplex_constant;
    //    }
    //    //std::cout << "pick Channel[" << next_channel->id << "], pkt_sent = " << pkt_sent << std::endl;
    //    //std::cout << "num_channels_in_agg = " << num_channels_in_agg << "; num_total_channels = " << num_total_channels << std::endl;
    //}
}
*/
