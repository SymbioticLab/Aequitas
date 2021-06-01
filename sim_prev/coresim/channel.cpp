#include <iostream>
#include <math.h>
#include <assert.h>

#include "channel.h"
#include "flow.h"
#include "packet.h"
#include "event.h"

#include "../run/params.h"

extern double get_current_time(); 
extern void add_to_event_queue(Event *);
extern DCExpParams params;
extern std::vector<std::vector<uint32_t>> cwnds;
extern std::vector<uint32_t> num_timeouts;

Channel::Channel(uint32_t id, Host *s, Host *d, uint32_t priority) {
    this->id = id;
    this->src = s;
    this->dst = d;
    this->priority = priority;
    this->cwnd_mss = params.initial_cwnd;
    this->cwnd = cwnd_mss;
    this->max_cwnd = params.max_cwnd;
    this->unused_cwnd = cwnd_mss;
    this->ai = 1;
    this->beta = 0.8;
    this->max_mdf = 0.5;
    this->retrans_cnt = 0;
    this->retrans_reset_thresh = 5;
    assert(pending_list.empty());
}

Channel::~Channel() {
}

void Channel::add_to_list(Flow *flow) {
    //std::cout << "flow[" << flow->id << "] added to channel[" << id << "]'s pending_list" << std::endl;
    pending_list.push_back(flow);
    process_list();
}

// Note: since send_ack() does not involve interactions between flows and the channel, ACK pkt is sent in the old way
void Channel::process_list() {
    //std::cout << "channel[" << id << "] process pending_list (size: " << pending_list.size() << "); unused_cwnd = " << unused_cwnd << std::endl;
    //// FIFO model check once
    while (!pending_list.empty() && unused_cwnd > 0) {
    //// Inter-leave model among flows
    //while (!pending_list.empty() && unused_cwnd > 0) {
        Flow *flow = pending_list.front();
        uint32_t pkts_sent = flow->send_pkts();
        if (unused_cwnd < pkts_sent) {  // in this case, scoreboard_sack_bytes must be > 0, which can happen in rare cases
            unused_cwnd = 0;
        } else {
            unused_cwnd -= pkts_sent;
        }
        //unused_cwnd -= pkts_sent;

        //assert(unused_cwnd >= 0);
        //// For FIFO model among flows in a channel, we can't pop a flow from the pending list until it's completed
        //// to be exact, we wait unitl all pkts are out (instead of all acks are received, which is too slow to get full line rate)
        //if (flow->finished) {        <-- this is too slow
        if (flow->next_seq_no == flow->size) {
            pending_list.pop_front();
        } else {
            // if unused_cwnd is 0 at this point, we'll exit the while loop very soon
            // if unused_cwnd > 0 at this point, an infinite loop can happen if there exists another flow trying to append to the list,
            // but the current flow at service has not received its ACK. In this case we just break the while loop
            break;
        }
        //// Inter-leave model
            //pending_list.pop_front();
        ////
    }
}

void Channel::additive_increase_on_ACK() {
    uint32_t prev_cwnd = cwnd_mss;

    cwnd = cwnd + (ai / cwnd_mss) * 1;  // num_acked is always 1 everytime this function gets called
    if (cwnd > max_cwnd) {
        cwnd = max_cwnd;
    }

    cwnd_mss = (uint32_t)floor(cwnd);

    if (cwnd_mss > prev_cwnd) {
        unused_cwnd += (cwnd_mss - prev_cwnd);
    } 
}

void Channel::multiplicative_decrease_on_ACK(double delay) {
    bool can_decrease_cwnd = ((get_current_time() - last_decrease_ts) * 1e6 >= rtt);

    if (can_decrease_cwnd) {
        cwnd = cwnd * std::max(1 - beta * ((delay - params.cc_delay_target) / delay), 1 - max_mdf);
        if (cwnd < 1) {
            cwnd = 1;
        }

        cwnd_mss = (uint32_t)floor(cwnd);

        if (cwnd_mss < unused_cwnd) {
            unused_cwnd = cwnd_mss;
        }

        last_decrease_ts = get_current_time();
    }
}

void Channel::reset_on_RTO() {
    //retrans_cnt = 0;      // TODO: do we reset cnt here?
    cwnd = 1;
    cwnd_mss = 1;

    if (cwnd_mss < unused_cwnd) {
        unused_cwnd = cwnd_mss;
    }
}

void Channel::multiplicative_decrease_on_RTO() {
    bool can_decrease_cwnd = ((get_current_time() - last_decrease_ts) * 1e6 >= rtt);

    if (can_decrease_cwnd) {
        cwnd = cwnd * (1 - max_mdf);
        if (cwnd < 1) {
            cwnd = 1;
        }

        cwnd_mss = (uint32_t)floor(cwnd);

        if (cwnd_mss < unused_cwnd) {
            unused_cwnd = cwnd_mss;
        }
    }
}

// On receiving ACK:
// Note: assume fractional cwnd (e.g., 0.5 sends 1 pkt every 2 rtt) is not implemented in CC for now
// varibale "cwnd" is double, and variable "cwnd_mss" = floor(cwnd)
// "cwnd_mss" is used in CC while "cwnd" is for intermediate calculation
// "unused_cwnd" is the actual available cwnd shared among all flows within the channel
// variable "rtt" is used to make sure MD happens only once per RTT; updated with new ACK
// ai = 1, beta = 0.8, max_mdf = 0.5
void Channel::adjust_cwnd_on_ACK(double delay) {
    retrans_cnt = 0;
    uint32_t prev_cwnd = cwnd_mss;

    if (delay < params.cc_delay_target) {
        additive_increase_on_ACK();
    } else {
        multiplicative_decrease_on_ACK(delay);
    }

    //std::cout << std::fixed;
    //std::cout << std::setprecision(2); 
    //std::cout << "Channel[" << id << "] On receiving ACK: delay = " << delay << " us; adjust cwnd_mss to " << cwnd_mss << std::endl;
    //std::cout << "cwnd = " << cwnd << "; cwnd_mss = " << cwnd_mss << "; unused_cwnd = " << unused_cwnd << "; prev_cwnd = " << prev_cwnd << std::endl;
    assert(unused_cwnd <= cwnd_mss);
    cwnds[priority].push_back(cwnd_mss); 

    rtt = delay;
}

void Channel::adjust_cwnd_on_RTO() {
    bool can_decrease_cwnd = ((get_current_time() - last_decrease_ts) * 1e6 >= rtt);
    retrans_cnt++;
    if (retrans_cnt >= retrans_reset_thresh) {
        reset_on_RTO();
    } else {
        multiplicative_decrease_on_RTO();
    }
    assert(unused_cwnd <= cwnd_mss);
    cwnds[priority].push_back(cwnd_mss); 
    //std::cout << "on Receiving RTO: adjust cwnd to " << cwnd_mss << "; rtt = " << rtt << "; unused_cwnd = " << unused_cwnd << std::endl;
}

void Channel::release_used_cwnd() {
    unused_cwnd += 1;       // give back used cwnd. This increment is unrelated to CC.
    if (unused_cwnd > cwnd_mss) {
        unused_cwnd = cwnd_mss;
    }
}

// delay/rtt is in us
void Channel::report_ack(Flow *flow, double delay) {
    release_used_cwnd();
    adjust_cwnd_on_ACK(delay);
    //std::cout << "flow[" << flow->id << "] reports ack to channel[" << id << "]; cwnd becomes " << cwnd_mss << " and unused_cwnd becomes " << unused_cwnd << std::endl;
}

void Channel::report_timeout(Flow *flow) {
    num_timeouts[priority]++;
    release_used_cwnd();
    adjust_cwnd_on_RTO();
    //std::cout << "flow[" << flow->id << "] reports timeout to channel[" << id << "]; cwnd becomes " << cwnd_mss << " and unused_cwnd becomes " << unused_cwnd << std::endl;
}
