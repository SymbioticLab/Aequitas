#ifndef EXT_D3_FLOW_H
#define EXT_D3_FLOW_H

#include <cstdint>

#include "../coresim/flow.h"

class Packet;

class D3Flow : public Flow {
  public:
    D3Flow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d,
                uint32_t flow_priority);
    void start_flow() override;
    void send_syn_pkt();
    void send_fin_pkt();
    double calculate_desired_rate();
    void send_next_pkt() override;
    void send_pending_data() override;
    uint32_t send_pkts() override;
    Packet *send_with_delay(uint64_t seq, double delay);
    void receive(Packet* p) override;
    void receive_data_pkt(Packet* p) override;
    void receive_syn_pkt(Packet *syn_pkt);
    void receive_syn_ack_pkt(Packet *p);
    void receive_fin_pkt(Packet *p);
    void send_ack_d3(uint64_t seq, std::vector<uint64_t> sack_list,
                  double pkt_start_ts, Packet* data_pkt); // to replace the original send_ack
    void receive_ack_d3(Ack *ack_pkt, uint64_t ack,
                  std::vector<uint64_t> sack_list); // to replace the original receive_ack
    void handle_timeout() override;
    void set_timeout(double time) override;

    bool has_sent_rrq_this_rtt;   // whether the RRQ is sent (piggybacked in the first data pkt) during the current RTT
    bool assigned_base_rate;      // when true, host will send a header-only RRQ for this RTT
};

#endif  // EXT_D3_FLOW_H
