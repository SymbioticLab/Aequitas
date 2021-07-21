#include "wf_queue.h"

#include <assert.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <unordered_set>

#include "../coresim/channel.h"
#include "../coresim/flow.h"
#include "../coresim/packet.h"
#include "../coresim/node.h"
#include "../run/params.h"


extern double get_current_time();
extern void add_to_event_queue(Event *ev);
extern DCExpParams params;
extern uint32_t num_pkt_drops;
extern uint32_t pkt_drops_agg_switches;
extern uint32_t pkt_drops_core_switches;
extern uint32_t pkt_drops_host_queues;
extern std::vector<uint32_t> pkt_drops_per_agg_sw;
extern std::vector<uint32_t> pkt_drops_per_host_queues;
extern std::vector<uint32_t> pkt_drops_per_prio;
extern std::vector<std::vector<double>> per_prio_qd;
extern std::vector<double> switch_max_inst_load;
extern std::vector<std::vector<uint32_t>> num_outstanding_rpcs_one_sw;
double measure_intval = 100 * 1e-6;    // 100 us

/* Weighted-Fair Queue */
WFQueue::WFQueue(uint32_t id, double rate, uint32_t limit_bytes, int location)
    : Queue(id, rate, limit_bytes, location) {
        this->num_priorities = params.weights.size();
        assert(num_priorities > 1);
        this->weights = params.weights;
        this->sum_weights = params.sum_weights;
        this->curr_pos = 0;

        this->prio_queues.resize(num_priorities);
        this->bytes_per_prio = std::vector<int>(num_priorities, 0);
        this->pkt_drop_per_prio = std::vector<int>(num_priorities, 0);
        this->last_v_finish_time = std::vector<double>(num_priorities, -std::numeric_limits<double>::max());
        //this->last_v_finish_time = std::vector<double>(num_priorities, 0);
        this->served_pkts_per_prio = std::vector<uint32_t>(num_priorities, 0);

        this->last_queuing_delay = std::vector<double>(num_priorities, 0);

        this->max_inst_load = 0;
        this->interval_start = 0;
        this->bytes_in_interval = 0;

        this->outstd_interval_start = 0;

        this->buffer_carving = params.buffer_carving;
        this->has_buffer_carving = buffer_carving.empty() ? false : true;
        if (has_buffer_carving) {
            assert(buffer_carving.size() == num_priorities);
        } else {
            //std::cout << "No buffer carving." << std::endl;
        }
    }


void WFQueue::update_vtime(Packet *packet, int prio) {
    assert(!last_v_finish_time.empty());
    double v_start_time = std::max(packet->last_enque_time, last_v_finish_time[prio]);
    packet->v_finish_time = v_start_time + get_transmission_delay(packet) / (static_cast<double>(weights[prio])/sum_weights);
    last_v_finish_time[prio] = packet->v_finish_time;
}

void WFQueue::enque(Packet *packet) {
    assert(packet->pf_priority <= num_priorities);
    served_pkts_per_prio[packet->pf_priority]++;      // note this also includes retransmitted packets

    p_arrivals += 1;
    b_arrivals += packet->size;
    packet->enque_queue_size = b_arrivals;

    if (measure_inst_load) {
        if (get_current_time() - interval_start < params.load_measure_interval) {
            bytes_in_interval += packet->size;
        } else {
            //std::cout << "bytes_in_interval = " << bytes_in_interval << std::endl;
            double inst_load = (double)bytes_in_interval * 8 /1e9 / params.load_measure_interval;
            //std::cout << "inst load = " << inst_load << std::endl;
            if (inst_load > max_inst_load) {
                max_inst_load = inst_load;
                switch_max_inst_load[id] = inst_load;
            }
            interval_start = get_current_time();
            bytes_in_interval = 0;
        }
    }

    if (!has_buffer_carving) {
        if (bytes_in_queue + packet->size <= limit_bytes) {
            prio_queues[packet->pf_priority].push_back(packet);
            bytes_in_queue += packet->size;
            bytes_per_prio[packet->pf_priority] += packet->size;
            packet->last_enque_time = get_current_time();
        } else {
            pkt_drop++;
            drop(packet);
            return;
        }
    } else {
        // TODO: implement buffer carving feature. Remember to check whether you need to return and skip 'update_vtime' here also
        exit(1);
    }

    //if (measure_inst_load && id == 0 & packet->size > 40) {
    //    std::cout << "At time: " << get_current_time() << ", packet[" << packet->unique_id
    //            << "] from Flow[" << packet->flow->id << "] from Host[" << packet->flow->src->id << "] arrives. Bytes in queue = " << bytes_in_queue << std::endl;
    //}

    update_vtime(packet, packet->pf_priority);

    // log outstanding rpcs
    /*
    if (measure_inst_load && id == 0) {    // measure_inst_load is used here to exclude the other host queue
        uint32_t sum_qos_h_and_m = 0;
        for (int i = 0; i < params.num_qos_level; i++) {
            std::unordered_set<uint32_t> count_set;
            for (auto const &x : prio_queues[i]) {
                count_set.insert(x->flow->id);
            }
            num_outstanding_rpcs_one_sw[i].push_back(count_set.size());
            if (i == 0 || i == 1) {
                sum_qos_h_and_m += count_set.size();
            }
        }
        num_outstanding_rpcs_one_sw.back().push_back(sum_qos_h_and_m);
    }
    */
}


void WFQueue::printQueueStatus() {
    std::cout << "**********[WF Queue [" << id << "] Current Status]**********" << std::endl;
    for (int i = 0; i < num_priorities; i++) {
        std::cout << "Priority " << i << ": " << prio_queues[i].size() << " packets "
            << "with total size of " << bytes_per_prio[i] << " bytes." << std::endl;
    }
    std::cout << "**********[End of WF Queue[" << id << "] Status]***********" << std::endl;
}


int WFQueue::select_prio() {
    int prio = -1;
    double min_v_finish_time = std::numeric_limits<double>::max();
    for (int i = 0; i < num_priorities; i++) {
        if (prio_queues[i].empty()) { continue; }
        Packet *head = prio_queues[i].front();
        //if (head->v_finish_time < min_v_finish_time && std::fabs(head->v_finish_time - min_v_finish_time) > SLIGHTLY_SMALL_TIME) {        // no longer using std::fabs
        if (head->v_finish_time < min_v_finish_time) {
            prio = i;
            min_v_finish_time = head->v_finish_time;
        }
    }
    // another loop to find all eqaul minimums and break ties randomly
    // this is to ensure fairness for WFQ where all weights are equal
    // Note: for a better "looking" simulation result, this random tie breaking will only apply to the case where all weights are equal
    // Note2: we will keep it here to check every time select_prio() gets called in case we do some dynamic qos ratio change exp in the future
    bool check_equal = true;
    for (int i = 0; i < weights.size(); i++) {
        if (weights[i] != weights[0]) {
            check_equal = false;
            break;
        }
    }
    if (check_equal) {
        std::vector<double> min_vtime_candidates;
        for (int i = 0; i < num_priorities; i++) {
            if (prio_queues[i].empty()) { continue; }
            if (std::fabs(prio_queues[i].front()->v_finish_time - min_v_finish_time) < 0.01) {
                min_vtime_candidates.push_back(i);
            }
        }
        if (min_vtime_candidates.size() > 1) {
            int random_idx = rand() % min_vtime_candidates.size();
            prio = min_vtime_candidates[random_idx];
        }
    }

    assert(prio >= 0);
    return prio;
}

Packet* WFQueue::deque(double deque_time) {
    Packet *p = NULL;
    if (bytes_in_queue > 0) {
        int serve_prio = select_prio();
        assert(serve_prio < params.weights.size());
        assert(!prio_queues[serve_prio].empty());
        p = prio_queues[serve_prio].front();
        prio_queues[serve_prio].pop_front();
        assert(p);  // must have found one packet if there are bytes left in queues
        assert(p->pf_priority == serve_prio);
        bytes_in_queue -= p->size;
        bytes_per_prio[p->pf_priority] -= p->size;
        p_departures += 1;
        b_departures += p->size;
        p->total_queuing_delay += get_current_time() - p->last_enque_time;
        if (p->type == NORMAL_PACKET) {
            last_queuing_delay[p->pf_priority] = (get_current_time() - p->last_enque_time) * 1e6;    // update queue's per-priority last_queuing_delay (in us) based on packet's priority
            if (!params.disable_pkt_logging) {
                per_prio_qd[p->pf_priority].push_back(last_queuing_delay[p->pf_priority]);
            }
        }
        if (p->type ==  NORMAL_PACKET){
            assert(p->flow);
            if(p->flow->first_byte_send_time < 0)
                p->flow->first_byte_send_time = get_current_time();
            if(this->location == 0)
                p->flow->first_hop_departure++;
            if(this->location == 3)
                p->flow->last_hop_departure++;
        }
        //if (measure_inst_load && id == 0) {
        //    std::cout << "At time: " << get_current_time() << ", dequeued a packet[" << p->unique_id << "] (size=" << p->size << ") from Flow[" << p->flow->id << "]. Bytes in queue = " << bytes_in_queue << std::endl;
        //}
    }


    return p;
}

void WFQueue::drop(Packet *packet) {
    pkt_drop_per_prio[packet->pf_priority] += 1;
    num_pkt_drops += 1;

    if (params.big_switch == 0) {
        if (src->type == HOST) {
            pkt_drops_host_queues += 1;
            pkt_drops_per_host_queues[((Host *) src)->id] += 1;
        } else if (src->type == SWITCH) {
            if (((Switch *) src)->switch_type == AGG_SWITCH) {
                pkt_drops_agg_switches += 1;
                pkt_drops_per_agg_sw[((Switch *) src)->id] += 1;
            } else if (((Switch *) src)->switch_type == CORE_SWITCH) {
                pkt_drops_core_switches += 1;
            }
        }
    }

    pkt_drops_per_prio[packet->pf_priority] += 1;
    Queue::drop(packet);
}
