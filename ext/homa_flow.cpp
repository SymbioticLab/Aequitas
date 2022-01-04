#include "homa_flow.h"

#include <cstdio>

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

HomaFlow::HomaFlow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d,
    uint32_t flow_priority) : Flow(id, start_time, size, s, d, flow_priority) {
        this->channel = s->get_channel(s, d);
    }

void HomaFlow::start_flow() {
    run_priority = flow_priority;
    channel->add_to_channel(this);  // so we can do SRPT
}

int HomaFlow::get_unscheduled_priority() {
    for (int i = 0; i < unscheduled_offsets.size(); i++) {
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
    
    while (next_seq_no < RTTbytes) {    // assuming RTTbytes does not include hdr_size for simplicity
        if (size <= RTTbytes) {
            p = send_with_delay(seq, delay, size, false, priority);
        } else {
            p = send_with_delay(seq, delay, RTTbytes, false, priority);
        }
        next_seq_no += (p->size - hdr_size);
        seq = next_seq_no;
        pkts_sent++;
    }

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Flow[" << id << "] sends " << pkts_sent << " unschedueld pkts." << std::endl;
    }

    return pkts_sent; 
}

int HomaFlow::send_scheduled_data(int grant_priority) {
    // check if not allowed to send (more incoming flows than available scheduled priority levels at the receiver)
    if (grant_priority == -1) {
        return 0;
    }
    uint64_t seq = next_seq_no;
    double delay = 1e-12;
    Packet *p = NULL;
    uint32_t pkts_sent = 0;
    uint32_t bytes_sent_under_grant = 0;
    while ((seq + mss <= end_seq_no) || (seq != end_seq_no && (end_seq_no - seq < mss))) {
        p = send_with_delay(seq, delay, size, true, grant_priority);
        if (seq + mss < size) {
            next_seq_no = seq + mss;
            seq += mss;
        } else {
            next_seq_no = size;
            seq = size;
        }
        pkts_sent++;

        if (retx_event == NULL) {
            set_timeout(get_current_time() + retx_timeout);
        }

        bytes_sent_under_grant += (p->size - hdr_size);
        if (bytes_sent_under_grant >= RTTbytes) {
            break;
        }
    }

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Flow[" << id << "] sends " << pkts_sent << " schedueld pkts." << std::endl;
    }

    return pkts_sent;

}

// sent by receiver to allow sender to send scheduled data with a grant priority
void HomaFlow::send_grant_pkt(uint64_t seq, double pkt_start_ts, int grant_priority) {
    Packet *p = new Grant(
        this,
        seq,
        //0,  // TODO: try made it 0 size so it can't be dropped 
        hdr_size,
        dst,    // Grants are dst -> src
        src,
        grant_priority
        );
    assert(p->pf_priority == 0);
    p->start_ts = pkt_start_ts; // carry the orig packet's start_ts back to the sender for RTT measurement
    channel->get_unscheduled_offsets(p->unscheduled_offsets);       // always piggyback the unschedued offsets back to sender (no matter this flow is scheduled or not)
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "Host[" << src->id << "] sends out Fin Packet[" << p->unique_id << "] from Flow[" << id << "] at time: " << get_current_time() << std::endl;
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
    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "sending out Packet[" << p->unique_id << "] at time: " << get_current_time() + delay << " (base=" << get_current_time() << "; delay=" << delay << ")" << std::endl;
    }
    //PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + delay, p, next_hop);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + delay, p, next_hop);  // adding a pd since we skip the source queue
    add_to_event_queue(event);

    return p;
}

void HomaFlow::send_pending_data() {
    assert(false);
}

void HomaFlow::receive(Packet *p) {
    if (finished) {
        delete p;
        return;
    }

    if (p->type == GRANT_PACKET) {
        Grant *g = dynamic_cast<Grant *>(p);
        receive_grant_pkt(g);
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

void HomaFlow::receive_data_pkt(Packet* p) {
    //p->flow->channel->receive_data_pkt(p);
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
    }
    //std::cout << "Flow[" << id << "] receive_data_pkt: received_count = " << received_count << "; received_bytes = " << received_bytes << std::endl;

    channel->record_flow_size(p->flow, p->scheduled);   // it also triggers calculation of unscheduled priorities

    int grant_priority = 0;
    if (size > RTTbytes) {  // incoming flow is scheduled; decide grant priority for scheduled pkts
        channel->insert_active_flow(p->flow);
        grant_priority = channel->calculate_scheduled_priority(p->flow);
    }

    send_grant_pkt(recv_till, p->start_ts, grant_priority); // Cumulative Ack; grant_priority is DC for unscheduled flows
    if (recv_till == p->flow->size) {
        channel->remove_active_flow(p->flow);   // discards flow state after sending out last response pkt (orig paper S3.8)
    }
}

void HomaFlow::receive_grant_pkt(Grant *p) {
    uint64_t ack = p->seq_no;
    if (next_seq_no < ack) {
        next_seq_no = ack;
    }

    if (params.debug_event_info || (params.enable_flow_lookup && params.flow_lookup_id == id)) {
        std::cout << "HomaFlow[" << id << "] at Host[" << src->id << "] received GRANT packet"
            << "; ack = " << ack << ", next_seq_no = " << next_seq_no << ", last_unacked_seq = " << last_unacked_seq << std::endl;
    }

    // update unscheduled priority and scheduled priority
    unscheduled_offsets = p->unscheduled_offsets;
    int grant_priority = p->grant_priority;

    if (ack > last_unacked_seq) {
        last_unacked_seq = ack;

        // Send the remaining data (scheduled pkts)
        send_scheduled_data(grant_priority);
        //if (ack != size && !finished) {
        //    send_scheduled_data(grant_priority);
        //}

        // TODO: handle pkt loss in Homa
        // Update the retx timer
        if (retx_event != nullptr) { // Try to move
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
