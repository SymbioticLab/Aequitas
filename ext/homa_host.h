#ifndef EXT_HOMA_HOST_H
#define EXT_HOMA_HOST_H

#include "../coresim/node.h"
#include <map>

class Channel;
class Host;
class Packet;
class AggChannel;

/* HomaChannel: a single direction src-dst pair */
// Since Homa does not distinguish flow priorities in their design, we don't need to use per-QoS Channel
// In other words, each Channel can contain flows with different priorities assigned by users
// It's fine to skip the HomaHost impl and just use HomaChannel and HomaFlow, but I think this way looks more clean.
class HomaHost : public Host {
    public:
        HomaHost(uint32_t id, double rate, uint32_t queue_type, uint32_t host_type);
        ~HomaHost();

        void set_channel(Channel *channel) override;
        Channel *get_channel(Host *src, Host *dst) override;
    private:
        std::map<std::pair<Host *, Host*>, Channel *> channels;
};

#endif  // EXT_HOMAHOST_H
