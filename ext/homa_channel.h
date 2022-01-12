#ifndef EXT_HOMACHANNEL_H
#define EXT_HOMACHANNEL_H

#include "../coresim/channel.h"
#include <map>
#include <set>
#include <vector>
#include <queue>


class Flow;
class Host;
class Packet;
class AggChannel;

struct FlowComparatorHoma {
    bool operator() (Flow *a, Flow *b);
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
        void insert_active_flow(Flow *flow) override;
        void remove_active_flow(Flow *flow) override;
        int calculate_scheduled_priority(Flow *flow) override;
        void calculate_unscheduled_offsets() override;
        void get_unscheduled_offsets(std::vector<uint32_t> &vec) override;
        void record_flow_size(Flow* flow, bool scheduled) override;
        void add_to_grant_waitlist(Flow *flow) override;
        int get_sender_priority();
        void handle_flow_from_waitlist() override;


    private:
        int record_freq;
        std::priority_queue<Flow*, std::vector<Flow*>, FlowComparatorHoma> sender_flows;
        //std::map<Flow *, int> active_flows;            // flows with size > RTTbytes; maintained by receiver
        std::set<Flow *> active_flows;            // flows with size > RTTbytes; maintained by receiver
        std::vector<uint32_t> sampled_scheduled_flow_size;
        std::vector<uint32_t> sampled_unscheduled_flow_size;
        std::vector<uint32_t> curr_unscheduled_offsets;
        uint32_t curr_unscheduled_prio_levels;
        std::set<Flow *> sampled_scheduled_flows;
        std::set<Flow *> sampled_unscheduled_flows;
        std::deque<Flow *> grant_waitlist;
        std::set<uint32_t> waitlist_set;

};

#endif  // EXT_HOMACHANNEL_H
