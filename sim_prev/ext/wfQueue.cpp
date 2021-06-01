#include "wfQueue.h"
#include "../run/params.h"

#include <assert.h>
#include <iostream>
#include <limits.h>

extern double get_current_time();
extern void add_to_event_queue(Event *ev);
extern DCExpParams params;

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

        this->buffer_carving = params.buffer_carving;
        this->has_buffer_carving = buffer_carving.empty() ? false : true;
        if (has_buffer_carving) {
            assert(buffer_carving.size() == num_priorities);
        } else {
            //std::cout << "No buffer carving." << std::endl;
        }
    }

// Enque:
// Normal behavior: push back to the end of the queue.
// When queue if full: drop tail. If there is buffer carving,
// drop tail according to packet's corresponding priority queue.
/*
void WFQueue::enque(Packet *packet) {
    //std::cout << "PUPU: Enque packet_prio=" << packet->pf_priority << std::endl;
    assert(packet->pf_priority <= num_priorities);
    p_arrivals += 1;
    b_arrivals += packet->size;
    if (packet->type == NORMAL_PACKET) {
        //std::cout << "At time: " << get_current_time() <<", WFQ<" << id << "> [Enque] packet[" << packet->unique_id << "] of size " << packet->size << " to priority queue " << packet->pf_priority << std::endl;
        std::cout << "At time: " << get_current_time() <<", WFQ<" << id << "> [Enque] packet[" << packet->unique_id << "](" << packet->pf_priority << ")" << std::endl;
    } else {
        assert(false);
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
        }
    } else {
        // TODO: implement buffer carving feature.
    }
}
*/

void WFQueue::update_vtime(Packet *packet, int prio) {
    double v_start_time = std::max(get_current_time(), last_v_finish_time[prio]);
    packet->v_finish_time = v_start_time + get_transmission_delay(packet->size) / (static_cast<double>(weights[prio])/sum_weights);
    last_v_finish_time[prio] = packet->v_finish_time;
    //std::cout << "PUPU: update packet virtual finish time = " << packet->v_finish_time << std::endl;
}

// Enque ver 2: adding virtual time
void WFQueue::enque(Packet *packet) {
    assert(packet->pf_priority <= num_priorities);
    p_arrivals += 1;
    b_arrivals += packet->size;
    /*
    if (packet->type == NORMAL_PACKET) {
        //std::cout << "At time: " << get_current_time() <<", WFQ<" << id << "> [Enque] packet[" << packet->unique_id << "] of size " << packet->size << " to priority queue " << packet->pf_priority << std::endl;
        std::cout << "At time: " << get_current_time() <<", WFQ<" << id << "> [Enque] packet<NORMAL>[" << packet->unique_id << "](" << packet->pf_priority << ")" << std::endl;
    } else if (packet->type == ACK_PACKET) {
        std::cout << "At time: " << get_current_time() <<", WFQ<" << id << "> [Enque] packet<ACK>[" << packet->unique_id << "](" << packet->pf_priority << ")" << std::endl;
    }
    */

    if (!has_buffer_carving) {
        //std::cout << "bytes_in_queue = " << bytes_in_queue << ", pkt size = " << packet->size << ", limit_bytes = " << limit_bytes << std::endl;
        if (bytes_in_queue + packet->size <= limit_bytes) {
            prio_queues[packet->pf_priority].push_back(packet);
            bytes_in_queue += packet->size;
            bytes_per_prio[packet->pf_priority] += packet->size;
            packet->last_enque_time = get_current_time();
        } else {
            pkt_drop++;
            drop(packet);
        }
    } else {
        // TODO: implement buffer carving feature.
    }

    update_vtime(packet, packet->pf_priority);
}


void WFQueue::increment_curr_pos() {
    curr_pos += 1;
    if (curr_pos == sum_weights) {
        curr_pos = 0;
    }
    //std::cout << "Calling increment_curr_pos(). curr_pos is now " << curr_pos << std::endl;
}

void WFQueue::set_curr_pos(int pos) {
    curr_pos = pos;
    if (curr_pos == sum_weights) {
        curr_pos = 0;
    }
}

void WFQueue::printQueueStatus() {
    std::cout << "**********[WF Queue [" << id << "] Current Status]**********" << std::endl;
    for (int i = 0; i < num_priorities; i++) {
        std::cout << "Priority " << i << ": " << prio_queues[i].size() << " packets "
            << "with total size of " << bytes_per_prio[i] << " bytes." << std::endl;
    }
    std::cout << "**********[End of WF Queue[" << id << "] Status]***********" << std::endl;
}

// Deque: weighted-fair departure based on qos weights
/*
Packet* WFQueue::deque() {
    Packet *p = NULL;
    if (id != 1001) return p;
    //std::cout << "initial: curr_pos = " << curr_pos << std::endl;
    //std::cout << "initial: bytes_in_queue = " << bytes_in_queue << std::endl;
    //printQueueStatus();
    if (bytes_in_queue > 0) {
        int accumulated_weights = 0;
        int serve_prio = 0;
        //bool found_packet = false;
        int level = 0;
        //while (!found_packet) {
        while (true) {
            if (curr_pos < accumulated_weights + weights[level]) {
                if (prio_queues[level].size() != 0) {
                    serve_prio = level;
                    increment_curr_pos();
                    //std::cout << "PUPU serve_prio = " << serve_prio << "; increment curr_pos and it is now at " << curr_pos << std::endl;
                    //found_packet = true;
                    break;
                } else {    // if no packets, skip and jump to the next level
                    set_curr_pos(accumulated_weights + weights[level]);
                    //std::cout << "Changing curr_pos to " << curr_pos << std::endl;
                }
            }
            accumulated_weights += weights[level];
            level++;
            //std::cout << "level = " << level << std::endl;
            if (level == num_priorities) {
                level = 0;
                accumulated_weights = 0;
                assert(curr_pos == 0);  // at this stage curr_pos would have been reset to 0 in set_curr_pos()
            }
        }
        //std::cout << "after loop: curr_pos = " << curr_pos << std::endl;
        //std::cout << "after loop: serve_prio = " << serve_prio << std::endl;
        assert(!prio_queues[serve_prio].empty());
        p = prio_queues[serve_prio].front();
        prio_queues[serve_prio].pop_front();
        assert(p);  // must have found one packet if there are bytes left in queues
        assert(p->pf_priority == serve_prio);
        bytes_in_queue -= p->size;
        bytes_per_prio[p->pf_priority] -= p->size;
        p_departures += 1;
        b_departures += p->size;
        //std::cout << "At time: " << get_current_time() << ", WFQ<" << id << "> [Deque] packet[" << p->unique_id << "] of size " << p->size << " from priority queue " << serve_prio << std::endl;
        std::cout << "At time: " << get_current_time() << ", WFQ<" << id << "> [Deque] packet[" << p->unique_id << "](" << serve_prio << ")" << std::endl;
        //printQueueStatus();

        p->total_queuing_delay += get_current_time() - p->last_enque_time;
        std::cout << "packet[" << p->unique_id << "](" << p->pf_priority << ") total_queuing_delay = " << p->total_queuing_delay << std::endl;
        if (p->type ==  NORMAL_PACKET){
            //// Only count queuing delay when it is a normal data packet  (not an ACK packet)
            ////p->total_queuing_delay += get_current_time() - p->last_enque_time;
            //std::cout << "Deque an DATA PKT, queuing delay = " << p->total_queuing_delay << std::endl;
            if(p->flow->first_byte_send_time < 0)
                p->flow->first_byte_send_time = get_current_time();
            if(this->location == 0)
                p->flow->first_hop_departure++;
            if(this->location == 3)
                p->flow->last_hop_departure++;
        }
    } else {    //// even if nothing is in queue, will also move curr_pos when deque() is called
        std::cout << "At time: " << get_current_time() << ", [Deque] in queue[" << id << "] found nothing but increment pos" << std::endl;
        increment_curr_pos();
    }

    return p;
}
*/

int WFQueue::select_prio() {
    int prio = -1;
    double min_v_finish_time = std::numeric_limits<double>::max();
    for (int i = 0; i < num_priorities; i++) {
        if (prio_queues[i].empty()) { continue; }
        Packet *head = prio_queues[i].front();
        //if (unique_id == 2)
        //    std::cout << "prio[" << i<< "]head v_finish_time = " << head->v_finish_time << std::endl;
        //TODO: go back and check why "std::numeric_limits<double>::epsilon()" doesn't work out here (when early_pkt_in_highest_prio is false)
        if (head->v_finish_time < min_v_finish_time && std::fabs(head->v_finish_time - min_v_finish_time) > SLIGHTLY_SMALL_TIME) {
        //if (head->v_finish_time < min_v_finish_time && std::fabs(head->v_finish_time - min_v_finish_time) > std::numeric_limits<double>::epsilon()) {
            prio = i;
            min_v_finish_time = head->v_finish_time;
        }
    }
    //if (unique_id == 2)
    //    std::cout << "pick prio = " << prio << std::endl;
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
            //if (prio_queues[i].front()->v_finish_time == min_v_finish_time) {
            if (std::fabs(prio_queues[i].front()->v_finish_time - min_v_finish_time) < 0.01) {
                min_vtime_candidates.push_back(i);
            }
        }
        if (min_vtime_candidates.size() > 1) {
            int random_idx = rand() % min_vtime_candidates.size();
            prio = min_vtime_candidates[random_idx];
        } 
    }
    //if (prio == 0) {
    //    prio0_select_cnt++;
    //} else {
    //    prio1_select_cnt++;
    //}
    //if (unique_id == 2)
    //    std::cout << "prio 0 cnt = " << prio0_select_cnt << "; prio 1 cnt = " << prio1_select_cnt << std::endl;

    assert(prio >= 0);
    return prio;
}

// Deque ver 2: adding virtual time
Packet* WFQueue::deque() {
    Packet *p = NULL;
    //TODO: later remove this hack when moving to large scale exp
    //if (id != 1001) return p;
    //std::cout << "initial: curr_pos = " << curr_pos << std::endl;
    //std::cout << "initial: bytes_in_queue = " << bytes_in_queue << std::endl;
    //printQueueStatus();
    if (bytes_in_queue > 0) {
        int serve_prio = select_prio();
        assert(!prio_queues[serve_prio].empty());
        p = prio_queues[serve_prio].front();
        prio_queues[serve_prio].pop_front();
        assert(p);  // must have found one packet if there are bytes left in queues
        assert(p->pf_priority == serve_prio);
        bytes_in_queue -= p->size;
        bytes_per_prio[p->pf_priority] -= p->size;
        p_departures += 1;
        b_departures += p->size;
        //std::cout << "At time: " << get_current_time() << ", WFQ<" << id << "> [Deque] packet[" << p->unique_id << "] of size " << p->size << " from priority queue " << serve_prio << std::endl;
        //printQueueStatus();

        p->total_queuing_delay += get_current_time() - p->last_enque_time;
        //std::cout << "At time: " << get_current_time() << ", WFQ<" << id << "> [Deque] packet[" << p->unique_id << "](" << serve_prio << ")" << std::endl;
        //std::cout << "packet[" << p->unique_id << "](" << p->pf_priority << ") total_queuing_delay = " << p->total_queuing_delay << std::endl;
        if (p->type ==  NORMAL_PACKET){
            //// Only count queuing delay when it is a normal data packet  (not an ACK packet)
            ////p->total_queuing_delay += get_current_time() - p->last_enque_time;
            //std::cout << "Deque an DATA PKT, queuing delay = " << p->total_queuing_delay << std::endl;
            if(p->flow->first_byte_send_time < 0)
                p->flow->first_byte_send_time = get_current_time();
            if(this->location == 0)
                p->flow->first_hop_departure++;
            if(this->location == 3)
                p->flow->last_hop_departure++;
        }
    }

    return p;
}

// Flush everything in the WFQ queue
void WFQueue::flush() {
    std::cout << "PUPU! Flushing happens at time: " << get_current_time() << std::endl;
    for (int i = 0; i < weights.size(); i++) {
        prio_queues[i].clear();
        bytes_per_prio[i] = 0;
        //last_v_finish_time[i] = 0;
        last_v_finish_time[i] = -std::numeric_limits<double>::max();
    }
    bytes_in_queue = 0;
}

// Queue:drop() is basically bookkeeping for individual Packet. As a result,
// we can still directly use it altough we have multiple queues now.
void WFQueue::drop(Packet *packet) {
    pkt_drop_per_prio[packet->pf_priority] += 1;
    Queue::drop(packet);
}
