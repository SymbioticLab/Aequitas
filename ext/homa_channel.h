#ifndef EXT_HOMACHANNEL_H
#define EXT_HOMACHANNEL_H

#include "../coresim/channel.h"

class Flow;
class Host;
class Packet;
class AggChannel;

struct FlowComparator {
    bool operator() (Flow *a, Flow *b) {
        if (a->size == b->size) {
            return a->start_time > b->start_time;
        } else {
            return a->size > b->size;
        }
    }
};

/* HomaChannel: a single direction src-dst pair */
// Homa does not distinguish among different user priorities. Thus we will handle all priority traffic on the same channel
// also implements sender-side SRPT
class HomaChannel : public Channel {
    public:
        HomaChannel(uint32_t id, Host *s, Host *d, uint32_t priority, AggChannel *agg_channel);
        ~HomaChannel();

        void add_to_channel(Flow *flow) override;
        int next_flow_SRPT();
        int send_pkts() override;
        Packet *send_one_pkt(uint64_t seq, uint32_t pkt_size, double delay, Flow *flow) override;
        void receive_ack(uint64_t ack, Flow *flow, std::vector<uint64_t> sack_list, double pkt_start_ts) override;
        void set_timeout(double time) override;
        void handle_timeout() override;

    private:
        std::priority_queue<Flow*, std::vector<Flow*>, FlowComparator> sender_flows;
        uint64_t curr_end_seq;

};

#endif  // EXT_HOMACHANNEL_H
