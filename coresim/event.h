#ifndef CORESIM_EVENT_H
#define CORESIM_EVENT_H

#include <iostream>
#include <math.h>
#include <queue>

#include "random_variable.h"

//TODO import globals

#define FLOW_ARRIVAL 0
#define PACKET_QUEUING 1
#define PACKET_ARRIVAL 2
#define QUEUE_PROCESSING 3
#define RETX_TIMEOUT 5
#define FLOW_FINISHED 6
#define FLOW_PROCESSING 7
#define FLOW_CREATION_EVENT 8
#define LOGGING 9
#define CHANNEL_RETX_TIMEOUT 10
#define NIC_PROCESSING 11
#define QJUMP_EPOCH 12
#define RATE_LIMITING 13
#define PDQ_PROBING 14
#define RETX_TIMEOUT_SENDER 15

class Flow;
class Host;
class Packet;
class Queue;
class Channel;
class NIC;
class PDQFlow;

class Event {
    public:
        Event(uint32_t type, double time);
        virtual ~Event();
        bool operator == (const Event& e) const {
            return (time == e.time && type == e.type);
        }
        bool operator < (const Event& e) const {
            if (time < e.time) return true;
            else if (time == e.time && type < e.type) return true;
            else return false;
        }
        bool operator > (const Event& e) const {
            if (time > e.time) return true;
            else if (time == e.time && type > e.type) return true;
            else return false;
        }

        virtual void process_event() = 0;
        //double get_current_time() {
        //    return time;
        //}

        uint32_t unique_id;
        static uint32_t instance_count;

        uint32_t type;
        double time;
        bool cancelled;

        //// for a correct order of PacketQueuingEvent
        //uint32_t flow_id;       // only used for ordering PacketQueueingEvents

        //// multi-threading
        uint32_t qid;        // so that event knows which EventQueue to go to
                             // qid for this and its child events never changes
};

struct EventComparator
{
    bool operator() (Event *a, Event *b) {
        //if (fabs(a->time - b->time) < 1e-15) {
        if (fabs(a->time - b->time) < fabs(std::min(a->time, b->time)) * std::numeric_limits<double>::epsilon()) {
        //if (fabs(a->time - b->time) <= std::max(fabs(a->time), fabs(b->time)) * std::numeric_limits<double>::epsilon()) {
            //if (a->type == b->type && (a->type == PACKET_QUEUING || a->type == PACKET_ARRIVAL)) {
            ////if (a->type == b->type) {     // because we haven't update all events with a flow_id but only PktQueuingEvent
            //    if (a->flow_id == b->flow_id) {
            //        return a->unique_id > b->unique_id;
            //    }
            //}
            return a->type > b->type;

            //return a->unique_id > b->unique_id;   //<-- no longer need this hack
        } else {
            return a->time > b->time;
        }

        // Definitely Greater Than
        //return (a->time > b->time) > std::max(fabs(b->time), fabs(a->time)) * std::numeric_limits<double>::epsilon();
    }
};

//A flow arrival event Only used for FlowCreation
class FlowCreationForInitializationEvent : public Event {
    public:
        FlowCreationForInitializationEvent(
                double time,
                Host *src,
                Host *dst,
                EmpiricalRandomVariable *nv_bytes,
                RandomVariable *nv_intarr,
                bool is_QoSM = false,
                uint32_t qid = 0
                );
        ~FlowCreationForInitializationEvent();
        void process_event();
        Host *src;
        Host *dst;
        EmpiricalRandomVariable *nv_bytes;
        RandomVariable *nv_intarr;
        bool is_QoSM;
};

//A flow arrival event
class FlowArrivalEvent : public Event {
    public:
        FlowArrivalEvent(double time, Flow *flow);
        ~FlowArrivalEvent();
        void process_event();
        Flow *flow;
};

// packet gets queued
class PacketQueuingEvent : public Event {
    public:
        PacketQueuingEvent(double time, Packet *packet, Queue *queue);
        ~PacketQueuingEvent();
        void process_event();
        Packet *packet;
        Queue *queue;
};

// packet arrival
class PacketArrivalEvent : public Event {
    public:
        PacketArrivalEvent(double time, Packet *packet);
        ~PacketArrivalEvent();
        void process_event();
        Packet *packet;
};

class QueueProcessingEvent : public Event {
    public:
        QueueProcessingEvent(double time, Queue *queue, uint32_t qid);
        ~QueueProcessingEvent();
        void process_event();
        Queue *queue;
};

class LoggingEvent : public Event {
    public:
        LoggingEvent(double time);
        LoggingEvent(double time, double ttl);
        ~LoggingEvent();
        void process_event();
        double ttl;
};

//A flow finished event
class FlowFinishedEvent : public Event {
    public:
        FlowFinishedEvent(double time, Flow *flow);
        ~FlowFinishedEvent();
        void process_event();
        Flow *flow;
};

//A flow processing event
class FlowProcessingEvent : public Event {
    public:
        FlowProcessingEvent(double time, Flow *flow);
        ~FlowProcessingEvent();

        void process_event();
        Flow *flow;
};

class RetxTimeoutEvent : public Event {
    public:
        RetxTimeoutEvent(double time, Flow *flow);
        ~RetxTimeoutEvent();
        void process_event();
        Flow *flow;
};

// Used by Homa sender
class RetxTimeoutSenderEvent : public Event {
    public:
        RetxTimeoutSenderEvent(double time, Flow *flow);
        ~RetxTimeoutSenderEvent();
        void process_event();
        Flow *flow;
};

class ChannelRetxTimeoutEvent : public Event {
    public:
        ChannelRetxTimeoutEvent(double time, Channel *channel);
        ~ChannelRetxTimeoutEvent();
        void process_event();
        Channel *channel;
};

class NICProcessingEvent : public Event {
    public:
        NICProcessingEvent(double time, NIC *nic);
        ~NICProcessingEvent();
        NIC *nic;
        void process_event();
};

class QjumpEpochEvent : public Event {
    public:
        QjumpEpochEvent(double time, Host *host, uint32_t priority);
        ~QjumpEpochEvent();
        Host *host;
        uint32_t priority;
        void process_event();
};

// rate limiter at the flow level; make flow send packets at a certain rate
// here we don't pass in an input called rate; instead we will use 'allocated_rate' in Flow class
class RateLimitingEvent : public Event {
    public:
        RateLimitingEvent(double time, Flow* flow);
        ~RateLimitingEvent();
        Flow *flow;
        void process_event();
};

// to send the next probe packet in Is RTTs
class PDQProbingEvent : public Event {
    public:
        PDQProbingEvent(double time, PDQFlow* flow);
        ~PDQProbingEvent();
        PDQFlow *flow;
        void process_event();
};

// Until we need to update RCP fs rate periodically
/*
class RCPFairShareRateEvent : public Event {
    public:
        RCPFairShareRateEvent(double time, PDQQueue *pdq_queue);
        ~RCPFairShareRateEvent();
        PDQQueue *pdq_queue;
        void process_event();
}
*/

#endif // CORESIM_EVENT_H
