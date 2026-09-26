#include "logforge/FloatBridge.h"
#include "logforge/Localization.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <thread>

namespace logforge {
ThreadPlan PlanThreads(bool cuda) {
    const unsigned cores = std::clamp(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS), 1ul, 256ul);
    // Reserve the decoder/encoder's one-thread matrix filters as well. Three
    // stages still each need one worker on very small machines.
    const unsigned budget = cores > 2 ? cores - 2 : 1;
    const unsigned color = std::clamp(cuda ? 2u : budget / 2, 1u, 32u);
    const unsigned decode = std::clamp(cores / (cuda ? 4 : 8), 1u, 8u);
    const unsigned encode = std::clamp(budget > color + decode ? budget - color - decode : 1u, 1u, 48u);
    return {cores, decode, encode, color, 1};
}
int64_t RunFloatBridge(FFmpegProcess& decoder, FFmpegProcess& encoder, size_t frameSamples, bool creative,
                       const std::atomic_bool& cancel, const TransformCallback& transform,
                       BridgeTiming& timing) {
    struct Slot {
        std::vector<float> data;
        size_t filled = 0;
        int state = 0;
    };
    // Creative still needs all three sequential GBR planes; retain its one-frame
    // bound rather than incorrectly processing matching rows from different planes.
    const size_t count = creative ? 1 : 3,
                 chunk = creative ? frameSamples : std::min<size_t>(frameSamples, 1024 * 1024);
    std::array<Slot, 3> slots;
    for (size_t i = 0; i < count; ++i)
        slots[i].data.resize(chunk);
    timing.bufferBytes = chunk * sizeof(float);
    timing.buffers = static_cast<unsigned>(count);
    std::mutex mutex;
    std::condition_variable changed;
    bool abort = false;
    std::exception_ptr error;
    auto fail = [&] {
        {
            std::lock_guard lock(mutex);
            if (!error)
                error = std::current_exception();
            abort = true;
        }
        changed.notify_all();
        decoder.Terminate();
        encoder.Terminate();
    };
    const auto seconds = [](auto begin) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    };
    std::jthread reader, writer;
    struct JoinGuard {
        bool& abort;
        std::mutex& mutex;
        std::condition_variable& changed;
        FFmpegProcess& decoder;
        FFmpegProcess& encoder;
        std::jthread& reader;
        std::jthread& writer;
        bool complete = false;
        ~JoinGuard() {
            if (!complete) {
                {
                    std::lock_guard lock(mutex);
                    abort = true;
                }
                changed.notify_all();
                decoder.Terminate();
                encoder.Terminate();
            }
            if (reader.joinable())
                reader.join();
            if (writer.joinable())
                writer.join();
        }
    } guard{abort, mutex, changed, decoder, encoder, reader, writer};
    reader = std::jthread([&] {
        try {
            for (size_t i = 0;; i = (i + 1) % count) {
                auto& s = slots[i];
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock, [&] { return abort || s.state == 0; });
                    if (abort)
                        return;
                }
                auto start = std::chrono::steady_clock::now();
                size_t filled = 0;
                while (filled < s.data.size() * sizeof(float) && !cancel.load()) {
                    auto got = decoder.Read(reinterpret_cast<char*>(s.data.data()) + filled,
                                            s.data.size() * sizeof(float) - filled);
                    if (!got)
                        break;
                    filled += got;
                }
                timing.read += seconds(start);
                if (filled % sizeof(float) || (creative && filled && filled != s.data.size() * sizeof(float)))
                    throw AppError(TextId::IncompleteFrame);
                {
                    std::lock_guard lock(mutex);
                    s.filled = filled / sizeof(float);
                    s.state = 1;
                }
                changed.notify_all();
                if (!filled)
                    return;
            }
        } catch (...) {
            fail();
        }
    });
    writer = std::jthread([&] {
        try {
            for (size_t i = 0;; i = (i + 1) % count) {
                auto& s = slots[i];
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock, [&] { return abort || s.state == 2; });
                    if (abort)
                        return;
                }
                if (!s.filled) {
                    encoder.CloseInput();
                    return;
                }
                const auto start = std::chrono::steady_clock::now();
                encoder.Write(s.data.data(), s.filled * sizeof(float));
                timing.write += seconds(start);
                {
                    std::lock_guard lock(mutex);
                    s.state = 0;
                }
                changed.notify_all();
            }
        } catch (...) {
            fail();
        }
    });
    uint64_t samples = 0;
    try {
        for (size_t i = 0;; i = (i + 1) % count) {
            auto& s = slots[i];
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return abort || s.state == 1; });
                if (abort)
                    break;
            }
            if (cancel.load())
                throw AppError(TextId::Cancelled);
            const bool last = s.filled == 0;
            if (!last) {
                const auto start = std::chrono::steady_clock::now();
                transform(std::span(s.data).first(s.filled));
                timing.transform += seconds(start);
                samples += s.filled;
            }
            {
                std::lock_guard lock(mutex);
                s.state = 2;
            }
            changed.notify_all();
            if (last)
                break;
        }
    } catch (...) {
        fail();
    }
    reader.join();
    writer.join();
    guard.complete = true;
    if (error)
        std::rethrow_exception(error);
    if (cancel.load())
        throw AppError(TextId::Cancelled);
    if (samples % frameSamples)
        throw AppError(TextId::IncompleteFrame);
    return static_cast<int64_t>(samples / frameSamples);
}
} // namespace logforge
