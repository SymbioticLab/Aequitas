#ifndef EXT_HOMA_FLOW_H
#define EXT_HOMA_FLOW_H

#include "../coresim/flow.h"

#define RTTbytes 100*1024       // assuming 100Gbps network

class Packet;

class HomaFlow : public Flow {
    public:
        HomaFlow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d,
            uint32_t flow_priority);
        void start_flow() override;
        int send_unscheduled_data();
        void send_pending_data() override;
        Packet *send_with_delay(uint64_t seq, double delay, uint64_t end_seq_no, int priority);
        void receive_data_pkt(Packet* p) override;
        void receive_ack(uint64_t ack, std::vector<uint64_t> sack_list,
                        double pkt_start_ts, uint32_t priority,
                        uint32_t num_hops) override;

};

#endif  // EXT_HOMA_FLOW_H
