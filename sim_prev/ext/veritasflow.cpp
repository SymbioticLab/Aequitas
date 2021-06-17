#include "veritasflow.h"
#include "../coresim/event.h"
#include "../coresim/topology.h"
#include "../run/params.h"

extern double get_current_time(); 
extern void add_to_event_queue(Event *);
extern DCExpParams params;
extern uint32_t num_outstanding_packets;
extern uint32_t max_outstanding_packets;
extern uint32_t duplicated_packets_received;
extern Topology* topology;
extern uint32_t num_timeouts;

VeritasFlow::VeritasFlow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d,
    uint32_t flow_priority) : Flow(id, start_time, size, s, d, flow_priority) {
        //Congestion window parameters
        this->ssthresh = 100000;
        this->count_ack_additive_increase = 0;
        //std::cout << "PUPU Veritas flow priority: " << this->flow_priority << std::endl;

        //assert(params.big_switch);
    }

uint32_t VeritasFlow::send_pkts() {
    uint32_t pkts_sent = 0;
    uint32_t seq = next_seq_no;
    uint32_t window = channel->unused_cwnd * mss + scoreboard_sack_bytes;
    //std::cout << "seq: " << seq << ", window = " << window << ", last_unacked_seq = " << last_unacked_seq << ", sack_bytes = " << scoreboard_sack_bytes << std::endl;
    while (
        (seq + mss <= last_unacked_seq + window) &&
        ((seq + mss <= size) || (seq != size && (size - seq < mss)))
    ) {
        send_with_delay(seq, 1e-9 * (pkts_sent + 1));    // inc by 1 ns each pkt
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

// send with some tiny delay so that pkts from the same batch of a flow is easier to be distinguished by the event comparator
Packet *VeritasFlow::send_with_delay(uint32_t seq, double delay) {
    Packet *p = NULL;

    uint32_t pkt_size;
    if (seq + mss > this->size) {
        pkt_size = this->size - seq + hdr_size;
    } else {
        pkt_size = mss + hdr_size;
    }

    p = new Packet(
            get_current_time(), 
            this, 
            seq, 
            flow_priority,  // all packets follow the same priority of the flow
            pkt_size, 
            src, 
            dst
            );
    this->total_pkt_sent++;
    p->start_ts = get_current_time();

    if (params.unlimited_nic_speed) {
        Queue *next_hop = topology->get_next_hop(p, src->queue);
        add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + delay, p, next_hop));
    } else {
        add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, src->egress_queue));
    }
    /*
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    //PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + delay, p, next_hop);
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + delay, p, next_hop);
    event->flow_id = this->id;
    add_to_event_queue(event);
    */
    //add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, src->queue));

    // packet start from the switch queue; skipping the host queue
    //std::cout << "DEDEDE: PacketQueuingEvent at time:" << get_current_time() << std::endl;
    ////add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, topology->switches[0]->queues[dst->id]));
    return p;
}

/* orig version
Packet *VeritasFlow::send_with_delay(uint32_t seq, double delay) {
    Packet *p = NULL;

    uint32_t pkt_size;
    if (seq + mss > this->size) {
        pkt_size = this->size - seq + hdr_size;
    } else {
        pkt_size = mss + hdr_size;
    }

    p = new Packet(
            get_current_time(), 
            this, 
            seq, 
            flow_priority,  // all packets follow the same priority of the flow
            pkt_size, 
            src, 
            dst
            );
    this->total_pkt_sent++;
    p->start_ts = get_current_time();

    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + delay, p, src->queue);
    event->flow_id = this->id;
    add_to_event_queue(event);
    //add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, src->queue));

    // packet start from the switch queue; skipping the host queue
    //std::cout << "DEDEDE: PacketQueuingEvent at time:" << get_current_time() << std::endl;
    ////add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, topology->switches[0]->queues[dst->id]));
    return p;
}
*/

// another version with priority
Packet *VeritasFlow::send(uint32_t seq) {
    Packet *p = NULL;

    uint32_t pkt_size;
    if (seq + mss > this->size) {
        pkt_size = this->size - seq + hdr_size;
    } else {
        pkt_size = mss + hdr_size;
    }

    p = new Packet(
            get_current_time(), 
            this, 
            seq, 
            flow_priority,  // all packets follow the same priority of the flow
            pkt_size, 
            src, 
            dst
            );
    this->total_pkt_sent++;
    p->start_ts = get_current_time();

    //std::cout << "DE: [2] VeritasFlow::send(): adding new PacketQueuingEvent to queue[" << src->queue->unique_id << "]" << std::endl;
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, src->queue);
    event->flow_id = this->id;
    add_to_event_queue(event);
    //add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, src->queue));

    // packet start from the switch queue; skipping the host queue
    //std::cout << "DEDEDE: PacketQueuingEvent at time:" << get_current_time() << std::endl;
    ////add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, topology->switches[0]->queues[dst->id]));
    return p;
}


void VeritasFlow::send_ack(uint32_t seq, std::vector<uint32_t> sack_list, double pkt_start_ts) {
    Packet *p = new Ack(this, seq, sack_list, hdr_size, dst, src);  //Acks are dst->src
    p->pf_priority = flow_priority;
    p->start_ts = pkt_start_ts; // carry the orig packet's start_ts back to the sender for RTT measurement
    //std::cout << "Flow[" << id << "] with priority " << flow_priority << " send ack: " << seq << std::endl;
    /*
    Queue *next_hop = topology->get_next_hop(p, dst->queue);
    ////PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, next_hop);      // currnet version: skip src queue & include dst queue
    PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop);      // currnet version: skip src queue & include dst queue
    ////PacketQueuingEvent *event = new PacketQueuingEvent(get_current_time(), p, dst->queue);  // orig version: include src queue & skip dst queue
    event->flow_id = this->id;
    add_to_event_queue(event);
    //add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, dst->queue));
    */
    if (params.unlimited_nic_speed) {
        Queue *next_hop = topology->get_next_hop(p, dst->queue);
        add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay, p, next_hop));
    } else {
        add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, dst->egress_queue));
    }
}


