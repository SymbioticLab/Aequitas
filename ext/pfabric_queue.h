#ifndef EXT_PFABRIC_QUEUE_H
#define EXT_PFABRIC_QUEUE_H

#include "../coresim/queue.h"

class Packet;

class PFabricQueue : public Queue {
 public:
  PFabricQueue(uint32_t id, double rate, uint32_t limit_bytes, int location);
  void enque(Packet *packet) override;
  Packet *deque(double deque_time) override;
};

#endif  // EXT_PFABRIC_QUEUE_H
