#include "event.h"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <assert.h>

#include "channel.h"
#include "debug.h"
#include "flow.h"
#include "nic.h"
#include "packet.h"
#include "queue.h"
#include "topology.h"
#include "../ext/factory.h"
#include "../ext/pdq_flow.h"
#include "../ext/qjump_host.h"
#include "../run/params.h"

extern Topology* topology;
//extern std::vector<std::priority_queue<Event*, std::vector<Event*>, EventComparator>> event_queues;
//extern double current_time;
extern DCExpParams params;
//extern std::vector<std::deque<Event*>> flow_arrivals;
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
//extern uint32_t completed_packets;
extern uint32_t dead_packets;
extern uint32_t sent_packets;
extern uint32_t num_pkt_drops;

extern double total_time_period;
extern uint32_t total_num_periods;
extern std::map<std::pair<uint32_t, uint32_t>, double> time_spent_send_data;
extern std::map<std::pair<uint32_t, uint32_t>, double> burst_start_time;
extern std::map<std::pair<uint32_t, uint32_t>, uint32_t> curr_burst_size;
extern std::map<std::pair<uint32_t, uint32_t>, uint32_t> curr_burst_size_in_flow;
extern std::vector<uint32_t> per_pctl_downgrades;
extern std::vector<uint32_t> per_host_QoS_H_downgrades;
extern std::vector<uint32_t> per_host_QoS_H_rpcs;
extern uint32_t num_measurements_cleared;
extern uint32_t total_measurements;
extern std::vector<uint32_t> lat_cleared;
extern std::vector<double> qos_h_admit_prob;
extern std::vector<std::vector<double>> qos_h_admit_prob_per_host;
extern int init_count;
extern std::vector<uint32_t> qos_h_memory_misses;
//extern std::vector<uint32_t> qos_h_total_misses_per_host;
extern std::vector<uint32_t> qos_h_issued_rpcs_per_host;
extern uint32_t num_outstanding_rpcs;
extern std::vector<uint32_t> num_outstanding_rpcs_total;

extern EmpiricalRandomVariable *nv_bytes;

extern double get_current_time();
extern void add_to_event_queue(Event *);

uint32_t Event::instance_count = 0;

Event::Event(uint32_t type, double time) {
    this->type = type;
    this->time = time;
    this->cancelled = false;
    this->unique_id = Event::instance_count++;
    this->qid = 0;    // Inherited Events should update it
}

Event::~Event() {
}


/* Flow Creation */
// multi-threading: this event creates flows; so we can't assign QID from flows' QID like we normally do
// These events will be all cleared up before any FlowArrivalEvents can start
// For simplicity, we will let EventQueue[0] to handle all those events.
// Multi-threading will start after FlowArrivalEvents are pushed to the event queues.
FlowCreationForInitializationEvent::FlowCreationForInitializationEvent(
        double time,
        Host *src,
        Host *dst,
        EmpiricalRandomVariable *nv_bytes,
        RandomVariable *nv_intarr,
        bool is_QoSM,
        uint32_t qid      // Note qid for this event has a default value of 0
    ) : Event(FLOW_CREATION_EVENT, time) {
    this->src = src;
    this->dst = dst;
    this->nv_bytes = nv_bytes;
    this->nv_intarr = nv_intarr;
    this->is_QoSM = is_QoSM;
    this->qid = qid;
}

FlowCreationForInitializationEvent::~FlowCreationForInitializationEvent() {}

void FlowCreationForInitializationEvent::process_event() {
    uint32_t nvVal, size;
    uint32_t id = flows_to_schedule.size();
    if (params.enable_flow_lookup && id == params.flow_lookup_id) {
        std::cout << "At time: " << get_current_time() << ", Flow[" << id << "] FlowCreationForInitializationEvent" << std::endl;
    }
    if (params.bytes_mode) {
        nvVal = nv_bytes->value();
        size = (uint32_t) nvVal;
    } else {
        nvVal = (nv_bytes->value() + 0.5); // truncate(val + 0.5) equivalent to round to nearest int
        if (nvVal > 2500000) {
            std::cout << "Giant Flow! event.cpp::FlowCreation:" << 1000000.0 * time << " Generating new flow " << id << " of size " << (nvVal*1460) << " between " << src->id << " " << dst->id << "\n";
            nvVal = 2500000;
        }
        size = (uint32_t) nvVal * params.mss;
    }

    if (params.test_fairness) {
        if (params.weights.size() == 3) {
            params.fairness_qos_dist[0] = 0.5;
            params.fairness_qos_dist[1] = 0.2;
            params.fairness_qos_dist[2] = 0.3;

            ////params.fairness_qos_dist[0] = 0.15;
            ////params.fairness_qos_dist[1] = 0.2;
            ////params.fairness_qos_dist[2] = 0.65;

            //params.fairness_qos_dist[0] = 0.3;
            //params.fairness_qos_dist[1] = 0.2;
            //params.fairness_qos_dist[2] = 0.5;
        } else if (params.weights.size() == 2) {
            ////params.fairness_qos_dist[0] = 0.4;
            ////params.fairness_qos_dist[1] = 0.6;

            params.fairness_qos_dist[0] = 0.1;
            params.fairness_qos_dist[1] = 0.9;

            //params.fairness_qos_dist[0] = 0.5;
            //params.fairness_qos_dist[1] = 0.5;

            //params.fairness_qos_dist[0] = 0.15;
            //params.fairness_qos_dist[1] = 0.85;
        } else {
            std::cout << "must use 2 or 3 qos levels to test fairness" << std::endl;
            exit(1);
        }
    }

    if (size != 0) {
        // setting flow priority
        int flow_priority = 0;
        assert(!params.qos_ratio.empty());
        UniformRandomVariable uv;
        double rn = uv.value();
        double temp = 0;
        //if (!params.test_fairness || src->id % 4 != 0) {
        if (params.test_size_to_priority == 1) {
            // in this case, qos dist depends on the RPC size CDF, not on how user sets qos_ratio
            // Hardcoded for now
            if (size == 8192) {
                flow_priority = 0;
            } else if (size == 16384) {
                flow_priority = 1;
            } else if (size == 32768) {
                flow_priority = 2;
            }
        } else if (params.test_size_to_priority == 2) {
            // in this case, qos dist depends on the RPC size CDF, not on how user sets qos_ratio
            // Hardcoded for now
            if (size == 8192) {
                flow_priority = 2;
            } else if (size == 16384) {
                flow_priority = 1;
            } else if (size == 32768){
                flow_priority = 0;
            }
        } else {
            if (!params.test_fairness || src->id % 2 != 0) {
                for (int i = 0; i < params.qos_ratio.size(); i++) {
                    temp += params.qos_ratio[i];
                    if (rn <= temp) {
                        flow_priority = i;
                        break;
                    }
                }
            } else {
                for (int i = 0; i < params.fairness_qos_dist.size(); i++) {
                    temp += params.fairness_qos_dist[i];
                    if (rn <= temp) {
                        flow_priority = i;
                        break;
                    }
                }
            }
        }


        Flow *new_flow = Factory::get_flow(id, time, size, src, dst, params.flow_type, flow_priority);

        // setting flow deadline for D3/PDQ
        // let's do it using the qos targets for now
        if (params.flow_type == D3_FLOW || params.flow_type == PDQ_FLOW) {
            if (flow_priority == params.num_qos_level - 1) {    // lowest prio level has no deadline
                new_flow->has_ddl = false;
                new_flow->deadline = 0;
            } else {
                new_flow->has_ddl = true;
                //new_flow->deadline = params.hardcoded_targets[flow_priority];
                new_flow->deadline = params.targets[flow_priority];
            }
        }

        flows_to_schedule.push_back(new_flow);
        //std::cout << "At time: " << get_current_time() << ", create flow [" << id << "] [" << flow_priority << "]" << std::endl;
    }



    double intarr_value = nv_intarr->value();
    double tnext = time + intarr_value;
    //double tnext_heavy_burst = time + 1e-9;
    //std::cout << "size = " << size << "; intarr = " << nv_intarr->value() << std::endl;;
    //std::cout << "int_arr: " << intarr_value << std::endl;


    //// Dynamic Load setting:
    //// To work out the math, assume target avg load is load_avg, we measure time spent sending data (according to params.burst_size) is t_busy,
    //// the load we use to send data bursts is load_data, we want to find out the time we spent in waiting, which is t_idle:
    //// It is easy to see the formula: load_burst * t_busy / (t_busy + t_idle) = load_avg
    //// Thus, t_idle = load_data * t_busy / load_avg - t_busy
    // assume size != 0 always true
    bool end_of_busy_period = false;
    auto src_dst_pair = std::make_pair(src->id, dst->id);
    //std::cout << "src=" << src->id <<", dst=" << dst->id << ": Curr burst size = " << curr_burst_size[src_dst_pair] << "; tnext = " << tnext << std::endl;
    if (params.use_dynamic_load) {
        if (curr_burst_size[src_dst_pair] == 0) {
            burst_start_time[src_dst_pair] = time;
        }
        curr_burst_size[src_dst_pair] += size;
        curr_burst_size_in_flow[src_dst_pair]++;
        ////if (params.use_burst_byte) {    // Note: assuming use bytes mode not packets mode for the RPC (params.bytes_mode == 1)
        ////    curr_burst_size[src_dst_pair] += size;
        ////} else {
        ////    curr_burst_size[src_dst_pair]++;
        ////}
        time_spent_send_data[src_dst_pair] += intarr_value;
        ////curr_burst_size[src_dst_pair]++;
        uint32_t curr_burst = 0;
        if (params.use_burst_byte) {
            curr_burst = curr_burst_size[src_dst_pair];
        } else {
            curr_burst = curr_burst_size_in_flow[src_dst_pair];
        }
        if (curr_burst >= params.burst_size) {  // use '>=' instead of '==' to work for both cases where use_burst_byte == 0 and use_burst_byte == 1
        ////if (curr_burst_size[src_dst_pair] >= params.burst_size) {    // use '>=' instead of '==' to work for both cases where use_burst_byte == 0 and use_burst_byte == 1
            ////time_spent_send_data[src_dst_pair] = tnext - burst_start_time[src_dst_pair];    // the other method is better in that it also works when params.burst_with_no_spacing = 1

            end_of_busy_period = true;
            //std::cout << "end of busy period. time spent = " << time_spent_send_data[src_dst_pair] *1e6 << std::endl;

            double t_idle = 0;
            //std::cout << "tnext1 = " << tnext + t_idle << "; tnext2 = " << burst_start_time[src_dst_pair] + time_spent_send_data[src_dst_pair] + t_idle << std::endl;

            if (!params.burst_with_no_spacing) {
                t_idle = params.burst_load * time_spent_send_data[src_dst_pair] / params.load - time_spent_send_data[src_dst_pair];
                ////tnext = tnext + t_idle;    // this is correct
                tnext = burst_start_time[src_dst_pair] + time_spent_send_data[src_dst_pair] + t_idle;    // both this line and last line are correct in this case
            } else {
                uint32_t bytes_sent_in_period = curr_burst_size[src_dst_pair];
                ////if (!params.use_burst_byte) {    //Note: if not using use_burst_byte mode, assuming uniform size dist
                ////    bytes_sent_in_period = curr_burst_size[src_dst_pair] * size;    // note assuming size is always same; can fix this hack later but we are very likely to stick to use_burst_byte = 1 for mixed sized CDF
                ////}

                // t_idle in this case is the time that makes the instanteneous bytes rest long enough so to match avg load
                // in other words, t_idle = entire period
                // data_arriving_rate = load_avg * line_rate ==> bytes_in_period / period(which is t_idle) = load_avg * line_rate
                t_idle = (double)bytes_sent_in_period * 8 / (params.load * params.bandwidth / (params.num_hosts - 1));
                tnext = burst_start_time[src_dst_pair] + t_idle;
                //std::cout << "bytes_in_period = " << bytes_sent_in_period << "; t_idle = " << t_idle * 1e6 << " us" << std::endl;
            }

            total_time_period += time_spent_send_data[src_dst_pair] + t_idle;
            total_num_periods++;
            //std::cout << "idle (waiting) time = " << t_idle * 1e6 << "; total period = " << (t_idle + time_spent_send_data[src_dst_pair]) *1e6 <<
            //    "; ratio = " << time_spent_send_data[src_dst_pair] / t_idle << "; src id : " << src->id << "; dst id: " << dst->id << std::endl;
            //std::cout << "Total 32K RPCs sent during busy period = " << curr_burst << std::endl;

            curr_burst_size[src_dst_pair] = 0;
            time_spent_send_data[src_dst_pair] = 0;
        }
    }

    if (params.burst_with_no_spacing && !end_of_busy_period) {
        ////tnext = time + 1e-9;
        tnext = time + 1e-11;
    }

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
    this->qid = flow->qid;
}

FlowArrivalEvent::~FlowArrivalEvent() {
}

void FlowArrivalEvent::process_event() {
    if (params.debug_event_info) {
        std::cout << "At time: " << get_current_time() << ", Flow[" << flow->id << "] at Priority[" << flow->flow_priority << "] from Host[" << flow->src->id << "] FlowArrivalEvent" << std::endl;
    }
    if (params.enable_flow_lookup && flow->id == params.flow_lookup_id) {
        std::cout << "At time: " << get_current_time() << ", Flow[" << flow->id << "] at Priority[" << flow->flow_priority << "] from Host[" << flow->src->id << "] FlowArrivalEvent" << std::endl;
    }
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
    /*
    if (flow_arrivals[qid].size() > 0) {
        //std::cout << "PUPU after FA[" << unique_id << "] process Flow[" << flow->id << "], push FA[" << flow_arrivals.front()->unique_id << "] with Flow[" << ((FlowArrivalEvent *)flow_arrivals.front())->flow->id << "]" << std::endl;
        add_to_event_queue(flow_arrivals[qid].front());
        flow_arrivals[qid].pop_front();
    }
    */

    if(params.num_flows_to_run > 10 && flow_arrival_count % 100000 == 0){
    ////if(params.num_flows_to_run > 10 && flow_arrival_count % 10000 == 0){
        uint32_t num_unfinished_flows = 0;
        for (uint32_t i = 0; i < flows_to_schedule.size(); i++) {
            Flow *f = flows_to_schedule[i];
            if (f->start_time < get_current_time()) {
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

        std::cout << std::setprecision(15) << std::fixed;
        std::cout << "## " << get_current_time() << " NumPktOutstd " << num_outstanding_packets
            << " MaxPktOutstd " << max_outstanding_packets
            << " PktDropsSoFar " << num_pkt_drops
            << " NumUnfFlows " << num_unfinished_flows << " StartedFlows " << flow_arrival_count
            << " NumRPCsOutstd " << num_outstanding_rpcs
            << " StartedPkts " << arrival_packets_count << std::endl;
            //<< " NumDowngrades(H/M) " << per_pctl_downgrades[0] << "/" << per_pctl_downgrades[1] << std::endl;
        //for (int i = 0; i < params.num_hosts; i++) {
        //    std::cout << "host[" << i << "] QoS high rpc issued/downs: " << per_host_QoS_H_rpcs[i] << "/" << per_host_QoS_H_downgrades[i] << std::endl;
        //    per_host_QoS_H_downgrades[i] = 0;
        //    per_host_QoS_H_rpcs[i] = 0;
        //}
        num_outstanding_rpcs_total.push_back(num_outstanding_rpcs);
        if (params.priority_downgrade) {
            double sum_prob = 0;
            uint32_t num_samples = qos_h_admit_prob.size();
            for (double p : qos_h_admit_prob) {
                sum_prob += p;
            }
            double prob_avg = sum_prob / num_samples;
            std::cout << std::setprecision(2) << std::fixed;
            std::cout << "All QoS_H Host avg prob: " << prob_avg << std::endl;
            //sort(qos_h_down_prob.begin(), qos_h_down_prob.end());
            //double prob_50th = qos_h_down_prob[num_samples * 0.50];
            //double prob_90th = qos_h_down_prob[num_samples * 0.90];
            //double prob_99th = qos_h_down_prob[num_samples * 0.99];
            //double prob_max = qos_h_down_prob[num_samples - 1];
            //std::cout << std::setprecision(2) << std::fixed;
            //std::cout << "num prob samples = " << num_samples << "; prob (avg/50th/90th/99th/max) = " << prob_avg << "/" << prob_50th << "/"
            //        << prob_90th << "/" << prob_99th << "/" << prob_max << std::endl;
            qos_h_admit_prob.clear();


            /*
            ////
            std::cout << "per host avg admit prob: " << std::endl;
            for (int i = 0; i < qos_h_admit_prob_per_host.size(); i++) {
                double sum_prob = 0;
                for (double p: qos_h_admit_prob_per_host[i]) {
                    sum_prob += p;
                }
                double prob_avg = sum_prob / qos_h_admit_prob_per_host[i].size();
                std::cout << std::setprecision(2) << std::fixed;
                std::cout << "Host[" << i << "]: " << prob_avg << std::endl;
                qos_h_admit_prob_per_host[i].clear();
            }
            ////
            */

        }
        per_pctl_downgrades[0] = 0;
        per_pctl_downgrades[1] = 0;

        /*
        ////
        std::cout << "per host issued QoS_H RPCs:" << std::endl;
        for (int i = 0; i < qos_h_issued_rpcs_per_host.size(); i++) {
            std::cout << "Host[" << i << "]: " << qos_h_issued_rpcs_per_host[i] << std::endl;
            //qos_h_issued_rpcs_per_host[i] = 0;
        }
        std::cout << "per host QoS_H total misses:" << std::endl;
        for (int i = 0; i < qos_h_total_misses_per_host.size(); i++) {
            std::cout << "Host[" << i << "]: " << qos_h_total_misses_per_host[i] << std::endl;
        }
        ////
        */

    }
}


/* Packet Queuing */
PacketQueuingEvent::PacketQueuingEvent(double time, Packet *packet,
        Queue *queue) : Event(PACKET_QUEUING, time) {
    this->packet = packet;
    this->queue = queue;
    this->qid = packet->flow->qid;
    //std::cout << "New PacketQueuingEvent (constr) (queue[" << queue->id << "], packet[" << packet->unique_id << "]) at time: " << time << std::endl;
}

PacketQueuingEvent::~PacketQueuingEvent() {
}
/* original code, commented out for toy example: */
void PacketQueuingEvent::process_event() {
    //this->packet->flow->current_event_time = time;
    if (params.debug_event_info) {
        std::cout << "At time: " << get_current_time() << ", Queue[" << queue->unique_id << "] PacketQueuingEvent[" << unique_id << "] (packet[" << packet->unique_id << "]" << "<" << packet->type << ">{" << packet->seq_no << "}) from Flow[" << packet->flow->id << "]" << std::endl;
    }
    if (params.enable_flow_lookup && packet->flow->id == params.flow_lookup_id) {
        std::cout << "At time: " << get_current_time() << ", Queue[" << queue->unique_id << "] PacketQueuingEvent[" << unique_id << "] (packet[" << packet->unique_id << "]" << "<" << packet->type << ">{" << packet->seq_no << "}) from Flow[" << packet->flow->id << "]" << std::endl;
    }
    if (params.debug_event_info) {
        if (queue->busy) {
            std::cout << "Queue[" << queue->unique_id << "] is busy processing Event[" << queue->queue_proc_event->unique_id << "], type = " << queue->queue_proc_event->type << std::endl;
        }
    }

    if (!queue->busy) {
        uint32_t next_qid = packet->flow->qid;
        double next_ev_time = get_current_time();

        QueueProcessingEvent * next_ev = new QueueProcessingEvent(next_ev_time, queue, next_qid);
        if (params.debug_event_info) {
            std::cout << "Queue[" << queue->unique_id << "] adding a new QueueProcessingEvent[" << next_ev->unique_id << "] with start time:" << next_ev->time << std::endl;
        }
        //queue->queue_proc_event = new QueueProcessingEvent(next_ev_time, queue, next_qid);
        //queue->queue_proc_event = new QueueProcessingEvent(get_current_time(), queue, packet->flow->qid);
        queue->queue_proc_event = next_ev;
        add_to_event_queue(queue->queue_proc_event);
        queue->busy = true;
        queue->packet_transmitting = packet;

    }
    else if( params.preemptive_queue && this->packet->pf_priority < queue->packet_transmitting->pf_priority) {
        assert(false);
        double remaining_percentage = (queue->queue_proc_event->time - get_current_time()) / queue->get_transmission_delay(queue->packet_transmitting);

        if(remaining_percentage > 0.01){
            queue->preempt_current_transmission();
            queue->queue_proc_event = new QueueProcessingEvent(get_current_time(), queue, packet->flow->qid);
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
    this->qid = packet->flow->qid;
}

PacketArrivalEvent::~PacketArrivalEvent() {
}

void PacketArrivalEvent::process_event() {
    if (params.debug_event_info) {
        std::cout << "At time: " << get_current_time() << ", Packet[" << packet->unique_id << "] PacketArrivalEvent" << std::endl;
    }
    if (params.enable_flow_lookup && packet->flow->id == params.flow_lookup_id) {
        std::cout << "At time: " << get_current_time() << ", Packet[" << packet->unique_id << "] PacketArrivalEvent" << std::endl;
    }

    packet->flow->receive(packet);
}


/* Queue Processing */
QueueProcessingEvent::QueueProcessingEvent(double time, Queue *queue, uint32_t qid)
    : Event(QUEUE_PROCESSING, time) {
        this->queue = queue;
        this->qid = qid;
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
    //assert(queue);
    if (params.debug_event_info) {
        std::cout << "At time: " << get_current_time() << ", Queue[" << queue->unique_id << "] QueueProcessingEvent[" << unique_id << "]" << std::endl;
    }

    Packet *packet = queue->deque(get_current_time());
    if (packet) {
        //assert(packet->flow);
        if (params.debug_event_info) {
            std::cout << "At time: " << get_current_time() << ", Queue[" << queue->unique_id << "] dequeued a packet[" << packet->unique_id << "] from Flow[" << packet->flow->id << "]" << std::endl;
        }
        if (params.enable_flow_lookup && packet->flow->id == params.flow_lookup_id) {
            std::cout << "At time: " << get_current_time() << ", Queue[" << queue->unique_id << "] QueueProcessingEvent for packet[" << packet->unique_id << "]" << std::endl;
        }

        queue->busy = true;
        queue->packet_transmitting = packet;
        Queue *next_hop = topology->get_next_hop(packet, queue);
        double td = queue->get_transmission_delay(packet);
        double pd = queue->propagation_delay;

        queue->queue_proc_event = new QueueProcessingEvent(get_current_time() + td, queue, packet->flow->qid);
        add_to_event_queue(queue->queue_proc_event);


        if (next_hop == NULL) {
            ////Event* arrival_evt = new PacketArrivalEvent(time + td + pd, packet);
            Event* arrival_evt = new PacketArrivalEvent(get_current_time() + td, packet);  // NOTE: when we include dst queues, no more pd is included here
            if (params.debug_event_info) {
                std::cout << "Queue[" << queue->unique_id << "] adding a PacketArrivalEvent[" << arrival_evt->unique_id << "] with start time:" << arrival_evt->time << std::endl;
            }
            add_to_event_queue(arrival_evt);
        } else {
            Event* queuing_evt = NULL;
            if (params.cut_through == 1) {
                double cut_through_delay =
                    queue->get_cut_through_delay(packet);
                    //queue->get_transmission_delay(packet->flow->hdr_size);
                queuing_evt = new PacketQueuingEvent(get_current_time() + cut_through_delay + pd, packet, next_hop);
            } else {
                queuing_evt = new PacketQueuingEvent(get_current_time() + td + pd, packet, next_hop);
            }

            add_to_event_queue(queuing_evt);
        }
    } else {
        queue->busy = false;
        queue->packet_transmitting = NULL;
        queue->queue_proc_event = NULL;
    }
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
    //double current_time = get_current_time();
    // can log simulator statistics here.
}


/* Flow Finished */
FlowFinishedEvent::FlowFinishedEvent(double time, Flow *flow)
        : Event(FLOW_FINISHED, time) {
    this->flow = flow;
    this->qid = flow->qid;
}

FlowFinishedEvent::~FlowFinishedEvent() {}

void FlowFinishedEvent::process_event() {
    //this->flow->current_event_time = time;
    num_outstanding_rpcs--;

    //// These data has been updated in Flow::receive_ack(); but I think we need to do this again because now we should have the correct global time
    this->flow->finished = true;
    this->flow->finish_time = get_current_time();
    //this->flow->flow_completion_time = this->flow->finish_time - this->flow->start_time;
    /*
    if (!params.normalized_lat) {
        this->flow->flow_completion_time = this->flow->finish_time - this->flow->start_time;
    } else {
        this->flow->flow_completion_time = (this->flow->finish_time - this->flow->start_time) / this->flow->size_in_pkt;
    }
    */
    if (params.print_normalized_result) {
        if (!params.real_nic) {
            this->flow->flow_completion_time = (this->flow->finish_time - this->flow->start_time) / this->flow->size_in_pkt;
        } else {
            this->flow->flow_completion_time = (this->flow->finish_time - this->flow->rnl_start_time) / this->flow->size_in_pkt;
        }
    } else {
        if (!params.real_nic) {
            this->flow->flow_completion_time = this->flow->finish_time - this->flow->start_time;
        } else {
            this->flow->flow_completion_time = this->flow->finish_time - this->flow->rnl_start_time;
        }
    }

    total_finished_flows++;
    ////
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == flow->id)) {
        std::cout << "At time: " << get_current_time() << ", Flow[" << flow->id << "](" << flow->flow_priority << ") FlowFinishedEvent; FCT = "
            << flow->flow_completion_time * 1e6 << std::endl;
                //<< " us; avg_queuing_delay = " << flow->get_avg_queuing_delay_in_us() << std::endl;
    }

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

    // cleanup per-flow channel
    if (!params.channel_multiplexing) {
        delete flow->channel;
        flow->channel = NULL;
    }
}


/* Flow Processing */
// Note: since this event is not in use, I don't care update the current_event_time here
FlowProcessingEvent::FlowProcessingEvent(double time, Flow *flow)
    : Event(FLOW_PROCESSING, time) {
        this->flow = flow;
        this->qid  = flow->qid;
    }

FlowProcessingEvent::~FlowProcessingEvent() {
    if (flow->flow_proc_event == this) {
        flow->flow_proc_event = NULL;
    }
}

void FlowProcessingEvent::process_event() {
    this->flow->send_pending_data();
}


/* Retx Timeout */        // I still keep this so that pFabric can work
RetxTimeoutEvent::RetxTimeoutEvent(double time, Flow *flow)
        : Event(RETX_TIMEOUT, time) {
    this->flow = flow;
    this->qid = flow->qid;
}

RetxTimeoutEvent::~RetxTimeoutEvent() {
    if (flow->retx_event == this) {
        flow->retx_event = NULL;
    }
}

void RetxTimeoutEvent::process_event() {
    if (params.debug_event_info || (params.enable_flow_lookup && flow->id == params.flow_lookup_id)) {
        std::cout << "At time: " << get_current_time() << "; process RetxTimeoutEvent for Flow["<< flow->id << "]" << std::endl;
    }
    flow->handle_timeout();
}

/* Retx Timeout Sender */        // used by Homa senders
RetxTimeoutSenderEvent::RetxTimeoutSenderEvent(double time, Flow *flow)
        : Event(RETX_TIMEOUT_SENDER, time) {
    this->flow = flow;
    this->qid = flow->qid;
}

RetxTimeoutSenderEvent::~RetxTimeoutSenderEvent() {
    if (flow->retx_sender_event == this) {
        flow->retx_sender_event = NULL;
    }
}

void RetxTimeoutSenderEvent::process_event() {
    if (params.debug_event_info || (params.enable_flow_lookup && flow->id == params.flow_lookup_id)) {
        std::cout << "At time: " << get_current_time() << "; process RetxTimeoutSenderEvent for Flow["<< flow->id << "]" << std::endl;
    }
    flow->handle_timeout_sender();
}

/* Channel Retx Timeout */        // Added since Channel now handles timeout itself
ChannelRetxTimeoutEvent::ChannelRetxTimeoutEvent(double time, Channel *channel)
        : Event(CHANNEL_RETX_TIMEOUT, time) {
    this->channel = channel;
}

ChannelRetxTimeoutEvent::~ChannelRetxTimeoutEvent() {
    //TODO(yiwenzhang): check whether doing nothing is OK
    // keeping the following causes an use-after-delete in per-flow channel impl
    /*
    if (channel->retx_event == this) {    // what is the purpose of checking 'this'?
        //delete channel->retx_event;    //// check
        channel->retx_event = NULL;
    }
    */
}

void ChannelRetxTimeoutEvent::process_event() {
    if (params.debug_event_info) {
        std::cout << "At time: " << get_current_time() << "; process RetxTimeoutEvent for Channel["<< channel->id << "]" << std::endl;
    }
    channel->handle_timeout();
}

/* NIC Processing */
NICProcessingEvent::NICProcessingEvent(double time, NIC* nic)
        : Event(NIC_PROCESSING, time) {
    this->time = time;
    this->nic = nic;
}

NICProcessingEvent::~NICProcessingEvent() {
}

void NICProcessingEvent::process_event() {
    if (params.debug_event_info) {
        std::cout << "At time: " << get_current_time() << ", NIC from Host[" << nic->src->id << "] NICProcessingEvent" << std::endl;
    }
    //nic->send_next_pkt();
    nic->send_pkts();
}

/* Qjump Epoch */
QjumpEpochEvent::QjumpEpochEvent(double time, Host *host, uint32_t priority)
        : Event(QJUMP_EPOCH, time) {
    this->time = time;
    this->host = host;
    this->priority = priority;
}

QjumpEpochEvent::~QjumpEpochEvent(){
}

void QjumpEpochEvent::process_event() {
    if (params.debug_event_info) {
        std::cout << "At time: " << get_current_time() << ", Host[" << host->id
            << "] process QjumpEpochEvent for Priority[" << priority << "]" << std::endl;
    }
    host->send_next_pkt(priority);
}

/* Rate Limiting */
RateLimitingEvent::RateLimitingEvent(double time, Flow *flow)
        : Event(RATE_LIMITING, time) {
    this->time = time;
    this->flow = flow;
}

RateLimitingEvent::~RateLimitingEvent() {
    if (flow->rate_limit_event == this) {
        flow->rate_limit_event = NULL;
    }
}

void RateLimitingEvent::process_event() {
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == flow->id)) {
        std::cout << "At time: " << get_current_time() << ", Flow[" << flow->id << "] process RateLimitingEvent with allocated_rate = ";
        std::cout << std::setprecision(2) << std::fixed;
        std::cout << flow->allocated_rate / 1e9 << std::endl;
        std::cout << std::setprecision(15) << std::fixed;
    }
    flow->send_next_pkt();
}

PDQProbingEvent::PDQProbingEvent(double time, PDQFlow *flow)
        : Event(PDQ_PROBING, time) {
    this->time = time;
    this->flow = flow; 
}

PDQProbingEvent::~PDQProbingEvent() {
    if (flow->probing_event == this) {
        flow->probing_event = NULL;
    }
}

void PDQProbingEvent::process_event() {
    flow->send_probe_pkt();
}
