#ifndef CORESIM_CHANNEL_H
#define CORESIM_CHANNEL_H

#include <cstdint>
#include <deque>
#include <vector>
#include <unordered_map>

class Flow;
class Host;
class Packet;
class ChannelRetxTimeoutEvent;
class AggChannel;

/* Channel: a single direction src-dst pair per QoS */
// Handles (1) Transport in packet level (2) CC (3) Veritas admission control
// TODO(yiwenzhang): cleanup methods & variables that are no longer in use / needed
class Channel {
    public:
        Channel(uint32_t id, Host *s, Host *d, uint32_t priority, AggChannel *agg_channel);
        virtual ~Channel();

        void add_to_channel(Flow *flow);   // called by a flow to add its packets to the channel
        virtual void send_pkts();        // handle transport for flows (packet level)
        virtual Packet *send_one_pkt(uint64_t seq, uint32_t pkt_size, double delay, Flow *flow);    // send a packet with tiny delay
        int nic_send_next_pkt();    // use nic to allow channel to proceed with transport for flows (one packet each time)
        Flow *find_next_flow(uint64_t seq);
        void receive_data_pkt(Packet* p);
        void send_ack(uint64_t seq, std::vector<uint64_t> sack_list, double pkt_start_ts, Flow *flow);
        void receive_ack(uint64_t ack, Flow *flow, std::vector<uint64_t> sack_list, double pkt_start_ts);
        void cleanup_after_finish(Flow *flow);

        void set_timeout(double time);
        void handle_timeout();
        void cancel_retx_event();

        double get_admit_prob();
        void update_fct(double fct_in, uint32_t flow_id, double update_time, int flow_size);
        void additive_increase_on_ACK();
        void multiplicative_decrease_on_ACK(double delay);
        void reset_on_RTO();
        void multiplicative_decrease_on_RTO();
        void adjust_cwnd_on_ACK(double delay);
        void adjust_cwnd_on_RTO();
        void report_ack(double delay);  // flows inform Channel when receiving a new ACK
        void report_timeout(Flow *flow);  // flows inform Channel when a timeout happens

        //void window_insert(double fct_in, uint32_t flow_id, int flow_size);

        uint32_t id;
        uint32_t priority;
        Host *src;
        Host *dst;

        // Transport related
        uint64_t next_seq_no;
        uint64_t last_unacked_seq;
        uint64_t end_seq_no;
        std::deque<Flow *> outstanding_flows;
        uint32_t scoreboard_sack_bytes;
        std::unordered_map<uint64_t, bool> received;
        uint64_t received_bytes;
        uint64_t recv_till;
        uint64_t max_seq_no_recv;

        // CC related
        double cwnd;            // for intermediate calculation
        double mss;
        uint32_t cwnd_mss;      // actual cwnd used in CC; value = floor(cwnd); 
        uint32_t max_cwnd;
        uint32_t hdr_size;
        double retx_timeout;
        ChannelRetxTimeoutEvent *retx_event;
        double fct;             // most recent rpc fct
        double rtt;             // used to enforce per rtt MD; in us; keep updated with new delay
        double last_update_time;
        double last_decrease_ts;    // time when the most recent MD takes place
        double ai;
        double beta;
        double max_mdf;
        uint32_t retrans_cnt;
        uint32_t retrans_reset_thresh;

        AggChannel *agg_channel;

};

#endif  // CORESIM_CHANNEL_H
