#ifndef EXT_WF_QUEUE_H
#define EXT_WF_QUEUE_H

#include "../coresim/queue.h"

class Packet;

// Size of the "weights" vector determines the number of priorities.
// Vector for queue weights: [highest_weight, ..., lowest_weight]
// Vector for buffer carving: [1, 2, 3] means 1/6th space for prio_1, 2/6th space for prio_2, the rest for prio_3. 
// Default buffer carving (if specified nothing) is no carving. All prios share the entire pool.
// WFQueue will reuse the pfabric priority ("pf_priority") defined in class Packet.
class WFQueue : public Queue {
    public:
        WFQueue(uint32_t id, double rate, uint32_t limit_bytes, int location);
        void enque(Packet *packet);
        Packet *deque(double deque_time);  // add time to update flow's event time
        void printQueueStatus();
        void drop(Packet *packet);
        void update_vtime(Packet *packet, int prio);
        int select_prio();

        int num_priorities;
        std::vector<int> weights;
        int curr_pos;                       // current position of the service pointer
        int sum_weights;

        // measure instantaneous load
        double max_inst_load;
        double interval_start;
        uint32_t bytes_in_interval;

        // measure outstanding rpcs
        double outstd_interval_start;

        std::vector<std::deque<Packet *>> prio_queues;   // one queue for each priority
        std::vector<int> bytes_per_prio;
        std::vector<int> pkt_drop_per_prio;
        std::vector<double> last_v_finish_time;

        std::vector<double> last_queuing_delay;

        bool has_buffer_carving;
        std::vector<int> buffer_carving;

};

#endif  // EXT_WF_QUEUE_H
