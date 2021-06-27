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
    this->prio_idx_RR = 0;
    this->agg_channel_idx_RR.resize(params.num_qos_level, 0);
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
    prio_idx_RR++;
    if (prio_idx_RR == params.num_qos_level) {
        prio_idx_RR = 0;
    }
}

void NIC::increment_agg_channel_idx() {
    agg_channel_idx_RR[prio_idx_RR]++;
    if (agg_channel_idx_RR[prio_idx_RR] == agg_channels[prio_idx_RR].size()) {
        agg_channel_idx_RR[prio_idx_RR] = 0;
    }
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

// Use RR for now when arbitrate among Channels under an AggChannel
void NIC::send_next_pkt() {
    int pkt_sent = 0;
    int num_channels_in_agg = params.multiplex_constant;
    int num_total_channels = agg_channel_count * params.multiplex_constant;
    //std::cout << "num_channels_in_agg = " << num_channels_in_agg << std::endl;
    //std::cout << "num_total_channels = " << num_total_channels << std::endl;
    while (!pkt_sent) {
        AggChannel *next_agg_channel = agg_channels[prio_idx_RR][agg_channel_idx_RR[prio_idx_RR]];
        Channel *next_channel = next_agg_channel->pick_next_channel_RR();
        pkt_sent = next_channel->nic_send_next_pkt();
        num_channels_in_agg--;
        num_total_channels--;
        if (pkt_sent > 0) { // have just sent a pkt
            // need to increment agg_channel_idx before incrementing prio_idx
            increment_agg_channel_idx();    // RR among all priority levels
            increment_prio_idx();           // RR among all the channels within same priority
        } else if (pkt_sent == 0 && num_channels_in_agg == 0) { // no pkt sent in the current agg channel
            if (num_total_channels != 0) {
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
}
