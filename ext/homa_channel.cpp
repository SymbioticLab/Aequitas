#include "homa_channel.h"

#include <assert.h>
#include <cstddef>
#include <iostream>
#include <math.h>

#include "homa_host.h"
#include "../coresim/agg_channel.h"
#include "../coresim/event.h"
#include "../coresim/flow.h"
#include "../coresim/node.h"
#include "../coresim/nic.h"
#include "../coresim/packet.h"
#include "../coresim/queue.h"
#include "../coresim/topology.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern DCExpParams params;
extern Topology* topology;
extern std::vector<uint32_t> num_timeouts;
//extern uint32_t num_outstanding_packets;
extern uint32_t pkt_total_count;

HomaChannel::HomaChannel(uint32_t id, Host *s, Host *d, uint32_t priority, AggChannel *agg_channel)
    : Channel(id, s, d, priority, agg_channel) {
        overcommitment_degree = num_hw_prio_levels;
        num_active_flows = 0;
        //sender_priority = 0;
    }

HomaChannel::~HomaChannel() {}

// TODO: decide flow's start & end sequence only when the flow is selcted to transmit
void HomaChannel::add_to_channel(Flow *flow) {
    //flow->start_seq_no = end_seq_no;    
    //end_seq_no += flow->size;
    //outstanding_flows.push_back(flow);    // for RPC boundary, tie flow to pkt, easy handling of flow_finish, etc.
    //flow->end_seq_no = end_seq_no;
    //std::cout << "add_to_channel[" << id << "]: end_seq_no = " << end_seq_no << std::endl;
    sender_flows.push(flow);
    if (params.debug_event_info) {
        std::cout << "Flow[" << flow->id << "] added to Channel[" << id << "]" << std::endl;
    }
    send_pkts();
}

// Allow at maximum overcommit_degree num of flows to send pkts at the same time. Flows handling transmission by themselves like pFabric
int HomaChannel::send_pkts() {
    while (!sender_flows.empty() && num_active_flows < overcommitment_degree) {
        Flow *flow = sender_flows.top();
        sender_flows.pop();
        num_active_flows++;
        flow->send_pending_data();
    }
}

// TODO: call when a HomaFlow finishes
void HomaChannel::decrement_active_flows() {
    assert(num_active_flows > 0);
    num_active_flows--;
}

// TODO: receiver-side behavior (decide priorities, etc)
void QjumpChannel::receive_ack(uint64_t ack, Flow *flow, std::vector<uint64_t> sack_list, double pkt_start_ts) {
    if (params.debug_event_info) {
        std::cout << "Channel[" << id << "] with priority " << priority << " receive ack: " << ack << std::endl;
    }
    if (params.enable_flow_lookup && flow->id == params.flow_lookup_id) {
        std::cout << "Channel[" << id << "] Flow[" << flow->id << "] with priority " << priority
            << " receive ack: " << ack << "; last_unacked_seq: " << last_unacked_seq
            << "; Flow's end seq = " << flow->end_seq_no << ", start_seq = " << flow->start_seq_no << std::endl;
    }
    //this->scoreboard_sack_bytes = sack_list.size() * mss;
    this->scoreboard_sack_bytes = 0;        // ignore SACK for now

    // On timeouts; next_seq_no is updated to last_unacked_seq;
    // In such cases, the ack can be greater than next_seq_no; update it
    if (next_seq_no < ack) {
        next_seq_no = ack;
    }

    // New ack!
    if (ack > last_unacked_seq) {
        // Update the last unacked seq
        last_unacked_seq = ack;

        // Send the remaining data
        send_pkts();

        // Update the retx timer
        if (retx_event != NULL) { // Try to move
            cancel_retx_event();
            if (last_unacked_seq < end_seq_no) {
                // Move the timeout to last_unacked_seq
                double timeout = get_current_time() + retx_timeout;
                set_timeout(timeout);
                if (params.enable_flow_lookup && flow->id == params.flow_lookup_id) {
                    std::cout << "Flow[" << flow->id << "] at receive_ack(): set timeout at: " << timeout << std::endl;
                }
            }
        }

    }

    if (ack == flow->end_seq_no && !flow->finished) {
        flow->finished = true;
        cleanup_after_finish(flow);
        flow->finish_time = get_current_time();
        double flow_completion_time = flow->finish_time - flow->start_time;
        if (params.priority_downgrade) {
            update_fct(flow_completion_time, flow->id, get_current_time(), flow->size_in_pkt);
        }
        if (params.enable_flow_lookup && flow->id == params.flow_lookup_id) {
            std::cout << "Flow[" << flow->id << "] Finish time = "
                << flow->finish_time << "; start time = " << flow->start_time
                << "; completion time = " << flow_completion_time << std::endl;
        }
        FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), flow);
        add_to_event_queue(ev);
    }
}

// Note the tiny delay won't be used here since Qjump only send one packet at a time
Packet *QjumpChannel::send_one_pkt(uint64_t seq, uint32_t pkt_size, double delay, Flow *flow) {
    Packet *p = new Packet(
            get_current_time(),
            flow,
            seq,
            priority,
            pkt_size,
            src,
            dst
            );
    p->start_ts = get_current_time();

    if (params.debug_event_info) {
        std::cout << "Qjump sending out Packet[" << p->unique_id << "] (seq=" << seq << ") at time: " << get_current_time() + delay << " (base=" << get_current_time() << "; delay=" << delay << ")" << std::endl;
    }
    /*
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    ////add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + network_epoch, p, next_hop));
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop));
    add_to_event_queue(new QjumpEpochEvent(get_current_time() + network_epoch, src, priority));
    */
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop));
    return p;
}
// We won't apply epoch on ACK pkts to prioritize them for Qjump's sake

// Homa dealing with packet loss (orig paper S3.7)
//TODO
void HomaChannel::set_timeout(double time) {
    /*
    if (last_unacked_seq < end_seq_no) {
        ChannelRetxTimeoutEvent *ev = new ChannelRetxTimeoutEvent(time, this);
        add_to_event_queue(ev);
        retx_event = ev;
    }
    */
}

//TODO
void HomaChannel::handle_timeout() {
    /*
    num_timeouts[priority]++;
    next_seq_no = last_unacked_seq;
    send_pkts();
    set_timeout(get_current_time() + retx_timeout);
    */
}
