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
        int get_unscheduled_priority();
        int send_unscheduled_data();
        int send_scheduled_data();
        void send_grant_pkt(uint64_t seq, double start_pkt_ts, int grant_priority);
        void send_pending_data() override;
        Packet *send_with_delay(uint64_t seq, double delay, uint64_t end_seq_no, bool scheduled, int priority);
        void receive(Packet *p) override;
        void receive_data_pkt(Packet* p) override;
        void receive_grant_pkt(Packet *p);
    private:
        int grant_priority;
        std::vector<uint32_t> unscheduled_offsets;

};

#endif  // EXT_HOMA_FLOW_H
