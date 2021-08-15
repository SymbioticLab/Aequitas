#ifndef EXT_PDQ_FLOW_H
#define EXT_PDQ_FLOW_H

#include <cstdint>

#include "../coresim/flow.h"

class Packet;

class PDQFlow : public Flow {
  public:
    PDQFlow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d,
                uint32_t flow_priority);
    void start_flow() override;
    Packet *send_probe_pkt();
    void send_syn_pkt();
    void send_fin_pkt();
    void send_next_pkt() override;
    Packet *send_with_delay(uint64_t seq, double delay);
    void send_pending_data() override;
    uint32_t send_pkts() override;
    void receive(Packet* p) override;
    void receive_syn_pkt(Packet *syn_pkt);
    void receive_syn_ack_pkt(Packet *p);
    //void receive_data_pkt(Packet* p) override;
    void receive_fin_pkt(Packet *p);
    //void send_ack_pdq(uint64_t seq, std::vector<uint64_t> sack_list,
    //              double pkt_start_ts, Packet* data_pkt); // to replace the original send_ack
    //void receive_ack_pdq(Ack *ack_pkt, uint64_t ack,
    //              std::vector<uint64_t> sack_list); // to replace the original receive_ack

    bool has_sent_probe_this_rtt;   // like what we did in D3; so that we don't send too many before the next ACK (next RTT)
    //bool paused;                  // not necessary; can tell from whether allocated_rate == 0 (hopefully)
    uint32_t pause_sw_id;
    double inter_probing_time;      // set by the switch; not host/flow
    double measured_rtt;

};

#endif  // EXT_PDQ_FLOW_H
