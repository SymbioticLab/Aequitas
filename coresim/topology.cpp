#include "topology.h"

#include <assert.h>

#include "flow.h"
#include "packet.h"
#include "queue.h"
#include "../ext/factory.h"
#include "../run/params.h"

extern DCExpParams params;

Topology::Topology() {}
Topology::~Topology() {}

/* PFabric topology */
PFabricTopology::PFabricTopology(
        uint32_t num_hosts,
        uint32_t num_agg_switches,
        uint32_t num_core_switches,
        double bandwidth,
        uint32_t queue_type
        ) : Topology () {
    //assert(num_hosts % num_agg_switches == 0);
    if (num_hosts % num_agg_switches != 0) {
        std::cout << "Checking num_agg_switches parameter for Pfabric topology." << std::endl;
        exit(1);
    }
    this->hosts_per_agg_switch = num_hosts / num_agg_switches;
    this->num_hosts = num_hosts;
    this->num_agg_switches = num_agg_switches;
    this->num_core_switches = num_core_switches;

    //Capacities
    double c1 = bandwidth;
    double c2 = hosts_per_agg_switch * bandwidth / num_core_switches;

    // Create Hosts
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts.push_back(Factory::get_host(i, c1, queue_type, params.host_type)); 
    }

    // Create Switches
    for (uint32_t i = 0; i < num_agg_switches; i++) {
        AggSwitch* sw = new AggSwitch(i, hosts_per_agg_switch, c1, num_core_switches, c2, queue_type);
        agg_switches.push_back(sw); // TODO make generic
        switches.push_back(sw);
    }
    for (uint32_t i = 0; i < num_core_switches; i++) {
        CoreSwitch* sw = new CoreSwitch(i + num_agg_switches, num_agg_switches, c2, queue_type);
        core_switches.push_back(sw);
        switches.push_back(sw);
    }

    //Connect host queues
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts[i]->queue->set_src_dst(hosts[i], agg_switches[i/hosts_per_agg_switch]);
        //std::cout << "Linking Host " << i << " to Agg " << i/hosts_per_agg_switch << "\n";
    }

    // For agg switches -- REMAINING
    for (uint32_t i = 0; i < num_agg_switches; i++) {
        // Agg to Hosts
        for (uint32_t j = 0; j < hosts_per_agg_switch; j++) {
            Queue *q = agg_switches[i]->queues[j];
            q->set_src_dst(agg_switches[i], hosts[i * hosts_per_agg_switch + j]);
            //std::cout << "Linking Agg " << i << " to Host" << i * hosts_per_agg_switch + j << "\n";
        }
        // Agg to Core
        for (uint32_t j = 0; j < num_core_switches; j++) {
            Queue *q = agg_switches[i]->queues[j + hosts_per_agg_switch];
            q->set_src_dst(agg_switches[i], core_switches[j]);
            //std::cout << "Linking Agg " << i << " to Core" << j << "\n";
        }
    }

    //For core switches -- PERFECT
    for (uint32_t i = 0; i < num_core_switches; i++) {
        // Core to Agg
        for (uint32_t j = 0; j < num_agg_switches; j++) {
            Queue *q = core_switches[i]->queues[j];
            q->set_src_dst(core_switches[i], agg_switches[j]);
            //std::cout << "Linking Core " << i << " to Agg" << j << "\n";
        }
    }
}


// new get_next_hop with src queue skipped & dst queue included
Queue *PFabricTopology::get_next_hop(Packet *p, Queue *q) {
    if (q->dst->type == HOST) {
        assert(p->dst->id == q->dst->id);
        return p->dst->queue;    // also queue in the dst queue
    }

    // At host level
    if (q->src->type == HOST) { // Same Rack or not
        if (p->dst->id == q->src->id) {  // already dequed at the dst node
            return NULL;  // Packet Arrival
        } // else at the sender side
        assert (p->src->id == q->src->id);    // true if at the sender

        // If src & dst within the same rack
        if (p->src->id / hosts_per_agg_switch == p->dst->id / hosts_per_agg_switch) {
            // go to agg switch; ready to connect back to dst hosts
            return ((Switch *) q->dst)->queues[p->dst->id % hosts_per_agg_switch];
        }
        else {
            uint32_t hash_port = 0;
            if (params.load_balancing == 0)  // default
                hash_port = q->spray_counter++ % num_core_switches;
            else if(params.load_balancing == 1)
                hash_port = (p->src->id + p->dst->id + p->flow->id) % num_core_switches;
            // go to agg switch; ready to connect to one of the core switches
            return ((Switch *) q->dst)->queues[hosts_per_agg_switch + hash_port];
        }
    }

    // At switch level
    if (q->src->type == SWITCH) {
        if (((Switch *) q->src)->switch_type == AGG_SWITCH) {
            // go to core switch
            return ((Switch *) q->dst)->queues[p->dst->id / hosts_per_agg_switch];
        }
        if (((Switch *) q->src)->switch_type == CORE_SWITCH) {
            // go to agg switch
            return ((Switch *) q->dst)->queues[p->dst->id % hosts_per_agg_switch];
        }
    }

    assert(false);
    return NULL;
}


// orig get_next_hop with including src queue & skip dst queue; commented out
/*
Queue *PFabricTopology::get_next_hop(Packet *p, Queue *q) {
    if (q->dst->type == HOST) {
        return NULL; // Packet Arrival
    }

    // At host level
    if (q->src->type == HOST) { // Same Rack or not
        assert (p->src->id == q->src->id);

        // If src & dst within the same rack
        if (p->src->id / hosts_per_agg_switch == p->dst->id / hosts_per_agg_switch) {
            // go to agg switch; ready to connect back to dst hosts
            return ((Switch *) q->dst)->queues[p->dst->id % hosts_per_agg_switch];
        }
        else {
            uint32_t hash_port = 0;
            if (params.load_balancing == 0)  // default
                hash_port = q->spray_counter++ % num_core_switches;
            else if(params.load_balancing == 1)
                hash_port = (p->src->id + p->dst->id + p->flow->id) % num_core_switches;
            // go to agg switch; ready to connect to one of the core switches
            return ((Switch *) q->dst)->queues[hosts_per_agg_switch + hash_port];
        }
    }

    // At switch level
    if (q->src->type == SWITCH) {
        if (((Switch *) q->src)->switch_type == AGG_SWITCH) {
            // go to core switch
            return ((Switch *) q->dst)->queues[p->dst->id / hosts_per_agg_switch];
        }
        if (((Switch *) q->src)->switch_type == CORE_SWITCH) {
            // go to agg switch
            return ((Switch *) q->dst)->queues[p->dst->id % hosts_per_agg_switch];
        }
    }

    assert(false);
    return NULL;
}
*/


double PFabricTopology::get_oracle_fct(Flow *f) {
    assert(false);
    int num_hops = 4;
    if (f->src->id/16 == f->dst->id/16) {
        num_hops = 2;
    }
    double propagation_delay;
    if (params.ddc != 0) { 
        if (num_hops == 2) {
            propagation_delay = 0.440;
        }
        if (num_hops == 4) {
            propagation_delay = 2.040;
        }
    }
    else {
        propagation_delay = 2 * 1000000.0 * num_hops * f->src->queue->propagation_delay; //us
    }

    double pkts = (double) f->size / params.mss;
    uint32_t np = floor(pkts);
    uint32_t leftover = (pkts - np) * params.mss;
	double incl_overhead_bytes = (params.mss + f->hdr_size) * np + (leftover + f->hdr_size);

    double bandwidth = f->src->queue->rate / 1000000.0; // For us
    double transmission_delay;
    if (params.cut_through) {
        transmission_delay =
            (
                np * (params.mss + params.hdr_size)
                + 1 * params.hdr_size
                + 2.0 * params.hdr_size // ACK has to travel two hops
            ) * 8.0 / bandwidth;
        if (num_hops == 4) {
            //1 packet and 1 ack
            transmission_delay += 2 * (2*params.hdr_size) * 8.0 / (4 * bandwidth);
        }
        //std::cout << "pd: " << propagation_delay << " td: " << transmission_delay << std::endl;
    }
    else {
		transmission_delay = (incl_overhead_bytes + 2.0 * f->hdr_size) * 8.0 / bandwidth;
		if (num_hops == 4) {
			// 1 packet and 1 ack
			if (np == 0) {
				// less than mss sized flow. the 1 packet is leftover sized.
				transmission_delay += 2 * (leftover + 2*params.hdr_size) * 8.0 / (4 * bandwidth);

			} else {
				// 1 packet is full sized
				transmission_delay += 2 * (params.mss + 2*params.hdr_size) * 8.0 / (4 * bandwidth);
			}
		}
        //transmission_delay = 
        //    (
        //        (np + 1) * (params.mss + params.hdr_size) + (leftover + params.hdr_size)
        //        + 2.0 * params.hdr_size // ACK has to travel two hops
        //    ) * 8.0 / bandwidth;
        //if (num_hops == 4) {
        //    //1 packet and 1 ack
        //    transmission_delay += 2 * (params.mss + 2*params.hdr_size) * 8.0 / (4 * bandwidth);  //TODO: 4 * bw is not right.
        //}
    }
    return (propagation_delay + transmission_delay); //us
}


/*
 *BigSwitchTopology  with (params.num_hosts) hosts
 */
BigSwitchTopology::BigSwitchTopology(
        uint32_t num_hosts, 
        double bandwidth, 
        uint32_t queue_type
        ) : Topology () {
    this->num_hosts = num_hosts;
    double c1 = bandwidth;

    // Create Hosts
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts.push_back(Factory::get_host(i, c1, queue_type, params.host_type));
    }

    the_switch = new CoreSwitch(0, num_hosts, c1, queue_type);
    this->switches.push_back(the_switch);

    assert(this->switches.size() == 1);

    std::cout << "num_hosts = " << num_hosts << std::endl;

    //Connect host queues
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts[i]->queue->set_src_dst(hosts[i], the_switch);
        Queue *q = the_switch->queues[i];
        q->set_src_dst(the_switch, hosts[i]);
    }
}

Queue* BigSwitchTopology::get_next_hop(Packet *p, Queue *q) {
    /*
    if (q->dst->type == HOST) {
        assert(p->dst->id == q->dst->id);
        return NULL; // Packet Arrival
    }
    */

    if (!params.only_sw_queue) {
        // Also queue in the dst's queue
        // At the switch
        if (q->dst->type == HOST) {
            //std::cout << "packet[" << p->unique_id << "] get_next_hop: host[" << p->dst->id << "]; qid: "<< p->dst->queue->id << "" << std::endl;
            assert(p->dst->id == q->dst->id);
            return p->dst->queue;
        }

        // At host level
        if (q->src->type == HOST) {
            if (p->dst->id == q->src->id) {  // at receiver side
                //std::cout << "packet[" << p->unique_id << "] get_next_hop is NULL" << std::endl;
                return NULL;  // Packet Arrival
            } else {  // at sender side
                assert (p->src->id == q->src->id);
                //std::cout << "packet[" << p->unique_id << "] get_next_hop: sw queue[" << p->dst->id << "]; qid: " << the_switch->queues[p->dst->id]->id << std::endl;
                return the_switch->queues[p->dst->id];
            }
        }
    } else {
        // only keep the switch queue; Note: this is only supoported for this BigSwitchTopology
        if (q->dst->type == HOST) {
            assert(p->dst->id == q->dst->id);
            return NULL; // Packet Arrival
        }
        // At host level
        if (q->src->type == HOST) {
            if (p->dst->id == q->src->id) {  // at receiver side
                assert(false);
            } else {  // at sender side
                assert (p->src->id == q->src->id);
                //std::cout << "packet[" << p->unique_id << "] get_next_hop: sw queue[" << p->dst->id << "]; qid: " << the_switch->queues[p->dst->id]->id << std::endl;
                return the_switch->queues[p->dst->id];
            }
        }

    }

    assert(false);
    return NULL;
}

double BigSwitchTopology::get_oracle_fct(Flow *f) {
    assert(false);
    double propagation_delay = 2 * 1000000.0 * 2 * f->src->queue->propagation_delay; //us

    uint32_t np = ceil(f->size / params.mss); // TODO: Must be a multiple of 1460
    double bandwidth = f->src->queue->rate / 1000000.0; // For us
    double transmission_delay;
    if (params.cut_through) {
        transmission_delay = 
            (
                np * (params.mss + params.hdr_size)
                + 1 * params.hdr_size
                + 2.0 * params.hdr_size // ACK has to travel two hops
            ) * 8.0 / bandwidth;
    }
    else {
        transmission_delay = ((np + 1) * (params.mss + params.hdr_size) 
                + 2.0 * params.hdr_size) // ACK has to travel two hops
            * 8.0 / bandwidth;
    }
    return (propagation_delay + transmission_delay); //us
}
