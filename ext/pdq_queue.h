#ifndef EXT_PDQ_QUEUE_H
#define EXT_PDQ_QUEUE_H

#include "../coresim/queue.h"

class Packet;
class Flow;

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
    void perform_flow_control(Packet *packet);
    void perform_rate_control(Packet *packet);
    
    uint32_t flow_idx;    // used to manage ring buffer 'active_flows'
    std::vector<Flow *> active_flows;
};

#endif  // EXT_PDQ_QUEUE_H
