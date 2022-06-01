#include "flow.h"

#include <assert.h>
#include <iostream>
#include <math.h>
#include <cstddef>
#include <cstdint>

#include "agg_channel.h"
#include "channel.h"
#include "event.h"
#include "node.h"
#include "packet.h"
#include "../ext/factory.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern DCExpParams params;
extern uint32_t num_outstanding_packets;
extern uint32_t max_outstanding_packets;
extern uint32_t duplicated_packets_received;
extern std::vector<std::map<std::pair<uint32_t, uint32_t>, AggChannel *>> channels;
extern std::vector<std::vector<double>> per_pkt_lat;
extern std::vector<std::vector<double>> per_pkt_rtt;
extern uint32_t num_downgrades;
extern std::vector<uint32_t> num_downgrades_per_host;
extern std::vector<uint32_t> num_check_passed;
extern std::vector<uint32_t> num_check_failed_and_downgrade;
extern std::vector<uint32_t> num_check_failed_but_stay;
extern std::vector<uint32_t> num_qos_h_downgrades;
extern uint32_t num_qos_m_downgrades;
extern std::vector<uint32_t> qos_h_issued_rpcs_per_host;
extern std::vector<uint32_t> per_pctl_downgrades;
extern std::vector<uint32_t> per_host_QoS_H_downgrades;
extern std::vector<uint32_t> per_host_QoS_H_rpcs;
extern std::vector<uint32_t> dwnds_qosh;
extern std::vector<uint32_t> fairness_qos_h_bytes_per_host;
extern uint32_t num_outstanding_rpcs;

extern std::map<std::pair<uint32_t, uint32_t>, uint32_t> flip_coin;

Flow::Flow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d) {
    this->id = id;
    this->start_time = start_time;
    //this->current_event_time = start_time;
    this->finish_time = 0;
    this->size = size;
    this->src = s;
    this->dst = d;

    this->next_seq_no = 0;
    this->last_unacked_seq = 0;
    this->retx_event = nullptr;
    this->retx_sender_event = nullptr;
    this->flow_proc_event = nullptr;
    this->bytes_sent = 0;
    this->start_seq_no = 0;
    this->end_seq_no = 0;

    this->received_bytes = 0;
    this->recv_till = 0;
    this->max_seq_no_recv = 0;
    this->cwnd_mss = params.initial_cwnd;
    this->max_cwnd = params.max_cwnd;
    this->finished = false;
    this->total_queuing_time  = 0;
    this->received_count = 0;
    this->last_data_pkt_receive_time = 0;
    this->total_inter_pkt_spacing = 0;
    this->rnl_start_time = 0;

    //SACK
    this->scoreboard_sack_bytes = 0;

    this->retx_timeout = params.retx_timeout_value;
    this->mss = params.mss;
    this->hdr_size = params.hdr_size;
    this->total_pkt_sent = 0;
    this->size_in_pkt = (int)ceil((double)size/mss);

    this->pkt_drop = 0;
    this->data_pkt_drop = 0;
    this->ack_pkt_drop = 0;
    this->flow_priority = 0;
    this->run_priority = 0;
    this->first_byte_send_time = -1;
    this->first_byte_receive_time = -1;
    this->first_hop_departure = 0;
    this->last_hop_departure = 0;

    this->prev_desired_rate = 0;
    this->allocated_rate = 0;
    this->has_ddl = false;
    this->rate_limit_event = nullptr;
    this->terminated = false;

    this->sw_flow_state = FlowState();

    this->has_received_grant = false;
}

Flow::Flow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d, uint32_t flow_priority) :
    Flow(id, start_time, size, s, d) {
    this->flow_priority = flow_priority;

    if (params.flow_type == AEQUITAS_FLOW && params.priority_downgrade) {
        // Flow now starts a separate Channel by its own (using the same flow ID as the Channel ID),
        // and the Channel links itself to the AggChannel to get admit_prob
        //AggChannel *agg_channel = channels[flow_priority][std::make_pair(src->id, dst->id)];
        //channel = new Channel(id, s, d, flow_priority, agg_channel);
        agg_channel = channels[flow_priority][std::make_pair(src->id, dst->id)];
    }
}

Flow::~Flow() {
}

void Flow::start_flow() {
    run_priority = flow_priority;

    if (params.priority_downgrade) {    // assume lower values mean higher priority

        //std::pair src_dst = std::make_pair(src->id, dst->id);

        if (flow_priority == 0) {
            per_host_QoS_H_rpcs[src->id]++;
        }
        if (flow_priority < 2) {
            //double admit_prob = agchannel->get_admit_prob();
            double admit_prob = agg_channel->admit_prob;
            if ((double)rand() / (RAND_MAX) > admit_prob) {
                run_priority = params.weights.size() - 1;
                num_downgrades++;
                num_downgrades_per_host[src->id]++;
                if (flow_priority == 0) {
                    per_host_QoS_H_downgrades[src->id]++;
                    num_qos_h_downgrades[1]++;
                    per_pctl_downgrades[0]++;
                } else {
                    num_qos_m_downgrades++;
                    per_pctl_downgrades[1]++;
                }
            }

        }

        // Now if gets downgraded, delete the old one-shot channel and creates a new one
        /*
        if (run_priority != flow_priority) {
            delete channel;
            AggChannel *agg_channel = channels[run_priority][src_dst];
            channel = new Channel(id, src, dst, run_priority, agg_channel);
        }
        */

    }

    agg_channel = channels[run_priority][std::make_pair(src->id, dst->id)];
    if (params.channel_multiplexing) {
        // randomly assigns a Channel to the flow under the AggChannel the flow belongs to
        uint32_t pick_channel = rand() % params.multiplex_constant;
        channel = agg_channel->channels[pick_channel];
    } else {
        channel = new Channel(id, src, dst, run_priority, agg_channel);
    }

    if (run_priority == 0) {
        qos_h_issued_rpcs_per_host[src->id]++;
        if (params.test_fairness) {
            fairness_qos_h_bytes_per_host[src->id] += size;
        }
    }

    //send_pending_data();
    channel->add_to_channel(this);

    num_outstanding_rpcs++;
}

// send_pending_data() via a selected Channel to achieve per-channel CC
void Flow::send_pending_data() {
    channel->send_pkts();
}

// Called by Channel
// TODO: check whether sack is correctly implemented in the orig simulator
// since I see some TODOs related to SACK in the orig code
// NOTE: Aequitas override and uses its own "send_pkts()"
// NOTE: be careful about using this call directly; should use derived send_pkts() from a derived class
uint32_t Flow::send_pkts() {
    uint32_t pkts_sent = 0;
    uint64_t seq = next_seq_no;
    uint32_t window = cwnd_mss * mss;
    //uint32_t window = channel->cwnd_mss * mss + scoreboard_sack_bytes;
    //std::cout << "seq: " << seq << ", window = " << window << ", last_unacked_seq = " << last_unacked_seq << ", sack_bytes = " << scoreboard_sack_bytes << std::endl;
    while (
        (seq + mss <= last_unacked_seq + window) &&
        ((seq + mss <= size) || (seq != size && (size - seq < mss)))
    ) {
        send(seq);
        pkts_sent++;

        if (seq + mss < size) {
            next_seq_no = seq + mss;
            seq += mss;
        } else {
            //next_seq_no = end_seq_no;
            next_seq_no = size;
            seq = size;
        }

        if (retx_event == nullptr) {
            set_timeout(get_current_time() + retx_timeout);
            //std::cout << "Flow[" << id << "] at send_pkts(): set timeout at: " << get_current_time() + retx_timeout << std::endl;
        }
        //std::cout << "seq: " << seq << ", window = " << window << ", last_unacked_seq = " << last_unacked_seq << ", sack_bytes = " << scoreboard_sack_bytes << std::endl;
    }
    //std::cout << "Flow[" << id << "] sends " << pkts_sent << " pkts." << std::endl;
    return pkts_sent;
}

// Note: AequitasFlow overrides send() and send_ack()
Packet *Flow::send(uint64_t seq) {
    Packet *p = NULL;

    uint32_t pkt_size;
    if (seq + mss > this->size) {
        pkt_size = this->size - seq + hdr_size;
    } else {
        pkt_size = mss + hdr_size;
    }
    //std::cout << "seq: " << seq << "; mss: " << mss << "; hdr_size: " << hdr_size << "; flow_size: " << this->size << "; pkt_size: " << pkt_size << std::endl;
    uint32_t priority = get_priority(seq);
    p = new Packet(
            get_current_time(),
            this,
            seq,
            priority,
            pkt_size,
            src,
            dst
            );
    this->total_pkt_sent++;
    p->start_ts = get_current_time();

    add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, src->queue));
    return p;
}

void Flow::send_next_pkt() {
    assert(false);
}

void Flow::send_ack(uint64_t seq, std::vector<uint64_t> sack_list, double pkt_start_ts) {
    Packet *p = new Ack(this, seq, sack_list, hdr_size, dst, src);  //Acks are dst->src
    p->start_ts = pkt_start_ts; // carry the orig packet's start_ts back to the sender for RTT measurement
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, dst->queue));
}

void Flow::receive_ack(uint64_t ack, std::vector<uint64_t> sack_list, double pkt_start_ts, uint32_t priority, uint32_t num_hops) {
    if (params.debug_event_info) {
        std::cout << "Flow[" << id << "] with run priority " << run_priority << " receive ack: " << ack << std::endl;
    }
    this->scoreboard_sack_bytes = sack_list.size() * mss;

    // On timeouts; next_seq_no is updated to the last_unacked_seq;
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
        //std::cout << "rtt: current_time = " << get_current_time() << "; pkt_start_ts = " << pkt_start_ts << std::endl;
        //if (rtt > params.cc_delay_target)
        //    std::cout << "rtt measurements: " << rtt << std::endl;
        if (!params.disable_pkt_logging) {
            per_pkt_rtt[priority].push_back(rtt);
        }

        if (params.priority_downgrade) {
            // update flow fct here in advance so channel can keep it
            if (ack == size && !finished) {
                channel->update_fct(get_current_time() - start_time, id, get_current_time(), size_in_pkt);
            }
        }

        channel->report_ack(rtt);

        // Send the remaining data
        send_pending_data();

        // Update the retx timer
        if (retx_event != nullptr) { // Try to move
            cancel_retx_event();
            if (last_unacked_seq < size) {
                // Move the timeout to last_unacked_seq
                double timeout = get_current_time() + retx_timeout;
                set_timeout(timeout);
                //std::cout << "Flow[" << id << "] at receive_ack(): set timeout at: " << timeout << std::endl;
            }
        }

    }

    if (ack == size && !finished) {
        finished = true;
        received.clear();
        finish_time = get_current_time();
        flow_completion_time = finish_time - start_time;
        if (params.enable_flow_lookup && id == params.flow_lookup_id) {
            std::cout << "Flow[" << id << "] Finish time = "
                << finish_time << "; start time = " << start_time
                << "; completion time = " << flow_completion_time << std::endl;
        }
        ////channel->update_fct(flow_completion_time, id, get_current_time());   //// why not here?
        std::cout << "Flow[" << id << " about to finish" << std::endl;
        FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
        add_to_event_queue(ev);
    }
}


void Flow::receive(Packet *p) {
    if (finished) {
        delete p;
        return;
    }

    if (p->type == ACK_PACKET) {
        Ack *a = dynamic_cast<Ack *>(p);
        receive_ack(a->seq_no, a->sack_list, a->start_ts, p->pf_priority, p->num_hops);
    }
    else if(p->type == NORMAL_PACKET) {
        if (this->first_byte_receive_time == -1) {
            this->first_byte_receive_time = get_current_time();
        }
        this->receive_data_pkt(p);
    }
    else {
        assert(false);
    }

    delete p;
}

void Flow::receive_data_pkt(Packet* p) {
    received_count++;
    total_queuing_time += p->total_queuing_delay;
    if (last_data_pkt_receive_time != 0) {
        double inter_pkt_spacing = get_current_time() - last_data_pkt_receive_time;
        total_inter_pkt_spacing += inter_pkt_spacing;
    }
    last_data_pkt_receive_time = get_current_time();
    if (!params.disable_pkt_logging) {
        //// don't measure queuing delay; only measure RTT for now
        ////per_pkt_lat[p->pf_priority].push_back(p->total_queuing_delay);
    }

    if (received.count(p->seq_no) == 0) {
        received[p->seq_no] = true;
        if(num_outstanding_packets >= ((p->size - hdr_size) / (mss)))
            num_outstanding_packets -= ((p->size - hdr_size) / (mss));
        else
            num_outstanding_packets = 0;
        received_bytes += (p->size - hdr_size);
    } else {
        duplicated_packets_received += 1;
    }
    if (p->seq_no > max_seq_no_recv) {
        max_seq_no_recv = p->seq_no;
    }
    // Determing which ack to send
    uint64_t s = recv_till;
    bool in_sequence = true;
    std::vector<uint64_t> sack_list;
    while (s <= max_seq_no_recv) {
        if (received.count(s) > 0) {
            if (in_sequence) {
                if (recv_till + mss > this->size) {
                    recv_till = this->size;
                } else {
                    recv_till += mss;
                }
            } else {
                sack_list.push_back(s);
            }
        } else {
            in_sequence = false;
        }
        s += mss;
    }

    //std::cout << "Flow[" << id << "] receive_data_pkt: received_count = " << received_count << "; received_bytes = " << received_bytes << std::endl;
    send_ack(recv_till, sack_list, p->start_ts); // Cumulative Ack
}

void Flow::set_timeout(double time) {
    if (params.disable_aequitas_cc) {
        return;
    }
    if (last_unacked_seq < size) {
        RetxTimeoutEvent *ev = new RetxTimeoutEvent(time, this);
        add_to_event_queue(ev);
        retx_event = ev;
    }
}

void Flow::set_timeout_sender(double time) {
    assert(false);
}


// since set_timeout has the disable_cc check, no need to check again here
void Flow::handle_timeout() {
    next_seq_no = last_unacked_seq;
    //Reset congestion window to 1
    cwnd_mss = 1;
    ////channel->report_timeout(this); // commented out so that pFabric impl (flow-based CC) will be correct; we move timeout handling also to Channel
    send_pending_data();
    set_timeout(get_current_time() + retx_timeout);
}

void Flow::handle_timeout_sender() {
    assert(false);
}

void Flow::cancel_retx_event() {
    if (retx_event) {
        retx_event->cancelled = true;
    }
    retx_event = nullptr;
}

void Flow::cancel_retx_sender_event() {
    if (retx_sender_event) {
        retx_sender_event->cancelled = true;
    }
    retx_sender_event = nullptr;
}

void Flow::cancel_rate_limit_event() {
    if (rate_limit_event) {
        rate_limit_event->cancelled = true;
    }
    rate_limit_event = nullptr;
}

uint32_t Flow::get_priority(uint64_t seq) {
    //if (params.flow_type == NORMAL_FLOW) {
    //    return 1;
    //}
    if(params.deadline && params.schedule_by_deadline)
    {
        return (int)(this->deadline * 1000000);
    }
    else{
        ////return (size - last_unacked_seq - scoreboard_sack_bytes);
        return (size - last_unacked_seq);
    }
}


// No longer used after Channel impl
void Flow::increase_cwnd() {
    if (params.disable_aequitas_cc) {
        return;
    }
    cwnd_mss += 1;
    if (cwnd_mss > max_cwnd) {
        cwnd_mss = max_cwnd;
    }
}

double Flow::get_avg_queuing_delay_in_us() {
    return total_queuing_time/received_count * 1000000;
}

double Flow::get_avg_inter_pkt_spacing_in_us() {
    return total_inter_pkt_spacing/received_count * 1000000;
}

// return remaining size in bytes
uint32_t Flow::get_remaining_flow_size() {
    return size - last_unacked_seq - scoreboard_sack_bytes;
}

// assume 'deadline' is set in microseconds
// return remaining deadline in seconds
double Flow::get_remaining_deadline() {
    assert(has_ddl);
    return start_time + deadline / 1e6 - get_current_time();
}

// deadline in terms of the reference time
double Flow::get_deadline() {
    assert(has_ddl);
    return start_time + deadline / 1e6;
}

double Flow::get_expected_trans_time() {
    return get_remaining_flow_size() * 8.0 / params.bandwidth;
}

/* Flow State */
FlowState::FlowState() {
    this->rate = 0;
    this->paused = false;
    this->pause_sw_id = 0;
    this->has_ddl = false;
    this->deadline = 0;
    this->expected_trans_time = 0;
    this->measured_rtt = 0;
    this->inter_probing_time = 0;
    this->removed_from_pq = false;
}
