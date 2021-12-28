#ifndef EXT_HOMA_QUEUE_H
#define EXT_HOMA_QUEUE_H

#include "pfabric_queue.h"

// Homa uses PQ so we inherit PFbricQueue here
class HomaQueue : public PFabricQueue {
    public:
        HomaQueue(uint32_t id, double rate, uint32_t limit_bytes, int location)
            : PFabricQueue(id, rate, limit_bytes, location) {};
};

#endif  // EXT_HOMA_QUEUE_H
