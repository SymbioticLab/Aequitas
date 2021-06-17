#include "packet.h"
#include "flow.h"
#include <iostream>

#include "../ext/factory.h"

#include "../run/params.h"

extern DCExpParams params;

bool FlowComparator::operator() (Flow *a, Flow *b) {
    return a->flow_priority > b->flow_priority;
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
    //std::cout << "PUPU host queue type = " << queue_type << std::endl;
    queue = Factory::get_queue(id+10, rate, params.queue_size, queue_type, 0, 0, params.weights);
    this->host_type = host_type;
    if (!params.unlimited_nic_speed) {
        egress_queue = new HostEgressQueue(id+100, rate, 0);
    }
}

// TODO FIX superclass constructor
Switch::Switch(uint32_t id, uint32_t switch_type) : Node(id, SWITCH) {
    this->switch_type = switch_type;
}

CoreSwitch::CoreSwitch(uint32_t id, uint32_t nq, double rate, uint32_t type) : Switch(id, CORE_SWITCH) {
    for (uint32_t i = 0; i < nq; i++) {
        //std::cout << "PUPU coreswitch queue type = " << type << std::endl;
        // seems like the value of "location" doesn't matter too much (affects prop delay) if only using this type of switch
        queues.push_back(Factory::get_queue(i+1000, rate, params.queue_size, type, 0, 2, params.weights));
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
        //std::cout << "PUPU aggswitch queue type = " << type << std::endl;
        queues.push_back(Factory::get_queue(i, r1, params.queue_size, type, 0, 3, params.weights));
    }
    for (uint32_t i = 0; i < nq2; i++) {
        //std::cout << "PUPU aggswitch queue type = " << type << std::endl;
        queues.push_back(Factory::get_queue(i, r2, params.queue_size, type, 0, 1, params.weights));
    }
}
