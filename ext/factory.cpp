#include "factory.h"

#include <iostream>
#include <assert.h>

#include "../coresim/agg_channel.h"
#include "../coresim/channel.h"
#include "../coresim/node.h"
#include "aequitas_flow.h"
#include "d3_flow.h"
#include "d3_queue.h"
#include "homa_channel.h"
#include "homa_flow.h"
#include "homa_host.h"
#include "pdq_flow.h"
#include "pdq_queue.h"
#include "pfabric_flow.h"
#include "pfabric_queue.h"
#include "qjump_channel.h"
#include "qjump_flow.h"
#include "qjump_host.h"
#include "qjump_queue.h"
#include "wf_queue.h"

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
        case QJUMP_QUEUE:
            return new QjumpQueue(id, rate, queue_size, location);
        case D3_QUEUE:
            return new D3Queue(id, rate, queue_size, location);
        case PDQ_QUEUE:
            return new PDQQueue(id, rate, queue_size, location);
        case HOMA_QUEUE:
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
        case AEQUITAS_FLOW:
            return new AequitasFlow(id, start_time, size, src, dst, flow_priority);
        case PFABRIC_FLOW:
            return new PFabricFlow(id, start_time, size, src, dst, flow_priority);
        case QJUMP_FLOW:
            return new QjumpFlow(id, start_time, size, src, dst, flow_priority);
        case D3_FLOW:
            return new D3Flow(id, start_time, size, src, dst, flow_priority);
        case PDQ_FLOW:
            return new PDQFlow(id, start_time, size, src, dst, flow_priority);
        case HOMA_FLOW:
            return new HomaFlow(id, start_time, size, src, dst, flow_priority);
    }
    assert(false);
    return NULL;
}

Channel *Factory::get_channel(
                uint32_t id,
                Host *s,
                Host *d,
                uint32_t priority,
                AggChannel *agg_channel,
                uint32_t flow_type) {

        switch (flow_type) {
            case QJUMP_FLOW:
                return new QjumpChannel(id, s, d, priority, agg_channel);
            case HOMA_FLOW:
                return new HomaChannel(id, s, d, priority, agg_channel);
            default:
                return new Channel(id, s, d, priority, agg_channel);
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
        case QJUMP_HOST:
            return new QjumpHost(id, rate, queue_type, QJUMP_HOST);
        case HOMA_HOST:
            return new HomaHost(id, rate, queue_type, HOMA_HOST);
    }

    std::cerr << host_type << " unknown\n";
    assert(false);
    return NULL;
}

