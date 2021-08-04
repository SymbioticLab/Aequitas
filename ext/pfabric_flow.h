#ifndef EXT_PFABRIC_FLOW_H
#define EXT_PFABRIC_FLOW_H

#include <cstdint>

#include "../coresim/flow.h"

class Packet;

class PFabricFlow : public Flow {
 public:
  PFabricFlow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d,
              uint32_t flow_priority);
  void start_flow() override;
  void send_pending_data() override;
  uint32_t send_pkts() override;
  Packet *send(uint64_t seq) override;
  Packet *send_with_delay(uint64_t seq, double delay);
  void send_ack(uint64_t seq, std::vector<uint64_t> sack_list,
                double pkt_start_ts) override;
  void receive_ack(uint64_t ack, std::vector<uint64_t> sack_list,
                   double pkt_start_ts, uint32_t priority,
                   uint32_t num_hops) override;
  void increase_cwnd() override;
  void handle_timeout() override;
  uint32_t get_pfabric_priority(uint64_t seq);
 private:
  uint32_t ssthresh;
  uint32_t count_ack_additive_increase;
  std::vector<uint32_t> priority_thresholds;  // used for pFabric with limited number of priority queues
};

#endif  // EXT_PFABRIC_FLOW_H
