import Foundation
import Metal
import CoreVideo
import IOSurface

final class MetalColorProcessor {

    // MARK: - Errors

    enum ProcessorError: LocalizedError {

        case metalUnavailable
        case metalLibraryUnavailable
        case kernelUnavailable

        case invalidPixelFormat
        case invalidDimensions

        case inputMemoryUnavailable
        case outputMemoryUnavailable

        case inputStrideTooSmall
        case outputStrideTooSmall

        case commandBufferUnavailable
        case commandBufferFailed(String)

        case alreadyWaiting

        var errorDescription: String? {

            switch self {

            case .metalUnavailable:
                return L10n.text("error.metal")

            case .metalLibraryUnavailable:
                return L10n.text("error.metal") + " (library)"

            case .kernelUnavailable:
                return L10n.text("error.metal") + " (logForgeV210Kernel)"

            case .invalidPixelFormat:
                return L10n.text("error.sample")

            case .invalidDimensions:
                return L10n.text("error.size")

            case .inputMemoryUnavailable:
                return L10n.text("error.memory")

            case .outputMemoryUnavailable:
                return L10n.text("error.memory")

            case .inputStrideTooSmall:
                return L10n.text("error.sample") + " (input stride)"

            case .outputStrideTooSmall:
                return L10n.text("error.sample") + " (output stride)"

            case .commandBufferUnavailable:
                return L10n.text("error.metal") + " (command buffer)"

            case .commandBufferFailed(let message):
                return L10n.text("error.metal") + " \(message)"

            case .alreadyWaiting:
                return L10n.text("error.metal") + " (duplicate wait)"
            }
        }
    }

    // MARK: - Locked Memory

    private struct LockedMemory {

        let baseAddress: UnsafeMutableRawPointer

        private let unlockHandler: () -> Void

        init(
            baseAddress: UnsafeMutableRawPointer,
            unlockHandler: @escaping () -> Void
        ) {
            self.baseAddress = baseAddress
            self.unlockHandler = unlockHandler
        }

        func unlock() {
            unlockHandler()
        }
    }

    // MARK: - GPU Job

    final class Job {

        let outputPixelBuffer: CVPixelBuffer

        private let lock = NSLock()

        private var completionResult:
            Result<CVPixelBuffer, Error>?

        private var continuation:
            CheckedContinuation<CVPixelBuffer, Error>?

        private var hasWaiter = false
        private var didComplete = false

        private var cleanupHandler:
            (() -> Void)?

        init(
            outputPixelBuffer: CVPixelBuffer,
            cleanup: @escaping () -> Void
        ) {
            self.outputPixelBuffer =
                outputPixelBuffer

            self.cleanupHandler =
                cleanup
        }

        func complete(
            with result:
                Result<CVPixelBuffer, Error>
        ) {

            var waiter:
                CheckedContinuation<
                    CVPixelBuffer,
                    Error
                >?

            var cleanup:
                (() -> Void)?

            lock.lock()
            guard !didComplete else { lock.unlock(); return }
            didComplete = true
            cleanup = cleanupHandler
            cleanupHandler = nil
            lock.unlock()

            // Publish the result only after both CV locks are released. A waiter
            // arriving between GPU completion and cleanup must still suspend.
            cleanup?()
            lock.lock()

            if let existingContinuation =
                continuation {

                continuation = nil
                waiter = existingContinuation

            } else {

                completionResult = result
            }

            lock.unlock()

            waiter?.resume(
                with: result
            )
        }

        func wait()
            async throws -> CVPixelBuffer {

            try await
                withCheckedThrowingContinuation {
                    (
                        cont:
                        CheckedContinuation<
                            CVPixelBuffer,
                            Error
                        >
                    ) in

                    lock.lock()

                    if hasWaiter {

                        lock.unlock()

                        cont.resume(
                            throwing:
                                ProcessorError
                                .alreadyWaiting
                        )

                        return
                    }

                    hasWaiter = true

                    if let result =
                        completionResult {

                        completionResult = nil

                        lock.unlock()

                        cont.resume(
                            with: result
                        )

                        return
                    }

                    continuation = cont

                    lock.unlock()
                }
        }
    }

    // MARK: - Properties

    private let device: MTLDevice
    private let commandQueue: MTLCommandQueue
    private let pipelineState:
        MTLComputePipelineState

    // MARK: - Availability

    static var isAvailable: Bool {

        guard let device =
            MTLCreateSystemDefaultDevice()
        else {
            return false
        }

        guard let library =
            device.makeDefaultLibrary()
        else {
            return false
        }

        guard let function =
            library.makeFunction(
                name:
                    "logForgeV210Kernel"
            )
        else {
            return false
        }

        do {

            _ =
                try device.makeComputePipelineState(
                    function: function
                )

            return true

        } catch {

            return false
        }
    }

    // MARK: - Init

    init() throws {

        guard let device =
            MTLCreateSystemDefaultDevice()
        else {
            throw ProcessorError.metalUnavailable
        }

        self.device = device

        guard let commandQueue =
            device.makeCommandQueue()
        else {
            throw ProcessorError.commandBufferUnavailable
        }

        self.commandQueue =
            commandQueue

        guard let library =
            device.makeDefaultLibrary()
        else {
            throw ProcessorError.metalLibraryUnavailable
        }

        guard let function =
            library.makeFunction(
                name:
                    "logForgeV210Kernel"
            )
        else {
            throw ProcessorError.kernelUnavailable
        }

        do {

            self.pipelineState =
                try device.makeComputePipelineState(
                    function: function
                )

        } catch {

            throw ProcessorError.kernelUnavailable
        }
    }

    // MARK: - Pixel Buffer Memory

    private func lockPixelBuffer(
        _ pixelBuffer: CVPixelBuffer,
        readOnly: Bool
    ) throws -> LockedMemory {

        let cvFlags:
            CVPixelBufferLockFlags =
            readOnly
            ? [.readOnly]
            : []

        let lockStatus =
            CVPixelBufferLockBaseAddress(
                pixelBuffer,
                cvFlags
            )

        if lockStatus ==
            kCVReturnSuccess {

            if let baseAddress =
                CVPixelBufferGetBaseAddress(
                    pixelBuffer
                ) {

                return LockedMemory(
                    baseAddress:
                        baseAddress
                ) {

                    CVPixelBufferUnlockBaseAddress(
                        pixelBuffer,
                        cvFlags
                    )
                }
            }

            CVPixelBufferUnlockBaseAddress(
                pixelBuffer,
                cvFlags
            )
        }

        throw readOnly ? ProcessorError.inputMemoryUnavailable : ProcessorError.outputMemoryUnavailable
    }

    // MARK: - Submit GPU Job

    func submit(
        inputPixelBuffer:
            CVPixelBuffer,

        outputPixelBuffer: CVPixelBuffer,
        options: ConversionOptions = ConversionOptions(),
        fullRange: Bool = false, centeredChroma: Bool = false
    ) throws -> Job {

        let width =
            CVPixelBufferGetWidth(
                inputPixelBuffer
            )

        let height =
            CVPixelBufferGetHeight(
                inputPixelBuffer
            )

        guard width > 0, height > 0, width % 2 == 0,
              CVPixelBufferGetWidth(outputPixelBuffer) == width,
              CVPixelBufferGetHeight(outputPixelBuffer) == height else {

            throw ProcessorError
                .invalidDimensions
        }

        let inputFormat =
            CVPixelBufferGetPixelFormatType(
                inputPixelBuffer
            )

        let outputFormat =
            CVPixelBufferGetPixelFormatType(
                outputPixelBuffer
            )

        guard inputFormat ==
                kCVPixelFormatType_422YpCbCr10,
              outputFormat ==
                kCVPixelFormatType_422YpCbCr10 else {

            throw ProcessorError
                .invalidPixelFormat
        }

        let inputStride =
            CVPixelBufferGetBytesPerRow(
                inputPixelBuffer
            )

        let outputStride =
            CVPixelBufferGetBytesPerRow(
                outputPixelBuffer
            )

        let groupsPerRow =
            (width + 5) / 6

        let minimumRowBytes =
            groupsPerRow * 16

        guard inputStride >= minimumRowBytes
        else {

            throw ProcessorError
                .inputStrideTooSmall
        }

        guard outputStride >= minimumRowBytes
        else {

            throw ProcessorError
                .outputStrideTooSmall
        }

        let inputMemory =
            try lockPixelBuffer(
                inputPixelBuffer,
                readOnly: true
            )

        let outputMemory: LockedMemory

        do {

            outputMemory =
                try lockPixelBuffer(
                    outputPixelBuffer,
                    readOnly: false
                )

        } catch {

            inputMemory.unlock()

            throw error
        }

        let inputLength =
            inputStride * height

        guard let inputBuffer = device.makeBuffer(bytes: inputMemory.baseAddress, length: inputLength, options: .storageModeShared),
              let outputBuffer = device.makeBuffer(length: outputStride * height, options: .storageModeShared) else {
            outputMemory.unlock(); inputMemory.unlock()
            throw ProcessorError.outputMemoryUnavailable
        }
        memset(outputBuffer.contents(), 0, outputStride * height)

        guard let commandBuffer =
            commandQueue.makeCommandBuffer()
        else {

            outputMemory.unlock()
            inputMemory.unlock()

            throw ProcessorError
                .commandBufferUnavailable
        }

        commandBuffer.label =
            "LogForge V210 Frame"

        guard let encoder =
            commandBuffer
                .makeComputeCommandEncoder()
        else {

            outputMemory.unlock()
            inputMemory.unlock()

            throw ProcessorError
                .commandBufferUnavailable
        }

        encoder.label =
            "LogForge V210 Compute"

        encoder.setComputePipelineState(
            pipelineState
        )

        encoder.setBuffer(
            inputBuffer,
            offset: 0,
            index: 0
        )

        encoder.setBuffer(
            outputBuffer,
            offset: 0,
            index: 1
        )

        var width32 =
            UInt32(width)

        var height32 =
            UInt32(height)

        var inputStride32 =
            UInt32(inputStride)

        var outputStride32 =
            UInt32(outputStride)

        encoder.setBytes(
            &width32,
            length:
                MemoryLayout<UInt32>.size,
            index: 2
        )

        encoder.setBytes(
            &height32,
            length:
                MemoryLayout<UInt32>.size,
            index: 3
        )

        encoder.setBytes(
            &inputStride32,
            length:
                MemoryLayout<UInt32>.size,
            index: 4
        )

        encoder.setBytes(
            &outputStride32,
            length:
                MemoryLayout<UInt32>.size,
            index: 5
        )

        let parameters = options.parameters(fullRange: fullRange, centeredChroma: centeredChroma)
        parameters.withUnsafeBytes { bytes in
            encoder.setBytes(bytes.baseAddress!, length: bytes.count, index: 6)
        }
        let grid =
            MTLSize(
                width:
                    groupsPerRow,
                height:
                    height,
                depth:
                    1
            )

        let threadExecutionWidth =
            pipelineState
                .threadExecutionWidth

        let maxThreads =
            pipelineState
                .maxTotalThreadsPerThreadgroup

        let threadsPerGroupWidth =
            max(
                1,
                min(
                    threadExecutionWidth * 4,
                    maxThreads
                )
            )

        let threadsPerGroup =
            MTLSize(
                width:
                    threadsPerGroupWidth,
                height:
                    1,
                depth:
                    1
            )

        encoder.dispatchThreads(
            grid,
            threadsPerThreadgroup:
                threadsPerGroup
        )

        encoder.endEncoding()

        let job =
            Job(
                outputPixelBuffer:
                    outputPixelBuffer
            ) {

                outputMemory.unlock()
                inputMemory.unlock()
            }

        commandBuffer.addCompletedHandler {
            [job, inputBuffer, outputBuffer, inputPixelBuffer]
            commandBuffer in
            _ = inputBuffer; _ = inputPixelBuffer

            if let error =
                commandBuffer.error {

                job.complete(
                    with:
                        .failure(
                            ProcessorError
                                .commandBufferFailed(
                                    error
                                        .localizedDescription
                                )
                        )
                )

            } else {
                memcpy(outputMemory.baseAddress, outputBuffer.contents(), outputStride * height)
                job.complete(
                    with:
                        .success(
                            outputPixelBuffer
                        )
                )
            }
        }

        commandBuffer.commit()

        return job
    }
    func qualify(options: ConversionOptions) async throws {
        guard let library = device.makeDefaultLibrary(), let function = library.makeFunction(name: "logForgeColorReferenceKernel") else { throw ProcessorError.kernelUnavailable }
        let pipeline = try await device.makeComputePipelineState(function: function)
        let vectors: [SIMD4<Float>] = [SIMD4(0,0,0,0), SIMD4(0.75,0.75,0.75,0), SIMD4(-0.2,0.4,1.2,0), SIMD4(2,0.001,0.5,0), SIMD4(0.25,0.5,0.9,0)]
        let length = vectors.count * MemoryLayout<SIMD4<Float>>.stride
        guard let input = vectors.withUnsafeBytes({ device.makeBuffer(bytes: $0.baseAddress!, length: length, options: .storageModeShared) }),
              let output = device.makeBuffer(length: length, options: .storageModeShared),
              let command = commandQueue.makeCommandBuffer(), let encoder = command.makeComputeCommandEncoder() else { throw ProcessorError.commandBufferUnavailable }
        encoder.setComputePipelineState(pipeline); encoder.setBuffer(input, offset: 0, index: 0); encoder.setBuffer(output, offset: 0, index: 1)
        let parameters = options.parameters(fullRange: false, centeredChroma: false)
        parameters.withUnsafeBytes { encoder.setBytes($0.baseAddress!, length: $0.count, index: 2) }
        var count = UInt32(vectors.count); encoder.setBytes(&count, length: 4, index: 3)
        encoder.dispatchThreads(MTLSize(width:vectors.count,height:1,depth:1), threadsPerThreadgroup:MTLSize(width:1,height:1,depth:1)); encoder.endEncoding()
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            command.addCompletedHandler { completed in
                _ = input; _ = output
                if let error = completed.error { continuation.resume(throwing:error) } else { continuation.resume() }
            }
            command.commit()
        }
        let actual = output.contents().assumingMemoryBound(to:SIMD4<Float>.self)
        for i in vectors.indices {
            let v = vectors[i]
            let reference = ReferenceColor.transform(SIMD3(Double(v.x), Double(v.y), Double(v.z)),options:options)
            guard abs(Double(actual[i].x)-reference.x) <= 2e-6,
                  abs(Double(actual[i].y)-reference.y) <= 2e-6,
                  abs(Double(actual[i].z)-reference.z) <= 2e-6 else {
                throw NativeFailure("error.gpuPrecision", "vector=\(i), exposure=\(options.exposure), creative=\(options.creativeEnabled), GPU=\(actual[i]), CPU=\(reference)")
            }
        }
    }

}
