#ifndef CHANNEL_H
#define CHANNEL_H

#include <deque>
#include "node.h"

class Flow;
class Packet;
class Ack;
class Probe;
class RetxTimeoutEvent;
class FlowProcessingEvent;

/* Channel: a single direction src-dst pair */
// A flow appends itself to the tail of the pending_list in order to send data
// The Channel distribute 1 token (=1 MSS, or 1 pkt) to the head of the list every time to achieve RR among multiple pending flows
class Channel {
    public:
        Channel(uint32_t id, Host *s, Host *d, uint32_t priority);

        ~Channel(); // Destructor

        //virtual void start_channel();
        virtual void add_to_list(Flow *flow);   // called by a flow to add itself to pending_list
        virtual void process_list();    // pop the flow from pending_list to send data until the list is empty (packet-based RR)

        virtual void additive_increase_on_ACK();
        virtual void multiplicative_decrease_on_ACK(double delay);
        virtual void reset_on_RTO();
        virtual void multiplicative_decrease_on_RTO();
        virtual void adjust_cwnd_on_ACK(double delay);
        virtual void adjust_cwnd_on_RTO();
        virtual void release_used_cwnd();
        virtual void report_ack(Flow *flow, double delay);  // flows inform Channel when receiving a new ACK
        virtual void report_timeout(Flow *flow);  // flows inform Channel when a timeout happens

        // timeout event is still handled by Flow; but a flow timeout now changes channel's cwnd
        // TODO: timeout related funtions need to be modified
        //virtual void set_timeout(double time);
        //virtual void handle_timeout();
        //virtual void cancel_retx_event();

        uint32_t id;
        uint32_t priority;
        Host *src;
        Host *dst;
        std::deque<Flow *> pending_list;       // active flows that wait to send data
        
        // CC related
        double cwnd;            // for intermediate calculation
        uint32_t cwnd_mss;      // actual cwnd used in CC; value = floor(cwnd); 
        uint32_t max_cwnd;
        uint32_t unused_cwnd;   // available cwnd_mss that can be used by flows
        double rtt;             // used to enforce per rtt MD; in us; keep updated with new delay
        double last_decrease_ts;    // time when the most recent MD takes place
        double ai;
        double beta;
        double max_mdf;
        uint32_t retrans_cnt;
        uint32_t retrans_reset_thresh;
};

#endif
