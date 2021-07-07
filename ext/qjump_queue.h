#ifndef EXT_QJUMP_QUEUE_H
#define EXT_QJUMP_QUEUE_H

#include "pfabric_queue.h"

// Qjump Queue supports muliple priority levels with Strict Priority instead of WFQ.
// So we just use PFbricQueue here.
class QjumpQueue : public PFabricQueue {
    public:
        QjumpQueue(uint32_t id, double rate, uint32_t limit_bytes, int location)
            : PFabricQueue(id, rate, limit_bytes, location) {};
};

#endif  // EXT_QJUMP_QUEUE_H
