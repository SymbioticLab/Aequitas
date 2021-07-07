#ifndef CORESIM_AGG_CHANNEL_H
#define CORESIM_AGG_CHANNEL_H

#include <cstdint>
#include <deque>
#include <vector>
#include <unordered_map>

class Channel;
class Flow;
class Host;

/* AggChannel: an aggregated channel that manages the admit prob of its sub-channels in one-RPC-one-Channel impl */
class AggChannel {
    public:
        AggChannel(uint32_t id, Host *s, Host *d, uint32_t priority);
        ~AggChannel();

        void process_latency_signal(double fct_in, uint32_t flow_id, int flow_size);
        Channel *pick_next_channel_RR();

        uint32_t id;
        uint32_t priority;
        Host *src;
        Host *dst;
        std::vector<Channel *> channels;
        uint32_t channel_idx_RR;

        double admit_prob;

        //// time window for latency signals and admit prob
        uint32_t curr_memory;
        uint32_t num_misses_in_mem;
        bool collect_memory;
        double RPC_latency_target;                   // default to params.hardcoded_targets[priority]; RPC latency target
        double memory_start_time;
        double memory_time_duration;
        uint32_t num_rpcs_in_memory;
        ////
};

#endif  // CORESIM_AGG_CHANNEL_H
