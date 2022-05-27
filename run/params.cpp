#include "params.h"

#include <assert.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "../ext/factory.h"

DCExpParams params;

/* Read parameters from a config file */
void read_experiment_parameters(std::string conf_filename, uint32_t exp_type) {
    std::cout << "PUPU: config filename: " << conf_filename << std::endl;
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
    params.targets = std::vector<double>();
    params.hardcoded_targets = std::vector<double>();
    params.qos_ratio = std::vector<double>();
    params.buffer_carving = std::vector<int>();
    params.use_dynamic_load = 0;
    params.use_random_jitter = 0;
    params.first_flow_start_time = 1.0;     // just need to be put into a para to make it consistent in all places
    //params.random_flow_start = 1;
    params.early_pkt_in_highest_prio = 0;
    //params.load_change_freq = 100;
    params.burst_size = 100;        // dult value
    params.use_burst_byte = 0;
    params.burst_with_no_spacing = 0;
    params.channel_multiplexing = 0;
    params.multiplex_constant = 1;
    params.flushing_coefficient = 1;
    params.disable_poisson_arrival = 0;    // default: enable poisson
    params.disable_aequitas_cc = 0;
    params.traffic_pattern = 0;
    params.cc_delay_target = 10;
    params.high_prio_lat_target = 10;
    params.target_expiration = 50;
    params.load_measure_interval = 1;        // in us;
    params.qd_expiration = 50;
    params.rtt_expiration = 50;
    params.dwnd_alpha = 3;
    params.uwnd_beta = 3;
    params.dp_alpha = 0.01;
    params.dp_beta = 0.1;
    params.downgrade_batch_size = 10;
    params.upgrade_batch_size = 10;
    params.expiration_count = 150;
    params.qd_num_hops = 2;
    params.num_pctl = 10;
    params.memory_time_duration = 60000;
    params.smart_time_window = 0;
    params.target_pctl = 1000;
    params.normalized_lat = 0;
    params.print_normalized_result = 0;
    params.big_switch = 0;
    params.disable_pkt_logging = 0;
    params.only_sw_queue = 0;
    params.load_balancing = 0;
    params.preemptive_queue = 0;
    params.test_fairness = 0;
    params.test_size_to_priority = 0;
    params.disable_cwnd_logging = 0;
    params.disable_dwnd_logging = 1;
    params.track_qosh_dwnds = 0;
    params.priority_downgrade = 0;
    params.debug_event_info = 0;
    params.enable_flow_lookup = 0;
    params.flow_lookup_id = 0;
    params.mtu = 5120;
    params.real_nic = 0;
    params.nic_use_WF = 0;
    params.qjump_cumulative_pd = 1;
    params.enable_qjump_retransmission = 0;
    params.qjump_tput_factor = std::vector<int>();
    params.early_termination = 0;
    params.pfabric_limited_priority = 0;
    params.cdf_info = 0;
    params.homa_sampling_freq = 5000;
    params.homa_rttbytes_in_mss = 100;   // assuming 100Gbps network
    //params.enable_initial_shift = 0;
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
        else if (key == "load_measure_interval") {
            double interval = 0;
            lineStream >> interval;
            params.load_measure_interval = interval / 1e6;
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
        else if (key == "disable_poisson_arrival") {
            lineStream >> params.disable_poisson_arrival;
        }
        else if (key == "disable_aequitas_cc") {
            lineStream >> params.disable_aequitas_cc;
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
        //else if (key == "random_flow_start") {
        //    lineStream >> params.random_flow_start;
        //}
        else if (key == "early_pkt_in_highest_prio") {
            lineStream >> params.early_pkt_in_highest_prio;
        }
        else if (key == "flushing_coefficient") {
            lineStream >> params.flushing_coefficient;
        }
        else if (key == "cc_delay_target") {
            lineStream >> params.cc_delay_target;
        }
        else if (key == "qjump_cumulative_pd") {
            lineStream >> params.qjump_cumulative_pd;
        }
        else if (key == "enable_qjump_retransmission") {
            lineStream >> params.enable_qjump_retransmission;
        }
        else if (key == "high_prio_lat_target") {
            lineStream >> params.high_prio_lat_target;
        }
        else if (key == "target_expiration") {
            lineStream >> params.target_expiration;
        }
        else if (key == "qd_expiration") {
            lineStream >> params.qd_expiration;
        }
        else if (key == "rtt_expiration") {
            lineStream >> params.rtt_expiration;
        }
        else if (key == "qd_num_hops") {
            lineStream >> params.qd_num_hops;
            std::cout << "qd num_hops hint = " << params.qd_num_hops << std::endl;
        }
        else if (key == "num_pctl") {
            lineStream >> params.num_pctl;
            std::cout << "num pctl for qos dist breakdown = " << params.num_pctl << std::endl;
        }
        else if (key == "memory_time_duration") {
            lineStream >> params.memory_time_duration;
            std::cout << "memory time duration (us) = " << params.memory_time_duration << std::endl;
        }
        else if (key == "smart_time_window") {
            lineStream >> params.smart_time_window;
        }
        else if (key == "target_pctl") {
            lineStream >> params.target_pctl;
        }
        else if (key == "normalized_lat") {
            lineStream >> params.normalized_lat;
        }
        else if (key == "print_normalized_result") {
            lineStream >> params.print_normalized_result;
        }
        else if (key == "dwnd_alpha") {
            lineStream >> params.dwnd_alpha;
        }
        else if (key == "dp_alpha") {
            lineStream >> params.dp_alpha;
        }
        else if (key == "dp_beta") {
            lineStream >> params.dp_beta;
        }
        else if (key == "downgrade_batch_size") {
            lineStream >> params.downgrade_batch_size;
        }
        else if (key == "upgrade_batch_size") {
            lineStream >> params.upgrade_batch_size;
        }
        else if (key == "expiration_count") {
            lineStream >> params.expiration_count;
        }
        //else if (key == "load_change_freq") {
        //    lineStream >> params.load_change_freq;
        //}
        else if (key == "burst_size") {
            lineStream >> params.burst_size;
        }
        else if (key == "use_burst_byte") {
            lineStream >> params.use_burst_byte;
        }
        else if (key == "burst_with_no_spacing") {
            lineStream >> params.burst_with_no_spacing;
        }
        else if (key == "channel_multiplexing") {
            lineStream >> params.channel_multiplexing;
        }
        else if (key == "multiplex_constant") {
            lineStream >> params.multiplex_constant;
        }
        else if (key == "preemptive_queue") {
            lineStream >> params.preemptive_queue;
        }
        else if (key == "big_switch") {
            lineStream >> params.big_switch;
        }
        else if (key == "disable_pkt_logging") {
            lineStream >> params.disable_pkt_logging;
        }
        else if (key == "disable_cwnd_logging") {
            lineStream >> params.disable_cwnd_logging;
        }
        else if (key == "disable_dwnd_logging") {
            lineStream >> params.disable_dwnd_logging;
        }
        else if (key == "only_sw_queue") {
            lineStream >> params.only_sw_queue;
        }
        else if (key == "test_fairness") {
            lineStream >> params.test_fairness;
        }
        else if (key == "test_size_to_priority") {
            lineStream >> params.test_size_to_priority;
        }
        else if (key == "track_qosh_dwnds") {
            lineStream >> params.track_qosh_dwnds;
        }
        else if (key == "debug_event_info") {
            lineStream >> params.debug_event_info;
        }
        else if (key == "enable_flow_lookup") {
            lineStream >> params.enable_flow_lookup;
        }
        else if (key == "flow_lookup_id") {
            lineStream >> params.flow_lookup_id;
        }
        else if (key == "mtu") {
            lineStream >> params.mtu;
        }
        else if (key == "real_nic") {
            lineStream >> params.real_nic;
        }
        else if (key == "nic_use_WF") {
            lineStream >> params.nic_use_WF;
        }
        //else if (key == "enable_initial_shift") {
        //    lineStream >> params.enable_initial_shift;
        //}
        else if (key == "priority_downgrade") {
            lineStream >> params.priority_downgrade;
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
        else if (key == "homa_sampling_freq") {
            lineStream >> params.homa_sampling_freq;
        }
        else if (key == "homa_rttbytes_in_mss") {
            lineStream >> params.homa_rttbytes_in_mss;
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
            params.sum_weights = 0;
            std::cout << "QoS weights: ";
            for (int i = 0; i < params.weights.size(); i++) {
                std::cout << params.weights[i] << " ";
                params.sum_weights += params.weights[i];
            }
            std::cout << std::endl;

            std::cout << "Sum weights = " << params.sum_weights << std::endl;
            std::cout << "Fair Share Ratio = ";

            for (int i = 0; i < params.weights.size(); i++) {
                if (i < params.weights.size() - 1) {
                    std::cout << (double) params.weights[i] / params.sum_weights << " : ";
                } else {
                    std::cout << (double) params.weights[i] / params.sum_weights << std::endl;
                }
            }
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
            std::vector<double> temp_vec;
            while (ss.good()) {
                std::string qos_ratio_str;
                getline(ss, qos_ratio_str, ',');
                //int qos_ratio_int = stoi(qos_ratio_str);
                double qos_ratio_double = stod(qos_ratio_str);
                ratio_sum += qos_ratio_double;
                temp_vec.push_back(qos_ratio_double);
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
        else if (key == "hardcoded_targets") {
            std::string temp_str;
            lineStream >> temp_str;
            std::stringstream ss(temp_str);
            while (ss.good()) {
                std::string target;
                getline(ss, target, ',');
                params.hardcoded_targets.push_back(stod(target));
            }
            std::cout << "Downgrade Hardcoded Targets: ";
            for (auto i: params.hardcoded_targets) {
                std::cout << i << " ";
            }
            std::cout << std::endl;
        }
        else if (key == "targets") {
            std::string temp_str;
            lineStream >> temp_str;
            std::stringstream ss(temp_str);
            while (ss.good()) {
                std::string target;
                getline(ss, target, ',');
                params.targets.push_back(stod(target));
            }
            std::cout << "Final targets (unnormalized latency) (for counting traffic meeting SLOs): ";
            for (auto i: params.targets) {
                std::cout << i << " ";
            }
            std::cout << std::endl;
        }
        else if (key == "qjump_tput_factor") {
            std::string temp_str;
            lineStream >> temp_str;
            std::stringstream ss(temp_str);
            while (ss.good()) {
                std::string factor;
                getline(ss, factor, ',');
                params.qjump_tput_factor.push_back(stoi(factor));
            }
            std::cout << "Qjump throughput factors: ";
            for (auto i: params.qjump_tput_factor) {
                std::cout << i << " ";
            }
            std::cout << std::endl;
        }
        else if (key == "early_termination") {
            lineStream >> params.early_termination;
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
        else if (key == "pfabric_priority_type") {
            lineStream >> params.pfabric_priority_type;
        }
        else if (key == "pfabric_limited_priority") {
            lineStream >> params.pfabric_limited_priority;
            if (params.pfabric_limited_priority) {
                std::cout << "Pfabric uses limited priority levels." << std::endl;
            } else {
                std::cout << "Pfabric uses unlimited priority levels." << std::endl;
            }
        }
        else if (key == "cdf_info") {
            lineStream >> params.cdf_info;
        }
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
        if (params.use_burst_byte) {
            std::cout << "burst size (# of bytes) in the dynamic load setting: " << params.burst_size << std::endl;
        } else {
            std::cout << "burst size (# of RPCs) in the dynamic load setting: " << params.burst_size << std::endl;
        }
        if (params.burst_with_no_spacing) {
            std::cout << "Heavy burst mode: RPCs with the same burst period arrives at the same time (with tiny delay)" << std::endl;
        }
        if (params.channel_multiplexing) {
            std::cout << "Using Channel Multiplexing. Multiplexing Constant = " << params.multiplex_constant << std::endl;
        }
        std::cout << "burst load = " << params.burst_load << std::endl;
        if (params.use_random_jitter) {
            std::cout << "Use random jitter." << std::endl;
        }
    } else {
        std::cout << "Load: " << params.load << std::endl;
    }
    std::cout << "CDF: " << params.cdf_or_flow_trace << std::endl;
    //std::cout << "Exponential Random Flow Start Time: " << params.random_flow_start << std::endl;
    params.num_qos_level = params.weights.size();
    if (params.host_type == 2) {
        std::cout << "use pFabric host" << std::endl;
    } else if (params.host_type == 6) {
        std::cout << "use Aequitas(WFQ) host" << std::endl;
    }
    if (params.num_qos_level < 1) {
        std::cout << "please specify valid qos weights " << std::endl;
    }
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
    std::cout << "Per port queue size: " << params.queue_size << " Bytes" << std::endl;
    if (params.disable_aequitas_cc) {
        std::cout << "Aequitas Flow: disable CC" << std::endl;
    }
    if (params.disable_poisson_arrival) {
        std::cout << "Disable Poisson Arrival; use initial shift instead" << std::endl;
    } else {
        std::cout << "Enable Poisson Arrival; don't use initial shift" << std::endl;
    }
    if (params.disable_pkt_logging) {
        std::cout << "Disable Packet Logging (to save memory)" << std::endl;
    }
    if (params.disable_cwnd_logging) {
        std::cout << "Disable CWND Logging (to save memory)" << std::endl;
    }
    if (params.disable_dwnd_logging) {
        std::cout << "Disable DWND Logging (to save memory)" << std::endl;
    }
    if (params.only_sw_queue) {
        std::cout << "only use the switch queue in BigSwitchTopo (skip other host queues)" << std::endl;
    }
    if (params.test_fairness) {
        std::cout << "testing fairness with disproportional qos dist" << std::endl;
        params.fairness_qos_dist.resize(params.weights.size(), 0);
    }
    if (params.test_size_to_priority == 1) {
        if (params.test_fairness) {
            std::cout << "can't test together with fairness" << std::endl;
            exit(1);
        }
        std::cout << "Extra size experiment: size corresponds to priority (please use an appropriate rpc size CDF)" << std::endl;
    } else if (params.test_size_to_priority == 2) {
        if (params.test_fairness) {
            std::cout << "can't test together with fairness" << std::endl;
            exit(1);
        }
        std::cout << "Extra size experiment: size does not correspond to priority (please use an appropriate rpc size CDF)" << std::endl;
    }
    if (params.track_qosh_dwnds) {
        std::cout << "Track QoS_H DWNDS" << std::endl;
    }
    if (params.debug_event_info) {
        std::cout << "print event info." << std::endl;
    }
    if (params.enable_flow_lookup) {
        std::cout << "Enable flow lookup. lookup flow id = " << params.flow_lookup_id << std::endl;
    }
    //if (params.enable_initial_shift) {
    //    std::cout << "Enable initial shift." << std::endl;
    //} else {
    //    std::cout << "Disable initial shift." << std::endl;
    //}
    if (params.smart_time_window) {
        std::cout << "Enable smart time window. Adjust per qos window time duration based on target X " << params.target_pctl << std::endl;
    }
    if (params.normalized_lat) {
        std::cout << "Enable latency measurement (in the simulation results) by RPC size (# of MTUs)" << std::endl;
    }
    if (params.priority_downgrade) {
        std::cout << "Enable Priority Downgrade." << std::endl;
        std::cout << "target expires in " << params.target_expiration << " us" << std::endl;
        std::cout << "qd expires in " << params.qd_expiration << " us" << std::endl;
        std::cout << "qd estimated from rtt expires in " << params.rtt_expiration << " us" << std::endl;
        //std::cout << "dwnd alpha: " <<  params.dwnd_alpha << std::endl;
        std::cout << "dp alpha: " <<  params.dp_alpha << std::endl;
        std::cout << "dp beta: " <<  params.dp_beta << std::endl;
        std::cout << "downgrade batch size: " <<  params.downgrade_batch_size << std::endl;
        std::cout << "upgrade batch size: " <<  params.upgrade_batch_size << std::endl;
        std::cout << "expiration count: " <<  params.expiration_count << std::endl;
    } else {
        std::cout << "Disable Priority Downgrade." << std::endl;
    }

    std::cout << "Host type: " << params.host_type << std::endl;
    std::cout << "Queue type: " << params.queue_type << std::endl;
    std::cout << "Flow type: " << params.flow_type << std::endl;
    params.mss = params.mtu - params.hdr_size;
    params.homa_rtt_bytes = params.homa_rttbytes_in_mss * params.mss;
    std::cout << "Init cwnd: " << params.initial_cwnd << std::endl;
    std::cout << "Max cwnd: " << params.max_cwnd << std::endl;
    std::cout << "Num Hosts: " << params.num_hosts << std::endl;
    if (!params.use_flow_trace) {
        std::cout << "Num total RPCs to run: " << params.num_flows_to_run << std::endl;
    }
    if (params.traffic_pattern == 0) {
        std::cout << "traffic pattern: incast" << std::endl;
    } else if (params.traffic_pattern == 1) {
        std::cout << "traffic pattern: all-to-all" << std::endl;
    }
    std::cout << "mtu: " << params.mtu << std::endl;
    std::cout << "mss: " << params.mss << std::endl;
    std::cout << "Swift Delay target: " << params.cc_delay_target << " us" << std::endl;
    if (params.real_nic) {
        std::cout << "Real Nic on" << std::endl;
        assert(params.channel_multiplexing);    // when limiting nic speed with line rate, params.channel_multiplexing must be on
        if (params.nic_use_WF) {
            std::cout << "Real NIC uses WF" << std::endl;
        } else {
            std::cout << "Real NIC uses RR" << std::endl;

        }
    }
    if (params.flow_type == QJUMP_FLOW) {    // Qjump disables CC
        assert(params.channel_multiplexing && params.multiplex_constant == 1);
        assert(params.real_nic == 0);
        std::cout << "Qjump cumulative processing delay: " << params.qjump_cumulative_pd << " us." << std::endl;
    }
    if (params.flow_type == HOMA_FLOW) {
        std::cout << "Homa RTTbytes = " << params.homa_rtt_bytes << std::endl;
        std::cout << "Homa sampling freq = " << params.homa_sampling_freq << std::endl;
    }
    assert(params.burst_size > 0);
    //std::cout << "Flushing Coefficient = " << params.flushing_coefficient << std::endl;
    std::cout << "Retransmission Timeout: " << params.retx_timeout_value * 1e6 << " us" << std::endl;

    //TODO: assert(params.real_nic == 0);

}
