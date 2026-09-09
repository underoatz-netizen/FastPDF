#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "fastpdf/renderer/render_key.h"

namespace fastpdf::renderer {

// Bounded, coalescing, priority-ordered render job scheduler.
//
// Jobs are ordered by RenderPriority (lower value first) with FIFO ordering
// within a priority. Pushing a job whose RenderKey matches a pending job
// coalesces the two (the stored job keeps its position and adopts the higher
// priority and the newer epoch/job id). The queue is bounded: when full, the
// lowest-priority pending job is evicted to make room, and an incoming job
// that is not higher priority than everything already queued is dropped. This
// guarantees there is never an unbounded render queue.
//
// Not thread-safe: the owning render worker guards it with its own mutex.
class PriorityScheduler {
public:
    struct Job {
        RenderKey key;
        RenderPriority priority = RenderPriority::Background;
        std::uint64_t viewEpoch = 0;
        std::uint64_t jobId = 0;
    };

    explicit PriorityScheduler(std::size_t capacity) noexcept;

    // Adds (or coalesces) |job|. Drops the job when the queue is full and it is
    // not higher priority than any queued job.
    void push(Job job);

    // Removes and returns the highest-priority (then oldest) job, if any.
    std::optional<Job> pop();

    void clear() noexcept;

    // Drops every pending job whose view epoch is not |viewEpoch|.
    void removeStaleViewEpochs(std::uint64_t viewEpoch) noexcept;

    std::size_t size() const noexcept { return jobs_.size(); }
    std::size_t capacity() const noexcept { return capacity_; }

private:
    std::size_t capacity_;
    std::vector<Job> jobs_;
};

}  // namespace fastpdf::renderer
