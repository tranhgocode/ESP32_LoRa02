#pragma once
#include <atomic>
#include <stdint.h>
namespace gateway
{
namespace adapter
{
struct PublishedAck
{
    uint32_t generation = 0U;
    int id = -1;
};
// One MQTT callback producer, one application-loop consumer. Release/acquire
// keeps a callback arriving before enqueue returns available for later matching.
class MqttAckQueue
{
public:
    void push(uint32_t generation, int id)
    {
        const unsigned write = writeIndex.load(std::memory_order_relaxed);
        const unsigned next = (write + 1U) % CAPACITY;
        if (next == readIndex.load(std::memory_order_acquire))
        {
            overflow.store(true, std::memory_order_release);
            return;
        }
        entries[write].generation = generation;
        entries[write].id = id;
        writeIndex.store(next, std::memory_order_release);
    }
    bool pop(PublishedAck *ack)
    {
        const unsigned read = readIndex.load(std::memory_order_relaxed);
        if (ack == nullptr || read == writeIndex.load(std::memory_order_acquire))
        {
            return false;
        }
        *ack = entries[read];
        readIndex.store((read + 1U) % CAPACITY, std::memory_order_release);
        return true;
    }
    bool takeOverflow()
    {
        return overflow.exchange(false, std::memory_order_acq_rel);
    }
private:
    static constexpr unsigned CAPACITY = 32U;
    PublishedAck entries[CAPACITY] = {};
    std::atomic<unsigned> writeIndex{0U};
    std::atomic<unsigned> readIndex{0U};
    std::atomic<bool> overflow{false};
};
// Application-loop state only. Exact message and session identity ensure an
// expired registration's late ACK cannot authorize its replacement.
class RegistrationAckTracker
{
public:
    void expect(uint32_t newGeneration, int newId)
    {
        generation = newGeneration;
        id = newId;
        acknowledged = false;
    }
    void observe(const PublishedAck &ack)
    {
        if (ack.id == id && ack.generation == generation)
        {
            acknowledged = true;
        }
    }
    void invalidate()
    {
        acknowledged = false;
    }
    bool isAcknowledged() const
    {
        return acknowledged;
    }
private:
    uint32_t generation = 0U;
    int id = -1;
    bool acknowledged = false;
};

}
}
