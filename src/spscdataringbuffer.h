#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

template <typename T>
class SpscDataRingBuffer
{
public:
    explicit SpscDataRingBuffer(std::size_t capacity, std::size_t maxElementsPerSlot);

    SpscDataRingBuffer(const SpscDataRingBuffer&) = delete;
    SpscDataRingBuffer& operator=(const SpscDataRingBuffer&) = delete;

    // Designed for one producer thread and one consumer thread.
    std::size_t capacity() const noexcept;
    std::size_t maxElementsPerSlot() const noexcept;
    void push(const T* data, std::size_t count);
    bool tryReadLatest(std::vector<T>& data);

private:
    static_assert(std::is_trivially_copyable_v<T>,
                  "SpscDataRingBuffer requires T to be trivially copyable");

    struct Block
    {
        explicit Block(std::size_t maxElements)
            : storage(std::make_unique<T[]>(maxElements))
            , size(0)
        {
        }

        std::unique_ptr<T[]> storage;
        std::size_t size;
    };

    struct Slot
    {
        std::atomic<std::uint64_t> version{0};
        std::shared_ptr<const Block> payload;
    };

    std::vector<Slot> m_slots;
    std::size_t m_maxElementsPerSlot;
    std::atomic<std::uint64_t> m_nextTicket{1};
    std::atomic<std::uint64_t> m_latestPublished{0};
    std::atomic<std::uint64_t> m_lastConsumed{0};
};

template <typename T>
SpscDataRingBuffer<T>::SpscDataRingBuffer(std::size_t capacity, std::size_t maxElementsPerSlot)
    : m_slots(capacity)
    , m_maxElementsPerSlot(maxElementsPerSlot)
{
    if (capacity == 0) {
        throw std::invalid_argument("SpscDataRingBuffer capacity must be greater than zero");
    }

    if (maxElementsPerSlot == 0) {
        throw std::invalid_argument("SpscDataRingBuffer maxElementsPerSlot must be greater than zero");
    }
}

template <typename T>
std::size_t SpscDataRingBuffer<T>::capacity() const noexcept
{
    return m_slots.size();
}

template <typename T>
std::size_t SpscDataRingBuffer<T>::maxElementsPerSlot() const noexcept
{
    return m_maxElementsPerSlot;
}

template <typename T>
void SpscDataRingBuffer<T>::push(const T* data, std::size_t count)
{
    if (count > m_maxElementsPerSlot) {
        throw std::out_of_range("SpscDataRingBuffer push count exceeds slot capacity");
    }

    if (count > 0 && data == nullptr) {
        throw std::invalid_argument("SpscDataRingBuffer push data must not be null when count > 0");
    }

    const std::uint64_t ticket = m_nextTicket.fetch_add(1, std::memory_order_relaxed);
    Slot& slot = m_slots[(ticket - 1) % m_slots.size()];
    auto mutableBlock = std::make_shared<Block>(m_maxElementsPerSlot);
    mutableBlock->size = count;

    if (count > 0) {
        std::memcpy(mutableBlock->storage.get(), data, count * sizeof(T));
    }

    std::shared_ptr<const Block> block = mutableBlock;

    slot.version.store(ticket * 2 - 1, std::memory_order_release);
    std::atomic_store_explicit(&slot.payload, std::move(block), std::memory_order_release);
    slot.version.store(ticket * 2, std::memory_order_release);
    m_latestPublished.store(ticket, std::memory_order_release);
}

template <typename T>
bool SpscDataRingBuffer<T>::tryReadLatest(std::vector<T>& data)
{
    std::uint64_t latest = m_latestPublished.load(std::memory_order_acquire);
    if (latest == 0) {
        return false;
    }

    while (true) {
        const std::uint64_t lastConsumed = m_lastConsumed.load(std::memory_order_relaxed);
        if (latest <= lastConsumed) {
            return false;
        }

        const std::uint64_t oldestAvailable = latest > m_slots.size() ? latest - m_slots.size() + 1 : 1;
        const std::uint64_t candidate = (lastConsumed + 1 > oldestAvailable) ? lastConsumed + 1 : oldestAvailable;
        const std::uint64_t target = latest;
        Slot& slot = m_slots[(target - 1) % m_slots.size()];
        const std::uint64_t expectedVersion = target * 2;

        const std::uint64_t versionBefore = slot.version.load(std::memory_order_acquire);
        if (versionBefore != expectedVersion) {
            latest = m_latestPublished.load(std::memory_order_acquire);
            if (latest < candidate) {
                latest = candidate;
            }
            continue;
        }

        const std::shared_ptr<const Block> snapshot =
            std::atomic_load_explicit(&slot.payload, std::memory_order_acquire);
        const std::uint64_t versionAfter = slot.version.load(std::memory_order_acquire);
        if (versionBefore != versionAfter || !snapshot) {
            latest = m_latestPublished.load(std::memory_order_acquire);
            if (latest < candidate) {
                latest = candidate;
            }
            continue;
        }

        data.resize(snapshot->size);
        if (snapshot->size > 0) {
            std::memcpy(data.data(), snapshot->storage.get(), snapshot->size * sizeof(T));
        }

        m_lastConsumed.store(target, std::memory_order_release);
        return true;
    }
}
