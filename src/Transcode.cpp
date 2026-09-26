#include "logforge/Transcode.h"
#include "logforge/AppleLogIdentification.h"
#include "logforge/Color.h"
#include "logforge/FloatBridge.h"
#include "logforge/FloatTransformer.h"
#include "logforge/PixelSanity.h"
#include "logforge/StorageSafety.h"
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <thread>

namespace logforge {
namespace {
void logCommand(Logger& log, const fs::path& exe, const std::vector<std::wstring>& args) {
    log.Write("Command: " + Utf8(CommandLine(exe, args)));
}
} // namespace
ValidationReport TranscodeJob::Run(const FFmpegInstallation& tools, const MediaInfo& input,
                                   const fs::path& output, Logger& log, const std::atomic_bool& cancel,
                                   const JobCallback& progress, const TranscodeOptions& options) {
    const auto jobStart = std::chrono::steady_clock::now();
    auto m = input;
    auto lease = ToolTrust::Acquire(tools.ffmpeg);
    if (tools.ffmpeg != lease->identity.ffmpeg || tools.ffprobe != lease->identity.ffprobe)
        throw AppError(Message(TextId::FFmpegUntrusted, {PathText(tools.ffmpeg)}));
    if (!tools.numericallyVerified)
        throw AppError(TextId::FFmpegUnverified);
    if (!tools.lease || lease->identity.ffmpegHash != tools.lease->identity.ffmpegHash ||
        lease->identity.ffprobeHash != tools.lease->identity.ffprobeHash)
        throw AppError(TextId::FFmpegHashChanged);
    if (cancel.load())
        throw AppError(TextId::Cancelled);
    ValidateExposureStops(options.exposureStops);
    ValidateToneAdjustments(options.tone);
    if (auto errors = m.UnsupportedReasons(); !errors.empty())
        throw AppError(TextId::InputRejected, errors);
    const auto dest = fs::absolute(output);
    if (_wcsicmp(dest.extension().c_str(), L".mov") != 0)
        throw AppError(TextId::OutputExtension);
    if (fs::exists(dest))
        throw AppError(TextId::OutputExists);
    if (!fs::is_directory(dest.parent_path()))
        throw AppError(TextId::OutputDirectory);
    RequireWritableDirectory(DataDirectory() / L"logs");
    const auto sourceAtoms = ReferenceMovAnalyzer::Analyze(m.path);
    auto timeline = InspectMovTimeline(m, sourceAtoms, tools.ffprobe, &cancel);
    progress({Message(TextId::VerifyTiming)});
    log.Write("Input media: " + m.raw.dump());
    m.cadence = VerifyConstantFrameRate(tools.ffprobe, m, cancel);
    m.fps = m.cadence.rate;
    const auto expectedFrames = m.cadence.packets;
    PixelSanity pixelSanity(m, expectedFrames);
    // Estimate from Apple's approximate HQ target (220 Mb/s at 1080p29.97),
    // scaled for raster/cadence with 75% headroom. Add audio and rotation's second
    // copy. This is a planning heuristic, never an assertion of exact bitrate.
    const long double videoEstimate = 220000000.L / 8 * m.videoDuration *
                                      (static_cast<long double>(m.width) * m.height / (1920.L * 1080)) *
                                      (m.fps.Value() / (30000.L / 1001)) * 1.75L;
    long double audioEstimate = 0;
    for (const auto& audio : m.audio)
        audioEstimate += static_cast<long double>(audio.channels) * audio.sampleRate * 4 *
                         std::max(audio.duration, m.videoDuration);
    const long double estimatedBytes =
        (videoEstimate + audioEstimate) * (std::abs(m.rotation) > 0.01 ? 2 : 1) + 256ull * 1024 * 1024;
    if (estimatedBytes >= static_cast<long double>(std::numeric_limits<uint64_t>::max()))
        throw AppError(TextId::OutputSpace);
    const auto estimate = static_cast<uint64_t>(estimatedBytes);
    const auto availableSpace = [&](const fs::path& p) {
        return options.io && options.io->availableSpace ? options.io->availableSpace(p)
                                                        : fs::space(p).available;
    };
    if (availableSpace(dest.parent_path()) < estimate)
        throw AppError(TextId::OutputSpace);
    fs::path partial =
        dest.parent_path() / (dest.stem().wstring() + L".logforge-" + std::to_wstring(GetCurrentProcessId()) +
                              L"-" + std::to_wstring(GetTickCount64()) + L".partial.mov");
    Handle reservation(
        CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!reservation)
        throw AppError(Message(TextId::OutputCreate, {Utf8(WinError())}));
    reservation.reset();
    struct PartialGuard {
        fs::path p;
        bool keep = false;
        ~PartialGuard() {
            if (!keep) {
                std::error_code ec;
                fs::remove(p, ec);
            }
        }
    } guard{partial};
    OwnedJobFiles owned;
    owned.Track(partial);
    log.Write(
        "CFR timestamps verified: " + std::to_string(expectedFrames) +
        " frames. BT.2408 HLG75% -> 100% reflectance; scale=" + std::to_string(HLGToReflectanceScale()) +
        "; exposure EV=" + std::to_string(options.exposureStops));
    log.Write("Creative adjustment enabled=" + std::to_string(options.tone.enabled) +
              "; shadow lift EV=" + std::to_string(options.tone.shadowStops) +
              "; highlight compression EV=" + std::to_string(options.tone.highlightStops) +
              "; saturation=" + std::to_string(options.tone.saturation));
    Json backend{{"requested", BackendName(options.backend)}, {"backend", "cpu"}};
    std::unique_ptr<CudaTransformer> cuda;
    if (options.backend != ProcessingBackend::CPU) {
        try {
            if (options.io && options.io->cudaCheckpoint)
                options.io->cudaCheckpoint("initialize");
            cuda = std::make_unique<CudaTransformer>(options.exposureStops, options.tone);
            backend.update(cuda->Report());
        } catch (const std::exception& e) {
            if (options.backend == ProcessingBackend::CUDA)
                throw AppError(Message(TextId::BackendFailed, {e.what()}));
            backend["fallback_reason"] = e.what();
            log.Write(std::string("Auto CPU fallback: ") + e.what());
        }
    }
    const auto threads = PlanThreads(cuda != nullptr);
    const std::wstring range = m.range == "pc" ? L"full" : L"limited";
    std::wstring decodeFilter = L"zscale=matrixin=2020_ncl:matrix=gbr:rangein=" + range +
                                L":range=full:transferin=arib-std-b67:transfer=arib-std-b67:primariesin=2020:"
                                L"primaries=2020:chromalin=" +
                                Wide(m.EffectiveChromaLocation()) + L":filter=spline36,format=gbrpf32le";
    std::vector<std::wstring> decode{L"-hide_banner",
                                     L"-loglevel",
                                     L"warning",
                                     L"-nostdin",
                                     L"-threads",
                                     std::to_wstring(threads.decode),
                                     L"-filter_threads",
                                     L"1",
                                     L"-noautorotate",
                                     L"-guess_layout_max",
                                     L"0",
                                     L"-i",
                                     m.path.wstring(),
                                     L"-map",
                                     L"0:v:0",
                                     L"-an",
                                     L"-sn",
                                     L"-dn",
                                     L"-vf",
                                     decodeFilter,
                                     L"-fps_mode",
                                     L"passthrough",
                                     L"-f",
                                     L"rawvideo",
                                     L"-pix_fmt",
                                     L"gbrpf32le",
                                     L"pipe:1"};
    // Equal input/output transfer tags on zscale make it a matrix/range/resampler ONLY.
    // Apple Log math is already applied to float samples by LogForge.
    const std::wstring encodeFilter =
        L"zscale=matrixin=gbr:matrix=2020_ncl:rangein=full:range=limited:transferin=linear:transfer=linear:"
        L"primariesin=2020:primaries=2020:chromal=left:filter=spline36:dither=error_diffusion,format="
        L"yuv422p10le,setparams=color_primaries=bt2020:color_trc=unknown:colorspace=bt2020nc:range=limited";
    std::vector<std::wstring> encode{
        L"-hide_banner", L"-loglevel", L"warning", L"-nostdin", L"-y", L"-copyts", L"-f", L"rawvideo",
        L"-pixel_format", L"gbrpf32le", L"-video_size",
        std::to_wstring(m.width) + L"x" + std::to_wstring(m.height), L"-framerate", m.fps.Text(), L"-i",
        L"pipe:0", L"-itsoffset", std::to_wstring(-m.startTime), L"-noautorotate",
        // Audio is copied verbatim; do not invent a layout for unlabelled channels.
        L"-guess_layout_max", L"0", L"-i", m.path.wstring(), L"-map", L"0:v:0", L"-map", L"1:a?",
        L"-map_chapters", L"1", L"-vf", encodeFilter, L"-c:v", L"prores_ks", L"-profile:v", L"3", L"-pix_fmt",
        L"yuv422p10le", L"-threads:v", std::to_wstring(threads.encode), L"-filter_threads", L"1",
        L"-fps_mode", L"passthrough", L"-c:a", L"copy", L"-avoid_negative_ts", L"disabled", L"-progress",
        L"pipe:1", L"-stats_period", L"0.25", L"-nostats"};
    const auto metadata = AppleLogMetadataWriter::Arguments(m, options.exposureStops, options.tone);
    encode.insert(encode.end(), metadata.begin(), metadata.end());
    encode.push_back(partial.wstring());
    logCommand(log, tools.ffmpeg, decode);
    logCommand(log, tools.ffmpeg, encode);
    FFmpegProcess decoder(tools.ffmpeg, decode), encoder(tools.ffmpeg, encode, true);
    std::string decoderError, encoderError;
    std::exception_ptr progressError, decodeReadError, encodeReadError;
    std::jthread readDecoderError, readEncoderError, progressReader, cancellation;
    struct StopBeforeJoin {
        FFmpegProcess &decoder, &encoder;
        bool complete = false;
        ~StopBeforeJoin() {
            if (!complete) {
                decoder.Terminate();
                encoder.Terminate();
            }
        }
    } stopReaders{decoder, encoder};
    readDecoderError = std::jthread([&] {
        try {
            ReadLines(decoder.ErrorPipe(), [&](const auto& line) {
                log.Write("decode: " + line);
                if (decoderError.size() + line.size() + 1 <= 65536)
                    decoderError += line + '\n';
            });
        } catch (...) {
            decodeReadError = std::current_exception();
            decoder.Terminate();
            encoder.Terminate();
        }
    });
    readEncoderError = std::jthread([&] {
        try {
            ReadLines(encoder.ErrorPipe(), [&](const auto& line) {
                log.Write("encode: " + line);
                if (encoderError.size() + line.size() + 1 <= 65536)
                    encoderError += line + '\n';
            });
        } catch (...) {
            encodeReadError = std::current_exception();
            decoder.Terminate();
            encoder.Terminate();
        }
    });
    progressReader = std::jthread([&] {
        try {
            std::array<char, 8192> b{};
            std::string pending;
            JobProgress p{Message(TextId::Converting)};
            size_t got;
            while ((got = encoder.Read(b.data(), b.size()))) {
                pending.append(b.data(), got);
                size_t pos;
                while ((pos = pending.find('\n')) != std::string::npos) {
                    if (pos > 65536)
                        throw AppError(TextId::ProcessOutputLimit);
                    auto line = pending.substr(0, pos);
                    pending.erase(0, pos + 1);
                    auto equal = line.find('=');
                    if (equal == std::string::npos)
                        continue;
                    auto key = line.substr(0, equal), value = line.substr(equal + 1);
                    try {
                        if (key == "frame")
                            p.frame = std::stoll(value);
                        else if (key == "fps")
                            p.fps = std::stod(value);
                        else if (key == "out_time_us")
                            p.seconds = std::stod(value) / 1000000;
                        else if (key == "speed")
                            p.speed = std::stod(value);
                        else if (key == "progress") {
                            p.seconds = std::max(p.seconds, static_cast<double>(p.frame) / m.fps.Value());
                            p.fraction = std::clamp(p.seconds / m.videoDuration, 0.0, 1.0);
                            progress(p);
                        }
                    } catch (const std::invalid_argument&) {
                    } catch (const std::out_of_range&) {
                    }
                }
                if (pending.size() > 65536)
                    throw AppError(TextId::ProcessOutputLimit);
            }
        } catch (...) {
            progressError = std::current_exception();
            decoder.Terminate();
            encoder.Terminate();
        }
    });
    cancellation = std::jthread([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            if (cancel.load()) {
                decoder.Terminate();
                encoder.Terminate();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    int64_t frames = 0;
    int decodeExit = -1, encodeExit = -1;
    SignalStatistics signal;
    size_t floatBufferBytes = 0;
    unsigned transformWorkers = 0;
    const auto transformStart = std::chrono::steady_clock::now();
    double decodeReadSeconds = 0, transformSeconds = 0, encodeWriteSeconds = 0;
    const auto elapsed = [](auto start) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    std::exception_ptr workError;
    try {
        const size_t frameSamples = static_cast<size_t>(m.width) * m.height * 3;
        FloatTransformer transformer(options.exposureStops, options.tone, threads.transform);
        transformWorkers = transformer.Workers();
        BridgeTiming timing;
        progress({Message(TextId::Decoding), 0});
        frames = RunFloatBridge(
            decoder, encoder, frameSamples, options.tone.enabled, cancel,
            [&](std::span<float> samples) {
                pixelSanity.Observe(samples);
                if (cuda) {
                    try {
                        if (options.io && options.io->cudaCheckpoint)
                            options.io->cudaCheckpoint("transform");
                        cuda->Apply(samples, signal);
                    } catch (const std::exception& e) {
                        if (options.backend == ProcessingBackend::CUDA)
                            throw AppError(Message(TextId::BackendFailed, {e.what()}));
                        backend.update(cuda->Report());
                        backend["fallback_reason"] = e.what();
                        backend["backend"] = "cpu after cuda";
                        log.Write(std::string("CUDA failed; unchanged chunk retried on CPU: ") + e.what());
                        cuda.reset();
                        transformer.Apply(samples, signal);
                    }
                } else
                    transformer.Apply(samples, signal);
            },
            timing);
        decodeReadSeconds = timing.read;
        transformSeconds = timing.transform;
        encodeWriteSeconds = timing.write;
        floatBufferBytes = timing.bufferBytes;
        backend["bridge_buffers"] = timing.buffers;
        backend["bridge_total_bytes"] = timing.buffers * timing.bufferBytes;
        decodeExit = decoder.Wait();
        encodeExit = encoder.Wait();
    } catch (...) {
        workError = std::current_exception();
        decoder.Terminate();
        encoder.Terminate();
        decoder.Wait();
        encoder.Wait();
    }
    // Both primary processes have exited; release any descendant-held pipe ends.
    decoder.Terminate();
    encoder.Terminate();
    cancellation.request_stop();
    cancellation.join();
    readDecoderError.join();
    readEncoderError.join();
    progressReader.join();
    stopReaders.complete = true;
    log.Write("Decoder exit=" + std::to_string(decodeExit) + "; encoder exit=" + std::to_string(encodeExit) +
              "; processed frames=" + std::to_string(frames));
    if (cancel.load())
        throw AppError(TextId::CancelledClean);
    if (progressError)
        std::rethrow_exception(progressError);
    if (decodeReadError)
        std::rethrow_exception(decodeReadError);
    if (encodeReadError)
        std::rethrow_exception(encodeReadError);
    if (workError) {
        try {
            std::rethrow_exception(workError);
        } catch (const std::exception& e) {
            throw AppError(Message(TextId::ProcessingFailed,
                                   {std::string(e.what()) + "\n" + encoderError + decoderError}));
        }
    }
    if (decodeExit || encodeExit || frames != expectedFrames)
        throw AppError(Message(TextId::EncodeFailed, {encoderError + decoderError}));
    const double pipelineSeconds = elapsed(transformStart);
    const auto remuxStart = std::chrono::steady_clock::now();
    // rawvideo has no orientation field. Remux from our encoded file to apply the
    // original display rotation using FFmpeg's documented display_rotation option.
    if (std::abs(m.rotation) > 0.01) {
        progress({Message(TextId::PreservingRotation)});
        auto rotated = partial;
        rotated += L".rotated.mov";
        PartialGuard rotatedGuard{rotated};
        Handle rotatedReservation(CreateFileW(rotated.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                              FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!rotatedReservation)
            throw AppError(TextId::OutputCreate);
        rotatedReservation.reset();
        owned.Track(rotated);
        if (availableSpace(dest.parent_path()) < fs::file_size(partial) + 32ull * 1024 * 1024)
            throw AppError(TextId::RotationSpace);
        std::vector<std::wstring> args{L"-hide_banner",
                                       L"-v",
                                       L"error",
                                       L"-nostdin",
                                       L"-display_rotation:v:0",
                                       std::to_wstring(m.rotation),
                                       L"-guess_layout_max",
                                       L"0",
                                       L"-i",
                                       partial.wstring(),
                                       L"-map",
                                       L"0:v:0",
                                       L"-map",
                                       L"0:a?",
                                       L"-c",
                                       L"copy",
                                       L"-map_chapters",
                                       L"0",
                                       L"-y"};
        args.insert(args.end(), metadata.begin(), metadata.end());
        args.push_back(rotated.wstring());
        logCommand(log, tools.ffmpeg, args);
        auto r = RunProcess(tools.ffmpeg, args, &cancel, 0);
        if (r.exitCode)
            throw AppError(Message(TextId::RotationFailed, {r.error}));
        fs::remove(partial);
        fs::rename(rotated, partial);
        owned.Track(partial);
    }
    const double remuxSeconds = elapsed(remuxStart);
    const auto headerStart = std::chrono::steady_clock::now();
    const auto headerTimes = PreserveMovCreationTimes(sourceAtoms, partial, cancel);
    const double headerSeconds = elapsed(headerStart);
    const auto identificationStart = std::chrono::steady_clock::now();
    progress({Message(TextId::WritingIdentification)});
    AppleLogIdentificationWriter::WriteToEncodedPartial(partial, cancel);
    log.Write("Apple Log identification: ProRes sample-entry logs / com.apple.rec2020.apple-log");
    const double identificationSeconds = elapsed(identificationStart);
    const auto validationStart = std::chrono::steady_clock::now();
    progress({Message(TextId::ValidatingOutput)});
    auto out = Probe(tools.ffprobe, partial, &cancel);
    out.outputChromaVerified =
        tools.numericallyVerified && tools.numeric.value("chroma_reference_passed", false);
    out.cadence = VerifyConstantFrameRate(tools.ffprobe, out, cancel, false);
    out.fps = out.cadence.rate;
    auto report = ValidateOutput(m, out, frames);
    report.signal["pixel_sanity"] =
        pixelSanity.Validate(tools.ffmpeg, partial, cancel, options.exposureStops, options.tone);
    if (!report.signal["pixel_sanity"]["passed"].get<bool>()) {
        report.passed = false;
        report.errors.emplace_back(TextId::PixelSanityFailed);
    }
    report.ffmpeg = tools.ToJson();
    report.metadata["timeline_policy"] = timeline;
    report.metadata["creation_headers"] = headerTimes;
    report.metadata["chroma"] = {
        {"input_ffprobe", m.chromaLocation},
        {"input_explicit_override", m.inputChromaOverride},
        {"input_used", m.EffectiveChromaLocation()},
        {"output_ffprobe", out.chromaLocation},
        {"output_declared", out.tags.value("logforge.chroma_location", "")},
        {"verification", "explicit zscale siting plus reference signal test; MOV/ffprobe may omit siting"}};
    const auto atomReport = ReferenceMovAnalyzer::Analyze(partial);
    report.metadata["output_timeline_policy"] = InspectMovTimeline(out, atomReport, tools.ffprobe, &cancel);
    report.metadata["apple_log_identification"] = AppleLogIdentificationWriter::Validate(atomReport);
    if (!report.metadata["apple_log_identification"]["passed"].get<bool>()) {
        report.passed = false;
        report.errors.emplace_back(TextId::OutputIdentification);
    }
    bool correctColr = false;
    for (const auto& a : atomReport["atoms"])
        if (a.value("type", std::string()) == "colr" && a.value("color_type", std::string()) == "nclc" &&
            a.value("primaries", 0) == 9 && a.value("transfer", 0) == 2 && a.value("matrix", 0) == 9)
            correctColr = true;
    if (!correctColr) {
        report.passed = false;
        report.errors.emplace_back(TextId::OutputColr);
    }
    report.signal.update(
        {{"scope", "all transformed RGB components, before YCbCr quantization and ProRes encoding"},
         {"samples", signal.samples},
         {"input_hlg_min", signal.inputMinimum},
         {"input_hlg_max", signal.inputMaximum},
         {"output_apple_log_min", signal.outputMinimum},
         {"output_apple_log_max", signal.outputMaximum},
         {"apple_floor_clipped", signal.appleFloorClipped},
         {"above_nominal_white", signal.aboveNominalWhite}});
    report.signalWarning = signal.appleFloorClipped || signal.aboveNominalWhite;
    if (signal.appleFloorClipped)
        report.warnings.emplace_back(TextId::SignalFloor);
    if (signal.aboveNominalWhite)
        report.warnings.emplace_back(TextId::SignalWhite);
    report.timing["stages"] = {{"decode_pipe_read_seconds", decodeReadSeconds},
                               {"decode_cpu_seconds", decoder.CpuSeconds()},
                               {"encode_cpu_seconds", encoder.CpuSeconds()},
                               {"transform_seconds", transformSeconds},
                               {"encode_pipe_write_seconds", encodeWriteSeconds},
                               {"decode_transform_encode_wall_seconds", pipelineSeconds},
                               {"rotation_remux_seconds", remuxSeconds},
                               {"creation_metadata_seconds", headerSeconds},
                               {"identification_seconds", identificationSeconds},
                               {"validation_seconds", elapsed(validationStart)},
                               {"total_before_publication_seconds", elapsed(jobStart)},
                               {"definition", "Pipe read/write include backpressure; decode and encode run "
                                              "concurrently. Stage values are not additive."}};
    report.timing["threads"] = {{"logical_cpus", threads.logical},
                                {"decoder", threads.decode},
                                {"encoder", threads.encode},
                                {"color", threads.transform},
                                {"filter", threads.filter}};
    report.timing["mode"] = options.tone.enabled ? "creative" : "standard";
    if (cuda)
        backend.update(cuda->Report());
    report.timing["backend"] = backend;
    report.timing["space_estimate"] = {
        {"bytes", estimate},
        {"heuristic", true},
        {"hq_target_mbps_1080p2997", 220},
        {"headroom_factor", 1.75},
        {"rotation_second_copy", std::abs(m.rotation) > 0.01},
        {"note",
         "Scene complexity and encoder behavior affect actual ProRes size; write failures remain fatal."}};
    Json full{
        {"version", Version},
        {"build", BuildNumber},
        {"validation", report.ToJson()},
        {"input", m.raw},
        {"output", out.raw},
        {"output_atoms", atomReport},
        {"processed_frames", frames},
        {"processing",
         {{"float_buffer_bytes", floatBufferBytes},
          {"transform_workers", transformWorkers},
          {"mode", options.tone.enabled ? "planar frame with parallel RGB tiles" : "bounded planar chunks"},
          {"transform_and_encode_seconds", pipelineSeconds}}},
        {"color",
         {{"transform", "inverse HLG OETF -> reflectance scale -> Apple Log"},
          {"reference", "BT.2408: 75% HLG -> 100% reflectance"},
          {"reference_scale", HLGToReflectanceScale()},
          {"exposure_ev", options.exposureStops},
          {"creative_adjustment",
           {{"enabled", options.tone.enabled},
            {"algorithm", "creative-luma-v1"},
            {"shadow_lift_ev", options.tone.shadowStops},
            {"highlight_compression_ev", options.tone.highlightStops},
            {"saturation", options.tone.saturation}}},
          {"scale", HLGToReflectanceScale() * std::exp2(options.exposureStops)},
          {"intermediate", "gbrpf32le"},
          {"math", "double"},
          {"range", "video (Y 64..940; C 64..960)"}}}};
    auto reportPath = DataDirectory() / L"logs" /
                      (L"LogForge-" + dest.filename().wstring() + L"-" + std::to_wstring(GetTickCount64()) +
                       L".validation.json");
    auto reportTemporary = fs::path(reportPath.wstring() + L".tmp");
    PartialGuard reportGuard{reportTemporary};
    if (options.io && options.io->beforeReportWrite)
        options.io->beforeReportWrite(reportTemporary);
    std::ofstream file(reportTemporary);
    file << full.dump(2);
    file.close();
    if (!file || !MoveFileExW(reportTemporary.c_str(), reportPath.c_str(), MOVEFILE_WRITE_THROUGH))
        throw AppError(TextId::ReportSave);
    log.Write("Validation report: " + PathText(reportPath) + "\n" + report.ToJson().dump());
    if (!report.passed)
        throw AppError(TextId::ValidationFailed, report.errors);
    if (cancel.load())
        throw AppError(TextId::Cancelled);
    // Same-volume rename publishes only a fully validated file, with no overwrite.
    // Windows std::filesystem::rename may replace an existing destination.
    // No REPLACE_EXISTING flag: also refuse a file created during conversion.
    if (!MoveFileExW(partial.c_str(), dest.c_str(), MOVEFILE_WRITE_THROUGH)) {
        if (fs::exists(dest))
            throw AppError(TextId::OutputExists);
        throw AppError(Message(TextId::OutputCreate, {Utf8(WinError())}));
    }
    guard.keep = true;
    progress({Message(report.signalWarning   ? TextId::CompleteWarning
                      : options.tone.enabled ? TextId::CompleteCreative
                                             : TextId::CompleteStandard),
              1.0, m.videoDuration, 0, 0, frames});
    return report;
}
} // namespace logforge
