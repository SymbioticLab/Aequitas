#include "factory.h"

#include <iostream>

#include "veritasflow.h"
#include "node.h"
#include "pfabric_flow.h"
#include "pfabric_queue.h"
#include "wfQueue.h"

/* Factory method to return appropriate queue */
Queue* Factory::get_queue(
        uint32_t id,
        double rate,
        uint32_t queue_size,
        uint32_t type,
        double drop_prob,
        int location,
        std::vector<int> weights
        ) { // Default drop_prob is 0.0
    switch (type) {
        case WF_QUEUE:
            return new WFQueue(id, rate, queue_size, location);
        case PFABRIC_QUEUE:
            return new PFabricQueue(id, rate, queue_size, location);
    }
    assert(false);
    return NULL;
}

int Factory::flow_counter = 0;

Flow* Factory::get_flow(
        double start_time,
        uint32_t size,
        Host *src,
        Host *dst,
        uint32_t flow_type,
        uint32_t flow_priority,
        double rate
        ) {
    assert(false);
    return Factory::get_flow(Factory::flow_counter++, start_time, size, src, dst, flow_type, flow_priority, rate);
}

Flow* Factory::get_flow(
        uint32_t id,
        double start_time,
        uint32_t size,
        Host *src,
        Host *dst,
        uint32_t flow_type,
        uint32_t flow_priority,
        double rate
        ) { // Default rate is 1.0
    switch (flow_type) {
        //case NORMAL_FLOW:
        //    return new Flow(id, start_time, size, src, dst);
        //    break;
        case VERITAS_FLOW:
            return new VeritasFlow(id, start_time, size, src, dst, flow_priority);
            break;
        case PFABRIC_FLOW:
            return new PFabricFlow(id, start_time, size, src, dst, flow_priority);
            break;
    }
    assert(false);
    return NULL;
}

Host* Factory::get_host(
        uint32_t id,
        double rate,
        uint32_t queue_type,
        uint32_t host_type
        ) {
    switch (host_type) {
        case NORMAL_HOST:
            return new Host(id, rate, queue_type, NORMAL_HOST);
            break;
    }

    std::cerr << host_type << " unknown\n";
    assert(false);
    return NULL;
}

