#include <iostream>
#include <algorithm>
#include <fstream>
#include <stdlib.h>
#include <deque>
#include <stdint.h>
#include <time.h>
#include "assert.h"
#include <map>

#include "flow.h"
#include "packet.h"
#include "node.h"
#include "event.h"
#include "topology.h"
#include "queue.h"
#include "random_variable.h"

#include "../ext/factory.h"

#include "../run/params.h"

using namespace std;

Topology* topology;
double current_time = 0;
std::priority_queue<Event*, std::vector<Event*>, EventComparator> event_queue;
std::deque<Flow*> flows_to_schedule;
std::deque<Event*> flow_arrivals;
std::vector<std::map<std::pair<Host *, Host *>, Channel *>> channels; // for every qos level, a map of channels indexed by src-dst pair 
std::vector<std::vector<double>> per_pkt_lat;
std::vector<std::vector<double>> per_pkt_rtt;
std::vector<std::vector<uint32_t>> cwnds;
double total_time_period = 0;
uint32_t total_num_periods = 0;
//std::vector<double> time_spent_send_data;
//std::vector<double> burst_start_time;
//std::vector<uint32_t> curr_burst_size;
std::map<std::pair<Host *, Host *>, double> time_spent_send_data;
std::map<std::pair<Host *, Host *>, double> burst_start_time;
std::map<std::pair<Host *, Host *>, uint32_t> curr_burst_size;

uint32_t num_outstanding_packets = 0;
uint32_t max_outstanding_packets = 0;
uint32_t num_outstanding_packets_at_50 = 0;
uint32_t num_outstanding_packets_at_100 = 0;
uint32_t arrival_packets_at_50 = 0;
uint32_t arrival_packets_at_100 = 0;
uint32_t arrival_packets_count = 0;
uint32_t total_finished_flows = 0;
uint32_t duplicated_packets_received = 0;

uint32_t injected_packets = 0;
uint32_t duplicated_packets = 0;
uint32_t dead_packets = 0;
uint32_t completed_packets = 0;
uint32_t backlog3 = 0;
uint32_t backlog4 = 0;
uint32_t total_completed_packets = 0;
uint32_t sent_packets = 0;
std::vector<uint32_t> num_timeouts;

extern DCExpParams params;
double start_time = -1;
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

int get_event_queue_size() {
    return event_queue.size();
}

double get_current_time() {
    return current_time; // in seconds
}

/* Runs a initialized scenario */
void run_scenario() {
    // Flow Arrivals create new flow arrivals
    // Add the first flow arrival
    if (flow_arrivals.size() > 0) {
        add_to_event_queue(flow_arrivals.front());
        flow_arrivals.pop_front();
    }
    int last_evt_type = -1;
    int same_evt_count = 0;

    //// QueueProcessingEvent creates itself
    //// So does QueueFlushingEvent
    //// Add the first QueueProcessingEvent & QueueFlushingEvent for each queue
    //// For Toy Example, assume only switches have queues. Host do not.
    /*
    double simulation_start_time = 0.0;     // assume 0.0 is the simulation start time
    double td_std;
    for (int i = 0; i < topology->switches.size(); i++) {
        for (int j = 0; j < topology->switches[i]->queues.size(); j++) {
            Queue *queue = topology->switches[i]->queues[j];
            queue->queue_proc_event = new QueueProcessingEvent(simulation_start_time, queue);     
            add_to_event_queue(queue->queue_proc_event);
            queue->busy = true;


            td_std = queue->get_transmission_delay(params.mss + params.hdr_size);    // (mss + hdr) should be 1500 Bytes
            double flushing_start_time = simulation_start_time + params.sum_weights * td_std * params.flushing_coefficient - 5 * SLIGHTLY_SMALL_TIME_POS;
            //double flushing_start_time = simulation_start_time + params.sum_weights * td_std * params.flushing_coefficient - SLIGHTLY_SMALL_TIME_POS/2;
            //double flushing_start_time = simulation_start_time + params.sum_weights * td_std * params.flushing_coefficient + SLIGHTLY_SMALL_TIME_POS/2;
            if (params.early_pkt_in_highest_prio) {
                flushing_start_time = simulation_start_time + (params.sum_weights - 1) * td_std * params.flushing_coefficient + SLIGHTLY_SMALL_TIME_POS / 5;
            }
            add_to_event_queue(new QueueFlushingEvent(flushing_start_time, queue));
        }
    }
    std::cout << "td_std = " << td_std * 1000000 << " us" << std::endl;
    */

    while (event_queue.size() > 0) {
        Event *ev = event_queue.top();
        event_queue.pop();
        current_time = ev->time;
        if (start_time < 0) {
            start_time = current_time;
        }
        if (ev->cancelled) {
            delete ev; //TODO: Smarter
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
    }
    simulaiton_event_duration = get_current_time() - start_time;
}

extern void run_experiment(int argc, char** argv, uint32_t exp_type);

int main (int argc, char ** argv) {
    time_t clock_start_time;
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
    cout << currentDateTime() << " Simulator ended. Program execution time: " << duration << " seconds\n";
    std::cout << "Simulation event duration: " << simulaiton_event_duration << " seconds" << std::endl;
}

