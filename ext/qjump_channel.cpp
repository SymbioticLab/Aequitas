#include "qjump_channel.h"

#include <assert.h>
#include <cstddef>
#include <iostream>
#include <math.h>

#include "qjump_host.h"
#include "../coresim/agg_channel.h"
#include "../coresim/event.h"
#include "../coresim/flow.h"
#include "../coresim/node.h"
#include "../coresim/nic.h"
#include "../coresim/packet.h"
#include "../coresim/queue.h"
#include "../coresim/topology.h"
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

QjumpChannel::QjumpChannel(uint32_t id, Host *s, Host *d, uint32_t priority, AggChannel *agg_channel)
    : Channel(id, s, d, priority, agg_channel) {
        network_epoch = dynamic_cast<QjumpHost *>(s)->network_epoch;   // assuming the host has figured out epoch value at this point
    }

QjumpChannel::~QjumpChannel() {}

// Qjump sends one packet at a time instead of as many pkts as the cwnd allows
// impl copied from Channel::nic_send_next_pkt()
//TODO: assert when choosing Qjump; param.real_nic must be 0
void QjumpChannel::send_pkts() {
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
        seq = next_seq_no;  // unnecessary here; remote later
        pkts_sent++;

        if (retx_event == NULL) {
            set_timeout(get_current_time() + retx_timeout);
        }
    }

    if (params.debug_event_info) {
        std::cout << "Channel[" << id << "] sends " << pkts_sent << " pkts." << std::endl;
    }
    pkt_total_count += pkts_sent;
}

// Note the tiny delay won't be used here since Qjump only send one packet at a time
Packet *QjumpChannel::send_one_pkt(uint64_t seq, uint32_t pkt_size, double delay, Flow *flow) {
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

    if (params.debug_event_info) {
        std::cout << "Qjump sending out Packet[" << p->unique_id << "] (seq=" << seq << ") at time: " << get_current_time() + delay << " (base=" << get_current_time() << "; delay=" << delay << ")" << std::endl;
    }
    Queue *next_hop = topology->get_next_hop(p, src->queue);
    add_to_event_queue(new PacketQueuingEvent(get_current_time() + next_hop->propagation_delay + network_epoch, p, next_hop));
    add_to_event_queue(new QjumpEpochEvent(get_current_time() + network_epoch, src));
    return p;
}
// We won't apply epoch on ACK pkts to prioritize them for Qjump's sake

