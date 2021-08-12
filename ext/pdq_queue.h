#ifndef EXT_PDQ_QUEUE_H
#define EXT_PDQ_QUEUE_H

#include <map>
#include <queue>

#include "../coresim/queue.h"

class Packet;
class Flow;

class MoreCritical {
  public:
    bool operator() (Flow *a, Flow *b);
};

// PDQ Queue (switch) maintains per-flow states (up to 2k) obtained from packets and
// use these info to resolve flow contention by assigning rate to most critical flows
// (and preempt other less critical ones).
class PDQQueue : public Queue {
  public:
    PDQQueue(uint32_t id, double rate, uint32_t limit_bytes, int location);
    void enque(Packet *packet) override;
    Packet *deque(double deque_time) override;
    double get_transmission_delay(Packet *packet) override;
    void drop(Packet *packet) override;     // check if a SYN pkt gets dropped
    void dec_num_flows();
    void allocate_rate(Packet *packet);
    void add_flow_to_list(Packet *packet);
    void remove_flow_from_list(Packet *packet);
    void remove_least_critical_flow();
    bool more_critical(Flow *a, Flow *b);
    double calculate_RCP_fair_share_rate();
    double calculate_available_bandwidth(Packet *packet);
    void perform_flow_control(Packet *packet);
    void perform_rate_control(Packet *packet);
    
    MoreCritical flow_comp;           // used by more_critical();
    uint32_t constant_k;              // number of sending flows; used as threshold to remove the least critical flow in "Algorithm 1"
    uint32_t max_num_active_flows;    // default to 2 * constant_k (following the original paper)
    std::map<uint32_t, Flow*> active_flows;   // use a map to search/remove flows more efficiently; use ordered map to fit "Algorithm 2"
    std::priority_queue<Flow*, std::vector<Flow*>, MoreCritical> active_flows_pq; // maintain a pq to remove the least critical flow efficiently
    double dampening_time_window;
    double time_accept_last_flow;
    uint32_t constant_early_start;    // constant "K" used in "Algorithm 2"; default to 2 according to PDQ paper
};

#endif  // EXT_PDQ_QUEUE_H
