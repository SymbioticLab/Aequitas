#include <cstddef>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <fstream>
#include <stdlib.h>
#include <deque>
#include <stdint.h>
#include <cstdlib>
#include <ctime>
#include <map>
#include <iomanip>
#include <utility>
#include <assert.h>
#include <math.h>

#include "../coresim/agg_channel.h"
#include "../coresim/channel.h"
#include "../coresim/event.h"
#include "../coresim/flow.h"
#include "../coresim/node.h"
#include "../coresim/nic.h"
#include "../coresim/packet.h"
#include "../coresim/queue.h"
#include "../coresim/random_variable.h"
#include "../coresim/topology.h"
#include "../ext/factory.h"
#include "flow_generator.h"
#include "params.h"


extern Topology *topology;
extern double current_time;
//extern std::vector<std::priority_queue<Event*, std::vector<Event*>, EventComparator>> event_queues;
extern std::priority_queue<Event*, std::vector<Event*>, EventComparator> event_queue;
extern std::deque<Flow*> flows_to_schedule;
//extern std::vector<std::deque<Event*>> flow_arrivals;
extern std::deque<Event*> flow_arrivals;
extern std::vector<std::map<std::pair<uint32_t, uint32_t>, AggChannel *>> channels;
extern std::vector<std::vector<double>> per_pkt_lat;
extern std::vector<std::vector<double>> per_pkt_rtt;
extern std::vector<std::vector<uint32_t>> cwnds;
extern std::vector<std::vector<uint32_t>> dwnds;
extern std::vector<double> D3_allocation_counter_per_queue;
extern std::vector<uint32_t> D3_num_allocations_per_queue;
extern std::vector<uint32_t> num_timeouts;
extern double total_time_period;
extern uint32_t total_num_periods;
extern uint32_t total_finished_flows;
extern uint32_t num_early_termination;

extern std::map<std::pair<uint32_t, uint32_t>, double> time_spent_send_data;
extern std::map<std::pair<uint32_t, uint32_t>, double> burst_start_time;
extern std::map<std::pair<uint32_t, uint32_t>, uint32_t> curr_burst_size;
extern std::map<std::pair<uint32_t, uint32_t>, uint32_t> curr_burst_size_in_flow;

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

extern uint32_t num_pkt_drops;
extern uint32_t pkt_drops_agg_switches;
extern uint32_t pkt_drops_core_switches;
extern uint32_t pkt_drops_host_queues;
extern std::vector<uint32_t> pkt_drops_per_agg_sw;
extern std::vector<uint32_t> pkt_drops_per_host_queues;
extern std::vector<uint32_t> pkt_drops_per_prio;
extern uint32_t num_downgrades;
extern std::vector<uint32_t> num_downgrades_per_host;
//extern std::map<uint32_t, uint32_t> num_RPCs_affected;
extern std::vector<uint32_t> num_check_passed;
extern std::vector<uint32_t> num_check_failed_and_downgrade;
extern std::vector<uint32_t> num_check_failed_but_stay;
extern std::vector<uint32_t> num_qos_h_downgrades;
extern std::vector<uint32_t> qos_h_issued_rpcs_per_host;
extern uint32_t num_qos_m_downgrades;
extern std::vector<std::vector<double>> per_prio_qd;
extern double last_passed_time;
extern std::vector<uint32_t> per_pctl_downgrades;
extern uint32_t num_bad_QoS_H_RPCs;
extern time_t clock_start_time;
extern uint32_t event_total_count;
extern uint32_t pkt_total_count;
extern std::vector<uint32_t> per_host_QoS_H_downgrades;
extern std::vector<uint32_t> per_host_QoS_H_rpcs;
extern std::map<std::pair<uint32_t, uint32_t>, uint32_t> flip_coin;
extern std::vector<double> total_qos_h_admit_prob;
extern std::vector<std::vector<double>> qos_h_admit_prob_per_host;
extern std::vector<std::vector<double>> fairness_qos_h_admit_prob_per_host;
extern std::vector<std::vector<double>> fairness_qos_h_ts_per_host;
extern std::vector<std::vector<double>> fairness_qos_h_rates_per_host;
extern std::vector<uint32_t> fairness_qos_h_bytes_per_host;
extern std::vector<double> fairness_last_check_time;
extern std::vector<uint32_t> num_outstanding_rpcs_total;
extern std::vector<double> switch_max_inst_load;
extern std::vector<std::vector<uint32_t>> num_outstanding_rpcs_one_sw;

//extern double start_time;
//extern double get_current_time();

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
    std::cout << "deadline feature should be turned off because D3Flow/PDQ is using it." << std::endl;
    assert(false);
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
    assert(false);
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

    /*
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
    */
}

void run_experiment(int argc, char **argv, uint32_t exp_type) {
    if (argc < 3) {
        std::cout << "Usage: <exe> exp_type conf_file" << std::endl;
        return;
    }

    std::string conf_filename(argv[2]);
    read_experiment_parameters(conf_filename, exp_type);

    if (params.big_switch) {
        topology = new BigSwitchTopology(params.num_hosts, params.bandwidth, params.queue_type);
    } else {
        std::cout << "Using Pfabric topology" << std::endl;
        topology = new PFabricTopology(params.num_hosts, params.num_agg_switches, params.num_core_switches, params.bandwidth, params.queue_type);
    }

    // Create AggChannel
    // Assume single direction INCAST traffic (N-1 send to 1)
    // The global variable "channels" is a vector of map with key being src-dst pair and value being ptr to the actual channel.
    // Each vector index classifies one QoS level, and within each QoS level we have a map of channels.
    // In other words, total # of channels = (# of hosts - 1) * (# of qos levels) in this INCAST case


    cwnds.resize(params.weights.size());
    dwnds.resize(params.weights.size());
    if (params.flow_type == D3_FLOW && params.big_switch && params.traffic_pattern == 1) {
        D3_allocation_counter_per_queue.resize(params.num_hosts * 2, 0);
        D3_num_allocations_per_queue.resize(params.num_hosts * 2, 0);
    }
    num_timeouts.resize(params.weights.size(), 0);
    pkt_drops_per_prio.resize(params.weights.size(), 0);
    pkt_drops_per_agg_sw.resize(params.num_agg_switches, 0);
    pkt_drops_per_host_queues.resize(params.num_hosts, 0);
    num_downgrades_per_host.resize(params.num_hosts, 0);
    num_check_passed.resize(params.weights.size(), 0);
    num_check_failed_and_downgrade.resize(params.weights.size(), 0);
    num_check_failed_but_stay.resize(params.weights.size(), 0);
    num_qos_h_downgrades.resize(params.weights.size(), 0);
    qos_h_issued_rpcs_per_host.resize(params.num_hosts, 0);
    per_prio_qd.resize(params.weights.size());
    per_pctl_downgrades.resize(params.weights.size(), 0);
    per_host_QoS_H_downgrades.resize(params.num_hosts, 0);
    per_host_QoS_H_rpcs.resize(params.num_hosts, 0);
    qos_h_admit_prob_per_host.resize(params.num_hosts);
    if (params.test_fairness) {
        fairness_qos_h_admit_prob_per_host.resize(params.num_hosts);
        fairness_qos_h_ts_per_host.resize(params.num_hosts);
        fairness_qos_h_rates_per_host.resize(params.num_hosts);
        fairness_qos_h_bytes_per_host.resize(params.num_hosts, 0);
        fairness_last_check_time.resize(params.num_hosts, 0);
    }
    num_outstanding_rpcs_one_sw.resize(params.num_qos_level + 1);    // the last one is QoS_H + QoS_M + QoS_L

    channels.resize(params.weights.size());
    //uint32_t num_src_dst_pair = 0;
    if (!params.use_flow_trace) {
        uint32_t count_channel = 0;
        if (params.traffic_pattern == 0) {
            if (params.flow_type == HOMA_FLOW) {
                for (uint32_t i = 0; i < params.num_hosts - 1; i++) {
                    auto channel = Factory::get_channel(count_channel, topology->hosts[i], topology->hosts[params.num_hosts - 1], 0, NULL, params.flow_type);
                    topology->hosts[i]->set_channel(channel);   // TODO: fix channel push
                    count_channel++;
                }
            } else {
                for (uint32_t i = 0; i < params.weights.size(); i++) {
                    for (uint32_t j = 0; j < params.num_hosts - 1; j++) {
                        auto src_dst_pair = std::make_pair(j, params.num_hosts - 1);
                        channels[i][src_dst_pair] = new AggChannel(count_channel, topology->hosts[j], topology->hosts[params.num_hosts - 1], i);
                        //std::cout << "creating channel[" << count_channel << "], src: " << topology->hosts[j]->id << ", dst: " << topology->hosts[params.num_hosts - 1]->id << ", prio: " << i  <<std::endl;
                        count_channel++;
                        // set agg_channel to NICs
                        if (params.real_nic) {
                            //channels[i][src_dst_pair]->src->nic->set_agg_channels(channels[i][src_dst_pair]);
                            topology->hosts[j]->nic->set_agg_channels(channels[i][src_dst_pair]);
                        } else if (params.flow_type == QJUMP_FLOW) {
                            topology->hosts[j]->set_agg_channels(channels[i][src_dst_pair]);
                        }
                    }
                }
            }
        } else {
            if (params.flow_type == HOMA_FLOW) {
                for (uint32_t i = 0; i < params.num_hosts; i++) {
                    for (uint32_t j = 0; j < params.num_hosts; j++) {
                        if (i != j) {
                            auto channel = Factory::get_channel(count_channel, topology->hosts[i], topology->hosts[j], 0, NULL, params.flow_type);
                            topology->hosts[i]->set_channel(channel);
                            count_channel++;
                        }
                    }
                }
            } else {
                for (uint32_t i = 0; i < params.weights.size(); i++) {
                    for (uint32_t j = 0; j < params.num_hosts; j++) {
                        for (uint32_t k = 0; k < params.num_hosts; k++) {
                            if (j != k) {
                                //auto src_dst_pair = std::make_pair(topology->hosts[j], topology->hosts[k]);
                                auto src_dst_pair = std::make_pair(j, k);
                                channels[i][src_dst_pair] = new AggChannel(count_channel, topology->hosts[j], topology->hosts[k], i);
                                //std::cout << "creating channel[" << count_channel << "], src: " << topology->hosts[j]->id << ", dst: " << topology->hosts[k]->id << ", prio: " << i  <<std::endl;
                                count_channel++;
                                // set agg_channel to NICs
                                if (params.real_nic) {
                                    //channels[i][src_dst_pair]->src->nic->set_agg_channels(channels[i][src_dst_pair]);
                                    topology->hosts[j]->nic->set_agg_channels(channels[i][src_dst_pair]);
                                } else if (params.flow_type == QJUMP_FLOW) {
                                    topology->hosts[j]->set_agg_channels(channels[i][src_dst_pair]);
                                }
                            }
                        }
                    }
                }
            }

        }

        // init the vector used in dynamic load setting
        for (uint32_t i = 0; i < params.num_hosts; i++) {
            for (uint32_t j = 0; j < params.num_hosts; j++) {
                auto src_dst_pair = std::make_pair(i, j);
                time_spent_send_data[src_dst_pair] = 0;
                burst_start_time[src_dst_pair] = 0;
                curr_burst_size[src_dst_pair] = 0;
                curr_burst_size_in_flow[src_dst_pair] = 0;
                flip_coin[src_dst_pair] = 0;
            }
        }
    }



    uint32_t num_flows = params.num_flows_to_run;

    // generate flows
    FlowGenerator *fg;
    if (params.use_flow_trace) {
        fg = new FlowReader(num_flows, topology, params.cdf_or_flow_trace);
        fg->make_flows();
        std::cout << "FlowReader make_flows() done" << std::endl;
    }
    //else if (params.interarrival_cdf != "none") {
    //    fg = new CustomCDFFlowGenerator(num_flows, topology, params.cdf_or_flow_trace, params.interarrival_cdf);
    //    fg->make_flows();
    //}
    //else if (params.permutation_tm != 0) {
    //    fg = new PermutationTM(num_flows, topology, params.cdf_or_flow_trace);
    //    fg->make_flows();
    //}
    else if (params.bytes_mode) {
        std::cout << "params.cdf_or_flow_trace: " << params.cdf_or_flow_trace << std::endl;
        //fg = new PoissonFlowBytesGenerator(num_flows, topology, params.cdf_or_flow_trace);
        fg = new FlowBytesGenerator(num_flows, topology, params.cdf_or_flow_trace);
        fg->make_flows();
    }
    else if (params.traffic_imbalance < 0.01) {
        fg = new PoissonFlowGenerator(num_flows, topology, params.cdf_or_flow_trace);
        fg->make_flows();
    }
    else {
        assert(false);
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

    //uint64_t total_arrival_bytes = 0;
    for (uint32_t i = 0; i < flows_sorted.size(); i++) {
        Flow* f = flows_sorted[i];
        if (exp_type == GEN_ONLY) {
            std::cout << f->id << " " << f->size << " " << f->src->id << " " << f->dst->id << " " << 1e6*f->start_time << "\n";
            //total_arrival_bytes += f->size;
            //std::cout << (f->start_time - params.first_flow_start_time) * 1e6 << "," << total_arrival_bytes << "\n";
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

    //// timing pure simulation execution time
    time_t clock_end_time;
    time(&clock_end_time);
    double duration = difftime(clock_end_time, clock_start_time);
    std::cout << " Pure simulation execution time: " << duration << " seconds\n";
    std::cout << " Events/sec: " << (double) event_total_count / duration << std::endl;
    std::cout << " Data Pkts/sec: " << (double) pkt_total_count / duration << std::endl;
    ////

    //uint32_t num_4K_RPCs = 0;
    uint32_t num_16K_RPCs = 0;
    uint32_t num_32K_RPCs = 0;
    uint32_t num_64K_RPCs = 0;
    uint32_t num_unfinished_flows = 0;
    for (uint32_t i = 0; i < flows_sorted.size(); i++) {
        Flow *f = flows_to_schedule[i];
        //validate_flow(f);     // do not validate flow
        if(!f->finished && !f->terminated) {
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
        //if (f->size == 4096) {
        //    num_4K_RPCs++;
        //}
        if (f->size == 16384) {
            num_16K_RPCs++;
        } else if (f->size == 32768) {
            num_32K_RPCs++;
        } else if (f->size == 65536) {
            num_64K_RPCs++;
        }
    }
    std::cout << "Unfinished Flows: " << num_unfinished_flows << std::endl;
    std::cout << "Finished Flows: " << flows_sorted.size() - num_unfinished_flows << std::endl;
    if (params.early_termination) {
        std::cout << "Terminated Flows: " << num_early_termination << std::endl;
    }
    if (params.test_size_to_priority) {
        std::cout << "Num 16K RPCs: " << num_16K_RPCs << std::endl;
        std::cout << "Num 32K RPCs: " << num_32K_RPCs << std::endl;
        std::cout << "Num 64K RPCs: " << num_64K_RPCs << std::endl;
    }

    std::vector<std::deque<Flow*>> flows_by_prio(params.weights.size());
    std::vector<std::deque<Flow*>> flows_by_init_prio(params.weights.size());
    std::vector<uint32_t> unfinished_flows_by_prio(params.weights.size());
    std::vector<uint32_t> num_flow_assigned_per_prio(params.weights.size());
    std::vector<uint32_t> num_flow_assigned_per_prio_unf(params.weights.size());
    uint32_t num_pctl = params.num_pctl;
    std::vector<std::vector<std::deque<Flow*>>> flows_by_prio_percentile(num_pctl);
    std::vector<double> useful_results;
    uint32_t flow_cnt = 0;
    uint32_t flow_percentile = 0;
    uint32_t num_QoS_H_RPCs_missed_target = 0;
    double total_flow_completion_time = 0;
    for (int i = 0; i < num_pctl; i++) {
        flows_by_prio_percentile[i].resize(params.weights.size());
    }
    for (Flow *f: flows_sorted) {
        flow_cnt++;
        if (!f->finished) {
            ////unfinished_flows_by_prio[f->flow_priority]++;
            unfinished_flows_by_prio[f->run_priority]++;
            num_flow_assigned_per_prio_unf[f->flow_priority]++;
        } else {
            ////flows_by_prio[f->flow_priority].push_back(f);
            flows_by_prio[f->run_priority].push_back(f);
            total_flow_completion_time += f->flow_completion_time;
            num_flow_assigned_per_prio[f->flow_priority]++;
            flows_by_prio_percentile[flow_percentile][f->run_priority].push_back(f);
            //if (f->flow_priority == 0 && f->run_priority == 0 && f->flow_completion_time * 1e6 > params.high_prio_lat_target) {
            if (f->flow_priority == 0 && f->run_priority == 0 && f->flow_completion_time * 1e6 > params.hardcoded_targets[0]) {
                num_QoS_H_RPCs_missed_target++;
            }
        }
        flows_by_init_prio[f->flow_priority].push_back(f);

        if (flow_cnt == num_flows / num_pctl) {
            flow_percentile++;
            flow_cnt = 0;
        }
    }

    double global_first_flow_time = std::numeric_limits<double>::max();
    double global_last_flow_time = 0;
    std::vector<double> start_end_time(params.weights.size());
    for (uint32_t i = 0; i < params.weights.size(); i++) {
        if (flows_by_prio[i].empty()) { continue; }
        double flow_start_time = flows_by_prio[i][0]->start_time;
        std::sort (flows_by_prio[i].begin(), flows_by_prio[i].end(), [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->finish_time < rhs->finish_time;
        });
        start_end_time[i] = flows_by_prio[i][flows_by_prio[i].size() - 1]->finish_time - flow_start_time;    // assuming RPCs from all Priority levels start roughly simultaneously
        //std::cout << "start_end_time[" << i << "]: " << start_end_time[i] << std::endl;

        if (global_first_flow_time > flow_start_time) {
            global_first_flow_time = flow_start_time;
        }
        if (global_last_flow_time < flows_by_prio[i][flows_by_prio[i].size() - 1]->finish_time) {
            global_last_flow_time = flows_by_prio[i][flows_by_prio[i].size() - 1]->finish_time;
        }
    }
    //std::cout << "global last flow time: " << global_last_flow_time << "; global first flow time: " << global_first_flow_time << std::endl;

    for (uint32_t i = 0; i < params.weights.size(); i++) {
        if (flows_by_prio[i].empty()) { continue; }
        std::sort (flows_by_prio[i].begin(), flows_by_prio[i].end(), [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->start_time < rhs->start_time;
        });
    }
    for (uint32_t i = 0; i < params.weights.size(); i++) {
        if (flows_by_init_prio[i].empty()) { continue; }
        std::sort (flows_by_init_prio[i].begin(), flows_by_init_prio[i].end(), [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->start_time < rhs->start_time;
        });
    }

    std::vector<std::deque<Flow *>> flows_by_prio_cp2 = flows_by_prio;
    std::vector<std::deque<Flow *>> flows_by_prio_cp3 = flows_by_prio;
    //std::vector<std::deque<Flow *>> flows_by_prio_cp4 = flows_by_prio;

    std::vector<double> avg_pkt_queuing_delays(params.weights.size());
    std::cout << std::setprecision(2) << std::fixed;
    for (uint32_t i = 0; i < flows_by_prio.size(); i++) {
        //uint32_t first_quarter = flows_by_prio[i].size() * 0.25;
        uint32_t third_quarter = flows_by_prio[i].size() * 0.75;
        uint32_t first_10th = flows_by_prio[i].size() * 0.1;
        uint32_t last_10th = flows_by_prio[i].size() * 0.9;
        std::sort(flows_by_prio[i].begin(), flows_by_prio[i].end(), [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->flow_completion_time < rhs->flow_completion_time;
        });
        std::sort(flows_by_prio_cp2[i].begin() + flows_by_prio_cp2[i].size() / 2, flows_by_prio_cp2[i].end(), [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->flow_completion_time < rhs->flow_completion_time;
        });
        std::sort(flows_by_prio_cp3[i].begin() + first_10th, flows_by_prio_cp3[i].begin() + last_10th, [](Flow *lhs, Flow *rhs) -> bool {
            return lhs->flow_completion_time < rhs->flow_completion_time;
        });

        //// used only for mixed size exp to measure 32KB & 64KB latency separately
        /*
        std::vector<std::vector<double>> rpcs_mid80_32KB(params.num_qos_level);
        std::vector<std::vector<double>> rpcs_mid80_64KB(params.num_qos_level);
        for (uint32_t j = first_10th; j < last_10th; j++) {
            if (flows_by_prio_cp3[i][j]->size == 32768) {
                rpcs_mid80_32KB[i].push_back(flows_by_prio_cp3[i][j]->flow_completion_time);
            } else if (flows_by_prio_cp3[i][j]->size == 65536) {
                rpcs_mid80_64KB[i].push_back(flows_by_prio_cp3[i][j]->flow_completion_time);
            }
        }
        // actually no need need to sort
        //std::sort(rpcs_mid80_32KB[i].begin(), rpcs_mid80_32KB[i].end());
        //std::sort(rpcs_mid80_64KB[i].begin(), rpcs_mid80_64KB[i].end());
        uint32_t median_mid_80_32KB = rpcs_mid80_32KB[i].size() * 0.5;
        uint32_t tail99_mid_80_32KB = rpcs_mid80_32KB[i].size() * 0.99;
        uint32_t tail999_mid_80_32KB = rpcs_mid80_32KB[i].size() * 0.999;
        uint32_t max_mid_80_32KB = rpcs_mid80_32KB[i].size() - 1;
        uint32_t median_mid_80_64KB = rpcs_mid80_64KB[i].size() * 0.5;
        uint32_t tail99_mid_80_64KB = rpcs_mid80_64KB[i].size() * 0.99;
        uint32_t tail999_mid_80_64KB = rpcs_mid80_64KB[i].size() * 0.999;
        uint32_t max_mid_80_64KB = rpcs_mid80_64KB[i].size() - 1;
        */


        //std::sort(flows_by_prio_cp4[i].begin(), flows_by_prio_cp4[i].begin() + flows_by_prio_cp4[i].size() / 2, [](Flow *lhs, Flow *rhs) -> bool {
        //    return lhs->flow_completion_time < rhs->flow_completion_time;
        //});
        //// find avg delay
        //double temp_sum = 0;
        //for (auto k : flows_by_prio[i]) {
        //    temp_sum += k->flow_completion_time;
        //}
        //double avg_delay = temp_sum / flows_by_prio[i].size();
        //// end of find avg delay
        if (flows_by_prio[i].empty()) {
            std::cout << "Priority " << i << ": (0 Flow)" << std::endl;
        } else {
            uint32_t median = flows_by_prio[i].size() / 2;
            uint32_t tail_70 = flows_by_prio[i].size() * 0.70;
            uint32_t tail_80 = flows_by_prio[i].size() * 0.80;
            uint32_t tail_90 = flows_by_prio[i].size() * 0.90;
            uint32_t tail_99 = flows_by_prio[i].size() * 0.99;
            uint32_t tail_999 = flows_by_prio[i].size() * 0.999;
            uint32_t total_max = flows_by_prio[i].size() - 1;
            uint32_t median_refined_mid_80 = median;
            ////uint32_t tail_10_refined_mid_80 = flows_by_prio[i].size() * (0.1 + 0.8 * 0.10);
            ////uint32_t tail_30_refined_mid_80 = flows_by_prio[i].size() * (0.1 + 0.8 * 0.30);
            uint32_t tail_70_refined_mid_80 = flows_by_prio[i].size() * (0.1 + 0.8 * 0.70);
            uint32_t tail_80_refined_mid_80 = flows_by_prio[i].size() * (0.1 + 0.8 * 0.80);
            uint32_t tail_90_refined_mid_80 = flows_by_prio[i].size() * (0.1 + 0.8 * 0.90);
            uint32_t tail_99_refined_mid_80 = flows_by_prio[i].size() * (0.1 + 0.8 * 0.99);
            uint32_t tail_999_refined_mid_80 = flows_by_prio[i].size() * (0.1 + 0.8 * 0.999);
            uint32_t mid_80_max = flows_by_prio[i].size() * 0.9 - 1;
            uint32_t median_refined_last_half = third_quarter;
            uint32_t tail_99_refined_last_half = flows_by_prio[i].size() * (0.5 + 0.5 * 0.99);
            uint32_t tail_999_refined_last_half = flows_by_prio[i].size() * (0.5 + 0.5 * 0.999);
            uint32_t last_half_max = flows_by_prio[i].size() - 1;
            //uint32_t median_refined_first_half = first_quarter;
            //uint32_t tail_99_refined_first_half = flows_by_prio[i].size() * (0.5 * 0.99);
            //uint32_t tail_999_refined_first_half = flows_by_prio[i].size() * (0.5 * 0.999);
            //uint32_t first_half_max = flows_by_prio[i].size() * 0.5 - 1;

            std::cout << "Priority " << i << ": (" << flows_by_prio[i].size() + unfinished_flows_by_prio[i]
                << " Flows, Finished/Unf: " << flows_by_prio[i].size() << "/"
                << unfinished_flows_by_prio[i] << ")" << " (Assigned: " << num_flow_assigned_per_prio[i] + num_flow_assigned_per_prio_unf[i]
                << ", Finished/Unf: " << num_flow_assigned_per_prio[i] << "/" <<  num_flow_assigned_per_prio_unf[i] << ")" << std::endl

                /*
                << "FCT (in us) (100% RPCs) (Median, 70th, 80th, 90th, 99th, 99.9th, max) = "
                << flows_by_prio[i][median]->flow_completion_time * 1000000 << ", "
                << flows_by_prio[i][tail_70]->flow_completion_time * 1000000 << ", "
                << flows_by_prio[i][tail_80]->flow_completion_time * 1000000 << ", "
                << flows_by_prio[i][tail_90]->flow_completion_time * 1000000 << ", "
                << flows_by_prio[i][tail_99]->flow_completion_time * 1000000 << ", "
                << flows_by_prio[i][tail_999]->flow_completion_time * 1000000 << ", "
                << flows_by_prio[i][total_max]->flow_completion_time * 1000000 << std::endl
                */

                //<< "Avg FCT (in us) (100% RPCs) = " << avg_delay * 1e6 << std::endl
                //<< "FCT (in us) (first 50% RPCs) (Median, 99th, 99.9th, max) = "
                //<< flows_by_prio_cp4[i][median_refined_first_half]->flow_completion_time * 1000000 << ", "
                //<< flows_by_prio_cp4[i][tail_99_refined_first_half]->flow_completion_time * 1000000 << ", "
                //<< flows_by_prio_cp4[i][tail_999_refined_first_half]->flow_completion_time * 1000000 << ", "
                //<< flows_by_prio_cp4[i][first_half_max]->flow_completion_time * 1000000 << std::endl
                ////<< "FCT (in us) (mid 80% RPCs) (Median, 99th, 99.9th, max) = "
                << "FCT (in us) (mid 80% RPCs) (Median, 70th, 80th, 90th, 99th, 99.9th, max) = "
                ////<< "FCT (in us) (mid 80% RPCs) (10th, 30th, Median, 99th, 99.9th, max) = "
                ////<< flows_by_prio_cp3[i][tail_10_refined_mid_80]->flow_completion_time * 1000000 << ", "
                ////<< flows_by_prio_cp3[i][tail_30_refined_mid_80]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp3[i][median_refined_mid_80]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp3[i][tail_70_refined_mid_80]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp3[i][tail_80_refined_mid_80]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp3[i][tail_90_refined_mid_80]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp3[i][tail_99_refined_mid_80]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp3[i][tail_999_refined_mid_80]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp3[i][mid_80_max]->flow_completion_time * 1000000 << std::endl
                //// used only for mixed size exp to measure 32KB & 64KB latency separately
		/*
                << "32KB RPC FCT (in us) (mid 80% RPCs) (Median, 99th, 99.9th, max) = "
                << rpcs_mid80_32KB[i][median_mid_80_32KB] * 1e6 << ", "
                << rpcs_mid80_32KB[i][tail99_mid_80_32KB] * 1e6 << ", "
                << rpcs_mid80_32KB[i][tail999_mid_80_32KB] * 1e6 << ", "
                << rpcs_mid80_32KB[i][max_mid_80_32KB] * 1e6 << std::endl
                << "64KB RPC FCT (in us) (mid 80% RPCs) (Median, 99th, 99.9th, max) = "
                << rpcs_mid80_64KB[i][median_mid_80_64KB] * 1e6 << ", "
                << rpcs_mid80_64KB[i][tail99_mid_80_64KB] * 1e6 << ", "
                << rpcs_mid80_64KB[i][tail999_mid_80_64KB] * 1e6 << ", "
                << rpcs_mid80_64KB[i][max_mid_80_64KB] * 1e6 << std::endl
		*/
                ////

                << "FCT (in us) (last 50% RPCs) (Median, 99th, 99.9th, max) = "
                << flows_by_prio_cp2[i][median_refined_last_half]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp2[i][tail_99_refined_last_half]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp2[i][tail_999_refined_last_half]->flow_completion_time * 1000000 << ", "
                << flows_by_prio_cp2[i][last_half_max]->flow_completion_time * 1000000 << std::endl;
                /*
                << "99.9th (out of 100% RPCs) RPC is RPC[" << flows_by_prio[i][tail_999]->id << "], size = "
                << flows_by_prio[i][tail_999]->size << "; completion time = "
                << flows_by_prio[i][tail_999]->flow_completion_time * 1e6;
                if (params.real_nic) {
                    std::cout << "; start time diff = " << (flows_by_prio[i][tail_999]->rnl_start_time - flows_by_prio[i][tail_999]->start_time) * 1e6 << std::endl;
                } else {
                    std::cout << std::endl;
                }
                */

                //"; total queuing time = " <<
                //flows_by_prio[i][tail_999]->total_queuing_time * 1e6 << "; avg inter-pkt spacing = " <<
                //flows_by_prio[i][tail_999]->get_avg_inter_pkt_spacing_in_us() << " us" << std::endl;
            //useful_results.push_back(flows_by_prio_cp2[i][median_refined_last_half]->flow_completion_time * 1000000);
            //useful_results.push_back(flows_by_prio_cp2[i][tail_999_refined_last_half]->flow_completion_time * 1000000);
            useful_results.push_back(flows_by_prio_cp3[i][median_refined_mid_80]->flow_completion_time * 1000000);
            useful_results.push_back(flows_by_prio_cp3[i][tail_999_refined_mid_80]->flow_completion_time * 1000000);



            ////

            /*
            if (i == 0) {
                std::cout << "Qos_H RPC completion time (10th/20th/30th/40th/50th/60th/70th/80th/90th/99.9th): "
                    << flows_by_prio_cp3[i][flows_by_prio[i].size() * (0.1 + 0.8 * 0.1)]->flow_completion_time * 1e6 << "/"
                    << flows_by_prio_cp3[i][flows_by_prio[i].size() * (0.1 + 0.8 * 0.2)]->flow_completion_time * 1e6 << "/"
                    << flows_by_prio_cp3[i][flows_by_prio[i].size() * (0.1 + 0.8 * 0.3)]->flow_completion_time * 1e6 << "/"
                    << flows_by_prio_cp3[i][flows_by_prio[i].size() * (0.1 + 0.8 * 0.4)]->flow_completion_time * 1e6 << "/"
                    << flows_by_prio_cp3[i][flows_by_prio[i].size() * (0.1 + 0.8 * 0.5)]->flow_completion_time * 1e6 << "/"
                    << flows_by_prio_cp3[i][flows_by_prio[i].size() * (0.1 + 0.8 * 0.6)]->flow_completion_time * 1e6 << "/"
                    << flows_by_prio_cp3[i][flows_by_prio[i].size() * (0.1 + 0.8 * 0.7)]->flow_completion_time * 1e6 << "/"
                    << flows_by_prio_cp3[i][flows_by_prio[i].size() * (0.1 + 0.8 * 0.8)]->flow_completion_time * 1e6 << "/"
                    << flows_by_prio_cp3[i][flows_by_prio[i].size() * (0.1 + 0.8 * 0.9)]->flow_completion_time * 1e6 << "/"
                    << flows_by_prio_cp3[i][flows_by_prio[i].size() * (0.1 + 0.8 * 0.999)]->flow_completion_time * 1e6 << std::endl;
            }
            */
        }

    }

    if (!params.disable_pkt_logging) {
        std::cout << "Measured from queues:" << std::endl;
        for (uint32_t i = 0; i < per_prio_qd.size(); i++) {
            if (per_prio_qd[i].empty()) {
                continue;
            }
            uint32_t first_10th = per_prio_qd[i].size() * 0.1;
            uint32_t last_10th = per_prio_qd[i].size() * 0.9;
            std::sort(per_prio_qd[i].begin() + first_10th, per_prio_qd[i].begin() + last_10th);
            uint32_t median_refined_mid_80 = per_prio_qd[i].size() * 0.5;
            uint32_t tail_80_refined_mid_80 = per_prio_qd[i].size() * (0.1 + 0.8 * 0.80);
            uint32_t tail_90_refined_mid_80 = per_prio_qd[i].size() * (0.1 + 0.8 * 0.90);
            uint32_t tail_99_refined_mid_80 = per_prio_qd[i].size() * (0.1 + 0.8 * 0.99);
            uint32_t tail_999_refined_mid_80 = per_prio_qd[i].size() * (0.1 + 0.8 * 0.999);
            uint32_t mid_80_max = per_prio_qd[i].size() * 0.9 - 1;
            std::cout << "Priority[" << i << "] Pkt Queuing Delay (for mid 80% total packets) (Median//80th//90th//99th//99.9th/max) = "
                << per_prio_qd[i][median_refined_mid_80] << "//"
                << per_prio_qd[i][tail_80_refined_mid_80] << "//"
                << per_prio_qd[i][tail_90_refined_mid_80] << "//"
                << per_prio_qd[i][tail_99_refined_mid_80] << "//"
                << per_prio_qd[i][tail_999_refined_mid_80] << "//"
                << per_prio_qd[i][mid_80_max] << std::endl;
        }
    }

    // pkt queuing/rtt logging
    if (!params.disable_pkt_logging) {
        std::cout << "Per pkt RTT (mid 80%):" << std::endl;
        for (uint32_t i = 0; i < per_pkt_rtt.size(); i++) {
            uint32_t first_10th = per_pkt_rtt[i].size() * 0.1;
            uint32_t last_10th = per_pkt_rtt[i].size() * 0.9;
            if (per_pkt_rtt[i].empty()) { continue; }
            sort(per_pkt_rtt[i].begin() + first_10th, per_pkt_rtt[i].begin() + last_10th);
            uint32_t median = per_pkt_rtt[i].size() * 0.5;
            uint32_t tail_99 = per_pkt_rtt[i].size() * (0.1 + 0.8 * 0.99);
            uint32_t tail_999 = per_pkt_rtt[i].size() * (0.1 + 0.8 * 0.999);
            uint32_t max = per_pkt_rtt[i].size() * 0.9 - 1;
            std::cout << "Priority[" << i << "] (100% packets) (tot pkts: " << per_pkt_rtt[i].size()
                << ") (Median, 99th, 99.9th, Max) (us) = " << per_pkt_rtt[i][median] << ", "
                << per_pkt_rtt[i][tail_99] << ", "<< per_pkt_rtt[i][tail_999] << ", " << per_pkt_rtt[i][max] << std::endl;
        }
    }

    // cwnd logging
    if (!params.disable_cwnd_logging) {
        for (uint32_t i = 0; i < params.weights.size(); i++) {
          if (cwnds[i].empty()) { continue; }
          sort(cwnds[i].begin(), cwnds[i].end());
          uint32_t cwnd_1th = cwnds[i].size() * 0.01;
          uint32_t cwnd_median = cwnds[i].size() * 0.5;
          uint32_t cwnd_tail = cwnds[i].size() * 0.99;
          std::cout << "Cwnd[" << i << "](1th, median, 99th) = " << cwnds[i][cwnd_1th] << ", " << cwnds[i][cwnd_median] << ", " << cwnds[i][cwnd_tail] << std::endl;
        }
    }

    // dwnd logging
    if (!params.disable_dwnd_logging) {
        for (uint32_t i = 0; i < params.weights.size(); i++) {
          if (dwnds[i].empty()) { continue; }
          sort(dwnds[i].begin(), dwnds[i].end());
          uint32_t dwnd_1th = dwnds[i].size() * 0.01;
          uint32_t dwnd_20th = dwnds[i].size() * 0.20;
          uint32_t dwnd_median = dwnds[i].size() * 0.5;
          uint32_t dwnd_80th = dwnds[i].size() * 0.80;
          uint32_t dwnd_tail = dwnds[i].size() * 0.99;
          std::cout << "Dwnd[" << i << "](1th, 20th, 50th, 80th, 99th) = " << dwnds[i][dwnd_1th] << ", "  << dwnds[i][dwnd_20th] << ", "
                  << dwnds[i][dwnd_median] << ", " << dwnds[i][dwnd_80th] << ", " << dwnds[i][dwnd_tail] << std::endl;
        }
    }

    // When a flow is a sinlge packet:
    // Calculate throughput (not including unfinished packets which are the one gets dropped by the queue)
    std::vector<uint64_t> sum_bytes_per_prio(params.weights.size(), 0);
    std::vector<double> throughput_per_prio(params.weights.size(), 0.0);
    for (uint32_t i = 0; i < params.weights.size(); i++) {
        for (int j = 0; j < flows_by_prio[i].size(); j++) {
            ////sum_bytes_per_prio[i] += flows_by_prio[i][j]->size;
            sum_bytes_per_prio[i] += flows_by_prio[i][j]->size + flows_by_prio[i][j]->size_in_pkt * params.hdr_size;    // count hdr size as well
        }
        throughput_per_prio[i] = sum_bytes_per_prio[i] * 8.0 / start_end_time[i] / 1000000000 / params.num_hosts;
        std::cout << "Priority[" << i << "] throughput = " << throughput_per_prio[i] << " Gbps" << std::endl;
    }
    //TODO: make generic on pkt drops
    // assume one big sw topo for now
    //for (uint32_t i = 0; i < topology->switches[0]->queues.size(); i++) {
    //    auto queue = topology->switches[0]->queues[i];
    //    if (queue->pkt_drop > 0) {
    //        std::cout << "At switch queue[" << queue->unique_id << "]: # Pkt drop = " << queue->pkt_drop << std::endl;
    //    }
    //}

    if (params.flow_type == D3_FLOW && params.big_switch && params.traffic_pattern == 1) {
        uint32_t num_queues = D3_num_allocations_per_queue.size();
        double sum_avg_allocations_by_queue = 0;
        for (uint32_t i = 0; i < num_queues; i++) {
            double avg_allocations_by_queue = D3_allocation_counter_per_queue[i] / D3_num_allocations_per_queue[i];
            //std::cout << "avg allocations at Queue[" << i << "]: " << avg_allocations_by_queue << std::endl;
            //std::cout << "num allocations at Queue[" << i << "]: " << D3_num_allocations_per_queue[i] << std::endl;
            sum_avg_allocations_by_queue += avg_allocations_by_queue;
        }
        std::cout << "Avg allocation counter: " << sum_avg_allocations_by_queue / num_queues << " Gbps" << std::endl;
    }


    ////if (params.flow_type != QJUMP_FLOW && params.flow_type != D3_FLOW && params.flow_type != PDQ_FLOW) {
    if (params.flow_type == AEQUITAS_FLOW) {
        std::vector<double> final_avg_qos_dist;
        std::cout << "Final QoS Dist: ";
        std::cout << std::setprecision(1) << std::fixed;
        for (uint32_t i = 0; i < params.weights.size(); i++) {
            double dist = (double)flows_by_prio[i].size() / total_finished_flows * 100;
            final_avg_qos_dist.push_back(dist);
            if (i < params.weights.size() - 1) {
                std::cout << dist << "/";
            } else {
                std::cout << dist << std::endl;
            }
        }



        //std::cout << "Percentile, QoS Dist trend, QoS_H dist, RPC Median(us), RPC 99.9th(us), end pctl timestamp(s):" << std::endl;
        std::cout << "Percentile, QoS Dist trend, QoS_H dist, QoS_H RPC 99.9th(us), QoS_M RPC 99.9th(us), end pctl timestamp(s):" << std::endl;
        std::vector<double> final_dist;
        for (uint32_t i = 0; i < flows_by_prio_percentile.size(); i++) {
            if (flows_by_prio_percentile[i].empty()) {
                continue;
            }
            //std::cout << "percentile " << i * ((double)100/num_pctl) << "~" << (i + 1) * ((double)100/num_pctl) << ",";
            std::cout << (i + 1) * ((double)100/num_pctl) << ", ";
            uint32_t sum_at_curr_pctl = 0;
            for (uint32_t j = 0; j < params.weights.size(); j++) {
                sum_at_curr_pctl += flows_by_prio_percentile[i][j].size();
                //std::cout << "prio[" << j << "]: " << flows_by_prio_percentile[i][j].size() << std::endl;
            }
            double qos_H_dist;
            for (uint32_t j = 0; j < params.weights.size(); j++) {
                double dist = (double)flows_by_prio_percentile[i][j].size() / sum_at_curr_pctl * 100;
                if (j == 0) {
                    qos_H_dist = dist;
                }
                if (j < params.weights.size() - 1) {
                    std::cout << dist << "/";
                } else {
                    std::cout << dist << ", ";
                }
                if (i == flows_by_prio_percentile.size() - 1) {
                    final_dist.push_back(dist);
                }
            }
            std::cout << qos_H_dist << ", ";
            //uint32_t median = flows_by_prio_percentile[i][0].size() * 0.5;
            uint32_t tail_999_h = flows_by_prio_percentile[i][0].size() * 0.999;
            uint32_t tail_999_m = flows_by_prio_percentile[i][1].size() * 0.999;
            std::sort(flows_by_prio_percentile[i][0].begin(), flows_by_prio_percentile[i][0].end(), [](Flow *lhs, Flow *rhs) -> bool {
                return lhs->flow_completion_time < rhs->flow_completion_time;
            });
            std::sort(flows_by_prio_percentile[i][1].begin(), flows_by_prio_percentile[i][1].end(), [](Flow *lhs, Flow *rhs) -> bool {
                return lhs->flow_completion_time < rhs->flow_completion_time;
            });
            std::cout << std::setprecision(2) << std::fixed;
            //std::cout << flows_by_prio_percentile[i][0][median]->flow_completion_time * 1e6 << ", "
            //    << flows_by_prio_percentile[i][0][tail_999]->flow_completion_time * 1e6;
            std::cout << flows_by_prio_percentile[i][0][tail_999_h]->flow_completion_time * 1e6 << ", "
                    << flows_by_prio_percentile[i][1][tail_999_m]->flow_completion_time * 1e6;
            std::cout << std::setprecision(6) << std::fixed;
            //std::cout << ", " << flows_by_prio_percentile[i][0][0]->start_time << std::endl;
            std::cout << ", " << flows_by_prio_percentile[i][0].back()->start_time << std::endl;
            std::cout << std::setprecision(1) << std::fixed;
        }

        // easy copy&paste for useful results:
        std::cout << std::setprecision(2) << std::fixed;
        std::cout << "[][][]: ";
        for (uint32_t i = 0; i < useful_results.size(); i++) {
            std::cout << useful_results[i] << ",";
        }
        std::cout << std::setprecision(1) << std::fixed;
        for (uint32_t i = 0; i < final_dist.size(); i++) {
            if (i < final_dist.size() - 1) {
                std::cout << final_dist[i] << "/";
            } else {
                //std::cout << final_dist[i] << std::endl;
                std::cout << final_dist[i];
            }
        }
        //std::cout << "," << final_dist[0] << std::endl;
        std::cout << "," << final_dist[0] << ",";
        for (uint32_t i = 0; i < final_avg_qos_dist.size(); i++) {
            std::cout << final_avg_qos_dist[i];
            if (i < final_avg_qos_dist.size() - 1) {
                std::cout << "/";
            }
        }
        std::cout << "," << final_avg_qos_dist[0] << std::endl;
        std::cout << std::setprecision(2) << std::fixed;
    }


    if (params.host_type == 6) { // per host dist is only meaningful to Aequitas
        // find per host dist:
        std::cout << "Per host distribution:" << std::endl;
        std::vector<std::vector<double>> per_host_dist(params.num_hosts, std::vector<double>(params.weights.size(), 0));
        for (uint32_t i = 0; i < params.num_hosts; i++) {
            for (uint32_t j = 0; j < params.weights.size(); j++) {

                //// In this method, assuming num_pctl = 10
                // take mid 80%
                if (params.num_pctl != 10) {
                    std::cout << "here num_pctl must be 10" << std::endl;
                    exit(1);
                }
                for (uint32_t l = 1; l <= 8; l++) {
                  for (uint32_t k = 0; k < flows_by_prio_percentile[l][j].size(); k++) {
                      if (flows_by_prio_percentile[l][j][k]->src->id == i) {
                          per_host_dist[i][j]++;
                      }
                  }
                }
                ////

                /* // last 10%
                for (uint32_t k = 0; k < flows_by_prio_percentile[flows_by_prio_percentile.size() - 1][j].size(); k++) {
                    if (flows_by_prio_percentile[flows_by_prio_percentile.size() - 1][j][k]->src->id == i) {
                        per_host_dist[i][j]++;
                    }
                }
                */

                /* // all (100%)
                for (uint32_t k = 0; k < flows_by_prio[j].size(); k++) {
                    if (flows_by_prio[j][k]->src->id == i) {
                        per_host_dist[i][j]++;
                    }
                }
                */
            }
        }
        for (uint32_t i = 0; i < params.num_hosts; i++) {
            uint32_t sum_dist = 0;
            std::cout << "Host[" << i << "]: ";
            for (uint32_t j = 0; j < params.weights.size(); j++) {
                sum_dist += per_host_dist[i][j];
            }
            for (uint32_t j = 0; j < params.weights.size(); j++) {
                double dist = (double) per_host_dist[i][j] / sum_dist;
                per_host_dist[i][j] = dist;
                if (j < params.weights.size() - 1) {
                    std::cout << dist * 100 << "/";
                } else {
                    std::cout << dist * 100 << std::endl;
                }
            }
        }
        std::vector<double> per_host_mean;
        std::vector<double> per_host_std_dev;
        per_host_mean.resize(params.weights.size());
        per_host_std_dev.resize(params.weights.size());
        // calculate mean & std dev
        for (uint32_t i = 0; i < params.weights.size(); i++) {
            double sum_dists = 0;
            for (uint32_t j = 0; j < params.num_hosts; j++) {
                sum_dists += per_host_dist[j][i];
            }
            per_host_mean[i] = sum_dists / params.num_hosts;
            double variance = 0;
            for (uint32_t j = 0; j < params.num_hosts; j++) {
                double x = per_host_dist[j][i] - per_host_mean[i];
                variance += x * x;
            }
            variance /= params.num_hosts;
            per_host_std_dev[i] = sqrt(variance);
        }


        std::cout << "Mean qos dist per host: ";
        for (uint32_t i = 0; i < params.weights.size(); i++) {
            if (i < params.weights.size() - 1) {
                std::cout << per_host_mean[i] << "/";
            } else {
                std::cout << per_host_mean[i] << std::endl;
            }
        }

        std::cout << std::setprecision(2) << std::fixed;
        std::cout << "Std Dev qos dist per host: ";
        for (uint32_t i = 0; i < params.weights.size(); i++) {
            if (i < params.weights.size() - 1) {
                std::cout << per_host_std_dev[i] << "/";
            } else {
                std::cout << per_host_std_dev[i] << std::endl;
            }
        }


        // per queue qos distribution -- standard deviation
        //std::vector<std::vector<uint32_t>> served_packets_per_prio;    // per queue per qos
        //served_packets_per_prio.resize(params.weights.size());
        std::vector<std::vector<double>> qos_dist;    // per qos per queue
        qos_dist.resize(params.weights.size());
        //std::vector<double> sum_qos_dist;    // per queue per qos
        //sum_qos_dist.resize(params.weights.size());
        std::vector<double> means_per_prio;
        means_per_prio.resize(params.weights.size());
        //std::vector<double> sum_packets_per_prio;
        //sum_packets_per_prio.resize(params.weights.size());
        std::vector<double> std_dev;
        std_dev.resize(params.weights.size());
        if (params.big_switch) {
            CoreSwitch *the_switch = ((BigSwitchTopology *)topology)->the_switch;
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                for (uint32_t j = 0; j < params.num_hosts; j++) {
                    qos_dist[i].push_back(the_switch->queues[j]->served_pkts_per_prio[i]);
                }
                for (uint32_t j = 0; j < params.num_hosts; j++) {
                    qos_dist[i].push_back(topology->hosts[j]->queue->served_pkts_per_prio[i]);
                }
            }
            uint32_t num_queues = qos_dist[0].size();
            for (uint32_t j = 0; j < num_queues; j++) {
                double sum_pkts = 0;
                for (uint32_t i = 0; i < params.weights.size(); i++) {
                    sum_pkts += qos_dist[i][j];
                }
                for (uint32_t i = 0; i < params.weights.size(); i++) {
                    qos_dist[i][j] = qos_dist[i][j] / sum_pkts;
                }
            }

            for (uint32_t i = 0; i < params.weights.size(); i++) {
                double sum_dists = 0;
                for (uint32_t j = 0; j < num_queues; j++) {
                    sum_dists += qos_dist[i][j];
                }
                means_per_prio[i] = sum_dists / num_queues;
                double variance = 0;
                for (uint32_t j = 0; j < num_queues; j++) {
                    double x = qos_dist[i][j] - means_per_prio[i];
                    variance += x * x;
                }
                variance /= num_queues;
                std_dev[i] = sqrt(variance);
            }


            std::cout << "Mean qos dist (pkts) in queues: prio[";
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                if (i < params.weights.size() - 1) {
                    std::cout << i << "/";
                } else {
                    std::cout << i << "]: ";
                }
            }
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                if (i < params.weights.size() - 1) {
                    std::cout << means_per_prio[i] << "/";
                } else {
                    std::cout << means_per_prio[i] << std::endl;
                }
            }

            std::cout << "Std Dev qos dist (pkts) in queues: prio[";
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                if (i < params.weights.size() - 1) {
                    std::cout << i << "/";
                } else {
                    std::cout << i << "]: ";
                }
            }
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                if (i < params.weights.size() - 1) {
                    std::cout << std_dev[i] << "/";
                } else {
                    std::cout << std_dev[i] << std::endl;
                }
            }

            /* display all queue qos distribution
            for (uint32_t i = 0; i < params.num_hosts; i++) {
                double total_pkts = 0;
                std::cout << "Big switch queue[" << i << "], prio[";
                for (uint32_t j = 0; j < params.weights.size(); j++) {
                    total_pkts += the_switch->queues[i]->served_pkts_per_prio[j];
                    if (j < params.weights.size() - 1) {
                        std::cout << j << ",";
                    } else {
                        std::cout << j << "]: ";
                    }
                }
                for (uint32_t j = 0; j < params.weights.size(); j++) {
                    if (j < params.weights.size() - 1) {
                        std::cout << the_switch->queues[i]->served_pkts_per_prio[j] / total_pkts << ",";
                    } else {
                      std::cout << the_switch->queues[i]->served_pkts_per_prio[j] / total_pkts << std::endl;
                    }
                }
            }
            for (uint32_t i = 0; i < params.num_hosts; i++) {
                double total_pkts = 0;
                std::cout << "Host[" << i << "], prio[";
                for (uint32_t j = 0; j < params.weights.size(); j++) {
                    total_pkts += topology->hosts[i]->queue->served_pkts_per_prio[j];
                    if (j < params.weights.size() - 1) {
                        std::cout << j << ",";
                    } else {
                        std::cout << j << "]: ";
                    }
                }
                for (uint32_t j = 0; j < params.weights.size(); j++) {
                    if (j < params.weights.size() - 1) {
                        std::cout << topology->hosts[i]->queue->served_pkts_per_prio[j] / total_pkts << ",";
                    } else {
                      std::cout << topology->hosts[i]->queue->served_pkts_per_prio[j] / total_pkts << std::endl;
                    }
                }
            }
            */
        } else {    // pfabric topo
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                // queues from switch
                for (uint32_t j = 0; j < topology->switches.size(); j++) {
                    for (uint32_t k = 0; k < ((Switch *) topology->switches[j])->queues.size(); k++) {
                        qos_dist[i].push_back(((Switch *) topology->switches[j])->queues[k]->served_pkts_per_prio[i]);
                    }
                }
                // queues from hosts
                for (uint32_t j = 0; j < params.num_hosts; j++) {
                    qos_dist[i].push_back(topology->hosts[j]->queue->served_pkts_per_prio[i]);
                }
            }
            uint32_t num_queues = qos_dist[0].size();
            std::cout << "num_queues: " << num_queues << std::endl;
            // convert from num_pkts to distribution (ratio)
            for (uint32_t j = 0; j < num_queues; j++) {
                double sum_pkts = 0;
                for (uint32_t i = 0; i < params.weights.size(); i++) {
                    sum_pkts += qos_dist[i][j];
                }
                for (uint32_t i = 0; i < params.weights.size(); i++) {
                    qos_dist[i][j] = qos_dist[i][j] / sum_pkts;
                }
            }

            // calculate mean & std dev
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                double sum_dists = 0;
                for (uint32_t j = 0; j < num_queues; j++) {
                    sum_dists += qos_dist[i][j];
                }
                means_per_prio[i] = sum_dists / num_queues;
                double variance = 0;
                for (uint32_t j = 0; j < num_queues; j++) {
                    double x = qos_dist[i][j] - means_per_prio[i];
                    variance += x * x;
                }
                variance /= num_queues;
                std_dev[i] = sqrt(variance);
            }


            std::cout << "Mean qos dist (pkts) in queues: prio[";
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                if (i < params.weights.size() - 1) {
                    std::cout << i << "/";
                } else {
                    std::cout << i << "]: ";
                }
            }
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                if (i < params.weights.size() - 1) {
                    std::cout << means_per_prio[i] << "/";
                } else {
                    std::cout << means_per_prio[i] << std::endl;
                }
            }

            std::cout << "Std Dev qos dist (pkts) in queues: prio[";
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                if (i < params.weights.size() - 1) {
                    std::cout << i << "/";
                } else {
                    std::cout << i << "]: ";
                }
            }
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                if (i < params.weights.size() - 1) {
                    std::cout << std_dev[i] << "/";
                } else {
                    std::cout << std_dev[i] << std::endl;
                }
            }
        }
    }

    // find how many QoS High RPCs and QoS Median RPCs meet target

    // method (1): count from run_time priority
    /*
    for (uint32_t i = 0; i < params.num_qos_level - 1; i++) {
        uint32_t num_RPCs = flows_by_prio_cp3[i].size();
        uint32_t first_10th = num_RPCs * 0.1;
        uint32_t last_10th = num_RPCs * 0.9;
        double pctg_passed = 0;
        uint32_t count = 0;
        for (uint32_t j = first_10th; j < last_10th; j++) {
            //std::cout << "lat = " << flows_by_prio_cp3[0][i]->flow_completion_time * 1e6 << " us" << std::endl;
            if (flows_by_prio_cp3[i][j]->flow_completion_time * 1e6 <= params.hardcoded_targets[i]) {
                count++;
            }
        }
        pctg_passed = (double) count / (num_RPCs * 0.8) * 100;
        std::cout << pctg_passed << "% out of Priority[" << i << "] RPCs passed the target(" << params.hardcoded_targets[i] << ")" << std::endl;
    }
    */

    // method (2): count from initially assigned priority
    /*
    for (uint32_t i = 0; i < params.num_qos_level - 1; i++) {
        uint32_t num_RPCs = flows_by_init_prio[i].size();
        double pctg_passed = 0;
        uint32_t count = 0;
        for (uint32_t j = 0; j < num_RPCs; j++) {
            if (flows_by_init_prio[i][j]->finished && flows_by_init_prio[i][j]->flow_completion_time * 1e6 <= params.hardcoded_targets[i]) {
                count++;
            }
        }
        pctg_passed = (double) count / num_RPCs * 100;
        // no need for counting mid 80%; numbers are pretty close
        //uint32_t first_10th = num_RPCs * 0.1;
        //uint32_t last_10th = num_RPCs * 0.9;
        //for (uint32_t j = first_10th; j < last_10th; j++) {
        //    if (flows_by_init_prio[i][j]->finished && flows_by_init_prio[i][j]->flow_completion_time * 1e6 <= params.hardcoded_targets[i]) {
        //        count++;
        //    }
        //}
        ////
        pctg_passed = (double) count / (num_RPCs * 0.8) * 100;
        std::cout << pctg_passed << "% out of Priority[" << i << "] RPCs passed the target(" << params.hardcoded_targets[i] << " us)" << std::endl;
    }
    */

    // method (3): count from initially assigned priority and count in terms of bytes instead of # of RPCs; use final target instead of downgrade targets
    if (!params.targets.empty()) {
        for (uint32_t i = 0; i < params.num_qos_level - 1; i++) {
            uint32_t num_RPCs = flows_by_init_prio[i].size();
            uint64_t sum_bytes = 0;
            double pctg_passed_bytes = 0;
            double pctg_passed_num_rpcs = 0;
            uint32_t num_RPCs_passed = 0;
            uint64_t bytes_passed = 0;
            for (uint32_t j = 0; j < flows_by_init_prio[i].size(); j++) {
                sum_bytes += flows_by_init_prio[i][j]->size;
                if (flows_by_init_prio[i][j]->finished && flows_by_init_prio[i][j]->flow_completion_time * 1e6 <= params.targets[i]) {
                    bytes_passed += flows_by_init_prio[i][j]->size;
                    num_RPCs_passed++;
                }
            }
            pctg_passed_bytes = (double) bytes_passed / sum_bytes * 100;
            pctg_passed_num_rpcs = (double) num_RPCs_passed / num_RPCs * 100;
            std::cout << pctg_passed_num_rpcs << "% out of Priority[" << i << "] RPCs (" << num_RPCs_passed << " out of " << num_RPCs << " RPCs) passed the final target(" << params.targets[i] << " us)" << std::endl;
            std::cout << pctg_passed_bytes << "% out of Priority[" << i << "] traffic (bytes) passed the final target(" << params.targets[i] << " us)" << std::endl;
        }
    }

    /*
    // mid 80% for method (3)
    for (uint32_t i = 0; i < params.num_qos_level - 1; i++) {
        uint32_t num_RPCs = flows_by_init_prio[i].size();
        uint64_t sum_bytes = 0;
        double pctg_passed_bytes = 0;
        double pctg_passed_num_rpcs = 0;
        uint32_t num_RPCs_passed = 0;
        uint64_t bytes_passed = 0;
        uint32_t first_10th = 0.1 * num_RPCs;
        uint32_t last_10th = 0.9 * num_RPCs;
        for (uint32_t j = first_10th; j < last_10th; j++) {
            sum_bytes += flows_by_init_prio[i][j]->size;
            if (flows_by_init_prio[i][j]->finished && flows_by_init_prio[i][j]->flow_completion_time * 1e6 <= params.targets[i]) {
                bytes_passed += flows_by_init_prio[i][j]->size;
                num_RPCs_passed++;
            }
        }
        pctg_passed_bytes = (double) bytes_passed / sum_bytes * 100;
        pctg_passed_num_rpcs = (double) num_RPCs_passed / num_RPCs * 100;
        std::cout << pctg_passed_num_rpcs << "(mid 80%) % out of Priority[" << i << "] RPCs passed the final target(" << params.targets[i] << " us)" << std::endl;
        std::cout << pctg_passed_bytes << "(mid 80%) % out of Priority[" << i << "] traffic (bytes) passed the final target(" << params.targets[i] << " us)" << std::endl;
    }

    // last 80% for method (3)
    for (uint32_t i = 0; i < params.num_qos_level - 1; i++) {
        uint32_t num_RPCs = flows_by_init_prio[i].size();
        uint64_t sum_bytes = 0;
        double pctg_passed_bytes = 0;
        double pctg_passed_num_rpcs = 0;
        uint32_t num_RPCs_passed = 0;
        uint64_t bytes_passed = 0;
        uint32_t first_20th = 0.1 * num_RPCs;
        for (uint32_t j = first_20th; j < num_RPCs; j++) {
            sum_bytes += flows_by_init_prio[i][j]->size;
            if (flows_by_init_prio[i][j]->finished && flows_by_init_prio[i][j]->flow_completion_time * 1e6 <= params.targets[i]) {
                bytes_passed += flows_by_init_prio[i][j]->size;
                num_RPCs_passed++;
            }
        }
        pctg_passed_bytes = (double) bytes_passed / sum_bytes * 100;
        pctg_passed_num_rpcs = (double) num_RPCs_passed / num_RPCs * 100;
        std::cout << pctg_passed_num_rpcs << "(last 80%) % out of Priority[" << i << "] RPCs passed the final target(" << params.targets[i] << " us)" << std::endl;
        std::cout << pctg_passed_bytes << "(last 80%) % out of Priority[" << i << "] traffic (bytes) passed the final target(" << params.targets[i] << " us)" << std::endl;
    }
    */
    
    // method (4) : same as (3) but compare per-packet target instead of final targets
    if (!params.print_normalized_result && params.normalized_lat) {
        for (uint32_t i = 0; i < params.num_qos_level - 1; i++) {
            uint32_t num_RPCs = flows_by_init_prio[i].size();
            uint64_t sum_bytes = 0;
            double pctg_passed_bytes = 0;
            double pctg_passed_num_rpcs = 0;
            uint32_t num_RPCs_passed = 0;
            uint64_t bytes_passed = 0;
            for (uint32_t j = 0; j < flows_by_init_prio[i].size(); j++) {
                sum_bytes += flows_by_init_prio[i][j]->size;
                double flow_completion_time = flows_by_init_prio[i][j]->flow_completion_time / flows_by_init_prio[i][j]->size_in_pkt;
                if (flows_by_init_prio[i][j]->finished && flow_completion_time * 1e6 <= params.hardcoded_targets[i]) {
                    bytes_passed += flows_by_init_prio[i][j]->size;
                    num_RPCs_passed++;
                }
            }
            pctg_passed_bytes = (double) bytes_passed / sum_bytes * 100;
            pctg_passed_num_rpcs = (double) num_RPCs_passed / num_RPCs * 100;
            std::cout << pctg_passed_num_rpcs << "% out of Priority[" << i << "] RPCs passed the per-packet target(" << params.hardcoded_targets[i] << " us)" << std::endl;
            std::cout << pctg_passed_bytes << "% out of Priority[" << i << "] traffic (bytes) passed the per-packet target(" << params.hardcoded_targets[i] << " us)" << std::endl;
        }
    }


    // sum up both qos h & m in method (3)
    if (!params.targets.empty()) {
        uint64_t total_sum_bytes = 0;
        uint64_t total_bytes_passed = 0;
        uint32_t total_num_RPCs_passed = 0;
        double pctg_passed_bytes = 0;
        double pctg_passed_num_rpcs = 0;
        uint32_t total_num_RPCs = 0;
        for (uint32_t i = 0; i < params.num_qos_level - 1; i++) {
            total_num_RPCs += flows_by_init_prio[i].size();
            for (uint32_t j = 0; j < flows_by_init_prio[i].size(); j++) {
                total_sum_bytes += flows_by_init_prio[i][j]->size;
                if (flows_by_init_prio[i][j]->finished && flows_by_init_prio[i][j]->flow_completion_time * 1e6 <= params.targets[i]) {
                    total_bytes_passed += flows_by_init_prio[i][j]->size;
                    total_num_RPCs_passed++;
                }
            }
        }
        pctg_passed_bytes = (double) total_bytes_passed / total_sum_bytes * 100;
        pctg_passed_num_rpcs = (double) total_num_RPCs_passed / total_num_RPCs * 100;
        std::cout << pctg_passed_num_rpcs << "% out of total RPCs passed the final target" << std::endl;
        std::cout << pctg_passed_bytes << "% out of total traffic (bytes) passed the final target" << std::endl;
    }



    if (params.cdf_info) {
        for (uint32_t i = 0; i < params.num_qos_level; i++) {
            if (flows_by_init_prio[i].empty()) { continue; }
            std::sort (flows_by_init_prio[i].begin(), flows_by_init_prio[i].end(), [](Flow *lhs, Flow *rhs) -> bool {
                if (lhs->finished && rhs->finished) {
                    return lhs->flow_completion_time < rhs->flow_completion_time;
                } else if (!lhs->finished && !rhs->finished) {
                    return lhs->start_time < rhs->start_time;
                } else {
                    return lhs->finished;
                }
            });

            std::string cdf_filename = "cdf_prio_" + std::to_string(i) + ".txt";
            if (params.flow_type == AEQUITAS_FLOW) {
                cdf_filename = "aequitas_" + cdf_filename;
            } else if (params.flow_type == PFABRIC_FLOW) {
                cdf_filename = "pfabric_" + cdf_filename;
            } else if (params.flow_type == QJUMP_FLOW) {
                cdf_filename = "qjump_" + cdf_filename;
            } else if (params.flow_type == D3_FLOW) {
                cdf_filename = "d3_" + cdf_filename;
            } else if (params.flow_type == PDQ_FLOW) {
                cdf_filename = "pdq_" + cdf_filename;
            }
            std::ofstream myfile;
            myfile.open(cdf_filename);
            for (uint32_t j = 0; j < 1000; j++) {
                uint32_t idx = flows_by_init_prio[i].size() * ((double) j / 1000);
                if (flows_by_init_prio[i][idx]->finished) {
                    myfile << flows_by_init_prio[i][idx]->flow_completion_time * 1e6 << std::endl;
                } else {
                    myfile << "inf" << std::endl;
                }
            }
            myfile.close();
        }
    }

    double sum_inst_load = 0;
    for (const auto &x: switch_max_inst_load) {
        sum_inst_load += x;
    }
    double avg_inst_load = sum_inst_load / switch_max_inst_load.size();
    std::sort(switch_max_inst_load.begin(), switch_max_inst_load.end());
    double tail_99_inst_load = switch_max_inst_load[switch_max_inst_load.size() * 0.99];
    double max_inst_load = switch_max_inst_load[switch_max_inst_load.size() - 1];
    std::cout << "instantaneous load (per-port at switch) (avg/99th/max): "
            << avg_inst_load << "/" << tail_99_inst_load << "/" << max_inst_load << " Gbps." << std::endl;

    // counting outstanding RPCs
    /*
    for (int i = 0; i < num_outstanding_rpcs_one_sw.size(); i++) {
        if (i < num_outstanding_rpcs_one_sw.size() - 1) {
            std::cout << "Priority[" << i << "] outstanding rpcs:" << std::endl;
        } else {
            std::cout << "QoS_H + QoS_M + QoS_L outstanding rpcs:" << std::endl;
        }
        double sum_outstd_rpcs = 0;    // we only do for 1 sw port buffer; not all
        for (const auto &x: num_outstanding_rpcs_one_sw[i]) {
            sum_outstd_rpcs += x;
        }
        double avg_outstd_rpcs = sum_outstd_rpcs / num_outstanding_rpcs_one_sw[i].size();
        std::sort(num_outstanding_rpcs_one_sw[i].begin(), num_outstanding_rpcs_one_sw[i].end());
        double tail_99_outstd_rpcs = num_outstanding_rpcs_one_sw[i][num_outstanding_rpcs_one_sw[i].size() * 0.99];
        double tail_999_outstd_rpcs = num_outstanding_rpcs_one_sw[i][num_outstanding_rpcs_one_sw[i].size() * 0.999];
        //double max_outstd_rpcs = num_outstanding_rpcs_one_sw[i][num_outstanding_rpcs_one_sw[i].size() - 1];
        std::cout << "Num outstanding RPCs (on port at one switch) (avg/99th/99.9th): "
                << avg_outstd_rpcs << "/" << tail_99_outstd_rpcs << "/" << tail_999_outstd_rpcs << std::endl;
    }
    */
    // outstanding RPC CDF
    /*
    std::vector<std::vector<uint32_t>> outstd_CDFs(2);    // qos_H + qos_M & qos_L
    std::sort(num_outstanding_rpcs_one_sw[3].begin(), num_outstanding_rpcs_one_sw[3].end());
    for (int j = 0; j < 100; j++) {
        uint32_t pctl_idx = num_outstanding_rpcs_one_sw[3].size() * ((double) j / 100);
        outstd_CDFs[0].push_back(num_outstanding_rpcs_one_sw[3][pctl_idx]);
    }
    outstd_CDFs[0].push_back(num_outstanding_rpcs_one_sw[3].back());
    std::sort(num_outstanding_rpcs_one_sw[2].begin(), num_outstanding_rpcs_one_sw[2].end());
    for (int j = 0; j < 100; j++) {
        uint32_t pctl_idx = num_outstanding_rpcs_one_sw[2].size() * ((double) j / 100);
        outstd_CDFs[1].push_back(num_outstanding_rpcs_one_sw[2][pctl_idx]);
    }
    outstd_CDFs[1].push_back(num_outstanding_rpcs_one_sw[2].back());
    std::cout << "CDF of outstanding RPCs for QoS_H + QoS_M" << std::endl;
    for (const auto &x : outstd_CDFs[0]) {
        std::cout << x << ",";
    }
    std::cout << std::endl;
    std::cout << "CDF of outstanding RPCs for QoS_L" << std::endl;
    for (const auto &x : outstd_CDFs[1]) {
        std::cout << x << ",";
    }
    std::cout << std::endl;
    */



    std::cout << "total pkt drops: " << num_pkt_drops << std::endl;
    if (params.big_switch == 0) {
        std::cout << "total host queue drops: " << pkt_drops_host_queues << std::endl;
        std::cout << "total agg switch drops: " << pkt_drops_agg_switches << std::endl;
        std::cout << "total core switch drops: " << pkt_drops_core_switches << std::endl;
        for (int i = 0; i < params.num_hosts; i++) {
            if (pkt_drops_per_host_queues[i] > 0) {
                std::cout << "Host Queue[" << i << "] pkt drops: " << pkt_drops_per_host_queues[i] << std::endl;
            }
        }
        for (int i = 0; i < params.num_agg_switches; i++) {
            if (pkt_drops_per_agg_sw[i] > 0) {
                std::cout << "Agg Switch[" << i << "] pkt drops: " << pkt_drops_per_agg_sw[i] << std::endl;
            }
        }
    }
    std::cout << "total priority downgrades: " << num_downgrades << std::endl;

    if (params.flow_type == AEQUITAS_FLOW) {
        for (uint32_t i = 0; i < params.weights.size(); i++) {
            std::cout << "Priority[" << i << "] pkt drops: " << pkt_drops_per_prio[i] << std::endl;
        }
    }

    std::cout << "Number of QoS_H RPCs that missed the target: " << num_QoS_H_RPCs_missed_target << " / " << num_bad_QoS_H_RPCs << std::endl;
    if (params.priority_downgrade) {

        // admit prob
        double sum_prob = 0;
        uint32_t num_samples = total_qos_h_admit_prob.size();
        for (double p : total_qos_h_admit_prob) {
            sum_prob += p;
        }
        double prob_avg = sum_prob / num_samples;
        std::cout << "All Host avg admit prob: " << prob_avg << std::endl;
        sort(total_qos_h_admit_prob.begin(), total_qos_h_admit_prob.end());
        double prob_50th = total_qos_h_admit_prob[num_samples * 0.50];
        double prob_90th = total_qos_h_admit_prob[num_samples * 0.90];
        double prob_99th = total_qos_h_admit_prob[num_samples * 0.99];
        double prob_max = total_qos_h_admit_prob[num_samples - 1];
        std::cout << "num prob samples = " << num_samples << "; admit prob (avg/50th/90th/99th/max) = " << prob_avg << "/" << prob_50th << "/"
                << prob_90th << "/" << prob_99th << "/" << prob_max << std::endl;
        // end of downgrade prob

        //std::cout << "per host downgrades: " << std::endl;
        //for (int i = 0; i < params.num_hosts; i++) {
        //    std::cout << "Host[" << i << "]: " << num_downgrades_per_host[i] << std::endl;
        //}

        std::cout << "QoS_H downgrades to QoS_M: " << num_qos_h_downgrades[0] << std::endl;
        std::cout << "QoS_H downgrades to QoS_L: " << num_qos_h_downgrades[1] << std::endl;
        std::cout << "QoS_M downgrades to QoS_L: " << num_qos_m_downgrades << std::endl;

    }
    for (uint32_t i = 0; i < params.weights.size(); i++) {
        std::cout << "num timeouts[" << i << "]: " << num_timeouts[i] << std::endl;
    }
    std::cout << "sum completion time of all finished RPCs = " << total_flow_completion_time << " seconds" << std::endl;
    //std::cout << "total time period = " << total_time_period * 1e6<< " us" << std::endl;
    std::cout << "avg sending period (busy + idle) = " << total_time_period / total_num_periods * 1e6 << " us" << std::endl;


    if (params.test_fairness) {
        std::cout << "For testing fairness, printing out per host admit prob:" << std::endl;
        uint32_t num_hosts = fairness_qos_h_admit_prob_per_host.size();
        if (params.traffic_pattern == 0) {
            num_hosts -= 1;
        }
        for (uint32_t i = 0; i < num_hosts; i++) {
            std::cout << "Host[" << i << "] ap:" << std::endl;
            uint32_t num_samples = fairness_qos_h_admit_prob_per_host[i].size();
            for (uint32_t j = 0; j < num_samples - 1; j++) {
                std::cout << fairness_qos_h_admit_prob_per_host[i][j] << ",";
            }
            std::cout << fairness_qos_h_admit_prob_per_host[i][num_samples - 1] << std::endl;
        }
        std::cout << std::setprecision(6) << std::fixed;
        for (uint32_t i = 0; i < num_hosts; i++) {
            std::cout << "Host[" << i << "] ts:" << std::endl;
            uint32_t num_samples = fairness_qos_h_ts_per_host[i].size();
            double ts_ref = fairness_qos_h_ts_per_host[i][0];
            for (uint32_t j = 0; j < num_samples - 1; j++) {
                std::cout << fairness_qos_h_ts_per_host[i][j] - ts_ref << ",";
            }
            std::cout << fairness_qos_h_ts_per_host[i][num_samples - 1] - ts_ref << std::endl;
        }
        std::cout << std::setprecision(2) << std::fixed;
        std::cout << "Hosts rate:" << std::endl;
        for (uint32_t i = 0; i < num_hosts; i++) {
            //std::cout << "Host[" << i << "] rate:" << std::endl;
            uint32_t num_samples = fairness_qos_h_rates_per_host[i].size();
            for (uint32_t j = 0; j < num_samples - 1; j++) {
                std::cout << fairness_qos_h_rates_per_host[i][j] / 1e9 * 8 << ",";
            }
            std::cout << fairness_qos_h_rates_per_host[i][num_samples - 1] / 1e9 * 8 << std::endl;
        }
    }

    if (num_outstanding_rpcs_total.size() > 0) {
        uint32_t sum_rpcs_out = 0;
        for (int i = 0; i < num_outstanding_rpcs_total.size(); i++) {
            sum_rpcs_out += num_outstanding_rpcs_total[i];
        }
        double avg_rpcs_out = (double)sum_rpcs_out / num_outstanding_rpcs_total.size();
        std::sort(num_outstanding_rpcs_total.begin(), num_outstanding_rpcs_total.end());
        uint32_t rpcs_out_p99 = num_outstanding_rpcs_total[num_outstanding_rpcs_total.size() * 0.99];
        uint32_t rpcs_out_p999 = num_outstanding_rpcs_total[num_outstanding_rpcs_total.size() * 0.999];
        std::cout << "Outstanding RPCs (avg/p99/p999): " << avg_rpcs_out << "/" << rpcs_out_p99
            << "/" << rpcs_out_p999 << std::endl;
    }

    std::cout << "Simulation event duration: " << global_last_flow_time - global_first_flow_time << " seconds." << std::endl;
    //cleanup
    delete fg;
}
