// Shared DB2-derived item display attachments and material targeting.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ItemDisplayIndex.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace wxl::runtime::db2::itemdisplay
{
    namespace
    {
        struct RequestState
        {
            std::mutex mutex;
            std::condition_variable ready;
            std::unordered_set<uint32_t> requests;
            std::unordered_set<uint32_t> inFlight;
        };

        std::shared_ptr<const Index>& CurrentSlot()
        {
            static auto* slot = new std::shared_ptr<const Index>();
            return *slot;
        }

        std::atomic<uint64_t>& GenerationSlot()
        {
            static auto* generation = new std::atomic<uint64_t>(0);
            return *generation;
        }

        RequestState& Requests()
        {
            static auto* state = new RequestState();
            return *state;
        }
    }

    const char* Index::Intern(std::string_view value)
    {
        if (value.empty()) return "";
        return strings.emplace(value).first->c_str();
    }

    const std::vector<ModelEntry>* Index::FindModels(uint32_t displayId) const noexcept
    {
        const auto found = models.find(displayId);
        return found == models.end() ? nullptr : &found->second;
    }

    void Publish(std::shared_ptr<Index> index)
    {
        std::atomic_store_explicit(&CurrentSlot(), std::shared_ptr<const Index>(std::move(index)),
                                   std::memory_order_release);
        GenerationSlot().fetch_add(1, std::memory_order_release);
    }

    std::shared_ptr<const Index> Current()
    {
        return std::atomic_load_explicit(&CurrentSlot(), std::memory_order_acquire);
    }

    uint64_t Generation() noexcept
    {
        return GenerationSlot().load(std::memory_order_acquire);
    }

    void Request(uint32_t displayId)
    {
        if (!displayId) return;
        RequestBatch(std::span<const uint32_t>(&displayId, 1));
    }

    void RequestBatch(std::span<const uint32_t> displayIds)
    {
        if (displayIds.empty()) return;

        RequestState& state = Requests();
        bool inserted = false;
        {
            const std::lock_guard lock(state.mutex);
            const auto current = Current();
            for (uint32_t displayId : displayIds)
            {
                if (!displayId) continue;
                if (current && current->resolvedDisplays.contains(displayId))
                    continue;
                if (state.inFlight.contains(displayId))
                    continue;
                inserted = state.requests.insert(displayId).second || inserted;
            }
        }
        if (inserted) state.ready.notify_one();
    }

    std::vector<uint32_t> WaitTakeRequests()
    {
        RequestState& state = Requests();
        std::unique_lock lock(state.mutex);
        state.ready.wait(lock, [&] { return !state.requests.empty(); });
        std::vector<uint32_t> requests(state.requests.begin(), state.requests.end());
        state.inFlight.insert(requests.begin(), requests.end());
        state.requests.clear();
        std::ranges::sort(requests);
        return requests;
    }

    std::vector<uint32_t> TakeRequests()
    {
        RequestState& state = Requests();
        const std::lock_guard lock(state.mutex);
        std::vector<uint32_t> requests(state.requests.begin(), state.requests.end());
        state.inFlight.insert(requests.begin(), requests.end());
        state.requests.clear();
        std::ranges::sort(requests);
        return requests;
    }

    void FinishRequests(std::span<const uint32_t> displayIds)
    {
        RequestState& state = Requests();
        const std::lock_guard lock(state.mutex);
        for (uint32_t displayId : displayIds)
            state.inFlight.erase(displayId);
    }
}
