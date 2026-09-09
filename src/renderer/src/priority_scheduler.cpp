#include "fastpdf/renderer/priority_scheduler.h"

#include <algorithm>

namespace fastpdf::renderer {

PriorityScheduler::PriorityScheduler(std::size_t capacity) noexcept
    : capacity_(std::max<std::size_t>(1, capacity)) {}

void PriorityScheduler::push(Job job) {
    // Coalesce: a pending job with the same key adopts the higher priority and
    // the newer epoch/id while keeping its (earlier) position, preserving FIFO
    // ordering within a priority.
    for (Job& existing : jobs_) {
        if (existing.key == job.key) {
            existing.priority = std::min(existing.priority, job.priority);
            existing.viewEpoch = job.viewEpoch;
            existing.jobId = job.jobId;
            return;
        }
    }

    if (jobs_.size() < capacity_) {
        jobs_.push_back(std::move(job));
        return;
    }

    // Queue is full. Find the lowest-priority job (highest enum value); evict
    // it when the incoming job is higher priority, otherwise drop the incoming
    // job. This keeps the queue strictly bounded.
    auto victim = jobs_.end();
    for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
        if (victim == jobs_.end() || it->priority > victim->priority) {
            victim = it;
        }
    }
    if (victim != jobs_.end() && job.priority < victim->priority) {
        *victim = std::move(job);
    }
}

std::optional<PriorityScheduler::Job> PriorityScheduler::pop() {
    if (jobs_.empty()) {
        return std::nullopt;
    }
    // Highest priority = lowest enum value; within a priority, the earliest
    // (lowest index) job wins.
    auto best = jobs_.begin();
    for (auto it = std::next(best); it != jobs_.end(); ++it) {
        if (it->priority < best->priority) {
            best = it;
        }
    }
    Job job = std::move(*best);
    jobs_.erase(best);
    return job;
}

void PriorityScheduler::clear() noexcept {
    jobs_.clear();
}

void PriorityScheduler::removeStaleViewEpochs(std::uint64_t viewEpoch) noexcept {
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                               [viewEpoch](const Job& job) {
                                   return job.viewEpoch != viewEpoch;
                               }),
                jobs_.end());
}

}  // namespace fastpdf::renderer
