#include <iostream>
#include <algorithm>
#include <fstream>
#include <stdlib.h>
#include <deque>
#include <stdint.h>
#include <time.h>
#include <assert.h>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "agg_channel.h"
#include "event.h"
#include "flow.h"
#include "node.h"
#include "packet.h"
#include "queue.h"
#include "random_variable.h"
#include "topology.h"
#include "../run/params.h"

Topology* topology;
double current_time = 0;
//std::vector<std::priority_queue<Event*, std::vector<Event*>, EventComparator>> event_queues;
std::priority_queue<Event*, std::vector<Event*>, EventComparator> event_queue;
std::deque<Flow*> flows_to_schedule;
//std::vector<std::deque<Event*>> flow_arrivals;
std::deque<Event*> flow_arrivals;
std::vector<std::map<std::pair<uint32_t, uint32_t>, AggChannel *>> channels; // for every qos level, a map of channels indexed by src-dst pair
std::vector<std::vector<double>> per_pkt_lat;
std::vector<std::vector<double>> per_pkt_rtt;
std::vector<std::vector<uint32_t>> cwnds;
std::vector<std::vector<uint32_t>> dwnds;
std::vector<double> D3_allocation_counter_per_queue;
std::vector<uint32_t> D3_num_allocations_per_queue;
double total_time_period = 0;
uint32_t total_num_periods = 0;
//std::vector<double> time_spent_send_data;
//std::vector<double> burst_start_time;
//std::vector<uint32_t> curr_burst_size;
std::map<std::pair<uint32_t, uint32_t>, double> time_spent_send_data;
std::map<std::pair<uint32_t, uint32_t>, double> burst_start_time;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> curr_burst_size;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> curr_burst_size_in_flow;

uint32_t num_outstanding_packets = 0;
uint32_t max_outstanding_packets = 0;
uint32_t num_outstanding_packets_at_50 = 0;
uint32_t num_outstanding_packets_at_100 = 0;
uint32_t arrival_packets_at_50 = 0;
uint32_t arrival_packets_at_100 = 0;
uint32_t arrival_packets_count = 0;
uint32_t total_finished_flows = 0;
uint32_t duplicated_packets_received = 0;
uint32_t num_early_termination = 0;

uint32_t injected_packets = 0;
uint32_t duplicated_packets = 0;
uint32_t dead_packets = 0;
uint32_t completed_packets = 0;
uint32_t backlog3 = 0;
uint32_t backlog4 = 0;
uint32_t total_completed_packets = 0;
uint32_t sent_packets = 0;
std::vector<uint32_t> num_timeouts;
uint32_t num_pkt_drops = 0;
uint32_t pkt_drops_agg_switches = 0;
uint32_t pkt_drops_core_switches = 0;
uint32_t pkt_drops_host_queues = 0;
std::vector<uint32_t> pkt_drops_per_agg_sw;
std::vector<uint32_t> pkt_drops_per_host_queues;
std::vector<uint32_t> pkt_drops_per_prio;
uint32_t num_downgrades = 0;
std::vector<uint32_t> num_downgrades_per_host;
std::vector<uint32_t> num_check_passed;
std::vector<uint32_t> num_check_failed_and_downgrade;
std::vector<uint32_t> num_check_failed_but_stay;
std::vector<uint32_t> num_qos_h_downgrades;
uint32_t num_qos_m_downgrades;
double last_passed_time = 0;
std::vector<std::vector<double>> per_prio_qd;
std::vector<uint32_t> per_pctl_downgrades;
uint32_t num_bad_QoS_H_RPCs = 0;
time_t clock_start_time;
uint32_t event_total_count = 0;
uint32_t pkt_total_count = 0;
std::vector<uint32_t> per_host_QoS_H_downgrades;
std::vector<uint32_t> per_host_QoS_H_rpcs;
std::vector<uint32_t> dwnds_qosh;
uint32_t num_measurements_cleared = 0;
uint32_t total_measurements = 0;
std::vector<uint32_t> lat_cleared;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> flip_coin;
std::vector<double> qos_h_admit_prob;
std::vector<double> total_qos_h_admit_prob;
std::vector<std::vector<double>> qos_h_admit_prob_per_host;
std::vector<uint32_t> qos_h_issued_rpcs_per_host;
//std::vector<uint32_t> qos_h_total_misses_per_host;
int init_count = 0;
std::vector<uint32_t> qos_h_memory_misses;
std::vector<uint32_t> per_host_qos_h_rpc_issued;
std::vector<uint32_t> per_host_qos_h_rpc_finished;
std::vector<std::vector<double>> fairness_qos_h_admit_prob_per_host;
std::vector<std::vector<double>> fairness_qos_h_ts_per_host;
std::vector<std::vector<double>> fairness_qos_h_rates_per_host;
std::vector<uint32_t> fairness_qos_h_bytes_per_host;
std::vector<double> fairness_last_check_time;
uint32_t num_outstanding_rpcs = 0;
std::vector<uint32_t> num_outstanding_rpcs_total;
std::vector<std::vector<uint32_t>> num_outstanding_rpcs_one_sw;
//double switch_max_inst_load = 0;
std::vector<double> switch_max_inst_load;

extern DCExpParams params;
//double start_time = -1;
double simulaiton_event_duration = 0;

const std::string currentDateTime() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    // Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
    // for more information about date/time format
    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

    return buf;
}

void add_to_event_queue(Event* ev) {
    event_queue.push(ev);
}


double get_current_time() {
    return current_time; // in seconds
}

void handle_events() {
    int last_evt_type = -1;
    int same_evt_count = 0;
    while (event_queue.size() > 0) {
        Event *ev = event_queue.top();
        event_queue.pop();
        current_time = ev->time;
        //if (start_time < 0) {
        //    start_time = current_time;
        //}
        if (ev->cancelled) {
            delete ev; //TODO: Smarter
            ////ev = NULL;
            continue;
        }
        ev->process_event();

        if(last_evt_type == ev->type && last_evt_type != 9)
            same_evt_count++;
        else
            same_evt_count = 0;

        last_evt_type = ev->type;

        if(same_evt_count > 1000000){       // make sure this value is large enough for large-scale exp
            std::cout << "Ended event dead loop. Type:" << last_evt_type << "\n";
            break;
        }

        delete ev;
        event_total_count++;
    }
}

/* Runs an initialized scenario (in fact this includes everything) */
void run_scenario() {
    // Flow Arrivals create new flow arrivals
    // Add the first flow arrival
    if (flow_arrivals.size() > 0) {
        add_to_event_queue(flow_arrivals.front());
        flow_arrivals.pop_front();
    }

    handle_events();

}

extern void run_experiment(int argc, char** argv, uint32_t exp_type);

int main (int argc, char ** argv) {
    ////time_t clock_start_time;
    time(&clock_start_time);

    //srand(time(NULL));
    srand(0);
    std::cout.precision(15);

    uint32_t exp_type = atoi(argv[1]);
    switch (exp_type) {
        case GEN_ONLY:
        case DEFAULT_EXP:
            run_experiment(argc, argv, exp_type);
            break;
        default:
            assert(false);
    }

    time_t clock_end_time;
    time(&clock_end_time);
    double duration = difftime(clock_end_time, clock_start_time);
    std::cout.precision(4);
    std::cout << currentDateTime() << " Simulator ended. Program execution time: " << duration << " seconds\n";
    //std::cout << "Simulation event duration: " << simulaiton_event_duration << " seconds" << std::endl;
}

