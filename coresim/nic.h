#ifndef CORESIM_NIC_H
#define CORESIM_NIC_H

#include <stdint.h>
#include <vector>
#include <deque>

class Host;
class AggChannel;
class Channel;
//class HostEgressQueue;

// Used to ensure packets going out of a host do not exceed line rate;
// Gets constructed in Host's constructor.
// Basic idea:
//     NIC instructs Channels to send one packet each time based on the line rate,
//     The next recurring event gets triggered based on (packet_size / line_rate).
// Note that:
// (1) Channel no longer sends as many packets as the cwnd allows, but wait until the nic to say so.
//     Everytime only 1 packet gets sent. cwnd is still controlled by AggChannel.
// (2) We use Round-Robin among active channels when selecting the next Channel to send the packet.
// (3) Although the data packets are not generated until the NIC allows to send, the Ack packets
//     on the other hand is generated once data packet arrives, and thus needs to be put in a infinitely long queue.
//     Note: this feature is a TODO. Current impl is to do the Ack the old way given it won't affect the results too much.
// (4) The 'busy' variable is used to allow nic to rest when no packets are found instead of creating
//     too many useless events.
class NIC {
  public:
    NIC(Host *s, double rate);
    ~NIC();
    void set_agg_channels(AggChannel* agg_channel);
    void increment_prio_idx();
    void increment_agg_channel_idx();
    void increment_WF_counters();
    void add_to_nic(Channel *channel);
    void start_nic();
    void send_pkts();
    uint32_t send_next_pkt(Channel *channel);

    bool busy;
    uint32_t agg_channel_count;     // among all prio levels
    uint32_t prio_idx;
    std::vector<uint32_t> WF_counters;
    std::vector<uint32_t> agg_channel_idx;
    double rate;
    Host *src;
    std::vector<std::vector<AggChannel *>> agg_channels;
    std::vector<Channel *> last_active_channels;
    std::deque<Channel *> pending_channels;
    //HostEgressQueue *ack_queue;

};

#endif // CORESIM_NIC_H
