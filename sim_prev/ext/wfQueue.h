#ifndef WF_QUEUE_H
#define WF_QUEUE_H

#include "../coresim/queue.h"
#include "../coresim/packet.h"

// Size of the "weights" vector determines the number of priorities.
// Vector for queue weights: [highest_weight, ..., lowest_weight]
// Vector for buffer carving: [1, 2, 3] means 1/6th space for prio_1, 2/6th space for prio_2, the rest for prio_3. 
// Default buffer carving (if specified nothing) is no carving. All prios share the entire pool.
// WFQueue will reuse the pfabric priority ("pf_priority") defined in class Packet.
class WFQueue : public Queue {
    public:
        WFQueue(uint32_t id, double rate, uint32_t limit_bytes, int location);
            //const std::vector<int> &weights,
            //const std::vector<int> &buffer_carving = std::vector<int>());
        void enque(Packet *packet);
        Packet *deque();
        void increment_curr_pos();
        void set_curr_pos(int pos);
        void printQueueStatus();
        void flush();
        void drop(Packet *packet);
        void update_vtime(Packet *packet, int prio);
        int select_prio();

        int num_priorities;
        std::vector<int> weights;
        int curr_pos;                       // current position of the service pointer
        int sum_weights;
        //uint32_t prio0_select_cnt = 0;
        //uint32_t prio1_select_cnt = 0;

        std::vector<std::deque<Packet *>> prio_queues;   // one queue for each priority
        std::vector<int> bytes_per_prio;
        std::vector<int> pkt_drop_per_prio;
        std::vector<double> last_v_finish_time;

        bool has_buffer_carving;
        std::vector<int> buffer_carving;

};

#endif
