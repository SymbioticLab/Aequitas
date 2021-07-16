#ifndef CORESIM_QUEUE_H
#define CORESIM_QUEUE_H

#include <deque>
#include <stdint.h>
#include <vector>

class Node;
class Packet;
class Event;

class QueueProcessingEvent;

class Queue {
    public:
        Queue(uint32_t id, double rate, uint32_t limit_bytes, int location);
        virtual ~Queue() = 0;
        void set_src_dst(Node *src, Node *dst);
        virtual void enque(Packet *packet);
        virtual Packet *try_deque();
        virtual Packet *deque(double deque_time);
        virtual void drop(Packet *packet);
        virtual void flush() {};
        //double get_transmission_delay(uint32_t size);
        virtual double get_transmission_delay(Packet *packet);
        double get_cut_through_delay(Packet *packet);
        void preempt_current_transmission();

        // Members
        uint32_t id;
        uint32_t unique_id;
        static uint32_t instance_count;
        double rate;
        uint32_t limit_bytes;
        std::deque<Packet *> packets;
        uint32_t bytes_in_queue;
        bool busy;
        QueueProcessingEvent *queue_proc_event;

        bool measure_inst_load;

        std::vector<Event*> busy_events;
        Packet* packet_transmitting;

        Node *src;
        Node *dst;

        uint64_t b_arrivals, b_departures;
        uint64_t p_arrivals, p_departures;

        double propagation_delay;
        bool interested;

        uint64_t pkt_drop;
        uint64_t spray_counter;

        int location;

        std::vector<uint32_t> served_pkts_per_prio;

        int num_active_flows;   // for D3
};


class ProbDropQueue : public Queue {
    public:
        ProbDropQueue(
                uint32_t id, 
                double rate, 
                uint32_t limit_bytes,
                double drop_prob, 
                int location
                );
        virtual void enque(Packet *packet);

        double drop_prob;
};

class HostEgressQueue : public Queue {
    public:
        HostEgressQueue(uint32_t id, double rate, int location);
        void enque(Packet *packet) override;
};

#endif  // CORESIM_QUEUE_H
