#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

template <typename Work>
void ParallelFor(size_t count, size_t workers, Work work)
{
    workers = std::max<size_t>(1, std::min(workers, count));
    if (workers == 1)
    {
        for (size_t i = 0; i < count; ++i)
            work(i, 0);
        return;
    }
    std::atomic<size_t> next{0};
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (size_t worker = 0; worker < workers; ++worker)
        threads.emplace_back([&, worker]() {
            for (size_t i = next++; i < count; i = next++)
                work(i, worker);
        });
    for (std::thread& thread : threads)
        thread.join();
}
