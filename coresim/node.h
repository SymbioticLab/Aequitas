#ifndef CORESIM_NODE_H
#define CORESIM_NODE_H

#include <stdint.h>
#include <vector>
#include <queue>

#define HOST 0
#define SWITCH 1

#define CORE_SWITCH 10
#define AGG_SWITCH 11

#define CPU 0
#define MEM 1
#define DISK 2

class Channel;
class Flow;
class NIC;
class Packet;
class Queue;
class AggChannel;

class FlowComparator{
    public:
        bool operator() (Flow *a, Flow *b);
};


class Node {
    public:
        Node(uint32_t id, uint32_t type);
        uint32_t id;
        uint32_t type;
};

class Host : public Node {
    public:
        Host(uint32_t id, double rate, uint32_t queue_type, uint32_t host_type);
        virtual void set_agg_channels(AggChannel *agg_channel);     // QjumpHost overrides this
        virtual void set_channel(Channel *channel);                 // HomaHost overrides this
        virtual Channel *get_channel(Host *src, Host *dst);         // HomaHost overrides this
        virtual void send_next_pkt(uint32_t priority);              // QjumpHost overrides this
        virtual void start_next_epoch(uint32_t priority);           // QjumpHost overrides this
        Queue *queue;
        int host_type;
        NIC *nic;
};

class Switch : public Node {
    public:
        Switch(uint32_t id, uint32_t switch_type);
        uint32_t switch_type;
        std::vector<Queue *> queues;
};

class CoreSwitch : public Switch {
    public:
        //All queues have same rate
        CoreSwitch(uint32_t id, uint32_t nq, double rate, uint32_t queue_type);
};

class AggSwitch : public Switch {
    public:
        // Different Rates
        AggSwitch(uint32_t id, uint32_t nq1, double r1, uint32_t nq2, double r2, uint32_t queue_type);
};

#endif  // CORESIM_NODE_H
