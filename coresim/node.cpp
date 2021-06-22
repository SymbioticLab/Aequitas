#include "node.h"

#include <iostream>

#include "flow.h"
#include "queue.h"
#include "../ext/factory.h"
#include "../run/params.h"


extern DCExpParams params;
extern std::vector<double> switch_max_inst_load;

bool FlowComparator::operator() (Flow *a, Flow *b) {
    return a->run_priority > b->run_priority;
    //  if(a->flow_priority > b->flow_priority)
    //    return true;
    //  else if(a->flow_priority == b->flow_priority)
    //    return a->id > b->id;
    //  else
    //    return false;
}

Node::Node(uint32_t id, uint32_t type) {
    this->id = id;
    this->type = type;
}

// TODO FIX superclass constructor
Host::Host(uint32_t id, double rate, uint32_t queue_type, uint32_t host_type) : Node(id, HOST) {
    queue = Factory::get_queue(id, rate, params.queue_size, queue_type, 0, 0, params.weights);
    this->host_type = host_type;
}

// TODO FIX superclass constructor
Switch::Switch(uint32_t id, uint32_t switch_type) : Node(id, SWITCH) {
    this->switch_type = switch_type;
}

CoreSwitch::CoreSwitch(uint32_t id, uint32_t nq, double rate, uint32_t type) : Switch(id, CORE_SWITCH) {
    for (uint32_t i = 0; i < nq; i++) {
        // seems like the value of "location" doesn't matter too much (affects prop delay) if only using this type of switch
        queues.push_back(Factory::get_queue(i, rate, params.queue_size, type, 0, 2, params.weights));
        queues.back()->measure_inst_load = true;
        switch_max_inst_load.push_back(0);
    }
}

//nq1: # host switch, nq2: # core switch
AggSwitch::AggSwitch(
        uint32_t id, 
        uint32_t nq1, 
        double r1,
        uint32_t nq2, 
        double r2, 
        uint32_t type
        ) : Switch(id, AGG_SWITCH) {
    for (uint32_t i = 0; i < nq1; i++) {
        queues.push_back(Factory::get_queue(i, r1, params.queue_size, type, 0, 3, params.weights));
    }
    for (uint32_t i = 0; i < nq2; i++) {
        queues.push_back(Factory::get_queue(i, r2, params.queue_size, type, 0, 1, params.weights));
    }
}
