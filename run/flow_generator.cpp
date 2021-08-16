#include "flow_generator.h"

#include <cstdint>
#include <ostream>
#include <map>

#include "../coresim/agg_channel.h"
#include "../coresim/channel.h"
#include "../coresim/event.h"
#include "../coresim/flow.h"
#include "../coresim/topology.h"
#include "../ext/factory.h"


extern Topology *topology;
extern double current_time;
extern std::deque<Flow *> flows_to_schedule;
//extern std::vector<std::priority_queue<Event *, std::vector<Event *>, EventComparator>> event_queues;
extern std::priority_queue<Event *, std::vector<Event *>, EventComparator> event_queue;
extern std::vector<std::map<std::pair<uint32_t, uint32_t>, AggChannel *>> channels;
extern DCExpParams params;
extern void add_to_event_queue(Event *);

FlowGenerator::FlowGenerator(uint32_t num_flows, Topology *topo, std::string filename) {
    this->num_flows = num_flows;
    this->topo = topo;
    this->filename = filename;
}

FlowGenerator::~FlowGenerator() {}

void FlowGenerator::write_flows_to_file(std::deque<Flow *> flows, std::string file){
    std::ofstream output(file);
    output.precision(20);
    for (uint i = 0; i < flows.size(); i++){
        output
            << flows[i]->id << " "
            << flows[i]->start_time << " "
            << flows[i]->finish_time << " "
            << flows[i]->size_in_pkt << " "
            << (flows[i]->finish_time - flows[i]->start_time) << " "
            << 0 << " "
            <<flows[i]->src->id << " "
            << flows[i]->dst->id << "\n";
    }
    output.close();
}

//sample flow generation. Should be overridden.
void FlowGenerator::make_flows() {
    EmpiricalRandomVariable *nv_bytes = new EmpiricalRandomVariable(filename);
    ExponentialRandomVariable *nv_intarr = new ExponentialRandomVariable(0.0000001);
    add_to_event_queue(new FlowCreationForInitializationEvent(params.first_flow_start_time, topo->hosts[0], topo->hosts[1], nv_bytes, nv_intarr));

    while (event_queue.size() > 0) {
        Event *ev = event_queue.top();
        event_queue.pop();
        current_time = ev->time;
        if (flows_to_schedule.size() < 10) {
            ev->process_event();
        }
        delete ev;
    }
    current_time = 0;
    /*
    while (event_queues[0].size() > 0) {
        Event *ev = event_queues[0].top();
        event_queues[0].pop();
        //current_time = ev->time;
        if (flows_to_schedule.size() < 10) {
            ev->process_event();
        }
        delete ev;
    }
    */
}

PoissonFlowGenerator::PoissonFlowGenerator(uint32_t num_flows, Topology *topo, std::string filename) : FlowGenerator(num_flows, topo, filename) {};

void PoissonFlowGenerator::make_flows() {
	assert(false);
    EmpiricalRandomVariable *nv_bytes;
    if (params.smooth_cdf)
        nv_bytes = new EmpiricalRandomVariable(filename);
    else
        nv_bytes = new CDFRandomVariable(filename);

    params.mean_flow_size = nv_bytes->mean_flow_size;

    //double lambda = params.bandwidth * params.load / (params.mean_flow_size * 8.0 / 1460 * 1500);
    double lambda = params.bandwidth * params.load / (params.mean_flow_size * 8.0 / params.mss * params.mtu);
    double lambda_per_host = lambda / (topo->hosts.size() - 1);


    ExponentialRandomVariable *nv_intarr;
    if (params.burst_at_beginning)
        nv_intarr = new ExponentialRandomVariable(0.0000001);
    else
        nv_intarr = new ExponentialRandomVariable(1.0 / lambda_per_host);

    //* [expr ($link_rate*$load*1000000000)/($meanFlowSize*8.0/1460*1500)]
    for (uint32_t i = 0; i < topo->hosts.size(); i++) {
        for (uint32_t j = 0; j < topo->hosts.size(); j++) {
            if (i != j) {
                double first_flow_time = params.first_flow_start_time + nv_intarr->value();
                add_to_event_queue(
                    new FlowCreationForInitializationEvent(
                        first_flow_time,
                        topo->hosts[i],
                        topo->hosts[j],
                        nv_bytes,
                        nv_intarr
                    )
                );
            }
        }
    }

    while (event_queue.size() > 0) {
        Event *ev = event_queue.top();
        event_queue.pop();
        current_time = ev->time;
        if (flows_to_schedule.size() < num_flows) {
            ev->process_event();
        }
        delete ev;
    }
    current_time = 0;
    /*
    while (event_queues[0].size() > 0) {
        Event *ev = event_queues[0].top();
        event_queues[0].pop();
        //current_time = ev->time;
        if (flows_to_schedule.size() < num_flows) {
            ev->process_event();
        }
        delete ev;
    }
    //current_time = 0;
    */
}

FlowBytesGenerator::FlowBytesGenerator(uint32_t num_flows, Topology *topo, std::string filename) : FlowGenerator(num_flows, topo, filename) {};

void FlowBytesGenerator::make_flows() {
    EmpiricalBytesRandomVariable *nv_bytes;
    nv_bytes = new EmpiricalBytesRandomVariable(filename, 0);

    params.mean_flow_size = nv_bytes->mean_flow_size;
    std::cout << "Mean flow size: " << params.mean_flow_size << std::endl;

    //// The new dynamic load method will work as the following:
    //// We send data (with possibly load > 1) to create bursts for some time, and then rest and do nothing for some other time.
    //// We adjust the time we send and wait to achieve some average load.
    //// For this setting, we will use 4 parameters:
    // (1) params.use_dynamic_load: if this value is equal to 1, it turns on the dynamic load setting
    // (2) params.load: previously as the load used in the static setting, this now becomes the avg load value we wanna achieve in the dynamic load setting
    // (3) params.burst_load: this is the load value we use when we are sending data (instead of sitting and does nothing, which has load = 0)
    // (4) params.burst_size: # of RPCs we send before switch to wait. Note/TODO: we may need to make this # of bytes in the future
    //// To work out the math, assume target avg load is load_avg, we measure time spent sending data (according to params.burst_size) is t_busy,
    //// the load we use to send data bursts is load_data, we want to find out the time we spent in waiting, which is t_idle:
    //// It is easy to see the formula: load_data * t_busy / (t_busy + t_idle) = load_avg
    //// Thus, t_idle = load_data * t_busy / load_avg - t_busy

    double load_val = (params.use_dynamic_load) ? params.burst_load : params.load;
    double rpcs_per_sec = params.bandwidth * load_val / (nv_bytes->sizeWithHeader * 8.0);
    double rpcs_per_sec_per_host = rpcs_per_sec / (topo->hosts.size() - 1);
    RandomVariable *rpc_intarr_per_host;
    //StaticVariable *rpc_intarr_per_host = new StaticVariable(1.0 / rpcs_per_sec_per_host);
    if (!params.disable_poisson_arrival) {
        rpc_intarr_per_host = new ExponentialRandomVariable(1.0 / rpcs_per_sec_per_host);
    } else {
        rpc_intarr_per_host = new StaticVariable(1.0 / rpcs_per_sec_per_host);
    }

    /*
    if (params.burst_with_no_spacing) {    // in Heavy burst mode, RPCs in the burst period arrives at the same time; No poisson arrival will be used even if it is turned on
        rpc_intarr_per_host = new StaticVariable(1e-9);
    } else if (params.disable_poisson_arrival) {
        rpc_intarr_per_host = new StaticVariable(1.0 / rpcs_per_sec_per_host);
    } else if (!params.disable_poisson_arrival) {
        rpc_intarr_per_host = new ExponentialRandomVariable(1.0 / rpcs_per_sec_per_host);
    }
    */

    ////if (params.disable_poisson_arrival) {
    ////    rpc_intarr_per_host = new StaticVariable(1.0 / rpcs_per_sec_per_host);
    ////}

    double initial_shift_incast = 0;
    double initial_shift_all_to_all = 0;
    //double initial_shift_incast = 1e-9;    // check later
    //double initial_shift_all_to_all = 1e-9;

    //if (params.burst_with_no_spacing || params.disable_poisson_arrival) {
    if (!params.burst_with_no_spacing && params.disable_poisson_arrival) {
        initial_shift_incast = 1.0 / rpcs_per_sec;
        initial_shift_all_to_all = 1.0 / (rpcs_per_sec * (topo->hosts.size() - 1));
    }


    //if (params.test_fairness) {
    //    initial_shift_incast = 0;
    //}


    //double first_flow_time = 1.0;
    double first_flow_time = params.first_flow_start_time;
    if (params.traffic_pattern == 0) {
        // INCAST pattern: for N hosts. The first (N-1) hosts send flows to the last host.
        double initial_shift = 0;
        for (uint32_t i = 0; i < topo->hosts.size() - 1; i++) {
            initial_shift += initial_shift_incast;

            add_to_event_queue(
                new FlowCreationForInitializationEvent(
                    first_flow_time + initial_shift,
                    topo->hosts[i],
                    topo->hosts[topo->hosts.size() - 1],
                    nv_bytes,
                    rpc_intarr_per_host
                )
            );
        }
    } else {
        // ALL-to-ALL pattern: for N hosts. every host send to (N-1) hosts
        double initial_shift = 0;
        for (uint32_t i = 0; i < topo->hosts.size(); i++) {
            for (uint32_t j = 0; j < topo->hosts.size(); j++) {
                if (i != j) {
                    initial_shift += initial_shift_all_to_all;

                    add_to_event_queue(
                        new FlowCreationForInitializationEvent(
                            first_flow_time + initial_shift,
                            topo->hosts[i],
                            topo->hosts[j],
                            nv_bytes,
                            rpc_intarr_per_host
                        )
                    );
                }
            }
        }
    }

    while (event_queue.size() > 0) {
        Event *ev = event_queue.top();
        event_queue.pop();
        current_time = ev->time;
        if (flows_to_schedule.size() < num_flows) {
            ev->process_event();
        }
        delete ev;
    }
    current_time = 0;

}

FlowReader::FlowReader(uint32_t num_flows, Topology *topo, std::string filename)
                      : FlowGenerator(num_flows, topo, filename) {};

void FlowReader::make_flows() {
    std::ifstream input(filename);
    std::string line;
    uint32_t id = 0;
    uint32_t count_channel = 0;
    while (std::getline(input, line)) {
        std::istringstream iss(line);
        std::string token;
        double start_time;
        uint32_t size, s, d;
        uint32_t priority;    // 0: be; 1: batch; 2: latency; 3: unknown
        std::vector<std::string> tokens;

        // old: <id> <start_time> blah blah <size in packets> blah blah <src> <dst>
        //if (!(iss >> id >> start_time >> temp >> temp >> size >> temp >> temp >> s >> d)) {
        //    break;
        //}
        //size = (uint32_t) (params.mss * size);
        //assert(size > 0);

        // new: <priority> <RPC_size> <start_time> <src> <dst>
        while (std::getline(iss, token, ',')) {
            tokens.push_back(token);
        }
        priority = std::stoul(tokens[0], nullptr, 0);
        size = std::stoul(tokens[1], nullptr, 0);
        start_time = std::stod(tokens[2]);
        s = std::stoul(tokens[3], nullptr, 0);
        d = std::stoul(tokens[4], nullptr, 0);

        //TODO: make this more generic later
        if (priority != 0 && priority != 1 && priority != 2) {
            continue;
        }
        if (s == d) {
          continue;
        }

        // map service class to our priority
        // TODO(yiwenzhang): this manual mapping needs to be improved
        if (priority == 0) {
            priority = 1;    // low: be
        } else {
            priority = 0;    // high: batch + lat
        }

        // create channel (based on the src-dst pair) in flow trace
        // we prepare channels for all priority levels given we might later move priorties dynamically
        if (channels[0].count({s, d}) == 0) {
            for (uint32_t i = 0; i < params.weights.size(); i++) {
                channels[i][{s, d}] = new AggChannel(count_channel, topo->hosts[s], topo->hosts[d], priority);
                //std::cout << "creating channel[" << i << "][" << count_channel << "], src: " << topo->hosts[s]->id << ", dst: " << topo->hosts[d]->id << std::endl;
                count_channel++;
            }
        }

        //std::cout << "Flow " << id << " " << priority << " " << start_time << " " << size << " " << s << " " << d << "\n";
        flows_to_schedule.push_back(
            Factory::get_flow(id, start_time, size, topo->hosts[s], topo->hosts[d], params.flow_type, priority)
        );

        if (flows_to_schedule.size() % 100000 == 0) {
            std::cout << "FlowReader: finished generating " << flows_to_schedule.size() << " flows." << std::endl;
        }

        id++;
    }
    params.num_flows_to_run = flows_to_schedule.size();
    input.close();
}
