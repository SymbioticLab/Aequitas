//
// flow_generator.cpp
// support arbitrary flow generation models.
//
// 6/15/2015 Akshay Narayan
//

#include "flow_generator.h"

FlowGenerator::FlowGenerator(uint32_t num_flows, Topology *topo, std::string filename) {
    this->num_flows = num_flows;
    this->topo = topo;
    this->filename = filename;
}

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
    add_to_event_queue(new FlowCreationForInitializationEvent(1.0, topo->hosts[0], topo->hosts[1], nv_bytes, nv_intarr));

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
                double first_flow_time = 1.0 + nv_intarr->value();
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
}

PoissonFlowBytesGenerator::PoissonFlowBytesGenerator(uint32_t num_flows, Topology *topo, std::string filename) : FlowGenerator(num_flows, topo, filename) {};
    
void PoissonFlowBytesGenerator::make_flows() {
    EmpiricalBytesRandomVariable *nv_bytes;
    // force to use non-smooth cdf; and modify bytes generator to use exact numbers from CDF rather than random numbers
    nv_bytes = new EmpiricalBytesRandomVariable(filename, 0);
    //nv_bytes = new EmpiricalBytesRandomVariable(filename);

    params.mean_flow_size = nv_bytes->mean_flow_size;

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
    double lambda = params.bandwidth * load_val / (nv_bytes->sizeWithHeader * 8.0);
    double lambda_per_host = lambda / (topo->hosts.size() - 1);
    ExponentialRandomVariable *nv_intarr = new ExponentialRandomVariable(1.0 / lambda_per_host);
    ExponentialRandomVariable *nv_intarr_all_host_incast = new ExponentialRandomVariable(1.0 / lambda);
    ExponentialRandomVariable *nv_intarr_all_host_all = new ExponentialRandomVariable(1.0 / (lambda * (topo->hosts.size() - 1)));
    if (params.use_dynamic_load) {
        std::cout << "burst lambda: " << lambda << std::endl;
        std::cout << "burst lambda_per_host: " << lambda_per_host << std::endl;
    } else {
        std::cout << "avg lambda: " << lambda << std::endl;
        std::cout << "avg lambda_per_host: " << lambda_per_host << std::endl;
    }

    /* old method, abandoned
    if (params.use_dynamic_load) {
        params.nv_intarr_vec = std::vector<ExponentialRandomVariable *>();
        for (int i = 0; i < params.dynamic_load.size(); i++) {
            double lambda = params.bandwidth * params.dynamic_load[i] / (nv_bytes->sizeWithHeader * 8.0);
            double lambda_per_host = lambda / (topo->hosts.size() - 1);
            params.nv_intarr_vec.push_back(new ExponentialRandomVariable(1.0 / lambda_per_host));
            //std::cout << "lambda per host(" << i+1 << "): " << lambda_per_host << std::endl;
        }
        nv_intarr = params.nv_intarr_vec[0];
    }
    */

    /*
    if (topo->hosts.size() > 2) {
        //* [expr ($link_rate*$load*1000000000)/($meanFlowSize*8.0/1460*1500)]
        for (uint32_t i = 0; i < topo->hosts.size(); i++) {
            for (uint32_t j = 0; j < topo->hosts.size(); j++) {
                if (i != j) {
                    double first_flow_time = 1.0 + nv_intarr->value();
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
    } else {
        //// instead of all-to-all pattern, single direction h1->h2:
        //// also make both packets (P0 & P1) arrive at the same time (t = 1,2,3,...)
        ////double first_flow_time = 0.0 + SLIGHTLY_SMALL_TIME;      //assmue simulation starts at t = 0.0
        //////double first_flow_time = 0.0 - SLIGHTLY_SMALL_TIME;      //assmue simulation starts at t = 0.0
        double first_flow_time = 1.0;      //assmue simulation starts at t = 0.0
        add_to_event_queue(
            new FlowCreationForInitializationEvent(
                first_flow_time,
                topo->hosts[0], 
                topo->hosts[1],
                nv_bytes, 
                nv_intarr
            )
        );
    }
    */

    std::cout << "I'm generating the first couple of flows" << std::endl;
    int cnt = 0;
    double first_flow_time = 1.0;
    if (params.traffic_pattern == 0) {
        // INCAST pattern: for N hosts. The first (N-1) hosts send flows to the last host.
        double initial_shift = 0;
        for (uint32_t i = 0; i < topo->hosts.size() - 1; i++) {
            //double random_jitter = 0;
            /*
            if (params.use_dynamic_load && params.use_random_jitter) {
                double random_percentage = (rand() % 100 + 1) / 100.0;
                random_jitter = nv_intarr->value() * random_percentage;
                //std::cout << "random percentage = " << random_percentage << std::endl;
                //std::cout << "random jitter = " << random_jitter << std::endl;
            }
            */
            initial_shift += nv_intarr_all_host_incast->value();

            add_to_event_queue(
                new FlowCreationForInitializationEvent(
                    first_flow_time + initial_shift,
                    topo->hosts[i], 
                    topo->hosts[topo->hosts.size() - 1],
                    nv_bytes, 
                    nv_intarr
                )
            );
            cnt++;
        }
    } else {
        // ALL-to-ALL pattern: for N hosts. every host send to (N-1) hosts
        // TODO: add random jitter
        double initial_shift = 0;
        for (uint32_t i = 0; i < topo->hosts.size(); i++) {
            for (uint32_t j = 0; j < topo->hosts.size(); j++) {
                if (i != j) {
                    /*
                    double random_jitter = 0;
                    if (params.use_dynamic_load && params.use_random_jitter) {
                        double random_percentage = (rand() % 100 + 1) / 100.0;
                        random_jitter = nv_intarr->value() * random_percentage;
                        //std::cout << "random percentage = " << random_percentage << std::endl;
                        //std::cout << "random jitter = " << random_jitter << std::endl;
                    }
                    */
                    initial_shift += nv_intarr_all_host_all->value();

                    add_to_event_queue(
                        new FlowCreationForInitializationEvent(
                            first_flow_time + initial_shift,
                            topo->hosts[i], 
                            topo->hosts[j],
                            nv_bytes, 
                            nv_intarr
                        )
                    );
                    cnt++;
                }
            }
        }
    }
    std::cout << "End of generating the first couple of flows. count = " << cnt << std::endl;

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

FlowReader::FlowReader(uint32_t num_flows, Topology *topo, std::string filename) : FlowGenerator(num_flows, topo, filename) {};

void FlowReader::make_flows() {
    std::ifstream input(filename);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream iss(line);
        double start_time, temp;
        uint32_t size, s, d;
        uint32_t id;
        
        // <id> <start_time> blah blah <size in packets> blah blah <src> <dst>

        if (!(iss >> id >> start_time >> temp >> temp >> size >> temp >> temp >> s >> d)) {
            break;
        }
        
        size = (uint32_t) (params.mss * size);
        assert(size > 0);

        std::cout << "Flow " << id << " " << start_time << " " << size << " " << s << " " << d << "\n";
        flows_to_schedule.push_back(
            Factory::get_flow(id, start_time, size, topo->hosts[s], topo->hosts[d], params.flow_type)
        );
    }
    params.num_flows_to_run = flows_to_schedule.size();
    input.close();
}

CustomCDFFlowGenerator::CustomCDFFlowGenerator(
        uint32_t num_flows, 
        Topology *topo, 
        std::string filename, 
        std::string interarrivals_cdf_filename
    ) : FlowGenerator(num_flows, topo, filename) {
    this->interarrivals_cdf_filename = interarrivals_cdf_filename;
};

std::vector<EmpiricalRandomVariable*>* CustomCDFFlowGenerator::makeCDFArray(std::string fn_template, std::string filename) {
    auto pairCDFs = new std::vector<EmpiricalRandomVariable*>(params.num_host_types * params.num_host_types);
    for (auto i = 0; i < params.num_host_types; i++) {
        for (auto j = 0; j < params.num_host_types; j++) {
            if (i == j) {
                pairCDFs->at(params.num_host_types * i + j) = NULL;
                continue;
            }
            char buffer[128];
            snprintf(buffer, 128, fn_template.c_str(), filename.c_str(), i, j);
            std::string cdf_fn = buffer;
            std::ifstream cdf_file(cdf_fn);
            if (cdf_file.good()) {
                if (fn_template.compare("%s/%d_%d_interarrivals.cdf") == 0) {
                    pairCDFs->at(params.num_host_types * i + j) = new EmpiricalRandomVariable(cdf_fn);
                }
                else {
                    pairCDFs->at(params.num_host_types * i + j) = new CDFRandomVariable(cdf_fn);
                }
            }
            else {
                pairCDFs->at(params.num_host_types * i + j) = NULL;
            }
        }
    }
    return pairCDFs;
}

uint32_t* customCdfFlowGenerator_getDestinations_rackscale(uint32_t num_hosts, uint32_t sender_id, uint32_t num_dests) {
    auto dests = new uint32_t[num_dests];
    // rack scale
    // for when num_host_types is 15:
    // one cluster per rack, one node per rack leftover
    uint32_t index = 0;
    uint32_t rack_start = sender_id / 16;
    assert(rack_start < num_hosts);
    for (auto i = 0; i < num_dests + 1; i++) {
        if (rack_start * 16 + i == sender_id) continue;
        assert(index <= num_dests + 1);
        dests[index] = rack_start * 16 + i;
        index++;
    }

        // for when num_host_types is 13:
        // one cluster per rack, 2 clusters spanning leftover nodes
        /*
        auto dests = new uint32_t[num_dests];
        uint32_t rack_start = sender_id / 16;
        if (sender_id % 16 < 13) {
            auto index = 0;
            for (auto i = 0; i < num_dests + 1; i++) {
                if (rack_start * 16 + i == sender_id) continue;
                dests[index++] = rack_start * 16 + i;
            }
        }
        else {
            uint32_t start = 0;
            if (sender_id >= 78) start = 78;
            auto j = 0;
            for (auto i = start; i < 144; i++) {
                if (j == num_dests) break;
                if (i % 16 < 13 || i == sender_id) continue;
                dests[j] = i;
                j++;
            }
        }

        return dests;
        */
    return dests;
}

uint32_t* customCdfFlowGenerator_getDestinations_dcscale(uint32_t num_hosts, uint32_t sender_id, uint32_t num_dests, uint32_t **dests_map) {
    if (dests_map[sender_id] == NULL) {
        auto num_unfilled = 0;
        for (auto i = 0; i < num_hosts; i++) if (dests_map[i] == NULL) num_unfilled++;
        if (num_unfilled < num_dests) {
            return NULL;
        }

        auto cluster = new uint32_t[num_dests];
        cluster[0] = sender_id;
        dests_map[sender_id] = cluster;
        for (auto i = 1; i < num_dests; i++) {
            uint32_t ind;
            do {
                ind = rand() % num_hosts;
            } while(dests_map[ind] != NULL);
            cluster[i] = ind;
            dests_map[ind] = cluster;
        }
    }

    return dests_map[sender_id];
}

void CustomCDFFlowGenerator::make_flows() {
    std::vector<EmpiricalRandomVariable*>* sizeMatrix = makeCDFArray("%s/%d_%d_sizes.cdf", filename);
    std::vector<EmpiricalRandomVariable*>* interarrivalMatrix = makeCDFArray("%s/%d_%d_interarrivals.cdf", filename);
    uint32_t num_hosts = topo->hosts.size();

    uint32_t** clusters;
    if (params.ddc_type == 0) {
        clusters = new uint32_t*[num_hosts];
        for (auto i = 0; i < num_hosts; i++) {
            clusters[i] = NULL;
        }
    }

    for (uint32_t i = 0; i < num_hosts; i++) {
        if (params.ddc_type != 0 && i % 16 == 15) {
            std::cout << i << " no flows\n";
            continue; // unused host, rack scale
        }

        // select a sender profile randomly, then pick n-1 destinations for each dest.
        uint32_t sender_profile;
        uint32_t* dests;
        if (params.ddc_type != 0) {
            sender_profile = i % params.num_host_types;
            dests = customCdfFlowGenerator_getDestinations_rackscale(num_hosts, i, params.num_host_types - 1);
        }
        else {
            dests = customCdfFlowGenerator_getDestinations_dcscale(num_hosts, i, params.num_host_types, clusters);
            
            if (dests == NULL) {
                std::cout << i << " no flows\n";
                continue; // unused host, dc scale
            }
            
            sender_profile = 0;
            for (auto t = 0; t < params.num_host_types; t++) {
                if (dests[t] == i) {
                    sender_profile = t;
                }
            }

            auto new_dests = new uint32_t[params.num_host_types-1];
            uint32_t t_newdests = 0;
            for (auto t = 0; t < params.num_host_types; t++) {
                if (dests[t] != i) {
                    new_dests[t_newdests++] = dests[t];
                }
            }

            dests = new_dests;
        }
        
        for (auto t = 0; t < params.num_host_types - 1; t++) assert(dests[t] < num_hosts);

        if (params.ddc_type != 0) {
            for (auto d = 0; d < params.num_host_types - 2; d++) {
                if (i / 16 != dests[d] / 16) {
                //if (i % 16 < params.num_host_types && (i / 16) != (dests[d] / 16)) {
                    std::cout << i << " " << d << " " << dests[d] << "  " << i / 16 << " " << dests[d] / 16 << "\n";
                    assert(false);
                }
            }
        }

        std::cout << i << " " << sender_profile << " dests:";
        for (auto t = 0; t < params.num_host_types - 1; t++) {
            std::cout << " " << dests[t];
        }
        std::cout << std::endl;

        for (uint32_t j = 0; j < params.num_host_types - 2; j++) {
            EmpiricalRandomVariable* nv_bytes = sizeMatrix->at(params.num_host_types * sender_profile + j);
            EmpiricalRandomVariable* nv_intarr = interarrivalMatrix->at(params.num_host_types * sender_profile + j);
            uint32_t d = dests[j];
            // each node represents 3x of that resource.
            for (uint32_t k = 0; k < 3; k++) {
                if (nv_bytes != NULL && nv_intarr != NULL) {
                    double first_flow_time = 1.0 + nv_intarr->value();
                    add_to_event_queue(
                        new FlowCreationForInitializationEvent(
                            first_flow_time,
                            topo->hosts[i], 
                            topo->hosts[d],
                            nv_bytes, 
                            nv_intarr
                        )
                    );
                }
            }
        }
        delete dests;
    }

    if (params.ddc_type == 0) {
        //for (auto i = 0; i < num_hosts; i++) {
        //    if (clusters[i] != NULL) {
        //        delete clusters[i];
        //    }
        //}
        delete clusters;
    }

    delete sizeMatrix;
    delete interarrivalMatrix;

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

PermutationTM::PermutationTM(uint32_t num_flows, Topology *topo, std::string filename) : FlowGenerator(num_flows, topo, filename) {}

void PermutationTM::make_flows() {
    assert(false);
    EmpiricalRandomVariable *nv_bytes;
    if (params.smooth_cdf)
        nv_bytes = new EmpiricalRandomVariable(filename);
    else
        nv_bytes = new CDFRandomVariable(filename);

    params.mean_flow_size = nv_bytes->mean_flow_size;

    double lambda = params.bandwidth * params.load / (params.mean_flow_size * 8.0 / 1460 * 1500);
    //std::cout << "Lambda: " << lambda << std::endl;

    auto *nv_intarr = new ExponentialRandomVariable(1.0 / lambda);

    std::set<uint32_t> dests;
    for (uint32_t i = 0; i < topo->hosts.size(); i++) {
        uint32_t j = i;
        while (j == i || dests.find(j) != dests.end()) { // orig. "j != i"
            j = rand() % topo->hosts.size();
        }
        dests.insert(j);
        double first_flow_time = 1.0 + nv_intarr->value();
        assert(i != j);
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

//
// uninimplemented flow generation schemes
//

// alternate flow generations
/*
   int get_flow_size(Host* s, Host* d){

   int matrix[3][3] =
   {
   {3*1460,    3*1460, 700*1460},
   {3*1460,    0,      0},
   {700*1460,  0,      0}
   };

   assert(s->host_type >= 0);
   assert(d->host_type >= 0);
   assert(s->host_type < 3);
   assert(d->host_type < 3);

   return matrix[s->host_type][d->host_type];
   }

   int get_num_src_or_dst(Host* d){
   if(d->host_type == CPU)
   return 143;
   else if(d->host_type == MEM)
   return 144/3;
   else if(d->host_type == DISK)
   return 144/3;
   else
   assert(false);
   }


   void generate_flows_to_schedule_fd_ddc(std::string filename, uint32_t num_flows, Topology *topo) {



   for (int i = 0; i < topo->hosts.size(); i++){
   topo->hosts[i]->host_type = i%3;
   }



   for (uint32_t dst = 0; dst < topo->hosts.size(); dst++) {
   for (uint32_t src = 0; src < topo->hosts.size(); src++) {
   if (src != dst) {
   int flow_size = get_flow_size(topo->hosts[src], topo->hosts[dst]);

   if(flow_size > 0){
   int num_sd_pair;
   if(params.ddc_normalize == 0) {
   num_sd_pair = get_num_src_or_dst(topo->hosts[src]);
   }
   else if(params.ddc_normalize == 1) {
   num_sd_pair = get_num_src_or_dst(topo->hosts[dst]);
   }
   else if (params.ddc_normalize == 2) {
   if (topo->hosts[src]->host_type == CPU) {
   num_sd_pair = get_num_src_or_dst(topo->hosts[src]);
   }
   else if(topo->hosts[dst]->host_type == CPU) {
   num_sd_pair = get_num_src_or_dst(topo->hosts[dst]);
   }
   else {
   assert(false);
   }
   }
   else {
   assert(false);
   }

   double lambda_per_pair = params.bandwidth * params.load / (flow_size * 8.0 / 1460 * 1500) / num_sd_pair;
//std::cout << src << " " << dst << " " << flow_size << " " <<lambda_per_pair << "\n";
ExponentialRandomVariable *nv_intarr = new ExponentialRandomVariable(1.0 / lambda_per_pair);
double first_flow_time = 1.0 + nv_intarr->value();
EmpiricalRandomVariable *nv_bytes = new ConstantVariable(flow_size/1460);

    add_to_event_queue(
            new FlowCreationForInitializationEvent(first_flow_time, topo->hosts[src], topo->hosts[dst], nv_bytes, nv_intarr)
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


void generate_flows_to_schedule_fd_with_skew(std::string filename, uint32_t num_flows,
        Topology *topo) {

    EmpiricalRandomVariable *nv_bytes;
    if(params.smooth_cdf)
        nv_bytes = new EmpiricalRandomVariable(filename);
    else
        nv_bytes = new CDFRandomVariable(filename);

    params.mean_flow_size = nv_bytes->mean_flow_size;

    double lambda = params.bandwidth * params.load / (params.mean_flow_size * 8.0 / 1460 * 1500);



    GaussianRandomVariable popularity(10, params.traffic_imbalance);
    std::vector<int> sources;
    std::vector<int> destinations;

    int self_connection_count = 0;
    for(int i = 0; i < topo->hosts.size(); i++){
        int src_count = (int)(round(popularity.value()));
        int dst_count = (int)(round(popularity.value()));
        std::cout << "node:" << i << " #src:" << src_count << " #dst:" << dst_count << "\n";
        self_connection_count += src_count * dst_count;
        for(int j = 0; j < src_count; j++)
            sources.push_back(i);
        for(int j = 0; j < dst_count; j++)
            destinations.push_back(i);
    }

    double flows_per_host = (sources.size() * destinations.size() - self_connection_count) / (double)topo->hosts.size();
    double lambda_per_flow = lambda / flows_per_host;
    std::cout << "Lambda: " << lambda_per_flow << std::endl;


    ExponentialRandomVariable *nv_intarr;
    if(params.burst_at_beginning)
        nv_intarr = new ExponentialRandomVariable(0.000000001);
    else
        nv_intarr = new ExponentialRandomVariable(1.0 / lambda_per_flow);

    // [expr ($link_rate*$load*1000000000)/($meanFlowSize*8.0/1460*1500)]
    for (uint32_t i = 0; i < sources.size(); i++) {
        for (uint32_t j = 0; j < destinations.size(); j++) {
            if (sources[i] != destinations[j]) {
                double first_flow_time = 1.0 + nv_intarr->value();
                add_to_event_queue(
                        new FlowCreationForInitializationEvent(first_flow_time,
                            topo->hosts[sources[i]], topo->hosts[destinations[j]],
                            nv_bytes, nv_intarr)
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
}
*/

