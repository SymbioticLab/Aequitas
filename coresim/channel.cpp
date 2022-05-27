#include "channel.h"

#include <assert.h>
#include <cstddef>
#include <iostream>
#include <math.h>

#include "agg_channel.h"
#include "event.h"
#include "flow.h"
#include "node.h"
#include "nic.h"
#include "packet.h"
#include "queue.h"
#include "topology.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern DCExpParams params;
extern Topology* topology;
extern std::vector<std::vector<uint32_t>> cwnds;
extern std::vector<uint32_t> num_timeouts;
extern uint32_t num_measurements_cleared;
extern uint32_t total_measurements;
extern std::vector<uint32_t> lat_cleared;
extern uint32_t num_outstanding_packets;
extern std::vector<std::vector<double>> per_pkt_rtt;
extern uint32_t pkt_total_count;

Channel::Channel(uint32_t id, Host *s, Host *d, uint32_t priority, AggChannel *agg_channel) {
    this->id = id;
    this->src = s;
    this->dst = d;
    this->priority = priority;
    this->agg_channel = agg_channel;
    if (!params.disable_aequitas_cc) {
        this->cwnd_mss = params.initial_cwnd;
    } else {
        this->cwnd_mss = params.max_cwnd;
    }
    this->next_seq_no = 0;
    this->last_unacked_seq = 0;
    this->end_seq_no = 0;
    this->scoreboard_sack_bytes = 0;
    this->received_bytes = 0;
    this->recv_till = 0;
    this->max_seq_no_recv = 0;
    this->retx_event = NULL;

    this->cwnd = cwnd_mss;
    this->max_cwnd = params.max_cwnd;
    this->mss = params.mss;
    this->hdr_size = params.hdr_size;
    this->ai = 1;
    this->beta = 0.8;
    this->max_mdf = 0.5;
    this->retx_timeout = params.retx_timeout_value;
    this->retrans_cnt = 0;
    this->retrans_reset_thresh = 5;
    this->last_decrease_ts = 0;
    this->fct = 0;
    this->rtt = 0;
    this->last_update_time = 0;
    //this->last_flow_sent = NULL;
    //this->curr_flow_done = false;

    assert(s != d);
}

Channel::~Channel() {
    if (retx_event != NULL) {
        cancel_retx_event();
    }
}

double Channel::get_admit_prob() {
    return agg_channel->admit_prob;
}

void Channel::update_fct(double fct_in, uint32_t flow_id, double update_time, int flow_size) {
    if (priority == params.weights.size() - 1) { return; }    // no need for qos_L
    fct = fct_in * 1e6;        // fct in us
    last_update_time = update_time;

    //window_insert(fct, flow_id, flow_size);
    agg_channel->process_latency_signal(fct, flow_id, flow_size);
}

// Flow calls add_to_channel() at sending_pending_data(); Does the following:
// (1) Adjusts Channel's end sequence number
// (2) Maintains RPC boundaries (used to decide packet size)
// (3) Calls send_pkts() to start sending pkts based on the current CWND
// Note: we assume a strict FIFO model (i.e., no DATA pkts interleaving) for the Flows in the same channel
void Channel::add_to_channel(Flow *flow) {
    flow->start_seq_no = end_seq_no;
    end_seq_no += flow->size;
    outstanding_flows.push_back(flow);    // for RPC boundary, tie flow to pkt, easy handling of flow_finish, etc.
    flow->end_seq_no = end_seq_no;
    //std::cout << "add_to_channel[" << id << "]: end_seq_no = " << end_seq_no << std::endl;
    if (params.real_nic) {
        ////src->nic->start_nic();  // wake up nic if it's not busy working already
        //std::cout << "Host[" << src->id << "]: Channel[" << id << "]::add_to_channel:" << std::endl;
        src->nic->add_to_nic(this);
    } else {
        send_pkts();
    }
}

// Given a sequence number, find the correspongding RPC whose packet contains this sequence number.
Flow * Channel::find_next_flow(uint64_t seq) {
    Flow *flow = NULL;
    for (const auto &f : outstanding_flows) {
        if (seq < f->end_seq_no) {
            flow = f;
            break;
        }
    }
    if (flow == NULL) {
        std::cout << "Error in next_flow_to_send(). Exiting." << std::endl;
        std::cout << "seq = " << seq << std::endl;
        std::cout << "outstanding flows:" << std::endl;
        for (const auto &f : outstanding_flows) {
            std::cout << "end_seq = " << f->end_seq_no << std::endl;
        }
        exit(1);
    }
    return flow;
}

// once called, send as many packets as possible
int Channel::send_pkts() {
    uint32_t pkts_sent = 0;
    uint64_t seq = next_seq_no;
    uint32_t window = cwnd_mss * mss + scoreboard_sack_bytes;  // Note sack_bytes is always 0 for now
    if (params.debug_event_info) {
        std::cout << "Channel[" << id << "] send_pkts():" << " seq = " << seq
            << ", window = " << window << ", last_unacked_seq = " << last_unacked_seq << std::endl;
    }

    while (
        (seq + mss <= last_unacked_seq + window) &&                                     // (1) CWND still allows me to send more packets
        ((seq + mss <= end_seq_no) || (seq != end_seq_no && (end_seq_no - seq < mss)))  // (2) I still have more packets to send
    ) {
        uint32_t pkt_size;
        Flow *flow_to_send = find_next_flow(seq);
        uint64_t next_flow_boundary = flow_to_send->end_seq_no;
        if (seq + mss < next_flow_boundary) {
            pkt_size = mss + hdr_size;
            next_seq_no = seq + mss;
        } else {
            pkt_size = (next_flow_boundary - seq) + hdr_size;
            next_seq_no = next_flow_boundary;
        }

        ////Packet *p = send_one_pkt(seq, pkt_size, 1e-9 * (pkts_sent + 1), flow_to_send);    // send with 1 ns delay for each pkt
        Packet *p = send_one_pkt(seq, pkt_size, 1e-12 * (pkts_sent + 1), flow_to_send);    // send with 1 ps delay for each pkt
        if (params.enable_flow_lookup && flow_to_send->id == params.flow_lookup_id) {    // NOTE: the Time print out here does not reflect the tiny delay
            std::cout << "At time: " << get_current_time() << ", Flow[" << flow_to_send->id << "] (flow_size=" << flow_to_send->size << ") send a packet["
                << p->unique_id <<"] (seq=" << seq << "), last_unacked_seq = " << last_unacked_seq << "; window = " << window
                << "; Flow start_seq = " << flow_to_send->start_seq_no << "; flow end_seq = " << flow_to_send->end_seq_no << std::endl;
        }
        seq = next_seq_no;
        pkts_sent++;

        if (retx_event == NULL) {
            set_timeout(get_current_time() + retx_timeout);
        }
    }

    if (params.debug_event_info) {
        std::cout << "Channel[" << id << "] sends " << pkts_sent << " pkts." << std::endl;
    }
    pkt_total_count += pkts_sent;    // for global logging

    return pkts_sent;
}

// send with some tiny delay so that pkts from the same batch of a flow can always be ordered correctly by the event comparator
Packet *Channel::send_one_pkt(uint64_t seq, uint32_t pkt_size, double delay, Flow *flow) {
    Packet *p = new Packet(
            get_current_time(),
            flow,
            seq,
            priority,
            pkt_size,
            src,
            dst
            );
    p->start_ts = get_current_time();
    if (params.real_nic && flow->bytes_sent == 0) {
        flow->rnl_start_time = get_current_time();
    }
    flow->bytes_sent += pkt_size;

    if (params.debug_event_info) {
        std::cout << "sending out Packet[" << p->unique_id << "] (seq=" << seq << ") at time: " << get_current_time() + delay << " (base=" << get_current_time() << "; delay=" << delay << ")" << std::endl;
    }
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    if (!params.real_nic) {
        add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + delay, p, next_hop));  // add a pd since we skip the source queue
    } else {
        double td = src->queue->get_transmission_delay(p);
        double pd = src->queue->propagation_delay;
        //std::cout << "Channel[" << id << "] td = " << td << "; pd = " << pd << std::endl;
        // TODO(yiwen): check whether 'td' is needed at PacketQueuingEvent
        add_to_event_queue(new PacketQueuingEvent(get_current_time() + pd + td, p, next_hop));  // tiny delay should be uncessary in this case; TODO: check later
        add_to_event_queue(new NICProcessingEvent(get_current_time() + td, src->nic));
    }
    return p;
}

// called by nic when params.real_nic is on
int Channel::nic_send_next_pkt() {
    uint32_t pkts_sent = 0;
    uint64_t seq = next_seq_no;
    uint32_t window = cwnd_mss * mss + scoreboard_sack_bytes;  // Note sack_bytes is always 0 for now
    if (params.debug_event_info) {
        std::cout << "Channel[" << id << "] nic_send_next_pkt():" << " seq = " << seq
            << ", window = " << window << ", last_unacked_seq = " << last_unacked_seq << std::endl;
        std::cout << "seq + mss = " << seq + mss << std::endl;
        std::cout << "end_seq_no = " << end_seq_no << std::endl;
    }

    // only send one packet each call; so use if statement instead of while loop
    if (
        (seq + mss <= last_unacked_seq + window) &&                                     // (1) CWND still allows me to send more packets
        ((seq + mss <= end_seq_no) || (seq != end_seq_no && (end_seq_no - seq < mss)))  // (2) I still have more packets to send
    ) {
        uint32_t pkt_size;
        Flow *flow_to_send = find_next_flow(seq);
        ////if (flow_to_send != last_flow_sent) {
        ////    flow_to_send->rnl_start_time = get_current_time();
        ////    //std::cout << "PUPU: flow[" << flow_to_send->id << "] start time diff = " << (flow_to_send->rnl_start_time - flow_to_send->start_time) * 1e6 << " us" << std::endl;
        ////    last_flow_sent = flow_to_send;
        ////}
        uint64_t next_flow_boundary = flow_to_send->end_seq_no;
        if (seq + mss < next_flow_boundary) {
            pkt_size = mss + hdr_size;
            next_seq_no = seq + mss;
        } else {
            pkt_size = (next_flow_boundary - seq) + hdr_size;
            next_seq_no = next_flow_boundary;
        }

        Packet *p = send_one_pkt(seq, pkt_size, 1e-12 * (pkts_sent + 1), flow_to_send);    // send with 1 ps delay for each pkt
        if (params.enable_flow_lookup && flow_to_send->id == params.flow_lookup_id) {    // NOTE: the Time print out here does not reflect the tiny delay
            std::cout << "At time: " << get_current_time() << ", NIC instructs Flow[" << flow_to_send->id << "] (flow_size=" << flow_to_send->size
                << ") to send a packet[" << p->unique_id <<"] (seq=" << seq << "), last_unacked_seq = " << last_unacked_seq << "; window = " << window
                << "; Flow start_seq = " << flow_to_send->start_seq_no << "; flow end_seq = " << flow_to_send->end_seq_no << std::endl;
        }
        //seq = next_seq_no;  // unnecessary here; remote later
        pkts_sent++;

        //if (next_seq_no == next_flow_boundary) {
        //    curr_flow_done = true;
        //} else {
        //    curr_flow_done = false;
        //}

        if (retx_event == NULL) {
            set_timeout(get_current_time() + retx_timeout);
        }
    }

    if (params.debug_event_info) {
        std::cout << "Channel[" << id << "] sends " << pkts_sent << " pkts." << std::endl;
    }
    pkt_total_count += pkts_sent;    // for global logging; pkts_sent supposed to be 1 or 0

    return pkts_sent;
}

// Note: we longer track Flow's avg_queuing_time and inter_pkt_spacing after moving this function to Channel
// We can add these metrics (per channel) later if needed
void Channel::receive_data_pkt(Packet* p) {
    if (received.count(p->seq_no) == 0) {
        received[p->seq_no] = true;
        p->flow->received_seq.push_back(p->seq_no);
        if (num_outstanding_packets >= ((p->size - hdr_size) / (mss))) {
            num_outstanding_packets -= ((p->size - hdr_size) / (mss));
        } else {
            num_outstanding_packets = 0;
        }
        received_bytes += (p->size - hdr_size);
    }
    if (p->seq_no > max_seq_no_recv) {
        max_seq_no_recv = p->seq_no;
    }
    if (params.enable_flow_lookup && p->flow->id == params.flow_lookup_id) {
        std::cout << "Receive data packet[" << p->unique_id << "] from Flow["<< p->flow->id << "], seq = "
            << p->seq_no << "; max_seq_no_recv = " << max_seq_no_recv << "; recv_till = " << recv_till << std::endl;
    }
    // Determing which ack to send
    uint64_t s = recv_till;
    uint64_t prev_recv_till = recv_till;
    bool in_sequence = true;
    std::vector<uint64_t> sack_list;
    Flow *flow_to_receive = p->flow;
    uint64_t next_end_seq_to_receive = flow_to_receive->end_seq_no;
    while (s <= max_seq_no_recv) {
        if (received.count(s) > 0) {
            if (in_sequence) {
                if (recv_till + mss > next_end_seq_to_receive) {
                    recv_till = next_end_seq_to_receive;
                } else {
                    recv_till += mss;
                }
            } else {
                sack_list.push_back(s);
            }
        } else {
            in_sequence = false;
            // YZ: since we don't do sack, need to disgard the current received data pkt (as if we have never received it)
            received.erase(p->seq_no);
        }
        s += mss;
    }
    if (params.enable_flow_lookup && p->flow->id == params.flow_lookup_id) {
        std::cout << "recv_till becomes " << recv_till << "; it was initially " << prev_recv_till
            << "; received_count(" << prev_recv_till << ") = " << received.count(prev_recv_till) << "; Data Packet seq = " << p->seq_no
            << "; Flow to receive = Flow[" << flow_to_receive->id << "]" << "; flow end_seq = " << flow_to_receive->end_seq_no << "; check end seq to recv = " << next_end_seq_to_receive << std::endl;
    }

    if (params.debug_event_info) {
        std::cout << "Channel[" << id << "] receive_data_pkt (seq=" << p->seq_no << "): next_end_seq = " << next_end_seq_to_receive << "; received_bytes = " << received_bytes 
            << "; max_seq_no_recv = " << max_seq_no_recv << "; data pkt seq no = " << p->seq_no << "; recv_till (seq for next ACK) = " << recv_till << std::endl;
    }
    send_ack(recv_till, sack_list, p->start_ts, flow_to_receive);
}

void Channel::send_ack(uint64_t seq, std::vector<uint64_t> sack_list, double pkt_start_ts, Flow *flow) {
    Packet *p = new Ack(flow, seq, sack_list, hdr_size, dst, src);  //Acks are dst->src
    p->pf_priority = priority;
    p->start_ts = pkt_start_ts; // carry the orig packet's start_ts back to the sender for RTT measurement
    if (params.debug_event_info) {
        std::cout << "Channel[" << id << "] with priority " << priority << " send ack: " << seq << std::endl;
    }
    if (params.enable_flow_lookup && flow->id == params.flow_lookup_id) {
        std::cout << "Channel[" << id << "] Flow[" << flow->id << "] with priority " << priority << " send ack: " << seq << std::endl;
    }

    //// When params.real_nic is on, for ACK pkts, just do the old way for now since they are really small and don't count much for the tput.
    //// This also prioritizes those Acks so they don't get blocked by whatever arbitration policy we do in the NIC.
    // TODO: Do a more realistic implementation (probably won't do given the complexity; and I'm sure the results will be very similar)
    Queue *next_hop = topology->get_next_hop(p, dst->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // skip src queue, include dst queue; add a pd delay
    add_to_event_queue(event);
}

void Channel::cleanup_after_finish(Flow *flow) {
    for (const auto & s: flow->received_seq) {
        received.erase(s);
    }

    // Assuming out-of-order flow completion can happen
    for (auto it = outstanding_flows.begin(); it != outstanding_flows.end(); it++) {
        if (*it == flow) {
            outstanding_flows.erase(it);
            break;
        }
    }
}

void Channel::receive_ack(uint64_t ack, Flow *flow, std::vector<uint64_t> sack_list, double pkt_start_ts) {
    if (params.debug_event_info) {
        std::cout << "Channel[" << id << "] with priority " << priority << " receive ack: " << ack << std::endl;
    }
    if (params.enable_flow_lookup && flow->id == params.flow_lookup_id) {
        std::cout << "Channel[" << id << "] Flow[" << flow->id << "] with priority " << priority
            << " receive ack: " << ack << "; last_unacked_seq: " << last_unacked_seq
            << "; Flow's end seq = " << flow->end_seq_no << ", start_seq = " << flow->start_seq_no << std::endl;
    }
    //this->scoreboard_sack_bytes = sack_list.size() * mss;
    this->scoreboard_sack_bytes = 0;        // ignore SACK for now

    // On timeouts; next_seq_no is updated to last_unacked_seq;
    // In such cases, the ack can be greater than next_seq_no; update it
    if (next_seq_no < ack) {
        next_seq_no = ack;
    }

    // New ack!
    if (ack > last_unacked_seq) {
        // Update the last unacked seq
        last_unacked_seq = ack;

        // Measure RTT (for delay-based CC)
        double rtt = (get_current_time() - pkt_start_ts) * 1000000;    // in us
        if (!params.disable_pkt_logging) {
            per_pkt_rtt[priority].push_back(rtt);
        }

        report_ack(rtt);

        // Send the remaining data
        if (params.real_nic) {
            ////src->nic->start_nic();
            //std::cout << "Host[" << src->id << "]: Channel[" << id << "]::receive_ack:" << std::endl;
            src->nic->add_to_nic(this);
        } else {
            send_pkts();
        }

        // Update the retx timer
        if (retx_event != NULL) { // Try to move
            cancel_retx_event();
            if (last_unacked_seq < end_seq_no) {
                // Move the timeout to last_unacked_seq
                double timeout = get_current_time() + retx_timeout;
                set_timeout(timeout);
                if (params.enable_flow_lookup && flow->id == params.flow_lookup_id) {
                    std::cout << "Flow[" << flow->id << "] at receive_ack(): set timeout at: " << timeout << std::endl;
                }
            }
        }

    }

    if (ack == flow->end_seq_no && !flow->finished) {
        flow->finished = true;
        cleanup_after_finish(flow);
        flow->finish_time = get_current_time();
        double flow_completion_time;
        if (params.real_nic) {
            flow_completion_time = flow->finish_time - flow->rnl_start_time;
        } else {
            flow_completion_time = flow->finish_time - flow->start_time;
        }
        //double flow_completion_time = flow->finish_time - flow->start_time;
        if (params.priority_downgrade) {
            update_fct(flow_completion_time, flow->id, get_current_time(), flow->size_in_pkt);
        }
        if (params.enable_flow_lookup && flow->id == params.flow_lookup_id) {
            std::cout << "Flow[" << flow->id << "] Finish time = "
                << flow->finish_time << "; start time = " << flow->start_time
                << "; completion time = " << flow_completion_time << std::endl;
        }
        FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), flow);
        add_to_event_queue(ev);
    }
}

void Channel::additive_increase_on_ACK() {
    cwnd = cwnd + (ai / cwnd_mss) * 1;  // num_acked is always 1 everytime this function gets called
    if (cwnd > max_cwnd) {
        cwnd = max_cwnd;
    }

    cwnd_mss = (uint32_t)floor(cwnd);
}

void Channel::multiplicative_decrease_on_ACK(double delay) {
    bool can_decrease_cwnd = ((get_current_time() - last_decrease_ts) * 1e6 >= rtt);
    if (can_decrease_cwnd) {
        cwnd = cwnd * std::max(1 - beta * ((delay - params.cc_delay_target) / delay), 1 - max_mdf);
        if (cwnd < 1) {
            cwnd = 1;
        }

        cwnd_mss = (uint32_t)floor(cwnd);
        last_decrease_ts = get_current_time();
    }
}

void Channel::reset_on_RTO() {
    cwnd = 1;
    cwnd_mss = 1;
}

void Channel::multiplicative_decrease_on_RTO() {
    bool can_decrease_cwnd = ((get_current_time() - last_decrease_ts) * 1e6 >= rtt);
    if (can_decrease_cwnd) {
        cwnd = cwnd * (1 - max_mdf);
        if (cwnd < 1) {
            cwnd = 1;
        }

        cwnd_mss = (uint32_t)floor(cwnd);
    }
}

// On receiving ACK:
// Note: assume fractional cwnd (e.g., 0.5 sends 1 pkt every 2 rtt) is not implemented in CC for now
// varibale "cwnd" is double, and variable "cwnd_mss" = floor(cwnd)
// "cwnd_mss" is used in CC while "cwnd" is for intermediate calculation
// variable "rtt" is used to make sure MD happens only once per RTT; updated with new ACK
// ai = 1, beta = 0.8, max_mdf = 0.5
void Channel::adjust_cwnd_on_ACK(double delay) {
    retrans_cnt = 0;
    if (delay < params.cc_delay_target) {
        additive_increase_on_ACK();
    } else {
        multiplicative_decrease_on_ACK(delay);
    }

    if (!params.disable_cwnd_logging) {
        cwnds[priority].push_back(cwnd_mss);
    }

    rtt = delay;
}

void Channel::adjust_cwnd_on_RTO() {
    retrans_cnt++;
    if (retrans_cnt >= retrans_reset_thresh) {
        reset_on_RTO();
    } else {
        multiplicative_decrease_on_RTO();
    }
    if (!params.disable_cwnd_logging) {
        cwnds[priority].push_back(cwnd_mss);
    }
}

// delay/rtt is in us
void Channel::report_ack(double delay) {
    if (params.disable_aequitas_cc) {
        return;
    } else {
        if (!params.disable_cwnd_logging) {
            cwnds[priority].push_back(cwnd_mss);
        }
    }

    adjust_cwnd_on_ACK(delay);
}

void Channel::set_timeout(double time) {
    if (params.disable_aequitas_cc) {
        return;
    }
    if (last_unacked_seq < end_seq_no) {
        ChannelRetxTimeoutEvent *ev = new ChannelRetxTimeoutEvent(time, this);
        add_to_event_queue(ev);
        retx_event = ev;
    }
}

void Channel::handle_timeout() {
    num_timeouts[priority]++;
    if (params.disable_aequitas_cc) {    // probably unnecessary since 'set_timeout()' has been checked already
        return;
    } else {
        if (!params.disable_cwnd_logging) {
            cwnds[priority].push_back(cwnd_mss);
        }
    }
    adjust_cwnd_on_RTO();

    next_seq_no = last_unacked_seq;
    if (params.real_nic) {
        ////src->nic->start_nic();
        //std::cout << "Host[" << src->id << "]: Channel[" << id << "]::handle_timeout:" << std::endl;
        src->nic->add_to_nic(this);
    } else {
        send_pkts();
    }
    set_timeout(get_current_time() + retx_timeout);
}

void Channel::cancel_retx_event() {
    retx_event->cancelled = true;
    retx_event = NULL;
}
