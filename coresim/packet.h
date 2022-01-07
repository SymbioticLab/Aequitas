#ifndef CORESIM_PACKET_H
#define CORESIM_PACKET_H

#include <stdint.h>
#include <vector>

#define NORMAL_PACKET 0
#define ACK_PACKET 1
#define SYN_PACKET 2        // used by D3; SYN/First Rate Request packet
#define SYN_ACK_PACKET 11   // used by D3; ACK to the SYN/First RR packet
#define FIN_PACKET 12       // used by D3;
#define PROBE_PACKET 13     // used by PDQ
#define GRANT_PACKET 14     // used by Homa
#define RESEND_PACKET 15    // used by Homa

#define RTS_PACKET 3
#define CTS_PACKET 4
#define OFFER_PACKET 5
#define DECISION_PACKET 6
#define CAPABILITY_PACKET 7
#define STATUS_PACKET 8
#define FASTPASS_RTS 9
#define FASTPASS_SCHEDULE 10

class Flow;
class Host;
class Queue;

class Packet {

    public:
        Packet(double sending_time, Flow *flow, uint64_t seq_no, uint32_t pf_priority,
                uint32_t size, Host *src, Host *dst);
        virtual ~Packet();

        double sending_time;
        Flow *flow;
        uint64_t seq_no;
        uint32_t pf_priority;
        uint32_t size;
        Host *src;
        Host *dst;
        uint32_t unique_id;
        static uint32_t instance_count;
        int remaining_pkts_in_batch;
        int capability_seq_num_in_data;

        uint32_t type; // Normal or Ack packet
        double total_queuing_delay;
        double last_enque_time;

        int capa_data_seq;

        double v_finish_time;   // For WFQ impl
        double start_ts;    // For RTT measurement
        uint32_t num_hops;    // ACK pkt will update it as it traverse thru

        uint32_t enque_queue_size;    // in terms of # of bytes enqueued

        // for D3 &/ PDQ
        std::vector<double> curr_rates_per_hop;     // rate allocated in the current RTT per hop (queue) by the router
        double allocated_rate;                      // rate to use for the current RTT; assigned by the router (min of curr_rates_per_hop) via RRQ and sent via ACK or SYN_ACK pkt
        int hop_count;                              // used to index curr_rates_per_hop when calculation transmission delay
        double desired_rate;                        // desired rate for the current RTT; used by the router to assign rates
        double prev_allocated_rate;                 // past info used by the router; carried by RRQ pkt
        double prev_desired_rate;                   // past info used by the router; carried by RRQ pkt
        bool has_rrq;                               // true if it is an RRQ packet (SYN/FIN/DATA_pkt_with_rrq); router uses this flag to decide whether to enter rate_allocation algo
        bool marked_base_rate;                      // so that D3queue takes special care of it
        bool data_pkt_with_rrq;                     // whether a data pkt is piggybacked with an RRQ (rate request) packet
        bool ack_pkt_with_rrq;                      // true if it is an ack pkt of the data pkt piggbybacked with an RRQ
        bool ack_to_rrq_no_payload;                 // true if it is an ack pkt of the data RRQ packet whose payload is removed
        bool paused;                                // used by PDQ; same as "marked_base_rate" in D3
        uint32_t pause_sw_id;                       // record which switch paused the flow
        double deadline;                            // PDQ
        double measured_rtt;                        // PDQ
        double expected_trans_time;                 // PDQ
        double inter_probing_time;                  // PDQ
        bool is_probe;                              // PDQ
        bool ack_to_probe;                          // PDQ
        bool scheduled;                             // Homa
        std::vector<uint32_t> unscheduled_offsets;  // Homa
        bool is_sender_resend;                      // Homa
};

class PlainAck : public Packet {
    public:
        PlainAck(Flow *flow, uint64_t seq_no_acked, uint32_t size, Host* src, Host* dst);
};

class Ack : public Packet {
    public:
        Ack(Flow *flow, uint64_t seq_no_acked, std::vector<uint64_t> sack_list,
                uint32_t size,
                Host* src, Host *dst);
        uint32_t sack_bytes;
        std::vector<uint64_t> sack_list;
};

class Syn : public Packet {
    public:
        Syn(double sending_time, double desired_rate, Flow *flow, uint32_t size, Host *src, Host *dst);
};

class SynAck : public Packet {
    public:
        SynAck(Flow *flow, uint64_t seq_no_acked, uint32_t size, Host* src, Host* dst);
};

class Fin : public Packet {
    public:
        Fin(double sending_time, double prev_desired_rate, double prev_allocated_rate, Flow *flow, uint32_t size, Host *src, Host *dst);
};

class Grant : public Ack {
    public:
        Grant(Flow *flow, uint64_t seq_no_acked, uint32_t size, Host *src, Host *dst, int grant_priority);
        int grant_priority;
};

class Resend : public Ack {
    public:
        Resend(Flow *flow, uint64_t seq_no_acked, uint32_t size, Host *src, Host *dst, int grant_priority);
        int grant_priority;
};

class RTSCTS : public Packet {
    public:
        //type: true if RTS, false if CTS
        RTSCTS(bool type, double sending_time, Flow *f, uint32_t size, Host *src, Host *dst);
};

class RTS : public Packet{
    public:
        RTS(Flow *flow, Host *src, Host *dst, double delay, int iter);
        double delay;
        int iter;
};

class OfferPkt : public Packet{
    public:
        OfferPkt(Flow *flow, Host *src, Host *dst, bool is_free, int iter);
        bool is_free;
        int iter;
};

class DecisionPkt : public Packet{
    public:
        DecisionPkt(Flow *flow, Host *src, Host *dst, bool accept);
        bool accept;
};

class CTS : public Packet{
    public:
        CTS(Flow *flow, Host *src, Host *dst);
};

class StatusPkt : public Packet{
    public:
        StatusPkt(Flow *flow, Host *src, Host *dst, int num_flows_at_sender);
        double ttl;
        bool num_flows_at_sender;
};

#endif  // CORESIM_PACKET_H

