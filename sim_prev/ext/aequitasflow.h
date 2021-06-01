#ifndef Veritas_FLOW_H
#define Veritas_FLOW_H

#include "../coresim/flow.h"
#include "../coresim/packet.h"
#include "../coresim/node.h"
#include "../coresim/channel.h"

class VeritasFlow : public Flow {
    public:
        VeritasFlow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d,
            uint32_t flow_priority);
        uint32_t ssthresh;
        uint32_t count_ack_additive_increase;
        uint32_t send_pkts() override;
        Packet *send(uint32_t seq) override;
        Packet *send_with_delay(uint32_t seq, double delay);
        void send_ack(uint32_t seq, std::vector<uint32_t> sack_list, double pkt_start_ts) override;
};

#endif
