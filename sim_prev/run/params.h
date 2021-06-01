#ifndef PARAMS_H
#define PARAMS_H

#include <string>
#include <fstream>
#include <vector>
#include "../coresim/random_variable.h"

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
        uint32_t random_flow_start; // if 0, no randomness on the int_arr at all
        uint32_t early_pkt_in_highest_prio; // if set to 1, the pkts with the highest priority arrive at (t_0 - (1 - tiny_time))  (to create delay of almost 1 instead of 0)
        // flushing start time will also change accordingly
        std::vector<int> weights;   // queue weights for WFQ
        int sum_weights;
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


        uint32_t num_hosts;
        uint32_t num_agg_switches;
        uint32_t num_core_switches;
        uint32_t preemptive_queue;
        uint32_t big_switch;
        uint32_t multi_switch;      // multi-BigSwitch; will overwrite the big_switch option if this is on
        uint32_t host_type;
        double traffic_imbalance;
        double load;
        uint32_t burst_size;        // number of RPCs sent before switch to wait and send nothing in the dynamic load setting; may change to # of bytes in the future
        double burst_load;          // load we use to send burst data in dynamic load setting. Better be > 1
        //uint32_t load_change_freq;    // number of flows sent before switching to the next load; 100 means 100 flows at each load value
        //std::vector<double> dynamic_load;
        //std::vector<ExponentialRandomVariable *> nv_intarr_vec; // no longer used
        //int load_idx = 0;
        uint32_t disable_Veritas_cc;       // disable cc in Veritas flow
        uint32_t flushing_coefficient;
        double cc_delay_target;             // delay target for swift/timely above which multiplicative decrease on cwnd will be performed
        uint32_t traffic_pattern;           // 0: incast; 1: all-to-all

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

void read_experiment_parameters(std::string conf_filename, uint32_t exp_type); 

/* General main function */
#define DEFAULT_EXP 1
#define GEN_ONLY 2

#define INFINITESIMAL_TIME 0.000000000001
//#define SLIGHTLY_SMALL_TIME     0.000000001
#define SLIGHTLY_SMALL_TIME     0.000000001
#define SLIGHTLY_SMALL_TIME_POS 0.000000001

#endif
