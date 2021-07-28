#ifndef EXT_D3_QUEUE_H
#define EXT_D3_QUEUE_H

#include "../coresim/queue.h"

class Packet;

// D3 Queue is a smart router/switch that will handle packets based on the flow rates
// assigned by D3 hosts. It does its own rate allocation using the algorithm given in
// the paper. The router does not keep per-flow state (no notion of 'flow')
class D3Queue : public Queue {
  public:
    D3Queue(uint32_t id, double rate, uint32_t limit_bytes, int location);
    void enque(Packet *packet) override;
    Packet *deque(double deque_time) override;
    double get_transmission_delay(Packet *packet) override;
    void drop(Packet *packet) override;     // check if a SYN pkt gets dropped
    void dec_num_flows();
    void allocate_rate(Packet *packet);
    
    double demand_counter;
    double allocation_counter;
    double base_rate;
};

#endif  // EXT_D3_QUEUE_H
