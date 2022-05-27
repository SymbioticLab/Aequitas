#ifndef RUN_PARAMS_H
#define RUN_PARAMS_H

#include <fstream>
#include <string>
#include <vector>

class DCExpParams {
    public:
        std::string param_str;

        uint32_t initial_cwnd;
        uint32_t max_cwnd;
        double retx_timeout_value;  // in us
        uint32_t mss;
        uint32_t mtu;
        uint32_t hdr_size;
        uint32_t queue_size;
        uint32_t queue_type;
        uint32_t flow_type;
        uint32_t load_balancing; //0 per pkt, 1 per flow
        uint32_t use_dynamic_load; // 0 static use "load" value; 1 specify in "dload" array
        uint32_t use_random_jitter; // random jitter at the beginning of sending bursts (in dynamic load setting)
        //uint32_t random_flow_start; // if 0, no randomness on the int_arr at all
        uint32_t early_pkt_in_highest_prio; // if set to 1, the pkts with the highest priority arrive at (t_0 - (1 - tiny_time))  (to create delay of almost 1 instead of 0)
        // flushing start time will also change accordingly
        std::vector<int> weights;   // queue weights for WFQ
        int sum_weights;
        int num_qos_level;
        std::vector<double> qos_ratio; // qos ratio in terms of num_flows
        std::vector<int> buffer_carving;

        double propagation_delay;
        double bandwidth;

        uint32_t num_flows_to_run;
        double end_time;
        std::string cdf_or_flow_trace;
        uint32_t bytes_mode;
        uint32_t cut_through;
        uint32_t mean_flow_size;
        double first_flow_start_time;

        double load_measure_interval;

        uint32_t num_hosts;
        uint32_t num_agg_switches;
        uint32_t num_core_switches;
        uint32_t preemptive_queue;
        uint32_t big_switch;              // else use pFabric topology
        uint32_t host_type;
        double traffic_imbalance;
        double load;
        uint32_t burst_size;              // number of RPCs sent before switch to wait and send nothing in the dynamic load setting; may change to # of bytes in the future
        uint32_t use_burst_byte;          // change burst_size from counting number of RPCs to counting number of bytes; default = 0: count number of RPCs
        double burst_load;                // load we use to send burst data in dynamic load setting. Better be > 1
        uint32_t burst_with_no_spacing;   // all the RPCs in the burst period arrives at the same time; default = false (0)
        uint32_t channel_multiplexing;    // Instead of one Channel per flow (all AggChannel controls all Channels), use one Channel for multiple flows; default off
        uint32_t multiplex_constant;      // # of Channels under each [src,dst,qos] tuple (AggChannel); default = 1; increase the value (e.g., to 10) when having a large scale exp
        uint32_t real_nic;                // packets sending out cannot exceed nic line rate; default on; when turned on, channel_multiplexing must be on
        uint32_t nic_use_WF;              // real nic uses WF instead of RR; default on; only meaningful when real_nic is on
        //uint32_t enable_initial_shift;
        //std::vector<double> dynamic_load;
        //int load_idx = 0;
        uint32_t disable_poisson_arrival;  // disable poisson arrival (default=0; enable); when ON, has initial shift; when OFF, has poisson but no initial shift
        uint32_t disable_aequitas_cc;       // disable cc in Aequitas flow
        uint32_t flushing_coefficient;
        double cc_delay_target;             // delay target for swift/timely above which multiplicative decrease on cwnd will be performed
        uint32_t traffic_pattern;           // 0: incast; 1: all-to-all
        uint32_t disable_pkt_logging;        // 0: enable; 1: disable (default = 0)
        uint32_t disable_cwnd_logging;        // 0: enable; 1: disable (default = 0)
        uint32_t disable_dwnd_logging;        // 0: enable; 1: disable (default = 1)
        uint32_t only_sw_queue;                // for now only implemented for BigSwitchTopo
        uint32_t test_fairness;            // assign disproportional qos distribution to test fairness; only available in ALL-to-ALL pattern for now
        std::vector<double> fairness_qos_dist;
        uint32_t test_size_to_priority;        // used to create cases where size is corresponding to priority or not; 0: turn off; 1: size corresponds to priority; 2: size doesn't correspdons to priority; default off
        uint32_t homa_sampling_freq;
        uint32_t homa_rttbytes_in_mss;
        uint32_t homa_rtt_bytes;

        uint32_t debug_event_info;

        uint32_t enable_flow_lookup;
        uint32_t flow_lookup_id;

        uint32_t priority_downgrade;
        double high_prio_lat_target;
        double target_expiration;
        double qd_expiration;
        double rtt_expiration;
        uint32_t expiration_count;
        double dp_alpha;
        double dp_beta;
        double dwnd_alpha;
        double uwnd_beta;
        uint32_t downgrade_batch_size;
        uint32_t upgrade_batch_size;
        uint32_t qd_num_hops;    // hint for estimating queuding delay from rtt
        std::vector<double> targets;
        std::vector<double> hardcoded_targets;
        uint32_t num_pctl;
        uint32_t track_qosh_dwnds;
        uint32_t memory_time_duration; // in us; for new downgrade prob
        uint32_t smart_time_window;  // set per window time duration based on target per qos
        uint32_t target_pctl;        // used to set the smart time window
        uint32_t normalized_lat;  // normalize latency measurement results by RPC size (# of MTUs)
        uint32_t print_normalized_result;  // in output log, print normalize latency measurement results; default off

        double qjump_cumulative_pd;     // used when calculating qjump's network epoch; in uint of us; default to be 1us
        uint32_t enable_qjump_retransmission;  // whether to turn on basic packet retransmission for Qjump; default off
        std::vector<int> qjump_tput_factor;    // specify the throughput factors of Qjump

        uint32_t early_termination;     // used by D3; default off

        double reauth_limit;

        double magic_trans_slack;
        uint32_t magic_delay_scheduling;
        uint32_t magic_inflate;

        uint32_t use_flow_trace;
        uint32_t smooth_cdf;
        uint32_t burst_at_beginning;
        double capability_timeout;
        double capability_resend_timeout;
        uint32_t capability_initial;
        uint32_t capability_window;
        uint32_t capability_prio_thresh;
        double capability_window_timeout;
        uint32_t capability_third_level;
        uint32_t capability_fourth_level;

        uint32_t ddc;
        double ddc_cpu_ratio;
        double ddc_mem_ratio;
        double ddc_disk_ratio;
        uint32_t ddc_normalize; //0: sender send, 1: receiver side, 2: both
        uint32_t ddc_type;

        uint32_t deadline;
        uint32_t schedule_by_deadline;
        double avg_deadline;
        std::string interarrival_cdf;
        uint32_t num_host_types;

        double fastpass_epoch_time;

        uint32_t permutation_tm;

        uint32_t dctcp_mark_thresh;
        //uint32_t dctcp_delayed_ack_freq;

        std::string pfabric_priority_type;
        uint32_t pfabric_limited_priority;    // when true, using size thresholds instead of unlimited number of priority levels

        uint32_t cdf_info;

        double get_full_pkt_tran_delay(uint32_t size_in_byte = 1500)
        {
            return size_in_byte * 8 / this->bandwidth;
        }

};


#define CAPABILITY_MEASURE_WASTE false
#define CAPABILITY_NOTIFY_BLOCKING false
#define CAPABILITY_HOLD true

//#define FASTPASS_EPOCH_TIME 0.000010
#define FASTPASS_EPOCH_PKTS 8

#define num_hw_prio_levels 8    // used by Homa; same as pFabric's limitation 

void read_experiment_parameters(std::string conf_filename, uint32_t exp_type); 

/* General main function */
#define DEFAULT_EXP 1
#define GEN_ONLY 2

#define INFINITESIMAL_TIME 0.000000000001
//#define SLIGHTLY_SMALL_TIME     0.000000001
#define SLIGHTLY_SMALL_TIME     0.000000001
#define SLIGHTLY_SMALL_TIME_POS 0.000000001

#endif  // RUN_PARAMS_H
