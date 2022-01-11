#ifndef EXT_HOMA_FLOW_H
#define EXT_HOMA_FLOW_H

#include "../coresim/flow.h"

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
        void send_resend_pkt(uint64_t seq, int grant_priority, bool is_sender_resend);
        Packet *send_with_delay(uint64_t seq, double delay, uint64_t end_seq_no, bool scheduled, int priority);
        void receive(Packet *p) override;
        void receive_data_pkt(Packet* p) override;
        void receive_grant_pkt(Packet *p);
        void receive_resend_pkt(Packet *p);
        void set_timeout(double time) override;
        void handle_timeout() override;
        void set_timeout_sender(double time) override;
        void handle_timeout_sender() override;
        void resend_grant() override;
    private:
        int grant_priority;
        std::vector<uint32_t> unscheduled_offsets;
        uint32_t offset_under_curr_grant_send;  // maintained by sender
        uint32_t offset_under_curr_grant_recv;  // maintained by receiver

};

#endif  // EXT_HOMA_FLOW_H
