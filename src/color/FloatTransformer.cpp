#include "logforge/FloatTransformer.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace logforge {
namespace {
void Merge(SignalStatistics& a, const SignalStatistics& b) {
    a.samples += b.samples;
    a.appleFloorClipped += b.appleFloorClipped;
    a.aboveNominalWhite += b.aboveNominalWhite;
    a.inputMinimum = std::min(a.inputMinimum, b.inputMinimum);
    a.inputMaximum = std::max(a.inputMaximum, b.inputMaximum);
    a.outputMinimum = std::min(a.outputMinimum, b.outputMinimum);
    a.outputMaximum = std::max(a.outputMaximum, b.outputMaximum);
}
} // namespace
struct FloatTransformer::Impl {
    double exposure;
    ToneAdjustments tone;
    std::mutex mutex;
    std::condition_variable_any ready;
    std::condition_variable complete;
    std::span<float> pixels;
    std::atomic_size_t next = 0;
    size_t epoch = 0, pending = 0;
    std::exception_ptr error;
    std::vector<SignalStatistics> stats;
    std::vector<std::vector<float>> tiles;
    std::vector<std::jthread> workers;
    static constexpr size_t Tile = 16384;
    Impl(double e, const ToneAdjustments& t, unsigned n) : exposure(e), tone(t), stats(n), tiles(n) {
        ValidateExposureStops(e);
        ValidateToneAdjustments(t);
        // Allocate on the caller before starting workers. Allocation failure must
        // propagate through the job's exception boundary, never escape a thread.
        for (auto& tile : tiles)
            tile.resize(tone.enabled ? Tile * 3 : 0);
        workers.reserve(n);
        for (unsigned worker = 0; worker < n; ++worker)
            workers.emplace_back([this, worker](std::stop_token stop) {
                size_t seen = 0;
                auto& tile = tiles[worker];
                while (true) {
                    {
                        std::unique_lock lock(mutex);
                        if (!ready.wait(lock, stop, [&] { return epoch != seen; }))
                            return;
                        seen = epoch;
                    }
                    SignalStatistics local;
                    try {
                        const size_t total = tone.enabled ? pixels.size() / 3 : pixels.size();
                        size_t offset;
                        while ((offset = next.fetch_add(Tile, std::memory_order_relaxed)) < total) {
                            const auto length = std::min(Tile, total - offset);
                            if (!tone.enabled)
                                TransformHLGToAppleLog(pixels.subspan(offset, length), exposure, tone,
                                                       &local);
                            else {
                                for (size_t c = 0; c < 3; ++c)
                                    std::copy_n(pixels.data() + c * total + offset, length,
                                                tile.data() + c * length);
                                TransformHLGToAppleLog(std::span(tile).first(length * 3), exposure, tone,
                                                       &local);
                                for (size_t c = 0; c < 3; ++c)
                                    std::copy_n(tile.data() + c * length, length,
                                                pixels.data() + c * total + offset);
                            }
                        }
                    } catch (...) {
                        std::lock_guard lock(mutex);
                        if (!error)
                            error = std::current_exception();
                    }
                    {
                        std::lock_guard lock(mutex);
                        stats[worker] = local;
                        if (--pending == 0)
                            complete.notify_one();
                    }
                }
            });
    }
    ~Impl() {
        for (auto& worker : workers)
            worker.request_stop();
        ready.notify_all();
        for (auto& worker : workers)
            worker.join();
    }
};
FloatTransformer::FloatTransformer(double e, const ToneAdjustments& t, unsigned workers)
    : impl_(std::make_unique<Impl>(e, t,
                                   workers ? std::clamp(workers, 1u, 16u)
                                           : std::clamp(std::thread::hardware_concurrency() / 2, 1u, 8u))) {}
FloatTransformer::~FloatTransformer() = default;
unsigned FloatTransformer::Workers() const {
    return static_cast<unsigned>(impl_->workers.size());
}
void FloatTransformer::Apply(std::span<float> p, SignalStatistics& cumulative) {
    auto& i = *impl_;
    if (p.size() < Impl::Tile * 2) {
        TransformHLGToAppleLog(p, i.exposure, i.tone, &cumulative);
        return;
    }
    if (i.tone.enabled && p.size() % 3) {
        TransformHLGToAppleLog(p, i.exposure, i.tone, &cumulative);
        return;
    }
    std::unique_lock lock(i.mutex);
    i.pixels = p;
    i.next = 0;
    i.error = nullptr;
    i.pending = i.workers.size();
    ++i.epoch;
    i.ready.notify_all();
    i.complete.wait(lock, [&] { return i.pending == 0; });
    if (i.error)
        std::rethrow_exception(i.error);
    for (const auto& s : i.stats)
        Merge(cumulative, s);
}
} // namespace logforge
