#include "homa_flow.h"

#include <cstdio>
#include <assert.h>

#include "../coresim/channel.h"
#include "../coresim/event.h"
#include "../coresim/node.h"
#include "../coresim/packet.h"
#include "../coresim/queue.h"
#include "../coresim/topology.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern DCExpParams params;
extern Topology* topology;
extern std::vector<uint32_t> num_timeouts;

HomaFlow::HomaFlow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d,
    uint32_t flow_priority) : Flow(id, start_time, size, s, d, flow_priority) {
        this->channel = s->get_channel(s, d);
        offset_under_curr_grant_send = 0;
        offset_under_curr_grant_recv = 0;
    }

void HomaFlow::start_flow() {
    run_priority = flow_priority;
    channel->add_to_channel(this);
}

int HomaFlow::get_unscheduled_priority() {
    if (unscheduled_offsets.empty()) {
        return 0;
    }

    for (size_t i = 0; i < unscheduled_offsets.size(); i++) {
        if (size <= unscheduled_offsets[i]) {
            return i;
        }
    }

    return num_hw_prio_levels - 1;
}

int HomaFlow::send_unscheduled_data() {
    Packet *p = NULL;
    uint32_t pkts_sent = 0;
    double delay = 1e-12;
    uint64_t seq = next_seq_no;
    int priority = get_unscheduled_priority();
    
    while (next_seq_no < params.homa_rtt_bytes && next_seq_no < size) {    // assuming RTTbytes does not include hdr_size for simplicity
        if (size <= params.homa_rtt_bytes) {
            p = send_with_delay(seq, delay * (pkts_sent + 1), size, false, priority);
        } else {
            p = send_with_delay(seq, delay * (pkts_sent + 1), params.homa_rtt_bytes, false, priority);
        }
        next_seq_no += (p->size - hdr_size);
        //std::cout << "next_seq_no: " << next_seq_no << std::endl;
        seq = next_seq_no;
        pkts_sent++;
    }

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Flow[" << id << "] sends " << pkts_sent << " unschedueld pkts." << std::endl;
    }

    return pkts_sent; 
}

int HomaFlow::send_scheduled_data() {
    // check if not allowed to send (more incoming flows than available scheduled priority levels at the receiver)
    if (grant_priority == -1) {
        return 0;
    }
    uint64_t seq = next_seq_no;
    double delay = 1e-12;
    Packet *p = NULL;
    uint32_t pkts_sent = 0;
    uint32_t bytes_sent_under_grant = 0;
    while ((seq + mss <= size) || (seq != size && (size - seq < mss))) {
        p = send_with_delay(seq, delay * (pkts_sent + 1), size, true, grant_priority);
        if (seq + mss < size) {
            next_seq_no = seq + mss;
            seq += mss;
        } else {
            next_seq_no = size;
            seq = size;
        }
        pkts_sent++;

        bytes_sent_under_grant += (p->size - hdr_size);     // safe to do this since rtt_bytes is in the unit of MSS
        if (bytes_sent_under_grant >= params.homa_rtt_bytes) {
            break;
        }
        if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
            std::cout << "<><><> seq: " << seq << ", next_seq_no: " << next_seq_no << std::endl;
        }
    }
    offset_under_curr_grant_send = seq;  // used by sender to match server's 'ack' in grant pkt

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Flow[" << id << "] sends " << pkts_sent << " schedueld pkts. offset_under_curr_grant_send = " << offset_under_curr_grant_send << std::endl;
    }

    return pkts_sent;

}

// sent by receiver to allow sender to send scheduled data with a grant priority
void HomaFlow::send_grant_pkt(uint64_t seq, double pkt_start_ts, int grant_priority) {
    assert(grant_priority >= 0);
    Packet *p = new Grant(
        this,
        seq,
        hdr_size,
        dst,    // Grants are dst -> src
        src,
        grant_priority
        );
    assert(p->pf_priority == 0);
    p->start_ts = pkt_start_ts; // carry the orig packet's start_ts back to the sender for RTT measurement
    channel->get_unscheduled_offsets(p->unscheduled_offsets);       // always piggyback the unschedued offsets back to sender (no matter this flow is scheduled or not)
    Queue *next_hop = topology->get_next_hop(p, dst->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << dst->id << "] sends out Grant Packet[" << p->unique_id << "] (ack = " << seq << ", grant prio = " << grant_priority << ") from Flow[" << id << "] at time: " << get_current_time() << std::endl;
    }
}

Packet *HomaFlow::send_with_delay(uint64_t seq, double delay, uint64_t end_seq_no, bool scheduled, int priority) {
    Packet *p = NULL;
    uint32_t pkt_size;
    if (seq + mss > end_seq_no) {
        pkt_size = end_seq_no - seq + hdr_size;
    } else {
        pkt_size = mss + hdr_size;
    }

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
    if (scheduled) {
        p->scheduled = true;
    } else {
        p->scheduled = false;
    }

    Queue *next_hop = topology->get_next_hop(p, src->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        if (scheduled) {
            std::cout << "Flow[" << id << "] from Host[" << src->id << "] sends out scheduled Packet[" << p->unique_id << "] (seq = " << seq << ", prio=" << priority << ") at time: " << get_current_time() + delay << std::endl;
        } else {
            std::cout << "Flow[" << id << "] from Host[" << src->id << "] sends out unscheduled Packet[" << p->unique_id << "] (seq = " << seq << ", prio=" << priority << ") at time: " << get_current_time() + delay << std::endl;
        }
    }

    return p;
}

void HomaFlow::send_pending_data() {
    if (!has_received_grant) {
        send_unscheduled_data();
    } else {
        send_scheduled_data();
    }

    if (retx_sender_event == NULL) {
        set_timeout_sender(get_current_time() + retx_timeout);
    }
}

// called by receiver
void HomaFlow::send_resend_pkt(uint64_t seq, int grant_priority, bool is_sender_resend) {
    Packet *p = NULL;
    Queue *next_hop = NULL;
    if (is_sender_resend) {
        p = new Resend(
            this,
            seq,
            hdr_size,
            src,        // from sender to receiver
            dst,
            grant_priority
            );
        next_hop = topology->get_next_hop(p, src->queue);
        p->is_sender_resend = is_sender_resend;
    } else {
        p = new Resend(
            this,
            seq,
            hdr_size,
            dst,        // from receiver to sender
            src,
            grant_priority
            );
        next_hop = topology->get_next_hop(p, dst->queue);
        channel->get_unscheduled_offsets(p->unscheduled_offsets);       // always piggyback the unschedued offsets back to sender (no matter this flow is scheduled or not)
    }
    assert(p->pf_priority == 0);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);
    add_to_event_queue(event);

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        if (is_sender_resend) {
            std::cout << "Host[" << src->id << "] (sender) sends out Resend Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
        } else {
            std::cout << "Host[" << dst->id << "] (receiver) sends out Resend Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
        }
    }
}

void HomaFlow::receive(Packet *p) {
    if (finished) {
        delete p;
        return;
    }

    if (p->type == GRANT_PACKET) {
        receive_grant_pkt(p);
    } else if (p->type == NORMAL_PACKET) {
        if (this->first_byte_receive_time == -1) {
            this->first_byte_receive_time = get_current_time();
        }
        this->receive_data_pkt(p);
    } else if (p->type == RESEND_PACKET) {
        receive_resend_pkt(p); 
    }
    else {
        assert(false);
    }

    delete p;
}

void HomaFlow::receive_data_pkt(Packet* p) {
    received_count++;
    total_queuing_time += p->total_queuing_delay;

    if (received.count(p->seq_no) == 0) {
        received[p->seq_no] = true;
        received_bytes += (p->size - hdr_size);
    }
    if (p->seq_no > max_seq_no_recv) {
        max_seq_no_recv = p->seq_no;
    }
    // Determing which ack to send
    // Yiwen: For simplicity, assume RTTbytes are alwyas N * mss
    uint64_t s = recv_till;
    bool in_sequence = true;
    //std::vector<uint64_t> sack_list;
    while (s <= max_seq_no_recv) {
        if (received.count(s) > 0) {
            if (in_sequence) {
                if (recv_till + mss > this->size) {
                    recv_till = this->size;
                } else {
                    recv_till += mss;
                }
            } else {
                //sack_list.push_back(s);
            }
        } else {
            in_sequence = false;
        }
        s += mss;
        if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
            std::cout << "[][][] s: " << s << ", recv_till: " << recv_till << ", max_seq_no_recv: "<< max_seq_no_recv << ", in_sequence: " << in_sequence << std::endl;
        }
    }

    if (size > params.homa_rtt_bytes) {
        if (offset_under_curr_grant_recv == 0) {
            offset_under_curr_grant_recv = params.homa_rtt_bytes;
        }
    }

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Flow[" << id << "] receive_data_pkt: received_count = " << received_count << "; received_bytes = " << received_bytes
            << "; max_seq_no_recv = " << max_seq_no_recv << "; recv_till: " << recv_till <<  "; offset_under_curr_grant_recv = " << offset_under_curr_grant_recv << std::endl;
    }

    channel->record_flow_size(this, p->scheduled);   // it also triggers calculation of unscheduled priorities

    int grant_priority = 0;
    if (size > params.homa_rtt_bytes) {  // incoming flow is scheduled; decide grant priority for scheduled pkts
        channel->insert_active_flow(this);
        grant_priority = channel->calculate_scheduled_priority(this);
        // if does not provide grant (grant_priority = -1), mark flow as inactive (i.e., remove it from active flow list)
        if (grant_priority == -1) {
            channel->remove_active_flow(this);
        }
    }

    // even if grant_priority = -1, we will still send out the grant so that those small flows get finish notification
    // of course this can be done more elegantly, but simplicity is the best

    // We send out the grant pkt in 2 cases:
    // (1) if the flow's size < rtt_bytes, send the grant when received bytes reach the flow's size (so it gets finish notification)
    // (2) if we have received all the bytes (i.e., # of rtt_bytes) under the current grant
    if (size < params.homa_rtt_bytes) {
        if (size == recv_till) {
            send_grant_pkt(recv_till, p->start_ts, 0); // grant_priority is DC for unscheduled flows; set it to 0 to pass the assertion
        }
    } else {
        if (offset_under_curr_grant_recv == recv_till) {
            if (grant_priority >= 0) {
                if (offset_under_curr_grant_recv + params.homa_rtt_bytes < size) {
                        offset_under_curr_grant_recv += params.homa_rtt_bytes;
                    } else {
                        offset_under_curr_grant_recv = size;     // will set to size twice, which is fine
                }
                send_grant_pkt(recv_till, p->start_ts, grant_priority);
            } else {
                channel->add_to_grant_waitlist(this);       // add to waitlist and handle it again when sche prio levels become available
            }
        }
    }
    ////send_grant_pkt(recv_till, p->start_ts, grant_priority); // Cumulative Ack; grant_priority is DC for unscheduled flows

    if (recv_till == size) {        // if have received all the bytes from the sender message
        channel->remove_active_flow(this);   // discards flow state after sending out last response pkt (orig paper S3.8)
        cancel_retx_event();
        channel->handle_flow_from_waitlist();   // try waitlist flow after an active flow "finishes"
    } else {
        // set receiver-side timeout
        if (retx_event == NULL) {
            set_timeout(get_current_time() + retx_timeout);
        } else {
            cancel_retx_event();
            if (last_unacked_seq < size) {
                double timeout = get_current_time() + retx_timeout;
                set_timeout(timeout);
            }
        }
    }

}

// used when pulling a flow from the waitlist to resend its grant pkt
void HomaFlow::resend_grant() {
    if (recv_till != offset_under_curr_grant_recv) {
        std::cout << "PUPU: Flow[" << id << "] recv_till = " << recv_till << ", offset_under_curr_grant_recv = " << offset_under_curr_grant_recv << std::endl;
    }
    assert(recv_till == offset_under_curr_grant_recv);
    if (offset_under_curr_grant_recv + params.homa_rtt_bytes < size) {
            offset_under_curr_grant_recv += params.homa_rtt_bytes;
        } else {
            offset_under_curr_grant_recv = size;     // will set to size twice, which is fine
    }
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "At time: " << get_current_time() << ", HomaFlow[" << id << "] at Host[" << src->id << "] resend grant pkt, "
        << "recv_till = " << recv_till << "; grant prio = " << grant_priority << std::endl;
    }
    send_grant_pkt(recv_till, 0, grant_priority);
}

void HomaFlow::receive_grant_pkt(Packet *packet) {
    Grant *p = dynamic_cast<Grant *>(packet);
    has_received_grant = true;

    uint64_t ack = p->seq_no;
    if (next_seq_no < ack) {
        next_seq_no = ack;
    }

    // update unscheduled priority and scheduled priority
    unscheduled_offsets = p->unscheduled_offsets;
    grant_priority = p->grant_priority;

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "HomaFlow[" << id << "] at Host[" << src->id << "] received GRANT packet"
            << "; grant_prio = " << grant_priority << ", ack = " << ack << ", next_seq_no = "
            << next_seq_no << ", last_unacked_seq = " << last_unacked_seq << "; offset_under_curr_grant_send = "
            << offset_under_curr_grant_send << "; offset_under_curr_grant_recv = " << offset_under_curr_grant_recv << std::endl;
    }

    // since we receive a new response message, update the sender-side retx timer
    //if (retx_sender_event == NULL) {
    //    set_timeout_sender(get_current_time() + retx_timeout);
    //} else {
    cancel_retx_sender_event();
    set_timeout_sender(get_current_time() + retx_timeout);
    //}

    if (ack > last_unacked_seq) {
        last_unacked_seq = ack;
    }

    if (p->grant_priority == -1 && ack < size) {
        return;
    }

    // send the next rtt_bytes of scheduled data
    channel->add_to_channel(this);

    // Send the remaining data (scheduled pkts) when the server has received all scheduled data in the last grant
    // which is option (1) (the one Homa paper uses)
    //if (offset_under_curr_grant == ack && ack != size) {
    //    channel->add_to_channel(this);
    //}


    // option (2) is immediately sending out next round after receiving a single ack
    // if (ack > last_unacked_seq) { channel->add_to_channel(this); }


    if (ack == size && !finished) {
        finished = true;
        cancel_retx_sender_event();
        received.clear();
        finish_time = get_current_time();
        flow_completion_time = finish_time - start_time;
        FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
        add_to_event_queue(ev);
    }
}

void HomaFlow::receive_resend_pkt(Packet *packet) {
    Resend *p = dynamic_cast<Resend *>(packet);
    if (p->is_sender_resend) {  // receiver receives resend pkt from sender (orig paper, S3.7)
        int grant_priority = 0;
        if (size > params.homa_rtt_bytes) {  // incoming flow is scheduled; decide grant priority for scheduled pkts
            channel->insert_active_flow(this);
            grant_priority = channel->calculate_scheduled_priority(this);
            channel->remove_active_flow(this);      // always remove it first since we haven't received the next data pkts
        }
        send_resend_pkt(recv_till, grant_priority, false); // Cumulative Ack; grant_priority is DC for unscheduled flows
        if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
            std::cout << "HomaFlow[" << id << "] at Host[" << dst->id << "] (receiver) received RESEND packet"
                << "; receiver sends resend pkt (recv_till: " << recv_till << ", grant_priority: " << grant_priority << ")" << std::endl;
        }
        return;
    }

    // When incoming RESEND pkt is sent by the receiver, sender tries to retransmit
    uint64_t ack = p->seq_no;

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "HomaFlow[" << id << "] at Host[" << src->id << "] (sender) received RESEND packet"
            << "; grant_prio = " << p->grant_priority << ", ack = " << ack << ", next_seq_no = " << next_seq_no << ", last_unacked_seq = " << last_unacked_seq << std::endl;
    }

    unscheduled_offsets = p->unscheduled_offsets;
    grant_priority = p->grant_priority;

    if (p->grant_priority == -1 && ack < size) {        // until we receive a positive grant
        return;
    }

    if (ack > last_unacked_seq) {
        last_unacked_seq = ack;
    }

    next_seq_no = ack;              // retransmit from ack
    channel->add_to_channel(this);
}

void HomaFlow::set_timeout(double time) {
    if (last_unacked_seq < size) {
        RetxTimeoutEvent *ev = new RetxTimeoutEvent(time, this);
        add_to_event_queue(ev);
        retx_event = ev;
    }
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "At time: " << get_current_time() << ", HomaFlow[" << id << "] set next timeout at " << time << std::endl;
    }
}

void HomaFlow::set_timeout_sender(double time) {
    if (last_unacked_seq < size) {
        RetxTimeoutSenderEvent *ev = new RetxTimeoutSenderEvent(time, this);
        add_to_event_queue(ev);
        retx_sender_event = ev;
    }
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "At time: " << get_current_time() << ", HomaFlow[" << id << "] set next sender timeout at " << time << std::endl;
    }
}

void HomaFlow::handle_timeout() {
    ////next_seq_no = last_unacked_seq;     // sender should update this
    num_timeouts[flow_priority]++;

    int grant_priority = 0;
    if (size > params.homa_rtt_bytes) {  // incoming flow is scheduled; decide grant priority for scheduled pkts
        channel->insert_active_flow(this);
        grant_priority = channel->calculate_scheduled_priority(this);
        channel->remove_active_flow(this);      // always remove it first since we haven't received the next data pkts
    }
    send_resend_pkt(recv_till, grant_priority, false); // Cumulative Ack; grant_priority is DC for unscheduled flows

    set_timeout(get_current_time() + retx_timeout);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "At time: " << get_current_time() << ", HomaFlow[" << id << "] handle timeout events" << std::endl;
    }
}

void HomaFlow::handle_timeout_sender() {
    send_resend_pkt(0, -1, true);

    set_timeout(get_current_time() + retx_timeout);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "At time: " << get_current_time() << ", HomaFlow[" << id << "] handle sender timeout events" << std::endl;
    }
}
