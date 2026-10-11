/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef GUILD_DISCORD_RELAY_QUEUE_H
#define GUILD_DISCORD_RELAY_QUEUE_H

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace GuildDiscord
{
    // Bounded MPSC-style queue: Push never blocks and drops the oldest item when full.
    template <typename T>
    class RelayQueue
    {
    public:
        explicit RelayQueue(std::size_t capacity) : _capacity(capacity ? capacity : 1) { }

        // Returns true if an older item was dropped to make room.
        bool Push(T item)
        {
            bool dropped = false;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_closed)
                    return false;
                if (_items.size() >= _capacity)
                {
                    _items.pop_front();
                    dropped = true;
                }
                _items.push_back(std::move(item));
            }
            _cv.notify_one();
            return dropped;
        }

        // Blocks until an item is available; returns nullopt once closed and drained.
        std::optional<T> Pop()
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait(lock, [this] { return _closed || !_items.empty(); });
            if (_items.empty())
                return std::nullopt;

            T item = std::move(_items.front());
            _items.pop_front();
            return item;
        }

        void Close()
        {
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _closed = true;
            }
            _cv.notify_all();
        }

    private:
        std::size_t _capacity;
        std::deque<T> _items;
        std::mutex _mutex;
        std::condition_variable _cv;
        bool _closed = false;
    };
}

#endif
