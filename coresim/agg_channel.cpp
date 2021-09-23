#include "agg_channel.h"

#include <cstddef>
#include <iostream>
#include <math.h>

//#include "flow.h"
#include "channel.h"
#include "node.h"
#include "../run/params.h"
#include "../ext/factory.h"

extern double get_current_time();
extern DCExpParams params;
extern uint32_t num_bad_QoS_H_RPCs;
extern std::vector<double> qos_h_admit_prob;
extern std::vector<double> total_qos_h_admit_prob;
extern std::vector<std::vector<double>> qos_h_admit_prob_per_host;
extern std::vector<uint32_t> qos_h_memory_misses;
extern std::vector<std::vector<double>> fairness_qos_h_admit_prob_per_host;
extern std::vector<std::vector<double>> fairness_qos_h_ts_per_host;
extern std::vector<std::vector<double>> fairness_qos_h_rates_per_host;
extern std::vector<uint32_t> fairness_qos_h_bytes_per_host;
extern std::vector<double> fairness_last_check_time;

AggChannel::AggChannel(uint32_t id, Host *s, Host *d, uint32_t priority) {
    this->id = id;
    this->src = s;
    this->dst = d;
    this->priority = priority;
    this->channel_idx_RR = 0;

    this->admit_prob = 1;
    this->curr_memory = 0;
    this->collect_memory = false;
    this->memory_start_time = 0;
    if (params.smart_time_window) {
        uint32_t duration = params.hardcoded_targets[priority] * params.target_pctl;
        this->memory_time_duration = duration;
    } else {
        this->memory_time_duration = params.memory_time_duration;  // in us
    }
    this->num_rpcs_in_memory = 0;

    this->num_misses_in_mem = 0;
    this->RPC_latency_target = params.hardcoded_targets[priority];

    if (s == d) {
        std::cout << "src dst can't be equal for a channel. Abort" << std::endl;
        exit(1);
    }

    // Create Channels (under AggChannel)
    if (params.channel_multiplexing) {
        for (uint32_t i = 0; i < params.multiplex_constant; i++) {
            //channels.push_back(new Channel(id, src, dst, priority, this));
            channels.push_back(Factory::get_channel(id, src, dst, priority, this, params.flow_type));
        }
    }
}

AggChannel::~AggChannel() {
    for (auto x : channels) {
        delete x;
    }
}

void AggChannel::process_latency_signal(double fct_in, uint32_t flow_id, int flow_size) {
    if (params.normalized_lat) {
        fct_in = fct_in / flow_size;
    }
    if (fct_in > RPC_latency_target) {    // increment with incoming miss
        num_misses_in_mem++;        // assuming entire window >= memory size; Idea 1
        if (priority == 0) {
            num_bad_QoS_H_RPCs++;
        }
    }
    num_rpcs_in_memory++;

    // Idea2: count by time
    double current_memory_time = get_current_time();
    if ((current_memory_time - memory_start_time) * 1e6 > memory_time_duration || num_misses_in_mem > 0) {
        collect_memory = true;
        memory_start_time = current_memory_time;
    }

    ////collect_memory = false;  // hack to not enter the following if loop for now (to try the new ver of algorithm)
    if (collect_memory) {
        if (num_misses_in_mem == 0) {
            admit_prob += params.dp_alpha;
            if (admit_prob > 1) {
                admit_prob = 1;
            }
        } else {
            //// Always use normalized beta value; i.e., always do dp_beta * flow_size regardless of the value of params.normalized_lat
            admit_prob -= params.dp_beta * flow_size;
            /*
            if (!params.normalized_lat) {
                admit_prob -= params.dp_beta;
            } else {
                admit_prob -= params.dp_beta * flow_size;    // flow_size is in # of MTUs for now
            }
            */
            if (admit_prob < 0.1) {
                admit_prob = 0.1;
            }
        }

        if (priority == 0) {
            qos_h_admit_prob.push_back(admit_prob);
            total_qos_h_admit_prob.push_back(admit_prob);
            qos_h_admit_prob_per_host[src->id].push_back(admit_prob);
            qos_h_memory_misses.push_back(num_misses_in_mem);
            if (params.test_fairness) {
                double time_elapsed  = get_current_time() - fairness_last_check_time[src->id];
                if (time_elapsed > 50000 / 1e6) {  // 50ms interval for rate measurement in 2-flow fairness
                ////if (time_elapsed > 20000 / 1e6) {  // 20ms interval for rate measurement in 33-node fairness
                    fairness_qos_h_admit_prob_per_host[src->id].push_back(admit_prob);
                    fairness_qos_h_ts_per_host[src->id].push_back(get_current_time());
                    double rate = (double) fairness_qos_h_bytes_per_host[src->id] / time_elapsed;
                    fairness_qos_h_rates_per_host[src->id].push_back(rate);
                    fairness_last_check_time[src->id] = get_current_time();
                    fairness_qos_h_bytes_per_host[src->id] = 0;
                }

            }
        }

        num_misses_in_mem = 0;
        num_rpcs_in_memory = 0;
        collect_memory = false;
    }
}

Channel* AggChannel::pick_next_channel_RR() {
    Channel* next_channel = channels[channel_idx_RR++];
    if (channel_idx_RR == channels.size()) {
        channel_idx_RR = 0;
    }
    return next_channel;
}
