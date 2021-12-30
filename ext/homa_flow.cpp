#include "homa_flow.h"

#include <cstdio>

#include "../coresim/channel.h"
#include "../coresim/event.h"
#include "../coresim/packet.h"
#include "../coresim/queue.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern DCExpParams params;
extern Topology* topology;

HomaFlow::HomaFlow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d,
    uint32_t flow_priority) : Flow(id, start_time, size, s, d, flow_priority) {}

void HomaFlow::start_flow() {
    run_priority = flow_priority;
    channel->add_to_channel(this);  // so we can do SRPT
}

int HomaFlow::send_unscheduled_data() {
    Packet *p = NULL;
    uint32_t pkts_sent = 0;
    double delay = 1e-12;
    uint64_t seq = next_seq_no;
    int priority = channel->priority_unscheduled;
    while (next_seq_no < RTTbytes) {    // assuming RTTbytes does not include hdr_size for simplicity
        if (size <= RTTbytes) {
            p = send_with_delay(seq, delay, size, priority);
        } else {
            p = send_with_delay(seq, delay, RTTbytes, priority);
        }
        bytes_sent += p->size;      // just for bookkeeping; should always use next_seq_no
        next_seq_no += (p->size - hdr_size);
        seq = next_seq_no;
        pkts_sent++;
    }

    return pkts_sent; 
}

Packet *HomaFlow::send_with_delay(uint64_t seq, double delay, uint64_t end_seq_no, int priority) {
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
}

void HomaFlow::receive_data_pkt(Packet* p) {
    p->flow->channel->receive_data_pkt(p);
}

void HomaFlow::receive_ack(uint64_t ack, std::vector<uint64_t> sack_list, double pkt_start_ts, uint32_t priority, uint32_t num_hops) {
    channel->receive_ack(ack, this, sack_list, pkt_start_ts);
}
