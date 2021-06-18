#ifndef CORESIM_TOPOLOGY_H
#define CORESIM_TOPOLOGY_H

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <math.h>
#include <vector>

#include "node.h"

class Flow;
class Packet;
class Queue;

class Topology {
    public:
        Topology();
        virtual ~Topology() = 0;
        virtual Queue *get_next_hop(Packet *p, Queue *q) = 0;
        virtual double get_oracle_fct(Flow* f) = 0;

        uint32_t num_hosts;

        std::vector<Host *> hosts;
        std::vector<Switch*> switches;
};

class PFabricTopology : public Topology {
    public:
        PFabricTopology(
                uint32_t num_hosts, 
                uint32_t num_agg_switches,
                uint32_t num_core_switches, 
                double bandwidth, 
                uint32_t queue_type
                );

        virtual Queue* get_next_hop(Packet *p, Queue *q);
        virtual double get_oracle_fct(Flow* f);

        uint32_t num_agg_switches;
        uint32_t num_core_switches;
        uint32_t hosts_per_agg_switch;

        std::vector<AggSwitch*> agg_switches;
        std::vector<CoreSwitch*> core_switches;
};


class BigSwitchTopology : public Topology {
    public:
        BigSwitchTopology(uint32_t num_hosts, double bandwidth, uint32_t queue_type);
        virtual Queue *get_next_hop(Packet *p, Queue *q);
        virtual double get_oracle_fct(Flow* f);

        CoreSwitch* the_switch;
};

#endif  // CORESIM_TOPOLOGY_H
