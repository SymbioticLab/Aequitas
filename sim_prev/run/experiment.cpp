#include <iostream>
#include <algorithm>
#include <fstream>
#include <stdlib.h>
#include <deque>
#include <stdint.h>
#include <cstdlib>
#include <ctime>
#include <map>
#include <iomanip>
#include "assert.h"
#include "math.h"

#include "../coresim/flow.h"
#include "../coresim/packet.h"
#include "../coresim/node.h"
#include "../coresim/event.h"
#include "../coresim/topology.h"
#include "../coresim/queue.h"
#include "../coresim/random_variable.h"
#include "../coresim/channel.h"

#include "../ext/factory.h"

#include "flow_generator.h"
#include "stats.h"
#include "params.h"

extern Topology *topology;
extern double current_time;
extern std::priority_queue<Event*, std::vector<Event*>, EventComparator> event_queue;
extern std::deque<Flow*> flows_to_schedule;
extern std::deque<Event*> flow_arrivals;
extern std::vector<std::map<std::pair<Host *, Host *>, Channel *>> channels;
extern std::vector<std::vector<double>> per_pkt_lat;
extern std::vector<std::vector<double>> per_pkt_rtt;
extern std::vector<std::vector<uint32_t>> cwnds;
extern std::vector<uint32_t> num_timeouts;
extern double total_time_period;
extern uint32_t total_num_periods;

//extern std::vector<double> time_spent_send_data;
//extern std::vector<double> burst_start_time;
//extern std::vector<uint32_t> curr_burst_size;
extern std::map<std::pair<Host *, Host *>, double> time_spent_send_data;
extern std::map<std::pair<Host *, Host *>, double> burst_start_time;
extern std::map<std::pair<Host *, Host *>, uint32_t> curr_burst_size;

extern uint32_t num_outstanding_packets;
extern uint32_t max_outstanding_packets;
extern DCExpParams params;
extern void add_to_event_queue(Event*);
extern void read_experiment_parameters(std::string conf_filename, uint32_t exp_type);
extern void read_flows_to_schedule(std::string filename, uint32_t num_lines, Topology *topo);
extern uint32_t duplicated_packets_received;

extern uint32_t num_outstanding_packets_at_50;
extern uint32_t num_outstanding_packets_at_100;
extern uint32_t arrival_packets_at_50;
extern uint32_t arrival_packets_at_100;

extern double start_time;
extern double get_current_time();

extern void run_scenario();

void validate_flow(Flow* f){
    double slowdown = 1000000.0 * f->flow_completion_time / topology->get_oracle_fct(f);
    if(slowdown < 0.999999){
        std::cout << "Flow " << f->id << " has slowdown " << slowdown << "\n";
        //assert(false);
    }
    //if(f->first_byte_send_time < 0 || f->first_byte_send_time < f->start_time - INFINITESIMAL_TIME)
    //    std::cout << "Flow " << f->id << " first_byte_send_time: " << f->first_byte_send_time << " start time:"
    //        << f->start_time << "\n";
}

void debug_flow_stats(std::deque<Flow *> flows){
    std::map<int,int> freq;
    for (uint32_t i = 0; i < flows.size(); i++) {
        Flow *f = flows[i];
        if(f->size_in_pkt == 3){
            int fct = (int)(1000000.0 * f->flow_completion_time);
            if(freq.find(fct) == freq.end())
                freq[fct] = 0;
            freq[fct]++;
        }
    }
    for(auto it = freq.begin(); it != freq.end(); it++)
        std::cout << it->first << " " << it->second << "\n";
}

void assign_flow_deadline(std::deque<Flow *> flows)
{
    ExponentialRandomVariable *nv_intarr = new ExponentialRandomVariable(params.avg_deadline);
    for(uint i = 0; i < flows.size(); i++)
    {
        Flow* f = flows[i];
        double rv = nv_intarr->value();
        f->deadline = f->start_time + std::max(topology->get_oracle_fct(f)/1000000.0 * 1.25, rv);
        //std::cout << f->start_time << " " << f->deadline << " " << topology->get_oracle_fct(f)/1000000 << " " << rv << "\n";
    }
}

void printQueueStatistics(Topology *topo) {
    double totalSentFromHosts = 0;

    uint64_t dropAt[4];
    uint64_t total_drop = 0;
    for (auto i = 0; i < 4; i++) {
        dropAt[i] = 0;
    }

    for (uint32_t i = 0; i < topo->hosts.size(); i++) {
        int location = topo->hosts[i]->queue->location;
        dropAt[location] += topo->hosts[i]->queue->pkt_drop;
    }


    for (uint32_t i = 0; i < topo->switches.size(); i++) {
        for (uint j = 0; j < topo->switches[i]->queues.size(); j++) {
            int location = topo->switches[i]->queues[j]->location;
            dropAt[location] += topo->switches[i]->queues[j]->pkt_drop;
        }
    }

    for (int i = 0; i < 4; i++) {
        total_drop += dropAt[i];
    }
    for (int i = 0; i < 4; i++) {
        std::cout << "Hop:" << i << " Drp:" << dropAt[i] << "("  << (int)((double)dropAt[i]/total_drop * 100) << "%) ";
    }

    for (auto h = (topo->hosts).begin(); h != (topo->hosts).end(); h++) {
        totalSentFromHosts += (*h)->queue->b_departures;
    }

    std::cout << " Overall:" << std::setprecision(2) <<(double)total_drop*1460/totalSentFromHosts << "\n";

    double totalSentToHosts = 0;
    for (auto tor = (topo->switches).begin(); tor != (topo->switches).end(); tor++) {
        for (auto q = ((*tor)->queues).begin(); q != ((*tor)->queues).end(); q++) {
            if ((*q)->rate == params.bandwidth) totalSentToHosts += (*q)->b_departures;
        }
    }

    double dead_bytes = totalSentFromHosts - totalSentToHosts;
    double total_bytes = 0;
    for (auto f = flows_to_schedule.begin(); f != flows_to_schedule.end(); f++) {
        total_bytes += (*f)->size;
    }

    double simulation_time = current_time - start_time;
    double utilization = (totalSentFromHosts * 8.0 / 144.0) / simulation_time;
    double dst_utilization = (totalSentToHosts * 8.0 / 144.0) / simulation_time;

    std::cout
        << "DeadPackets " << 100.0 * (dead_bytes/total_bytes)
        << "% DuplicatedPackets " << 100.0 * duplicated_packets_received * 1460.0 / total_bytes
        << "% Utilization " << utilization / 10000000000 * 100 << "% " << dst_utilization / 10000000000 * 100  
        << "%\n";
}

void run_experiment(int argc, char **argv, uint32_t exp_type) {
    if (argc < 3) {
        std::cout << "Usage: <exe> exp_type conf_file" << std::endl;
        return;
    }

    std::string conf_filename(argv[2]);
    read_experiment_parameters(conf_filename, exp_type);
    //params.num_hosts = 144;
    //params.num_agg_switches = 9;
    //params.num_core_switches = 4;
    
    if (params.big_switch && !params.multi_switch) {
        topology = new BigSwitchTopology(params.num_hosts, params.bandwidth, params.queue_type);
    } 
    else if (params.multi_switch) {
        topology = new MultiSwitchTopology(params.num_hosts, params.num_core_switches, params.bandwidth, params.queue_type);
    }
    else {
        std::cout << "Using Pfabric topo" << std::endl;
        topology = new PFabricTopology(params.num_hosts, params.num_agg_switches, params.num_core_switches, params.bandwidth, params.queue_type);
    }

    // Create Channel
    // Assume single direction INCAST traffic (N-1 send to 1)
    // The global variable "channels" is a vector of map with key being src-dst pair and value being ptr to the actual channel.
    // Each vector index classifies one QoS level, and within each QoS level we have a map of channels.
    // In other words, total # of channels = (# of hosts - 1) * (# of qos levels) in this INCAST case

    //// simple 2 host
    /*
    assert(params.num_hosts == 2);
    channels.resize(params.weights.size());
    for (int i = 0; i < params.weights.size(); i++) {
        auto src_dst_pair = std::make_pair(topology->hosts[0], topology->hosts[1]);
        channels[i][src_dst_pair] = new Channel(i, topology->hosts[0], topology->hosts[1], i);
    }
    */


    /*
    if (params.traffic_pattern == 0) {
        //// General single direction INCAST
        channels.resize(params.weights.size());
        //channels.resize((params.num_hosts - 1) * params.weights.size());
        for (uint32_t i = 0; i < params.weights.size(); i++) {
            for (uint32_t j = 0; j < params.num_hosts - 1; j++) {
                auto src_dst_pair = std::make_pair(topology->hosts[j], topology->hosts[params.num_hosts - 1]);
                channels[i][src_dst_pair] = new Channel(i, topology->hosts[j], topology->hosts[params.num_hosts - 1], i);
            }
        }
    } else if (params.traffic_pattern == 1) {
        //// General ALL-TO-ALL
        channels.resize(params.weights.size());
        for (uint32_t i = 0; i < params.weights.size(); i++) {
            for (uint32_t j = 0; j < params.num_hosts; j++) {
                for (uint32_t k = 0; k < params.num_hosts; k++) {
                    if (j != k) {
                        auto src_dst_pair = std::make_pair(topology->hosts[j], topology->hosts[k]);
                        channels[i][src_dst_pair] = new Channel(i, topology->hosts[j], topology->hosts[k], i);
                    }
                }
            }
        }
    } else {
        assert(false);
    }
    */

    cwnds.resize(params.weights.size());
    num_timeouts.resize(params.weights.size());

    //// Given send_ack() also go through channels (in the opposite direction of data pkts), always generate every possible pair of channel (in either direction)
    channels.resize(params.weights.size());
    for (uint32_t i = 0; i < params.weights.size(); i++) {
        for (uint32_t j = 0; j < params.num_hosts; j++) {
            for (uint32_t k = 0; k < params.num_hosts; k++) {
                if (j != k) {
                    auto src_dst_pair = std::make_pair(topology->hosts[j], topology->hosts[k]);
                    channels[i][src_dst_pair] = new Channel(i, topology->hosts[j], topology->hosts[k], i);
                }
            }
        }
    }

    uint32_t num_flows = params.num_flows_to_run;

    // init the vector used in dynamic load setting
    for (uint32_t i = 0; i < params.num_hosts; i++) {
        for (uint32_t j = 0; j < params.num_hosts; j++) {
            auto src_dst_pair = std::make_pair(topology->hosts[i], topology->hosts[j]);
            time_spent_send_data[src_dst_pair] = 0;
            burst_start_time[src_dst_pair] = 0;
            curr_burst_size[src_dst_pair] = 0;
        }
    }
    //time_spent_send_data.resize(num_flows, 0);
    //burst_start_time.resize(num_flows, 0);
    //curr_burst_size.resize(num_flows, 0);

    // generate flows
    FlowGenerator *fg;
    if (params.use_flow_trace) {
        fg = new FlowReader(num_flows, topology, params.cdf_or_flow_trace);
        fg->make_flows();
    }
    else if (params.interarrival_cdf != "none") {
        fg = new CustomCDFFlowGenerator(num_flows, topology, params.cdf_or_flow_trace, params.interarrival_cdf);
        fg->make_flows();
    }
    else if (params.permutation_tm != 0) {
        fg = new PermutationTM(num_flows, topology, params.cdf_or_flow_trace);
        fg->make_flows();
    }
    else if (params.bytes_mode) {
        fg = new PoissonFlowBytesGenerator(num_flows, topology, params.cdf_or_flow_trace);
        fg->make_flows();
    }
    else if (params.traffic_imbalance < 0.01) {
        fg = new PoissonFlowGenerator(num_flows, topology, params.cdf_or_flow_trace);
        fg->make_flows();
    }
    else {
        // TODO skew flow gen not yet implemented, need to move to FlowGenerator
        assert(false);
        //generate_flows_to_schedule_fd_with_skew(params.cdf_or_flow_trace, num_flows, topology);
    }

    if (params.deadline) {
        assign_flow_deadline(flows_to_schedule);
    }

    std::deque<Flow*> flows_sorted = flows_to_schedule;

    struct FlowComparator {
        bool operator() (Flow* a, Flow* b) {
            return a->start_time < b->start_time;
        }
    } fc;

    std::sort (flows_sorted.begin(), flows_sorted.end(), fc);

    for (uint32_t i = 0; i < flows_sorted.size(); i++) {
        Flow* f = flows_sorted[i];
        if (exp_type == GEN_ONLY) {
            std::cout << f->id << " " << f->size << " " << f->src->id << " " << f->dst->id << " " << 1e6*f->start_time << "\n";
        }
        else {
            flow_arrivals.push_back(new FlowArrivalEvent(f->start_time, f));
        }
    }

    if (exp_type == GEN_ONLY) {
        return;
    }

    //add_to_event_queue(new LoggingEvent((flows_sorted.front())->start_time));

    per_pkt_lat.resize(params.weights.size());
    per_pkt_rtt.resize(params.weights.size());
    
    // 
    // everything before this is setup; everything after is analysis
    //
    run_scenario();

    uint32_t num_unfinished_flows = 0;
    for (uint32_t i = 0; i < flows_sorted.size(); i++) {
        Flow *f = flows_to_schedule[i];
        //validate_flow(f);     // do not validate flow
        if(!f->finished) {
            std::cout 
                << "unfinished flow " 
                << "size:" << f->size 
                << " id:" << f->id 
                << " next_seq:" << f->next_seq_no 
                << " recv:" << f->received_bytes  
                << " src:" << f->src->id 
                << " dst:" << f->dst->id 
                << "\n";
            num_unfinished_flows++;
        }
    }
    std::cout << "Unfinished Flows: " << num_unfinished_flows << std::endl;

    // simple straightforward (bad) method to get tails labeled by priorities
    std::vector<std::deque<Flow*>> flows_by_prio(params.weights.size());
    std::vector<uint32_t> unfinished_flows_by_prio(params.weights.size());
    double total_flow_completion_time = 0;
    for (Flow *f: flows_sorted) {
        if (!f->finished) {
            unfinished_flows_by_prio[f->flow_priority]++;
        } else {
            flows_by_prio[f->flow_priority].push_back(f);
            total_flow_completion_time += f->flow_completion_time;
        }
    }

    std::vector<double> start_end_time(params.weights.size());
    for (uint32_t i = 0; i < params.weights.size(); i++) {
        double start_time = flows_by_prio[i][0]->start_time;
        std::sort (flows_by_prio[i].begin(), flows_by_prio[i].end(), [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->finish_time < rhs->finish_time;
        });
        //std::cout << "Priority " << i << ": last finish time = " << flows_by_prio[i][flows_by_prio[i].size() - 1]->finish_time << std::endl;
        start_end_time[i] = flows_by_prio[i][flows_by_prio[i].size() - 1]->finish_time - start_time;    // assuming RPCs from all Priority levels start roughly simultaneously
    }
    //std::cout << "Current time = " << get_current_time() << std::endl;

    // also get the flows which include the unfinished flows (for calculate per packet queuing delay)
    //std::vector<std::deque<Flow*>> flows_by_prio_all(params.weights.size());
    //for (Flow *f: flows_sorted) {
    //    flows_by_prio_all[f->flow_priority].push_back(f);
    //}

    std::vector<std::deque<Flow *>> flows_by_prio_cp = flows_by_prio;
    std::vector<std::deque<Flow *>> flows_by_prio_cp1 = flows_by_prio;
    std::vector<std::deque<Flow *>> flows_by_prio_cp2 = flows_by_prio;

    std::vector<double> avg_pkt_queuing_delays(params.weights.size());
    std::cout << std::setprecision(2) << std::fixed ;
    for (uint32_t i = 0; i < flows_by_prio.size(); i++) {
        uint32_t first_quarter = flows_by_prio[i].size() * 0.25;
        uint32_t third_quarter = flows_by_prio[i].size() * 0.75;
        std::sort(flows_by_prio[i].begin(), flows_by_prio[i].end(), [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->flow_completion_time < rhs->flow_completion_time;
        });
        std::sort(flows_by_prio_cp[i].begin() + first_quarter, flows_by_prio_cp[i].begin() + third_quarter, [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->flow_completion_time < rhs->flow_completion_time;
        });
        std::sort(flows_by_prio_cp1[i].begin(), flows_by_prio_cp1[i].begin() + flows_by_prio[i].size() / 2, [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->flow_completion_time < rhs->flow_completion_time;
        });
        std::sort(flows_by_prio_cp2[i].begin() + flows_by_prio[i].size() / 2, flows_by_prio_cp2[i].end(), [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->flow_completion_time < rhs->flow_completion_time;
        });
        if (flows_by_prio[i].size() == 0) {
            std::cout << "Priority " << i << ": (0 Flow)" << std::endl;
        } else {
            uint32_t median = flows_by_prio[i].size() / 2;
            uint32_t tail_99 = flows_by_prio[i].size() * 0.99;
            uint32_t tail_999 = flows_by_prio[i].size() * 0.999;
            uint32_t median_refined_mid_half = median;
            uint32_t tail_99_refined_mid_half = flows_by_prio[i].size() * (0.25 + 0.5 * 0.99);
            uint32_t tail_999_refined_mid_half = flows_by_prio[i].size() * (0.25 + 0.5 * 0.999);
            uint32_t median_refined_first_half = first_quarter;
            uint32_t tail_99_refined_first_half = flows_by_prio[i].size() * (0.5 * 0.99);
            uint32_t tail_999_refined_first_half = flows_by_prio[i].size() * (0.5 * 0.999);
            uint32_t median_refined_last_half = third_quarter;
            uint32_t tail_99_refined_last_half = flows_by_prio[i].size() * (0.5 + 0.5 * 0.99);
            uint32_t tail_999_refined_last_half = flows_by_prio[i].size() * (0.5 + 0.5 * 0.999);

            //std:: cout << "median: " << median << " tail 99: " << tail_99 << std::endl;
            std::cout << "Priority " << i << ": (" << flows_by_prio[i].size() + unfinished_flows_by_prio[i]
                << " Flows, Finished/Unf: " << flows_by_prio[i].size() << "/"
                << unfinished_flows_by_prio[i] << ")" << std::endl << "FCT (in us) (100% RPCs) (Median, 99th, 99.9th) = "
                << flows_by_prio[i][median]->flow_completion_time * 1000000 << ", "
                << flows_by_prio[i][tail_99]->flow_completion_time * 1000000 << ", "
                << flows_by_prio[i][tail_999]->flow_completion_time * 1000000 << std::endl
                << "FCT (in us) (mid 50% RPCs) (Median, 99th, 99.9th) = "
                << flows_by_prio_cp[i][median_refined_mid_half]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp[i][tail_99_refined_mid_half]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp[i][tail_999_refined_mid_half]->flow_completion_time * 1000000 << std::endl
                << "FCT (in us) (first 50% RPCs) (Median, 99th, 99.9th) = "
                << flows_by_prio_cp1[i][median_refined_first_half]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp1[i][tail_99_refined_first_half]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp1[i][tail_999_refined_first_half]->flow_completion_time * 1000000 << std::endl
                << "FCT (in us) (last 50% RPCs) (Median, 99th, 99.9th) = "
                << flows_by_prio_cp2[i][median_refined_last_half]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp2[i][tail_99_refined_last_half]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp2[i][tail_999_refined_last_half]->flow_completion_time * 1000000 << std::endl
                << "99.9th (out of 100% RPCs) RPC is RPC[" << flows_by_prio[i][tail_999]->id << "], completion time = " <<
                flows_by_prio[i][tail_999]->flow_completion_time * 1e6 << "; total queuing time = " <<
                flows_by_prio[i][tail_999]->total_queuing_time * 1e6 << "; avg inter-pkt spacing = " <<
                flows_by_prio[i][tail_999]->get_avg_inter_pkt_spacing_in_us() << " us" << std::endl;
        }

        std::sort(flows_by_prio[i].begin(), flows_by_prio[i].end(), [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->get_avg_queuing_delay_in_us() < rhs->get_avg_queuing_delay_in_us();
        });
        //median = flows_by_prio[i].size() / 2;
        //tail_99 = flows_by_prio[i].size() * 0.99;
        //tail_999 = flows_by_prio[i].size() * 0.999;
        /*
        std::sort(flows_by_prio_all[i].begin(), flows_by_prio_all[i].end(), [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->get_avg_queuing_delay_in_us() < rhs->get_avg_queuing_delay_in_us();
        });
        median = flows_by_prio_all[i].size() / 2;
        tail_99 = flows_by_prio_all[i].size() * 0.99;
        tail_999 = flows_by_prio_all[i].size() * 0.999;
        */
        //std:: cout << "median: " << median << " tail 99: " << tail_99 << std::endl;
        //std::cout << "Avg Pkt Queuing Delay (Median//99th//99.9th) = "
        //    << flows_by_prio[i][median]->get_avg_queuing_delay_in_us() / 1000000 << "//"
        //    << flows_by_prio[i][tail_99]->get_avg_queuing_delay_in_us() / 1000000 << "//"
        //    << flows_by_prio[i][tail_999]->get_avg_queuing_delay_in_us() / 1000000 << std::endl;

        //std::cout << "Avg Pkt Queuing Delay [0] = "
        //    << flows_by_prio[i][0]->get_avg_queuing_delay_in_us() << std::endl;
        double sum = 0;
        for (int j = 0; j < flows_by_prio[i].size(); j++) {
            //sum += flows_by_prio[i][j]->get_avg_queuing_delay_in_us() / 1000000;
            sum += flows_by_prio[i][j]->get_avg_queuing_delay_in_us();
        }
        avg_pkt_queuing_delays[i] = sum / flows_by_prio[i].size();
        //std::cout << "Avg Pkt Queuing Delay = " << avg_pkt_queuing_delays[i] << std::endl;
            
    }
    //if (params.weights.size() == 2) {
    //    std::cout << "QoS_H vs QoS_L avg queuing delay ratio = " << avg_pkt_queuing_delays[1] / avg_pkt_queuing_delays[0] << std::endl;
    //}

    std::vector<std::vector<double>> per_pkt_lat_cp = per_pkt_lat;
    std::cout << "Per pkt queuing delay:" << std::endl;
    for (uint32_t i = 0; i < per_pkt_lat.size(); i++) {
        sort(per_pkt_lat[i].begin(), per_pkt_lat[i].end());
        uint32_t median = per_pkt_lat[i].size() * 0.5;
        uint32_t tail_99 = per_pkt_lat[i].size() * 0.99;
        uint32_t tail_999 = per_pkt_lat[i].size() * 0.999;
        std::cout << "Priority[" << i << "] (100% packets) (tot pkts: " << per_pkt_lat[i].size()
            << ") (Median, 99th, 99.9th) (us) = " << per_pkt_lat[i][median] * 1000000 << ", "
            << per_pkt_lat[i][tail_99] * 1000000 << ", "<< per_pkt_lat[i][tail_999] * 1000000 << std::endl;
    }
    for (uint32_t i = 0; i < per_pkt_lat.size(); i++) {
        uint32_t first_quarter = per_pkt_lat[i].size() * 0.25;
        uint32_t third_quarter = per_pkt_lat[i].size() * 0.75;
        sort(per_pkt_lat_cp[i].begin() + first_quarter, per_pkt_lat_cp[i].begin() + third_quarter);
        uint32_t median_refined = per_pkt_lat[i].size() * 0.5;
        uint32_t tail_99_refined = per_pkt_lat[i].size() * (0.25 + 0.5 * 0.99);
        uint32_t tail_999_refined = per_pkt_lat[i].size() * (0.25 + 0.5 * 0.999);
        std::cout << "Priority[" << i << "] (mid 50% packets) (tot pkts: " << int(per_pkt_lat[i].size() * 0.5)
            << ") (Median, 99th, 99.9th) (us) = " << per_pkt_lat_cp[i][median_refined] * 1000000 << ", "
            << per_pkt_lat_cp[i][tail_99_refined] * 1000000 << ", "<< per_pkt_lat_cp[i][tail_999_refined] * 1000000 << std::endl;
    }

    std::vector<std::vector<double>> per_pkt_rtt_cp = per_pkt_rtt;
    std::cout << "Per pkt RTT:" << std::endl;
    for (uint32_t i = 0; i < per_pkt_rtt.size(); i++) {
        sort(per_pkt_rtt[i].begin(), per_pkt_rtt[i].end());
        uint32_t median = per_pkt_rtt[i].size() * 0.5;
        uint32_t tail_99 = per_pkt_rtt[i].size() * 0.99;
        uint32_t tail_999 = per_pkt_rtt[i].size() * 0.999;
        std::cout << "Priority[" << i << "] (100% packets) (tot pkts: " << per_pkt_rtt[i].size()
            << ") (Median, 99th, 99.9th, Max) (us) = " << per_pkt_rtt[i][median] << ", "
            << per_pkt_rtt[i][tail_99] << ", "<< per_pkt_rtt[i][tail_999] << ", " << per_pkt_rtt[i].back() << std::endl;
    }
    for (uint32_t i = 0; i < per_pkt_rtt.size(); i++) {
        uint32_t first_quarter = per_pkt_rtt[i].size() * 0.25;
        uint32_t third_quarter = per_pkt_rtt[i].size() * 0.75;
        sort(per_pkt_rtt_cp[i].begin() + first_quarter, per_pkt_rtt_cp[i].begin() + third_quarter);
        uint32_t median_refined = per_pkt_rtt[i].size() * 0.5;
        uint32_t tail_99_refined = per_pkt_rtt[i].size() * (0.25 + 0.5 * 0.99);
        uint32_t tail_999_refined = per_pkt_rtt[i].size() * (0.25 + 0.5 * 0.999);
        std::cout << "Priority[" << i << "] (mid 50% packets) (tot pkts: " << int(per_pkt_rtt[i].size() * 0.5)
            << ") (Median, 99th, 99.9th) (us) = " << per_pkt_rtt_cp[i][median_refined] << ", "
            << per_pkt_rtt_cp[i][tail_99_refined] << ", "<< per_pkt_rtt_cp[i][tail_999_refined] << std::endl;
    }

    for (uint32_t i = 0; i < params.weights.size(); i++) {
        sort(cwnds[i].begin(), cwnds[i].end());
        uint32_t tail_1th = cwnds[i].size() * 0.01;
        uint32_t median = cwnds[i].size() * 0.5;
        std::cout << "Cwnd[" << i << "](1th, median) = " << cwnds[i][tail_1th] << ", " << cwnds[i][median] << std::endl;
    }
    
    // When a flow is a sinlge packet:
    // Calculate throughput (not including unfinished packets which are the one gets dropped by the queue)
    std::vector<uint64_t> sum_bytes_per_prio(params.weights.size(), 0);
    std::vector<double> throughput_per_prio(params.weights.size(), 0.0);
    for (uint32_t i = 0; i < params.weights.size(); i++) {
        for (int j = 0; j < flows_by_prio[i].size(); j++) {
            sum_bytes_per_prio[i] += flows_by_prio[i][j]->size;
        }
        throughput_per_prio[i] = sum_bytes_per_prio[i] * 8.0 / start_end_time[i] / 1000000000;
        std::cout << "Priority[" << i << "] throughput = " << throughput_per_prio[i] << " Gbps" << std::endl;
    }
    // assume one big sw topo for now
    for (uint32_t i = 0; i < topology->switches[0]->queues.size(); i++) {
        auto queue = topology->switches[0]->queues[i];
        if (queue->pkt_drop > 0) {
            std::cout << "At switch queue[" << queue->unique_id << "]: # Pkt drop = " << queue->pkt_drop << std::endl;
        }
    }
    for (uint32_t i = 0; i < params.weights.size(); i++) {
        std::cout << "num timeouts[" << i << "]: " << num_timeouts[i] << std::endl;
    }
    std::cout << "sum completion time of all finished RPCs = " << total_flow_completion_time << " sec" << std::endl;
    std::cout << "avg sending period (busy + idle) = " << total_time_period / total_num_periods * 1e6 << " us" << std::endl;


    //cleanup
    delete fg;
}
