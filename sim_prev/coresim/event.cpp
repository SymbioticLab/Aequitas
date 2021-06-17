//
//  event.cpp
//  TurboCpp
//
//  Created by Gautam Kumar on 3/9/14.
//
//

#include <iomanip>

#include "event.h"
#include "packet.h"
#include "topology.h"
#include "debug.h"

#include "../ext/factory.h"

#include "../run/params.h"

extern Topology* topology;
extern std::priority_queue<Event*, std::vector<Event*>, EventComparator> event_queue;
extern double current_time;
extern DCExpParams params;
extern std::deque<Event*> flow_arrivals;
extern std::deque<Flow*> flows_to_schedule;

extern uint32_t num_outstanding_packets;
extern uint32_t max_outstanding_packets;

extern uint32_t num_outstanding_packets_at_50;
extern uint32_t num_outstanding_packets_at_100;
extern uint32_t arrival_packets_at_50;
extern uint32_t arrival_packets_at_100;
extern uint32_t arrival_packets_count;
extern uint32_t total_finished_flows;

extern uint32_t backlog3;
extern uint32_t backlog4;
extern uint32_t duplicated_packets_received;
extern uint32_t duplicated_packets;
extern uint32_t injected_packets;
extern uint32_t completed_packets;
extern uint32_t total_completed_packets;
extern uint32_t dead_packets;
extern uint32_t sent_packets;

extern double total_time_period;
extern uint32_t total_num_periods;
//extern std::vector<double> time_spent_send_data;
//extern std::vector<double> burst_start_time;
//extern std::vector<uint32_t> curr_burst_size;
extern std::map<std::pair<Host *, Host *>, double> time_spent_send_data;
extern std::map<std::pair<Host *, Host *>, double> burst_start_time;
extern std::map<std::pair<Host *, Host *>, uint32_t> curr_burst_size;

extern EmpiricalRandomVariable *nv_bytes;

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern int get_event_queue_size();

uint32_t Event::instance_count = 0;

Event::Event(uint32_t type, double time) {
    this->type = type;
    this->time = time;
    this->cancelled = false;
    this->unique_id = Event::instance_count++;
}

Event::~Event() {
}


/* Flow Arrival */
FlowCreationForInitializationEvent::FlowCreationForInitializationEvent(
        double time, 
        Host *src, 
        Host *dst,
        EmpiricalRandomVariable *nv_bytes, 
        RandomVariable *nv_intarr
    ) : Event(FLOW_CREATION_EVENT, time) {
    this->src = src;
    this->dst = dst;
    this->nv_bytes = nv_bytes;
    this->nv_intarr = nv_intarr;
}

FlowCreationForInitializationEvent::~FlowCreationForInitializationEvent() {}

void FlowCreationForInitializationEvent::process_event() {
    ////
    //static int N_sided_coin = 0;
    //static double time_spent_send_data = 0;
    //static double burst_start_time = 0;
    //static uint32_t curr_burst_size = 0;
    ////
    uint32_t nvVal, size;
    uint32_t id = flows_to_schedule.size();
    //std::cout << "At time: " << get_current_time() << ", Flow[" << id << "] FlowCreationForInitializationEvent" << std::endl;
    if (params.bytes_mode) {
        nvVal = nv_bytes->value();
        size = (uint32_t) nvVal;
    } else {
        nvVal = (nv_bytes->value() + 0.5); // truncate(val + 0.5) equivalent to round to nearest int
        if (nvVal > 2500000) {
            std::cout << "Giant Flow! event.cpp::FlowCreation:" << 1000000.0 * time << " Generating new flow " << id << " of size " << (nvVal*1460) << " between " << src->id << " " << dst->id << "\n";
            nvVal = 2500000;
        }
        //size = (uint32_t) nvVal * 1460;
        size = (uint32_t) nvVal * params.mss;
    }

    if (size != 0) {
        int flow_priority = 0;
        if (params.flow_type == VERITAS_FLOW) {
            assert(!params.qos_ratio.empty());
            UniformRandomVariable uv;
            double rn = uv.value();
            double temp = 0;
            for (int i = 0; i < params.qos_ratio.size(); i++) {
                temp += params.qos_ratio[i];
                if (rn <= temp) {
                    flow_priority = i;
                    break;
                }
            }
            //// all packets overloaded come all at once (for toy example)
            /*
            int num_priorities = params.weights.size();
                flow_priority = N_sided_coin;
            if (N_sided_coin != num_priorities - 1) {
                N_sided_coin++;
            } else {
                flow_priority = num_priorities - 1;
                N_sided_coin = 0;
            }
            */
            ////
        }
        //std::cout << "PUPU flow_pick_qos=" << flow_priority << std::endl;
        /*
        if (params.early_pkt_in_highest_prio && flow_priority == 0) {
            // assmue dst host queue is the same as the switch queue
            double td_std = dst->queue->get_transmission_delay(params.mss + params.hdr_size);
            double early_time = time + 2 * SLIGHTLY_SMALL_TIME - td_std;
            flows_to_schedule.push_back(Factory::get_flow(id, early_time, size, src, dst, params.flow_type, flow_priority));
        } else {
            flows_to_schedule.push_back(Factory::get_flow(id, time, size, src, dst, params.flow_type, flow_priority));
        }
        */
        flows_to_schedule.push_back(Factory::get_flow(id, time, size, src, dst, params.flow_type, flow_priority));
        //std::cout << "At time: " << get_current_time() << ", create flow [" << id << "] [" << flow_priority << "]" << std::endl;
    }

    /* old method, abandoned
    if (params.use_dynamic_load) {
        // change load dynamically at user specified frequency
        if (flows_to_schedule.size() != 0
            && flows_to_schedule.size() != params.num_flows_to_run
            //&& flows_to_schedule.size() % int(params.num_flows_to_run * params.load_change_freq) == 0) {
            && flows_to_schedule.size() % params.load_change_freq == 0) {
            params.load_idx = (params.load_idx + 1) % params.nv_intarr_vec.size();
            nv_intarr = params.nv_intarr_vec[params.load_idx];
            //std::cout << "Change load to " << params.dynamic_load[params.load_idx] << " (" << nv_intarr->value() << ") at " << flows_to_schedule.size() << "th flow creation." << std::endl;
        }
    }
    */

    double tnext = time + nv_intarr->value();

    //// Dynamic Load setting:
    //// To work out the math, assume target avg load is load_avg, we measure time spent sending data (according to params.burst_size) is t_busy,
    //// the load we use to send data bursts is load_data, we want to find out the time we spent in waiting, which is t_idle:
    //// It is easy to see the formula: load_data * t_busy / (t_busy + t_idle) = load_avg
    //// Thus, t_idle = load_data * t_busy / load_avg - t_busy
    // assume size != 0 always true
    bool end_of_busy_period = false;
    auto src_dst_pair = std::make_pair(src, dst);
    if (params.use_dynamic_load) {
        if (curr_burst_size[src_dst_pair] == 0) {
            burst_start_time[src_dst_pair] = time;
        }
        curr_burst_size[src_dst_pair]++;
        if (curr_burst_size[src_dst_pair] == params.burst_size) {
            time_spent_send_data[src_dst_pair] = tnext - burst_start_time[src_dst_pair];
            end_of_busy_period = true;
            curr_burst_size[src_dst_pair] = 0;
            //std::cout << "end of busy period. time spent = " << time_spent_send_data *1e6 << std::endl;

            double t_idle = params.burst_load * time_spent_send_data[src_dst_pair] / params.load - time_spent_send_data[src_dst_pair];
            tnext = tnext + t_idle;

            total_time_period += time_spent_send_data[src_dst_pair] + t_idle;
            total_num_periods++;
            //std::cout << "idle (waiting) time = " << t_idle * 1e6 << "; total period = " << (t_idle + time_spent_send_data[src_dst_pair]) *1e6 <<
            //    "; ratio = " << time_spent_send_data[src_dst_pair] / t_idle << "; src id : " << src->id << "; dst id: " << dst->id << std::endl;
        }
    }
    //std::cout << "tnext = " << tnext << "; src id: " << src->id << "; dst id: " << dst->id << std::endl;



    //// all packets overloaded come all at once (for toy example)
    /*
    if (N_sided_coin == 0) {
        tnext = time + params.weights.size() * nv_intarr->value();
    } else {
        tnext = time;
    }
    */
    ////

    add_to_event_queue(
            new FlowCreationForInitializationEvent(
                tnext,
                src, 
                dst,
                nv_bytes, 
                nv_intarr
                )
            );
}


/* Flow Arrival */
int flow_arrival_count = 0;

FlowArrivalEvent::FlowArrivalEvent(double time, Flow* flow) : Event(FLOW_ARRIVAL, time) {
    this->flow = flow;
}

FlowArrivalEvent::~FlowArrivalEvent() {
}

void FlowArrivalEvent::process_event() {
    //std::cout << "At time: " << get_current_time() << ", Flow[" << flow->id << "] FlowArrivalEvent" << std::endl;
    //Flows start at line rate; so schedule a packet to be transmitted
    //First packet scheduled to be queued

    num_outstanding_packets += (this->flow->size / this->flow->mss);
    arrival_packets_count += this->flow->size_in_pkt;
    if (num_outstanding_packets > max_outstanding_packets) {
        max_outstanding_packets = num_outstanding_packets;
    }
    this->flow->start_flow();
    flow_arrival_count++;
    if (flow_arrivals.size() > 0) {
        add_to_event_queue(flow_arrivals.front());
        flow_arrivals.pop_front();
    }

    if(params.num_flows_to_run > 10 && flow_arrival_count % 100000 == 0){
        double curr_time = get_current_time();
        uint32_t num_unfinished_flows = 0;
        for (uint32_t i = 0; i < flows_to_schedule.size(); i++) {
            Flow *f = flows_to_schedule[i];
            if (f->start_time < curr_time) {
                if (!f->finished) {
                    num_unfinished_flows ++;
                }
            }
        }
        if(flow_arrival_count == (int)(params.num_flows_to_run * 0.5))
        {
            arrival_packets_at_50 = arrival_packets_count;
            num_outstanding_packets_at_50 = num_outstanding_packets;
        }
        if(flow_arrival_count == params.num_flows_to_run)
        {
            arrival_packets_at_100 = arrival_packets_count;
            num_outstanding_packets_at_100 = num_outstanding_packets;
        }
        std::cout << "## " << current_time << " NumPacketOutstanding " << num_outstanding_packets
            << " MaxPacketOutstanding " << max_outstanding_packets
            << " NumUnfinishedFlows " << num_unfinished_flows << " StartedFlows " << flow_arrival_count
            << " StartedPkts " << arrival_packets_count << "\n";
    }
}


/* Packet Queuing */
// TODO: change constr to specify flow_id as an input parameter
PacketQueuingEvent::PacketQueuingEvent(double time, Packet *packet,
        Queue *queue) : Event(PACKET_QUEUING, time) {
    this->packet = packet;
    this->queue = queue;
    //std::cout << "New PacketQueuingEvent (constr) (queue[" << queue->id << "], packet[" << packet->unique_id << "]) at time: " << time << std::endl;
}

PacketQueuingEvent::~PacketQueuingEvent() {
}
/* original code, commented out for toy example: */
void PacketQueuingEvent::process_event() {
    //std::cout << "At time: " << get_current_time() << ", Queue[" << queue->unique_id << "] PacketQueuingEvent (packet[" << packet->unique_id << "]" << "<" << packet->type << ">{" << packet->seq_no << "}) from Flow[" << packet->flow->id << "]" << std::endl;
    if (!queue->busy) {
        queue->queue_proc_event = new QueueProcessingEvent(get_current_time(), queue);
        //queue->queue_proc_event = new QueueProcessingEvent(get_current_time() + (1 - SLIGHTLY_SMALL_TIME), queue);    // <- this line not original code
        add_to_event_queue(queue->queue_proc_event);
        queue->busy = true;
        queue->packet_transmitting = packet;
    }
    else if( params.preemptive_queue && this->packet->pf_priority < queue->packet_transmitting->pf_priority) {
        double remaining_percentage = (queue->queue_proc_event->time - get_current_time()) / queue->get_transmission_delay(queue->packet_transmitting->size);

        if(remaining_percentage > 0.01){
            queue->preempt_current_transmission();
            queue->queue_proc_event = new QueueProcessingEvent(get_current_time(), queue);
            add_to_event_queue(queue->queue_proc_event);
            queue->busy = true;
            queue->packet_transmitting = packet;
        }
    }

    queue->enque(packet);
}
//// For Toy Example: PacketQueuingEvent does not trigger QueueProcessingEvent (it only calls enque() now)
//// so that deque can happen at next integer time stamp
/*
void PacketQueuingEvent::process_event() {
    queue->enque(packet);
}
*/

/* Packet Arrival */
PacketArrivalEvent::PacketArrivalEvent(double time, Packet *packet)
    : Event(PACKET_ARRIVAL, time) {
        this->packet = packet;
    }

PacketArrivalEvent::~PacketArrivalEvent() {
}

void PacketArrivalEvent::process_event() {
    //std::cout << "At time: " << get_current_time() << ", Packet[" << packet->unique_id << "] PacketArrivalEvent" << std::endl;
    if (packet->type == NORMAL_PACKET) {
        completed_packets++;
    }

    packet->flow->receive(packet);
}


/* Queue Processing */
QueueProcessingEvent::QueueProcessingEvent(double time, Queue *queue)
    : Event(QUEUE_PROCESSING, time) {
        this->queue = queue;
        //std::cout << "New QueueProcessing Event (queue[" << queue->id << "]) at time:" << time << std::endl;
}

QueueProcessingEvent::~QueueProcessingEvent() {
    if (queue->queue_proc_event == this) {
        queue->queue_proc_event = NULL;
        queue->busy = false; //TODO is this ok??
    }
}

/* original code, commented out for toy example: */
void QueueProcessingEvent::process_event() {
    //std::cout << "At time: " << time << ", Queue[" << queue->unique_id << "] QueueProcessingEvent" << std::endl;
    Packet *packet = queue->deque();
    if (packet) {
        //std::cout << "At time: " << time << ", Queue[" << queue->unique_id << "] QueueProcessingEvent for packet[" << packet->unique_id << "]" << std::endl;
        queue->busy = true;
        queue->busy_events.clear();
        queue->packet_transmitting = packet;
        Queue *next_hop = topology->get_next_hop(packet, queue);
        double td = queue->get_transmission_delay(packet->size);
        double pd = queue->propagation_delay;
        //std::cout << "PUPU td: " << td << "; pd: " << pd << std::endl;
        //double additional_delay = 1e-10;
        queue->queue_proc_event = new QueueProcessingEvent(time + td, queue);
        add_to_event_queue(queue->queue_proc_event);
        queue->busy_events.push_back(queue->queue_proc_event);
        if (next_hop == NULL) {     // Arrive at DST
            //std::cout << "PUPU next_hop = NULL" << std::endl;
            //if (packet->type == NORMAL_PACKET) {
            //    std::cout << "NORMAL pkt" << std::endl;
            //} else if (packet->type == ACK_PACKET) {
            //    std::cout << "ACK pkt" << std::endl;
            //}
            Event* arrival_evt = new PacketArrivalEvent(time + td, packet);        // skip src queue & include dst queue: pd is not needed here
            ////Event* arrival_evt = new PacketArrivalEvent(time + td + pd, packet);
            arrival_evt->flow_id = packet->flow->id;
            add_to_event_queue(arrival_evt);
            queue->busy_events.push_back(arrival_evt);
        } else {
            //std::cout << "PUPU next_hop queue: " << next_hop->unique_id << std::endl;
            //if (packet->type == NORMAL_PACKET) {
            //    std::cout << "NORMAL pkt" << std::endl;
            //} else if (packet->type == ACK_PACKET) {
            //    std::cout << "ACK pkt" << std::endl;
            //}
            Event* queuing_evt = NULL;
            if (params.cut_through == 1) {
                double cut_through_delay =
                    queue->get_transmission_delay(packet->flow->hdr_size);
                //std::cout << "DE: [1] at time: " << time + cut_through_delay + pd << ", adding new PacketQueuingEvent packet[" << packet->unique_id << "] to queue[" << next_hop->unique_id << "]" << std::endl;
                //std::cout << "DE: pd = " << pd << "; cut through delay = " << cut_through_delay << std::endl;
                queuing_evt = new PacketQueuingEvent(time + cut_through_delay + pd, packet, next_hop);
                queuing_evt->flow_id = packet->flow->id;
            } else {
                queuing_evt = new PacketQueuingEvent(time + td + pd, packet, next_hop);
                queuing_evt->flow_id = packet->flow->id;
            }

            add_to_event_queue(queuing_evt);
            queue->busy_events.push_back(queuing_evt);
        }
    } else {
        queue->busy = false;
        queue->busy_events.clear();
        queue->packet_transmitting = NULL;
        queue->queue_proc_event = NULL;
    }
}
//// Foy Toy Example:
//// QueueProcessingEvent will kick off at the very begining of the simulation and triggered every trans delay of a standard packet
/*
void QueueProcessingEvent::process_event() {
    Packet *packet = queue->deque();
    // always triggers the next processing event after the transmission delay of a standard packet (1500B)
    double td_std = queue->get_transmission_delay(params.mss + params.hdr_size);    // (mss + hdr) should be 1500 Bytes
    assert(td_std > SLIGHTLY_SMALL_TIME);
    //std::cout << "PUPU td_std: " << td_std << std::endl;
    queue->queue_proc_event = new QueueProcessingEvent(time + td_std, queue);
    add_to_event_queue(queue->queue_proc_event);
    queue->busy_events.push_back(queue->queue_proc_event);
    if (packet) {
        queue->busy = true;         // queue->busy is no longer important (no one uses it)
        queue->busy_events.clear();
        queue->packet_transmitting = packet;
        Queue *next_hop = topology->get_next_hop(packet, queue);
        double td = queue->get_transmission_delay(packet->size);
        double pd = queue->propagation_delay;
        if (next_hop == NULL) {
            //// Toy Example: do not count pd & the last td in FCT
            Event* arrival_evt = new PacketArrivalEvent(time, packet);
            //Event* arrival_evt = new PacketArrivalEvent(time + td + pd, packet);
            ////
            add_to_event_queue(arrival_evt);
            queue->busy_events.push_back(arrival_evt);
        } else {
            Event* queuing_evt = NULL;
            if (params.cut_through == 1) {
                double cut_through_delay =
                    queue->get_transmission_delay(packet->flow->hdr_size);
                queuing_evt = new PacketQueuingEvent(time + cut_through_delay + pd, packet, next_hop);
            } else {
                queuing_evt = new PacketQueuingEvent(time + td + pd, packet, next_hop);
            }

            add_to_event_queue(queuing_evt);
            queue->busy_events.push_back(queuing_evt);
        }
    } else {
        queue->busy = false;
        queue->busy_events.clear();
        queue->packet_transmitting = NULL;
        queue->queue_proc_event = NULL;
    }
}
*/

/* Queue Flushing */
// Note: assuming only 2 qos levels: QoS_High and QoS_Low
// the start time of flushing should be greater than (simulation_start_time + Weight_of_QoS_High * td_std + SLIGHTLY_SMALL_TIME)
// but less than (simulation_start_time + Sum_of_QoS_Weights * td_std)
// for simplicity we will just pick start_time = simulation_start_time + Sum_of_QoS_Weights * td_std - a_tiny_small_time.
// and then it triggers itself every Weight_of_QoS_High * td_std
QueueFlushingEvent::QueueFlushingEvent(double time, Queue *queue)
    : Event(QUEUE_PROCESSING, time) {
        this->queue = queue;
        double td_std = queue->get_transmission_delay(params.mss + params.hdr_size);    // (mss + hdr) should be 1500 Bytes
        this->flushing_period = params.sum_weights * td_std * params.flushing_coefficient;
        //std::cout << "New QueueFlushing Event (queue[" << queue->id << "]) at time:" << time << std::endl;
}

QueueFlushingEvent::~QueueFlushingEvent() {
}

void QueueFlushingEvent::process_event() {
    queue->flush();
    add_to_event_queue(new QueueFlushingEvent(get_current_time() + flushing_period, queue));
}

LoggingEvent::LoggingEvent(double time) : Event(LOGGING, time){
    this->ttl = 1e10;
}

LoggingEvent::LoggingEvent(double time, double ttl) : Event(LOGGING, time){
    this->ttl = ttl;
}

LoggingEvent::~LoggingEvent() {
}

void LoggingEvent::process_event() {
    double current_time = get_current_time();
    // can log simulator statistics here.
}


/* Flow Finished */
FlowFinishedEvent::FlowFinishedEvent(double time, Flow *flow)
    : Event(FLOW_FINISHED, time) {
        this->flow = flow;
    }

FlowFinishedEvent::~FlowFinishedEvent() {}

void FlowFinishedEvent::process_event() {
    //std::cout << "At time: " << get_current_time() << ", Flow[" << flow->id << "](" << flow->flow_priority << ") FlowFinishedEvent; FCT = "
    //<< flow->flow_completion_time * 1e6 << " us; avg_queuing_delay = " << flow->get_avg_queuing_delay_in_us() << std::endl;
    this->flow->finished = true;
    this->flow->finish_time = get_current_time();
    this->flow->flow_completion_time = this->flow->finish_time - this->flow->start_time;
    total_finished_flows++;
    //// Does not compute oracle fct for now for the No_ACK_Pkt hack
    /*
    auto slowdown = 1000000 * flow->flow_completion_time / topology->get_oracle_fct(flow);
    if (slowdown < 1.0 && slowdown > 0.9999) {
        slowdown = 1.0;
    }
    if (slowdown < 1.0) {
        std::cout << "bad slowdown " << 1e6 * flow->flow_completion_time << " " << topology->get_oracle_fct(flow) << " " << slowdown << "\n";
    }
    assert(slowdown >= 1.0);
    */

    if (print_flow_result()) {
        std::cout << std::setprecision(4) << std::fixed ;
        std::cout
            << flow->id << " "
            << flow->size << " "
            << flow->src->id << " "
            << flow->dst->id << " "
            << 1000000 * flow->start_time << " "
            << 1000000 * flow->finish_time << " "
            << 1000000.0 * flow->flow_completion_time << " "
            ////<< topology->get_oracle_fct(flow) << " "
            ////<< slowdown << " "
            << flow->total_pkt_sent << "/" << (flow->size/flow->mss) << "//" << flow->received_count << " "
            << flow->data_pkt_drop << "/" << flow->ack_pkt_drop << "/" << flow->pkt_drop << " "
            << 1000000 * (flow->first_byte_send_time - flow->start_time) << " "
            //<< flow->get_avg_queuing_delay_in_us() / 1000000 << " "
            //<< "[" << flow->flow_priority << "]"
            << std::endl;
        std::cout << std::setprecision(9) << std::fixed;
    }
}


/* Flow Processing */
FlowProcessingEvent::FlowProcessingEvent(double time, Flow *flow)
    : Event(FLOW_PROCESSING, time) {
        this->flow = flow;
    }

FlowProcessingEvent::~FlowProcessingEvent() {
    if (flow->flow_proc_event == this) {
        flow->flow_proc_event = NULL;
    }
}

void FlowProcessingEvent::process_event() {
    this->flow->send_pending_data();
}


/* Retx Timeout */
RetxTimeoutEvent::RetxTimeoutEvent(double time, Flow *flow)
    : Event(RETX_TIMEOUT, time) {
        this->flow = flow;
    }

RetxTimeoutEvent::~RetxTimeoutEvent() {
    if (flow->retx_event == this) {
        flow->retx_event = NULL;
    }
}

void RetxTimeoutEvent::process_event() {
    flow->handle_timeout();
}
