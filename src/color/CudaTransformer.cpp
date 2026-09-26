#include "logforge/CudaTransformer.h"
#include "ColorKernel.ptx.h"
#include "logforge/Platform.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <stdexcept>

namespace logforge {
const char* BackendName(ProcessingBackend b) {
    return b == ProcessingBackend::Auto ? "auto" : b == ProcessingBackend::CPU ? "cpu" : "cuda";
}
namespace {
using DevicePtr = unsigned long long;
using Object = void*;
struct Statistics {
    uint64_t samples, floor, white, invalid;
    double inputMin, inputMax, outputMin, outputMax;
};
struct Driver {
    HMODULE dll{};
    // CUDA Driver API ABI on Windows x64. No SDK headers or CUDA runtime DLL
    // are linked into the application; load only the OS driver directory.
#define API(name, args) int(__stdcall * name) args{};
    API(cuInit, (unsigned))
    API(cuDeviceGetCount, (int*))
    API(cuDeviceGet, (int*, int)) API(cuDeviceGetName, (char*, int, int))
        API(cuDeviceGetAttribute, (int*, int, int)) API(cuDriverGetVersion, (int*))
            API(cuCtxCreate_v2, (Object*, unsigned, int)) API(cuCtxDestroy_v2, (Object))
                API(cuCtxSetCurrent, (Object)) API(cuModuleLoadData, (Object*, const void*))
                    API(cuModuleUnload, (Object)) API(cuModuleGetFunction, (Object*, Object, const char*))
                        API(cuMemAlloc_v2, (DevicePtr*, size_t)) API(cuMemFree_v2, (DevicePtr))
                            API(cuMemHostAlloc, (void**, size_t, unsigned)) API(cuMemFreeHost, (void*))
                                API(cuStreamCreate, (Object*, unsigned)) API(cuStreamDestroy_v2, (Object))
                                    API(cuStreamSynchronize, (Object))
                                        API(cuMemcpyHtoDAsync_v2, (DevicePtr, const void*, size_t, Object))
                                            API(cuMemcpyDtoHAsync_v2, (void*, DevicePtr, size_t, Object))
                                                API(cuLaunchKernel,
                                                    (Object, unsigned, unsigned, unsigned, unsigned, unsigned,
                                                     unsigned, unsigned, Object, void**, void**))
                                                    API(cuEventCreate, (Object*, unsigned))
                                                        API(cuEventDestroy_v2, (Object)) API(cuEventRecord,
                                                                                             (Object, Object))
                                                            API(cuEventElapsedTime, (float*, Object, Object))
                                                                API(cuGetErrorName, (int, const char**))
#undef API
                                                                    Driver() {
        dll = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll)
            throw std::runtime_error("CUDA driver is not installed");
        try {
#define LOAD(name)                                                                                           \
    name = reinterpret_cast<decltype(name)>(GetProcAddress(dll, #name));                                     \
    if (!name)                                                                                               \
        throw std::runtime_error("CUDA driver lacks " #name);
            LOAD(cuInit)
            LOAD(cuDeviceGetCount)
            LOAD(cuDeviceGet) LOAD(cuDeviceGetName) LOAD(cuDeviceGetAttribute) LOAD(cuDriverGetVersion)
                LOAD(cuCtxCreate_v2) LOAD(cuCtxDestroy_v2) LOAD(cuCtxSetCurrent) LOAD(cuModuleLoadData)
                    LOAD(cuModuleUnload) LOAD(cuModuleGetFunction) LOAD(cuMemAlloc_v2) LOAD(cuMemFree_v2)
                        LOAD(cuMemHostAlloc) LOAD(cuMemFreeHost) LOAD(cuStreamCreate) LOAD(cuStreamDestroy_v2)
                            LOAD(cuStreamSynchronize) LOAD(cuMemcpyHtoDAsync_v2) LOAD(cuMemcpyDtoHAsync_v2)
                                LOAD(cuLaunchKernel) LOAD(cuEventCreate) LOAD(cuEventDestroy_v2)
                                    LOAD(cuEventRecord) LOAD(cuEventElapsedTime) LOAD(cuGetErrorName)
#undef LOAD
        } catch (...) {
            FreeLibrary(dll);
            dll = nullptr;
            throw;
        }
    }
    ~Driver() {
        if (dll)
            FreeLibrary(dll);
    }
    void Check(int code) {
        if (code) {
            const char* name = nullptr;
            cuGetErrorName(code, &name);
            throw std::runtime_error(std::string("CUDA: ") + (name ? name : std::to_string(code)));
        }
    }
};
} // namespace
struct CudaTransformer::Impl {
    Driver d;
    Object context{}, module{}, kernel{}, stream{};
    std::array<Object, 4> events{};
    DevicePtr pixels{}, statistics{};
    void* host{};
    void* hostStats{};
    size_t capacity = 0;
    static constexpr unsigned Blocks = 256;
    double exposure = 0, uploadSeconds = 0, downloadSeconds = 0, kernelSeconds = 0;
    uint64_t calls = 0;
    ToneAdjustments tone;
    nlohmann::json report;
    Impl(double e, const ToneAdjustments& t) : exposure(e), tone(t) {
        ValidateExposureStops(e);
        ValidateToneAdjustments(t);
        try {
            d.Check(d.cuInit(0));
            int count = 0;
            d.Check(d.cuDeviceGetCount(&count));
            if (!count)
                throw std::runtime_error("No CUDA device");
            int device = -1, major = 0, minor = 0;
            for (int i = 0; i < count; ++i) {
                int candidate;
                d.Check(d.cuDeviceGet(&candidate, i));
                d.Check(d.cuDeviceGetAttribute(&major, 75, candidate)); // documented COMPUTE_CAPABILITY_MAJOR
                d.Check(d.cuDeviceGetAttribute(&minor, 76, candidate));
                if (major * 10 + minor >= 75 && !d.cuCtxCreate_v2(&context, 0, candidate)) {
                    device = candidate;
                    break;
                }
            }
            if (device < 0)
                throw std::runtime_error("No usable CUDA device with compute capability >= 7.5");
            char name[256]{};
            int driver = 0;
            d.Check(d.cuDeviceGetName(name, sizeof(name), device));
            d.Check(d.cuDriverGetVersion(&driver));
            d.Check(d.cuModuleLoadData(&module, ColorPTX));
            d.Check(d.cuModuleGetFunction(&kernel, module, "color"));
            d.Check(d.cuStreamCreate(&stream, 1));
            for (auto& event : events)
                d.Check(d.cuEventCreate(&event, 0));
            d.Check(d.cuMemAlloc_v2(&statistics, Blocks * sizeof(Statistics)));
            d.Check(d.cuMemHostAlloc(&hostStats, Blocks * sizeof(Statistics), 0));
            report = {{"backend", "cuda"},
                      {"gpu", name},
                      {"driver_cuda_version", driver},
                      {"runtime", "CUDA Driver API; no cudart"},
                      {"compute_capability", std::to_string(major) + "." + std::to_string(minor)},
                      {"ptx_target", "compute_75"},
                      {"math", "double expressions, FP32 input/output; FMA disabled"}};
            wchar_t modulePath[32768]{};
            if (GetModuleFileNameW(d.dll, modulePath, 32768)) {
                DWORD unused = 0;
                const auto bytes = GetFileVersionInfoSizeW(modulePath, &unused);
                std::vector<unsigned char> version(bytes);
                if (bytes && GetFileVersionInfoW(modulePath, 0, bytes, version.data())) {
                    VS_FIXEDFILEINFO* info = nullptr;
                    UINT size = 0;
                    if (VerQueryValueW(version.data(), L"\\", reinterpret_cast<void**>(&info), &size) &&
                        size >= sizeof(*info))
                        report["driver_file_version"] = std::to_string(HIWORD(info->dwFileVersionMS)) + "." +
                                                        std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
                                                        std::to_string(HIWORD(info->dwFileVersionLS)) + "." +
                                                        std::to_string(LOWORD(info->dwFileVersionLS));
                }
            }
        } catch (...) {
            Cleanup();
            throw;
        }
    }
    void Cleanup() noexcept {
        if (context) {
            d.cuCtxSetCurrent(context);
            if (stream)
                d.cuStreamSynchronize(stream);
            if (pixels)
                d.cuMemFree_v2(pixels);
            if (statistics)
                d.cuMemFree_v2(statistics);
            if (host)
                d.cuMemFreeHost(host);
            if (hostStats)
                d.cuMemFreeHost(hostStats);
            for (auto event : events)
                if (event)
                    d.cuEventDestroy_v2(event);
            if (stream)
                d.cuStreamDestroy_v2(stream);
            if (module)
                d.cuModuleUnload(module);
            d.cuCtxDestroy_v2(context);
            context = nullptr;
        }
    }
    ~Impl() {
        Cleanup();
    }
    void Apply(std::span<float> p, SignalStatistics& cumulative) {
        if (p.empty())
            return;
        if (tone.enabled && p.size() % 3)
            throw std::runtime_error("CUDA: incomplete GBR planes");
        d.Check(d.cuCtxSetCurrent(context));
        const size_t bytes = p.size_bytes();
        if (bytes > capacity) {
            if (pixels) {
                d.Check(d.cuMemFree_v2(pixels));
                pixels = 0;
            }
            if (host) {
                d.Check(d.cuMemFreeHost(host));
                host = nullptr;
            }
            capacity = 0;
            d.Check(d.cuMemAlloc_v2(&pixels, bytes));
            d.Check(d.cuMemHostAlloc(&host, bytes, 0));
            capacity = bytes;
        }
        std::memcpy(host, p.data(), bytes);
        unsigned long long n = p.size();
        int creative = tone.enabled;
        double scale = HLGToReflectanceScale() * std::exp2(exposure);
        void* args[] = {
            &pixels,          &n,         &scale, &creative, &tone.shadowStops, &tone.highlightStops,
            &tone.saturation, &statistics};
        d.Check(d.cuEventRecord(events[0], stream));
        d.Check(d.cuMemcpyHtoDAsync_v2(pixels, host, bytes, stream));
        d.Check(d.cuEventRecord(events[1], stream));
        d.Check(d.cuLaunchKernel(kernel, Blocks, 1, 1, 128, 1, 1, 0, stream, args, nullptr));
        d.Check(d.cuEventRecord(events[2], stream));
        d.Check(d.cuMemcpyDtoHAsync_v2(host, pixels, bytes, stream));
        d.Check(d.cuMemcpyDtoHAsync_v2(hostStats, statistics, Blocks * sizeof(Statistics), stream));
        d.Check(d.cuEventRecord(events[3], stream));
        d.Check(d.cuStreamSynchronize(stream));
        SignalStatistics local;
        const auto* stats = static_cast<const Statistics*>(hostStats);
        for (unsigned i = 0; i < Blocks; ++i) {
            if (stats[i].invalid)
                throw std::runtime_error("CUDA: non-finite input or output");
            local.samples += stats[i].samples;
            local.appleFloorClipped += stats[i].floor;
            local.aboveNominalWhite += stats[i].white;
            local.inputMinimum = std::min(local.inputMinimum, stats[i].inputMin);
            local.inputMaximum = std::max(local.inputMaximum, stats[i].inputMax);
            local.outputMinimum = std::min(local.outputMinimum, stats[i].outputMin);
            local.outputMaximum = std::max(local.outputMaximum, stats[i].outputMax);
        }
        if (local.samples != p.size())
            throw std::runtime_error("CUDA: sample count mismatch");
        float ms[3]{};
        for (int i = 0; i < 3; ++i)
            d.Check(d.cuEventElapsedTime(&ms[i], events[i], events[i + 1]));
        uploadSeconds += ms[0] / 1000.0;
        kernelSeconds += ms[1] / 1000.0;
        downloadSeconds += ms[2] / 1000.0;
        ++calls;
        std::memcpy(p.data(), host, bytes); // Commit only after all GPU operations pass.
        cumulative.samples += local.samples;
        cumulative.appleFloorClipped += local.appleFloorClipped;
        cumulative.aboveNominalWhite += local.aboveNominalWhite;
        cumulative.inputMinimum = std::min(cumulative.inputMinimum, local.inputMinimum);
        cumulative.inputMaximum = std::max(cumulative.inputMaximum, local.inputMaximum);
        cumulative.outputMinimum = std::min(cumulative.outputMinimum, local.outputMinimum);
        cumulative.outputMaximum = std::max(cumulative.outputMaximum, local.outputMaximum);
    }
    nlohmann::json Qualify() {
        const auto savedTone = tone;
        const auto savedExposure = exposure;
        std::vector<float> fixture(3 * 8192);
        std::mt19937 rng(26926);
        for (size_t i = 0; i < fixture.size() / 3; ++i)
            for (size_t c = 0; c < 3; ++c) {
                double v;
                if (i < 2048)
                    v = static_cast<double>(i) / 2047; // neutral ramp
                else if (i < 3072)
                    v = (static_cast<double>(i - 2048) / 1023 - .5) * .05; // signed shadows
                else if (i < 4096)
                    v = 1 + static_cast<double>(i - 3072) / 1023 * .5; // superwhite
                else if (i < 5120)
                    v = ((i >> c) & 1) ? 1 : 0; // saturated primaries
                else
                    v = static_cast<double>(rng()) / rng.max() * 1.7 - .2;
                fixture[c * 8192 + i] = static_cast<float>(v);
            }
        nlohmann::json cases = nlohmann::json::array();
        double largest = 0;
        for (bool creative : {false, true})
            for (double ev : {-8., 0., 8.}) {
                tone = savedTone;
                tone.enabled = creative;
                exposure = ev;
                auto cpu = fixture, gpu = fixture;
                SignalStatistics a, b;
                TransformHLGToAppleLog(cpu, ev, tone, &a);
                Apply(gpu, b);
                double maximum = 0, mean = 0;
                for (size_t i = 0; i < cpu.size(); ++i) {
                    double e = std::abs(static_cast<double>(cpu[i]) - gpu[i]);
                    maximum = std::max(maximum, e);
                    mean += e;
                }
                if (maximum > 2e-6 || a.samples != b.samples || a.appleFloorClipped != b.appleFloorClipped ||
                    a.aboveNominalWhite != b.aboveNominalWhite)
                    throw std::runtime_error("CUDA numerical qualification failed");
                largest = std::max(largest, maximum);
                cases.push_back({{"creative", creative},
                                 {"exposure_ev", ev},
                                 {"max_error", maximum},
                                 {"mean_error", mean / cpu.size()},
                                 {"samples", cpu.size()}});
            }
        tone = savedTone;
        exposure = savedExposure;
        uploadSeconds = downloadSeconds = kernelSeconds = 0;
        calls = 0;
        return {{"passed", true},
                {"reference", "independent scalar CPU double"},
                {"absolute_tolerance", 2e-6},
                {"max_error", largest},
                {"cases", cases}};
    }
};
CudaTransformer::CudaTransformer(double e, const ToneAdjustments& t) : impl_(std::make_unique<Impl>(e, t)) {
    impl_->report["qualification"] = impl_->Qualify();
}
CudaTransformer::~CudaTransformer() = default;
void CudaTransformer::Apply(std::span<float> p, SignalStatistics& s) {
    impl_->Apply(p, s);
}
nlohmann::json CudaTransformer::Report() const {
    auto j = impl_->report;
    j["upload_seconds"] = impl_->uploadSeconds;
    j["gpu_transform_seconds"] = impl_->kernelSeconds;
    j["download_seconds"] = impl_->downloadSeconds;
    j["chunks"] = impl_->calls;
    j["pinned_host_bytes"] = impl_->capacity + Impl::Blocks * sizeof(Statistics);
    j["device_buffer_bytes"] = impl_->capacity + Impl::Blocks * sizeof(Statistics);
    return j;
}
nlohmann::json CudaTransformer::Qualify() {
    return CudaTransformer(0, {}).Report();
}
} // namespace logforge
