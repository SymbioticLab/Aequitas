#include "params.h"

#include <assert.h>
#include <iostream>
#include <fstream>
#include <sstream>

DCExpParams params;

/* Read parameters from a config file */
void read_experiment_parameters(std::string conf_filename, uint32_t exp_type) {
    std::ifstream input(conf_filename);
    std::string line;
    std::string key;
    params.interarrival_cdf = "none";
    params.permutation_tm = 0;
    params.hdr_size = 40;
    params.num_hosts = 144;
    params.num_agg_switches = 9;
    params.num_core_switches = 4;
    params.weights = std::vector<int>();
    params.qos_ratio = std::vector<double>();
    params.buffer_carving = std::vector<int>();
    params.use_dynamic_load = 0;
    params.use_random_jitter = 0;
    params.random_flow_start = 1;
    params.early_pkt_in_highest_prio = 0;
    //params.load_change_freq = 100;
    params.burst_size = 100;        // default value
    params.flushing_coefficient = 1;
    params.disable_Veritas_cc = 0;
    params.traffic_pattern = 0;
    params.cc_delay_target = 50;
    params.big_switch = 0;
    params.multi_switch = 0;
    params.mtu = 5000;
    //params.dynamic_load = std::vector<double>();
    while (std::getline(input, line)) {
        std::istringstream lineStream(line);
        if (line.empty()) {
            continue;
        }


        lineStream >> key;
        assert(key[key.length()-1] == ':');
        key = key.substr(0, key.length()-1);
        if (key == "init_cwnd") {
            lineStream >> params.initial_cwnd;
        }
        else if (key == "max_cwnd") {
            lineStream >> params.max_cwnd;
        }
        else if (key == "retx_timeout") {
            double timeout;
            lineStream >> timeout;
            params.retx_timeout_value = timeout / 1e6;
        }
        else if (key == "queue_size") {
            lineStream >> params.queue_size;
        }
        else if (key == "propagation_delay") {
            lineStream >> params.propagation_delay;
        }
        else if (key == "bandwidth") {
            lineStream >> params.bandwidth;
        }
        else if (key == "queue_type") {
            lineStream >> params.queue_type;
        }
        else if (key == "flow_type") {
            lineStream >> params.flow_type;
        }
        else if (key == "num_flow") {
            lineStream >> params.num_flows_to_run;
        }
        else if (key == "num_hosts") {
            lineStream >> params.num_hosts;
        }
        else if (key == "num_agg_switches") {
            lineStream >> params.num_agg_switches;
        }
        else if (key == "num_core_switches") {
            lineStream >> params.num_core_switches;
        }
        else if (key == "flow_trace") {
            lineStream >> params.cdf_or_flow_trace;
        }
        else if (key == "cut_through") {
            lineStream >> params.cut_through;
        }
        else if (key == "mean_flow_size") {
            lineStream >> params.mean_flow_size;
        }
        else if (key == "load_balancing") {
            lineStream >> params.load_balancing;
        }
        else if (key == "disable_Veritas_cc") {
            lineStream >> params.disable_Veritas_cc;
        }
        else if (key == "traffic_pattern") {
            lineStream >> params.traffic_pattern;
        }
        else if (key == "use_dynamic_load") {
            lineStream >> params.use_dynamic_load;
        }
        else if (key == "use_random_jitter") {
            lineStream >> params.use_random_jitter;
        }
        else if (key == "random_flow_start") {
            lineStream >> params.random_flow_start;
        }
        else if (key == "early_pkt_in_highest_prio") {
            lineStream >> params.early_pkt_in_highest_prio;
        }
        else if (key == "flushing_coefficient") {
            lineStream >> params.flushing_coefficient;
        }
        else if (key == "cc_delay_target") {
            lineStream >> params.cc_delay_target;
        }
        //else if (key == "load_change_freq") {
        //    lineStream >> params.load_change_freq;
        //}
        else if (key == "burst_size") {
            lineStream >> params.burst_size;
        }
        else if (key == "preemptive_queue") {
            lineStream >> params.preemptive_queue;
        }
        else if (key == "big_switch") {
            lineStream >> params.big_switch;
        }
        else if (key == "multi_switch") {
            lineStream >> params.multi_switch;
        }
        else if (key == "mtu") {
            lineStream >> params.mtu;
        }
        else if (key == "host_type") {
            lineStream >> params.host_type;
        }
        else if (key == "imbalance") {
            lineStream >> params.traffic_imbalance;
        }
        else if (key == "load") {
            lineStream >> params.load;
        }
        else if (key == "burst_load") {
            lineStream >> params.burst_load;
        }
        else if (key == "traffic_imbalance") {
            lineStream >> params.traffic_imbalance;
        }
        else if (key == "reauth_limit") {
            lineStream >> params.reauth_limit;
        }
        else if (key == "magic_trans_slack") {
            lineStream >> params.magic_trans_slack;
        }
        else if (key == "magic_delay_scheduling") {
            lineStream >> params.magic_delay_scheduling;
        }
        else if (key == "capability_timeout") {
            lineStream >> params.capability_timeout;
        }
        else if (key == "use_flow_trace") {
            lineStream >> params.use_flow_trace;
        }
        else if (key == "smooth_cdf") {
            lineStream >> params.smooth_cdf;
        }
        else if (key == "burst_at_beginning") {
            lineStream >> params.burst_at_beginning;
        }
        else if (key == "capability_resend_timeout") {
            lineStream >> params.capability_resend_timeout;
        }
        else if (key == "capability_initial") {
            lineStream >> params.capability_initial;
        }
        else if (key == "capability_window") {
            lineStream >> params.capability_window;
        }
        else if (key == "capability_prio_thresh") {
            lineStream >> params.capability_prio_thresh;
        }
        else if (key == "capability_third_level") {
            lineStream >> params.capability_third_level;
        }
        else if (key == "capability_fourth_level") {
            lineStream >> params.capability_fourth_level;
        }
        else if (key == "capability_window_timeout") {
            lineStream >> params.capability_window_timeout;
        }
        else if (key == "ddc") {
            lineStream >> params.ddc;
        }
        else if (key == "ddc_cpu_ratio") {
            lineStream >> params.ddc_cpu_ratio;
        }
        else if (key == "ddc_mem_ratio") {
            lineStream >> params.ddc_mem_ratio;
        }
        else if (key == "ddc_disk_ratio") {
            lineStream >> params.ddc_disk_ratio;
        }
        else if (key == "ddc_normalize") {
            lineStream >> params.ddc_normalize;
        }
        else if (key == "ddc_type") {
            lineStream >> params.ddc_type;
        }
        else if (key == "deadline") {
            lineStream >> params.deadline;
        }
        else if (key == "schedule_by_deadline") {
            lineStream >> params.schedule_by_deadline;
        }
        else if (key == "avg_deadline") {
            lineStream >> params.avg_deadline;
        }
        else if (key == "magic_inflate") {
            lineStream >> params.magic_inflate;
        }
        else if (key == "interarrival_cdf") {
            lineStream >> params.interarrival_cdf;
        }
        else if (key == "num_host_types") {
            lineStream >> params.num_host_types;
        }
        else if (key == "permutation_tm") {
            lineStream >> params.permutation_tm;
        }
        else if (key == "dctcp_mark_thresh") {
            lineStream >> params.dctcp_mark_thresh;
        }
        else if (key == "hdr_size") {
            lineStream >> params.hdr_size;
            assert(params.hdr_size > 0);
        }
        else if (key == "bytes_mode") {
            lineStream >> params.bytes_mode;
        }
        //else if (key == "dctcp_delayed_ack_freq") {
        //    lineStream >> params.dctcp_delayed_ack_freq;
        //}
        //// weights format in config file: for example, for 3 qos levels with weights 8:2:1, write:
        //// qos_weights: 8,2,1
        else if (key == "qos_weights") {
            std::string temp_str;
            lineStream >> temp_str;
            std::stringstream ss(temp_str);
            while (ss.good()) {
                std::string weight;
                getline(ss, weight, ',');
                params.weights.push_back(stoi(weight));
            }
            std::cout << "QoS weights: ";
            for (auto i: params.weights) {
                std::cout << i << " ";
            }
            std::cout << std::endl;

            params.sum_weights = 0;
            for (int i = 0; i < params.weights.size(); i++) {
                assert(params.weights[i] != 0);
                params.sum_weights += params.weights[i];
            }
            std::cout << "Sum weights = " << params.sum_weights << std::endl;
        }
        //// qos_ratio: [30,70] means 30% of the flow gets first priority, 70% of the flows gets 2nd priority
        //// (in this case you can also write [3,7] as long as the ratio is what you want)
        //// number of entries should match that of qos_weights
        //// TODO: make it ratio in terms of bytes not number of flows
        else if (key == "qos_ratio") {
            std::string temp_str;
            lineStream >> temp_str;
            std::stringstream ss(temp_str);
            double ratio_sum = 0;
            std::vector<int> temp_vec;
            while (ss.good()) {
                std::string qos_ratio_str;
                getline(ss, qos_ratio_str, ',');
                int qos_ratio_int = stoi(qos_ratio_str);
                ratio_sum += qos_ratio_int;
                temp_vec.push_back(qos_ratio_int);
            }
            // normalize qos ratio
            for (auto i: temp_vec) {
                params.qos_ratio.push_back(i / ratio_sum);
            }
            std::cout << "QoS ratio: ";
            for (auto i: params.qos_ratio) {
                std::cout << i << " ";
            }
            std::cout << std::endl;
        }
        // Vector for buffer carving: [1, 2, 3] means 1/6th space for prio_1, 2/6th space for prio_2, the rest for prio_3. 
        else if (key == "buffer_carving") {
            std::string temp_str;
            lineStream >> temp_str;
            std::stringstream ss(temp_str);
            while (ss.good()) {
                std::string buffer_carve_ratio;
                getline(ss, buffer_carve_ratio, ',');
                params.buffer_carving.push_back(stoi(buffer_carve_ratio));
            }
            std::cout << "Buffer carving: ";
            for (auto i: params.buffer_carving) {
                std::cout << i << " ";
            }
            std::cout << std::endl;
        }
        //else if (key == "dynamic_load") {
        //    std::string temp_str;
        //    lineStream >> temp_str;
        //    std::stringstream ss(temp_str);
        //    while (ss.good()) {
        //        std::string load_val;
        //        getline(ss, load_val, ',');
        //        params.dynamic_load.push_back(stod(load_val));
        //    }
        //}
        else {
            std::cout << "Unknown conf param: " << key << " in file: " << conf_filename << "\n";
            assert(false);
        }


        params.fastpass_epoch_time = 1500 * 8 * (FASTPASS_EPOCH_PKTS + 0.5) / params.bandwidth;

        params.param_str.append(line);
        params.param_str.append(", ");
    }
    if (params.use_dynamic_load) {
        std::cout << "Use dynamic load." << std::endl;
        //assert(!params.dynamic_load.empty());
        //std::cout << "Dynamic load: ";
        //for (auto i: params.dynamic_load) {
        //    std::cout << i << " ";
        //}
        //std::cout << std::endl;
        //std::cout << "load change frequency: " << params.load_change_freq << std::endl;
        std::cout << "Avg load to achieve = " << params.load << std::endl;
        std::cout << "burst size (# of RPCs) in the dynamic load setting: " << params.burst_size << std::endl;
        std::cout << "burst load = " << params.burst_load << std::endl;
        if (params.use_random_jitter) {
            std::cout << "Use random jitter." << std::endl;
        }
    } else {
        std::cout << "Load: " << params.load << std::endl;
    }
    std::cout << "CDF: " << params.cdf_or_flow_trace << std::endl;
    std::cout << "Exponential Random Flow Start Time: " << params.random_flow_start << std::endl;
    if (params.early_pkt_in_highest_prio) {
        std::cout << "early_pkt_in_highest_prio is enabled." << std::endl;
        // this option should not be enabled when all qos weights are intially set to be equal
        bool check_equal = true;
        for (int i = 0; i < params.weights.size(); i++) {
            if (params.weights[i] != params.weights[0]) {
                check_equal = false;
                break;
            }
        }
        if (check_equal) {
            std::cout << "Error! Option \"early_pkt_in_highest_prio\" should not be enabled when all QoS weights are intially set to be equal." << std::endl;
            assert(false);
        }
    }
    if (params.disable_Veritas_cc) {
        std::cout << "Veritas Flow: disable CC" << std::endl;
    }
    std::cout << "Init cwnd: " << params.initial_cwnd << std::endl;
    std::cout << "Max cwnd: " << params.max_cwnd << std::endl;
    std::cout << "Num Hosts: " << params.num_hosts << std::endl;
    std::cout << "Num total RPCs to run: " << params.num_flows_to_run << std::endl;
    if (params.traffic_pattern == 0) {
        std::cout << "traffic pattern: incast" << std::endl;
    } else if (params.traffic_pattern == 1) {
        std::cout << "traffic pattern: all-to-all" << std::endl;
    }
    std::cout << "mtu: " << params.mtu << std::endl;
    std::cout << "mss: " << params.mss << std::endl;
    std::cout << "Swift Delay target: " << params.cc_delay_target << " us" << std::endl;
    assert(params.burst_size > 0);
    //std::cout << "Flushing Coefficient = " << params.flushing_coefficient << std::endl;
    std::cout << "Retransmission Timeout: " << params.retx_timeout_value * 1e6 << " us" << std::endl;

    params.mss = params.mtu - params.hdr_size;
}
