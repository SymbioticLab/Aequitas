#ifndef EXT_FACTORY_H
#define EXT_FACTORY_H

#include <cstdint>
#include <vector>

/* Queue types */
#define PFABRIC_QUEUE 2
#define WF_QUEUE 6
#define QJUMP_QUEUE 7
#define D3_QUEUE 8
#define PDQ_QUEUE 9
#define HOMA_QUEUE 10

/* Flow types */
#define PFABRIC_FLOW 2
#define AEQUITAS_FLOW 6
#define QJUMP_FLOW 7
#define D3_FLOW 8
#define PDQ_FLOW 9
#define HOMA_FLOW 10

/* Host types */
#define NORMAL_HOST 1
#define QJUMP_HOST 7
#define HOMA_HOST 10

class Flow;
class Host;
class Queue;
class Channel;
class AggChannel;

class Factory {
    public:
        static int flow_counter;
        static Flow *get_flow(
                uint32_t id,
                double start_time,
                uint32_t size,
                Host *src,
                Host *dst,
                uint32_t flow_type,
                uint32_t flow_priority = 0,
                double paced_rate = 0.0
                );

        static Flow *get_flow(
                double start_time,
                uint32_t size,
                Host *src,
                Host *dst,
                uint32_t flow_type,
                uint32_t flow_priority = 0,
                double paced_rate = 0.0
                );

        static Channel *get_channel(
                uint32_t id,
                Host *s,
                Host *d,
                uint32_t priority,
                AggChannel *agg_channel,
                uint32_t flow_type
        );

        static Queue *get_queue(
                uint32_t id,
                double rate,
                uint32_t queue_size,
                uint32_t type,
                double drop_prob,
                int location,
                std::vector<int> weights
                );

        static Host* get_host(
                uint32_t id,
                double rate,
                uint32_t queue_type,
                uint32_t host_type
                );
};

#endif  // EXT_FACTORY_H
