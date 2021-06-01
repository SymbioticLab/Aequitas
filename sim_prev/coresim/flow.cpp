#include <math.h>
#include <iostream>
#include <assert.h>

#include "flow.h"
#include "packet.h"
#include "event.h"
#include "channel.h"

#include "../run/params.h"

extern double get_current_time(); 
extern void add_to_event_queue(Event *);
extern int get_event_queue_size();
extern DCExpParams params;
extern uint32_t num_outstanding_packets;
extern uint32_t max_outstanding_packets;
extern uint32_t duplicated_packets_received;
extern std::vector<std::map<std::pair<Host *, Host *>, Channel *>> channels;
extern std::vector<std::vector<double>> per_pkt_lat;
extern std::vector<std::vector<double>> per_pkt_rtt;

Flow::Flow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d) {
    this->id = id;
    this->start_time = start_time;
    this->finish_time = 0;
    this->size = size;
    this->src = s;
    this->dst = d;

    this->next_seq_no = 0;
    this->last_unacked_seq = 0;
    this->retx_event = NULL;
    this->flow_proc_event = NULL;

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
    this->first_byte_send_time = -1;
    this->first_byte_receive_time = -1;
    this->first_hop_departure = 0;
    this->last_hop_departure = 0;
}

Flow::Flow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d, uint32_t flow_priority) :
    Flow(id, start_time, size, s, d) {
    this->flow_priority = flow_priority;
    // Find its channel by looking at its src-dst and priority
    channel = channels[flow_priority][std::make_pair(src, dst)];
    ack_channel = channels[flow_priority][std::make_pair(dst, src)];
}

Flow::~Flow() {
    //  packets.clear();
}

void Flow::start_flow() {
    send_pending_data();
}

// Original send_pending_data() before the Channel impl
/*
void Flow::send_pending_data() {
    if (received_bytes < size) {
        uint32_t seq = next_seq_no;
        uint32_t window = cwnd_mss * mss + scoreboard_sack_bytes;
        while (
            (seq + mss <= last_unacked_seq + window) &&
            ((seq + mss <= size) || (seq != size && (size - seq < mss)))
        ) {
            // TODO Make it explicit through the SACK list
            if (received.count(seq) == 0) {
                send(seq);
            }

            if (seq + mss < size) {
                next_seq_no = seq + mss;
                seq += mss;
            } else {
                next_seq_no = size;
                seq = size;
            }

            if (retx_event == NULL) {
                set_timeout(get_current_time() + retx_timeout);
            }
        }
    }
}
*/

// send_pending_data() via a selected Channel to achieve per-channel CC
void Flow::send_pending_data() {
    //std::cout << "Flow[" << id << "] send_pending_data: received_bytes = " << received_bytes << std::endl;
    ////
    /*
    if (received_bytes < size) {
        // append to Channel's pending_list and waits for the token to send a packet
        channel->add_to_list(this);
    }
    */
    ////
    channel->add_to_list(this);
}

void Flow::send_one_pkt() {
    if (received.count(next_seq_no) == 0) {
        send(next_seq_no);
    }

    if (next_seq_no + mss < size) {
        next_seq_no += mss;
    } else {
        next_seq_no = size;
    }

    if (retx_event == NULL) {
        set_timeout(get_current_time() + retx_timeout);
    }
}

// Called by Channel
// TODO: check whether sack is correctly implemented in the orig simulator
// since I see some TODOs related to SACK in the orig code
uint32_t Flow::send_pkts() {
    uint32_t pkts_sent = 0;
    uint32_t seq = next_seq_no;
    uint32_t window = channel->unused_cwnd * mss + scoreboard_sack_bytes;
    //std::cout << "seq: " << seq << ", window = " << window << ", last_unacked_seq = " << last_unacked_seq << ", sack_bytes = " << scoreboard_sack_bytes << std::endl;
    while (
        (seq + mss <= last_unacked_seq + window) &&
        ((seq + mss <= size) || (seq != size && (size - seq < mss)))
    ) {
        // TODO Make it explicit through the SACK list
        ////
        //if (received.count(seq) == 0) {
        //    send(seq);
        //    pkts_sent++;
        //}
        ////
        send(seq);
        pkts_sent++;

        if (seq + mss < size) {
            next_seq_no = seq + mss;
            seq += mss;
        } else {
            next_seq_no = size;
            seq = size;
        }

        if (retx_event == NULL) {
            set_timeout(get_current_time() + retx_timeout);
        }
        //std::cout << "seq: " << seq << ", window = " << window << ", last_unacked_seq = " << last_unacked_seq << ", sack_bytes = " << scoreboard_sack_bytes << std::endl;
    }
    //std::cout << "Flow[" << id << "] sends " << pkts_sent << " pkts." << std::endl;
    //assert(pkts_sent > 0);
    return pkts_sent;
}

// Note: VeritasFlow overrides send() and send_ack()
Packet *Flow::send(uint32_t seq) {
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

    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, src->queue);
    event->flow_id = this->id;
    add_to_event_queue(event);
    //add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, src->queue));
    return p;
}

void Flow::send_ack(uint32_t seq, std::vector<uint32_t> sack_list, double pkt_start_ts) {
    Packet *p = new Ack(this, seq, sack_list, hdr_size, dst, src);  //Acks are dst->src
    p->start_ts = pkt_start_ts; // carry the orig packet's start_ts back to the sender for RTT measurement
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, dst->queue);
    event->flow_id = this->id;
    add_to_event_queue(event);
    //add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, dst->queue));
}

void Flow::receive_ack(uint32_t ack, std::vector<uint32_t> sack_list, double pkt_start_ts, uint32_t priority) {
    //std::cout << "Flow[" << id << "] with priority " << flow_priority << " receive ack: " << ack << std::endl;
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
        //if (rtt > params.cc_delay_target)
        //    std::cout << "rtt measurements: " << rtt << std::endl;
        per_pkt_rtt[priority].push_back(rtt);

        //// Flow no longer adjust cwnd by itself. Channel handles it instead.
        //increase_cwnd();
        channel->report_ack(this, rtt);

        // Send the remaining data
        send_pending_data();

        // Update the retx timer
        if (retx_event != NULL) { // Try to move
            cancel_retx_event();
            if (last_unacked_seq < size) {
                // Move the timeout to last_unacked_seq
                double timeout = get_current_time() + retx_timeout;
                set_timeout(timeout);
            }
        }

    }

    if (ack == size && !finished) {
        finished = true;
        received.clear();
        finish_time = get_current_time();
        flow_completion_time = finish_time - start_time;
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
        Ack *a = (Ack *) p;
        receive_ack(a->seq_no, a->sack_list, a->start_ts, p->pf_priority);
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
    per_pkt_lat[p->pf_priority].push_back(p->total_queuing_delay);
    //std::cout << "PUPU: total queuing delay: " << total_queuing_time << std::endl;

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
    uint32_t s = recv_till;
    bool in_sequence = true;
    std::vector<uint32_t> sack_list;
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
    if (last_unacked_seq < size) {
        RetxTimeoutEvent *ev = new RetxTimeoutEvent(time, this);
        add_to_event_queue(ev);
        retx_event = ev;
    }
}


// Flow still handles timeout by itself, but now report timeout to Channel
//, which manages cwnd and decrease it accordingly.
void Flow::handle_timeout() {
    next_seq_no = last_unacked_seq;
    //Reset congestion window to 1
    //cwnd_mss = 1;
    channel->report_timeout(this); 
    send_pending_data(); //TODO Send again
    set_timeout(get_current_time() + retx_timeout);  // TODO
}


void Flow::cancel_retx_event() {
    if (retx_event) {
        retx_event->cancelled = true;
    }
    retx_event = NULL;
}


uint32_t Flow::get_priority(uint32_t seq) {
    if (params.flow_type == 1) {
        return 1;
    }
    if(params.deadline && params.schedule_by_deadline)
    {
        return (int)(this->deadline * 1000000);
    }
    else{
        return (size - last_unacked_seq - scoreboard_sack_bytes);
    }
}


// No longer used after Channel impl
void Flow::increase_cwnd() {
    cwnd_mss += 1;
    if (cwnd_mss > max_cwnd) {
        cwnd_mss = max_cwnd;
    }
}

double Flow::get_avg_queuing_delay_in_us()
{
    return total_queuing_time/received_count * 1000000;
}

double Flow::get_avg_inter_pkt_spacing_in_us() {
    return total_inter_pkt_spacing/received_count * 1000000;
}
