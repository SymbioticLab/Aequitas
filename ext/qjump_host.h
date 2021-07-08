#ifndef EXT_QJUMP_HOST_H
#define EXT_QJUMP_HOST_H

#include "../coresim/node.h"

class Host;
class Packet;
class AggChannel;

/* QjumpChannel: a single direction src-dst pair per QoS */
// implement Qjump's network epoch and rate limiting per priority level (Qjump level)
// Note: Qjump's network epoch is on the per=host level (not per-channel)
class QjumpHost : public Host {
    public:
        QjumpHost(uint32_t id, double rate, uint32_t queue_type, uint32_t host_type);
        ~QjumpHost();

        void set_agg_channels(AggChannel *agg_channel) override;
        void increment_prio_idx();
        void increment_agg_channel_idx();
        void increment_WF_counters();

        void start_next_epoch() override;
        void send_next_pkt() override;

        bool busy;  // whether has sent a pkt during the current epoch
        //std::vector<double> network_epoch;  // each priority level has an epoch value
        double network_epoch;  // each priority level has an epoch value
        uint32_t agg_channel_count;     // among all prio levels
        uint32_t prio_idx;
        std::vector<uint32_t> WF_counters;
        std::vector<uint32_t> agg_channel_idx;
        std::vector<std::vector<AggChannel *>> agg_channels;
};

#endif  // EXT_QJUMPHOST_H
