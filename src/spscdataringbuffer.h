#pragma once

#include <QList>
#include <QtGlobal>
#include <QVector>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

template <typename T>
class SpscDataRingBuffer
{
public:
    // capacity is the number of slots kept in the ring, maxElementsPerSlot is the
    // maximum number of T elements stored in one published block.
    explicit SpscDataRingBuffer(qsizetype capacity, qsizetype maxElementsPerSlot);

    SpscDataRingBuffer(const SpscDataRingBuffer&) = delete;
    SpscDataRingBuffer& operator=(const SpscDataRingBuffer&) = delete;

    // Designed for one producer thread and one consumer thread.
    qsizetype capacity() const noexcept;
    qsizetype maxElementsPerSlot() const noexcept;
    // Publishes one contiguous block. Returns false on invalid input.
    bool push(const T* data, qsizetype count);
    bool push(const QVector<T>& data);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    bool push(const QList<T>& data);
#endif
    // Reads the latest fully published block and drops older unread blocks.
    bool tryReadLatest(QVector<T>& data);

private:
    static_assert(std::is_trivially_copyable_v<T>,
                  "SpscDataRingBuffer requires T to be trivially copyable");

    struct Block
    {
        explicit Block(qsizetype maxElements)
            : storage(std::make_unique<T[]>(maxElements))
            , size(0)
        {
        }

        std::unique_ptr<T[]> storage;
        qsizetype size;
    };

    struct Slot
    {
        std::atomic<std::uint64_t> version{0};
        std::shared_ptr<const Block> payload;
    };

    std::vector<Slot> m_slots;
    qsizetype m_maxElementsPerSlot;
    std::atomic<std::uint64_t> m_nextTicket{1};
    std::atomic<std::uint64_t> m_latestPublished{0};
    std::atomic<std::uint64_t> m_lastConsumed{0};
};

template <typename T>
SpscDataRingBuffer<T>::SpscDataRingBuffer(qsizetype capacity, qsizetype maxElementsPerSlot)
    : m_slots(static_cast<std::size_t>(capacity > 0 ? capacity : 1))
    , m_maxElementsPerSlot(maxElementsPerSlot > 0 ? maxElementsPerSlot : 1)
{
}

template <typename T>
qsizetype SpscDataRingBuffer<T>::capacity() const noexcept
{
    return static_cast<qsizetype>(m_slots.size());
}

template <typename T>
qsizetype SpscDataRingBuffer<T>::maxElementsPerSlot() const noexcept
{
    return m_maxElementsPerSlot;
}

template <typename T>
bool SpscDataRingBuffer<T>::push(const T* data, qsizetype count)
{
    if (count < 0 || count > m_maxElementsPerSlot) {
        return false;
    }

    if (count > 0 && data == nullptr) {
        return false;
    }

    const std::uint64_t ticket = m_nextTicket.fetch_add(1, std::memory_order_relaxed);
    Slot& slot = m_slots[(ticket - 1) % m_slots.size()];
    auto mutableBlock = std::make_shared<Block>(m_maxElementsPerSlot);
    mutableBlock->size = count;

    if (count > 0) {
        std::memcpy(mutableBlock->storage.get(), data, static_cast<std::size_t>(count) * sizeof(T));
    }

    std::shared_ptr<const Block> block = mutableBlock;

    // Publish in two phases so the consumer can detect an in-progress overwrite.
    slot.version.store(ticket * 2 - 1, std::memory_order_release);
    std::atomic_store_explicit(&slot.payload, std::move(block), std::memory_order_release);
    slot.version.store(ticket * 2, std::memory_order_release);
    m_latestPublished.store(ticket, std::memory_order_release);
    return true;
}

template <typename T>
bool SpscDataRingBuffer<T>::push(const QVector<T>& data)
{
    return push(data.constData(), data.size());
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
template <typename T>
bool SpscDataRingBuffer<T>::push(const QList<T>& data)
{
    if (data.isEmpty()) {
        return push(static_cast<const T*>(nullptr), 0);
    }

    QVector<T> contiguous = data.toVector();
    return push(contiguous.constData(), contiguous.size());
}
#endif

template <typename T>
bool SpscDataRingBuffer<T>::tryReadLatest(QVector<T>& data)
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

        // Copy from an immutable snapshot so producer and consumer never touch
        // the same payload object concurrently.
        data.resize(snapshot->size);
        if (snapshot->size > 0) {
            std::memcpy(data.data(), snapshot->storage.get(), static_cast<std::size_t>(snapshot->size) * sizeof(T));
        }

        m_lastConsumed.store(target, std::memory_order_release);
        return true;
    }
}
