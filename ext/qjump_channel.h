#ifndef EXT_QJUMPCHANNEL_H
#define EXT_QJUMPCHANNEL_H

#include "../coresim/channel.h"

class Flow;
class Host;
class Packet;
class AggChannel;

/* QjumpChannel: a single direction src-dst pair per QoS */
// implement Qjump's transport level
// Qjump does not use congestion control, so don't implement CC-related features
// Note QjumpChannel is implemented assuming there exists one QjumpChannel per AggChannel;
// namely params.multiplexing_constant = 1 and params.channel_multiplex == 1
class QjumpChannel : public Channel {
    public:
        QjumpChannel(uint32_t id, Host *s, Host *d, uint32_t priority, AggChannel *agg_channel);
        ~QjumpChannel();

        void add_to_channel(Flow *flow) override;
        int send_pkts() override;
        Packet *send_one_pkt(uint64_t seq, uint32_t pkt_size, double delay, Flow *flow) override;
        void receive_ack(uint64_t ack, Flow *flow, std::vector<uint64_t> sack_list, double pkt_start_ts) override;
        void set_timeout(double time) override;
        void handle_timeout() override;

        double network_epoch;

};

#endif  // EXT_QJUMPCHANNEL_H
