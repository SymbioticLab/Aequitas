#include "homa_host.h"

#include <cstdio>
#include <assert.h>

#include "factory.h"
#include "../coresim/agg_channel.h"
#include "../coresim/channel.h"
#include "../coresim/event.h"
#include "../coresim/queue.h"
#include "../run/params.h"

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern DCExpParams params;

HomaHost::HomaHost(uint32_t id, double rate, uint32_t queue_type, uint32_t host_type)
        : Host(id, rate, queue_type, HOMA_HOST) {}

HomaHost::~HomaHost() {}

// This single sender-side channel will handle all priority levels (but enforces SRPT)
void HomaHost::set_channel(Channel *channel) {
    auto src_dst_pair = std::make_pair(channel->src, channel->dst);
    assert(channels.find(src_dst_pair) == channels.end());
    channels[src_dst_pair] = channel;
}

Channel *HomaHost::get_channel(Host *src, Host *dst) {
    auto src_dst_pair = std::make_pair(src, dst);
    return channels[src_dst_pair];
}
