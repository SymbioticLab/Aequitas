#ifndef CORESIM_FLOW_H
#define CORESIM_FLOW_H

#include <unordered_map>
#include <map>
#include <vector>

class Packet;
class Ack;
class Host;
class Probe;
class RetxTimeoutEvent;
class RetxTimeoutSenderEvent;
class FlowProcessingEvent;
class Channel;
class AggChannel;
class RateLimitingEvent;

// TODO: consider if we need a vector of FlowState so that each switch in the path has independent flow states
class FlowState {   // used by PDQ
  public:
    FlowState();

    double rate;
    bool paused;
    uint32_t pause_sw_id;
    bool has_ddl;
    double deadline;                    // in unit of sec
    double expected_trans_time;         // in unit of sec
    double measured_rtt;                // in unit of sec
    double inter_probing_time;          // in unit of sec
    bool removed_from_pq;
};

class Flow {
    public:
        Flow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d);
        Flow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d, uint32_t flow_priority);

        virtual ~Flow() = 0;

        virtual void start_flow();
        virtual void send_pending_data();
        //virtual void send_pending_data(Channel *channel);
        //virtual void send_one_pkt();
        virtual uint32_t send_pkts();
        virtual Packet *send(uint64_t seq);
        virtual void send_next_pkt();    // D3Flow overrides this
        virtual void send_ack(uint64_t seq, std::vector<uint64_t> sack_list, double pkt_start_ts);
        virtual void receive_ack(uint64_t ack, std::vector<uint64_t> sack_list, double pkt_start_ts, uint32_t priority, uint32_t num_hops);
        virtual void receive_data_pkt(Packet* p);
        virtual void receive(Packet *p);

        // Only sets the timeout if needed; i.e., flow hasn't finished
        virtual void set_timeout(double time);
        virtual void set_timeout_sender(double time);
        virtual void handle_timeout();
        virtual void handle_timeout_sender();
        virtual void cancel_retx_event();
        virtual void cancel_retx_sender_event();

        virtual uint32_t get_priority(uint64_t seq);
        virtual void increase_cwnd();
        virtual double get_avg_queuing_delay_in_us();
        virtual double get_avg_inter_pkt_spacing_in_us();
        virtual uint32_t get_remaining_flow_size();
        virtual double get_remaining_deadline();
        virtual double get_deadline();
        virtual void cancel_rate_limit_event();

        virtual double get_expected_trans_time();   // used by PDQ

        virtual void resend_grant() {};    // used by Homa

        //double get_current_time() {
        //    return current_event_time;
        //}

        uint32_t id;
        double start_time;
        double finish_time;
        //double current_event_time;
        uint32_t size;
        Host *src;
        Host *dst;
        uint32_t cwnd_mss;
        uint32_t max_cwnd;
        double retx_timeout;
        uint32_t mss;
        uint32_t hdr_size;

        // Sender variables
        uint64_t next_seq_no;        // DC if using channel-based CC
        uint64_t last_unacked_seq;   // DC if using channel-based CC
        RetxTimeoutEvent *retx_event;
        RetxTimeoutSenderEvent *retx_sender_event;  // used by Homa
        FlowProcessingEvent *flow_proc_event;
        uint32_t bytes_sent;
        uint64_t start_seq_no;
        uint64_t end_seq_no;

        // Receiver variables
        std::unordered_map<uint64_t, bool> received;
        uint32_t received_bytes;
        uint64_t recv_till;
        uint64_t max_seq_no_recv;       // Yiwen: TBH, I think the name should be 'max_seq_recv' based on the logic implemented in this simulator
        std::vector<uint64_t> received_seq;

        uint32_t total_pkt_sent;
        int size_in_pkt;
        int pkt_drop;
        int data_pkt_drop;
        int ack_pkt_drop;
        int first_hop_departure;
        int last_hop_departure;
        uint32_t received_count;
        // Sack
        uint32_t scoreboard_sack_bytes;
        // finished variables
        bool finished;
        double flow_completion_time;
        double total_queuing_time;
        double first_byte_send_time;
        double first_byte_receive_time;
        double last_data_pkt_receive_time;
        double total_inter_pkt_spacing;
        double rnl_start_time;

        uint32_t flow_priority;  // assigned_priority
        uint32_t run_priority;   // DC if params.prioriy_downgrade is not on
        double deadline;
        Channel *channel;
        AggChannel *agg_channel;
        //Channel *ack_channel;

        // used by D3 and/or PDQ
        double prev_desired_rate;       // desired_rate in the prev RTT (past info required by the router)
        double allocated_rate;          // rate to send in the current RTT (assigned by router during last RTT)
        bool has_ddl;                   // tell apart from non-ddl flows
        RateLimitingEvent *rate_limit_event;        // points to the next RateLimitingEvent; maintains this so we can cancel it when base rate is assigned
        bool terminated;                

        FlowState sw_flow_state;     // used by PDQ; flow states maintained by the switch; hosts should not modify them (except initialize flow ddl)

        // QID: specifies which EventQueue this flow's events should go to
        uint32_t qid;       //TOOD: completely remove

        bool has_received_grant;  // used by Homa
};

#endif // CORESIM_FLOW_H
