import AVFoundation
import CoreMedia
import CoreVideo
import SwiftUI
import UIKit

/// Presents decoded NV12 frames through iOS's native video display path.
///
/// `AVSampleBufferDisplayLayer` handles YUV conversion and drawable scheduling
/// itself. This avoids the Core Image -> MTKView path, which can remain black
/// on a physical device even though VideoToolbox is producing valid frames.
struct NativeVideoView: UIViewRepresentable {
    @Environment(\.scenePhase) private var scenePhase
    let pixelBuffer: CVPixelBuffer?
    let frameID: UInt64
    let captureUptime: TimeInterval
    let onFrameSubmitted: (UInt64, TimeInterval) -> Void

    func makeUIView(context: Context) -> SampleBufferVideoView {
        SampleBufferVideoView(frame: .zero)
    }

    func updateUIView(_ uiView: SampleBufferVideoView, context: Context) {
        if uiView.display(pixelBuffer, isActive: scenePhase == .active) {
            onFrameSubmitted(frameID, captureUptime)
        }
    }
}

final class SampleBufferVideoView: UIView {
    override class var layerClass: AnyClass {
        AVSampleBufferDisplayLayer.self
    }

    private var videoLayer: AVSampleBufferDisplayLayer {
        layer as! AVSampleBufferDisplayLayer
    }

    private var lastPixelBuffer: CVPixelBuffer?
    private var renderingActive = false
    private var submittedFrames: UInt64 = 0
    private var droppedFrames: UInt64 = 0

    override init(frame: CGRect) {
        super.init(frame: frame)
        configureLayer()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        configureLayer()
    }

    private func configureLayer() {
        backgroundColor = .black
        isOpaque = true
        videoLayer.backgroundColor = UIColor.black.cgColor
        // The SwiftUI host gives this view the exact aspect-fit video rect.
        // Filling that rect avoids a second, independently rounded aspect-fit
        // calculation inside AVSampleBufferDisplayLayer. That rounding becomes
        // visibly amplified when the remote desktop is zoomed up to 10x.
        videoLayer.videoGravity = .resize
    }

    /// True only when a new buffer was submitted; no presentation timestamp is
    /// exposed by this display layer, so latency ends at submission like the
    /// desktop renderer's submission fallback.
    func display(_ pixelBuffer: CVPixelBuffer?, isActive: Bool) -> Bool {
        if renderingActive != isActive {
            renderingActive = isActive
            // Backgrounding can invalidate the display layer without changing
            // the retained frame. Clear the queue and allow that frame to be
            // submitted again when the scene becomes active.
            lastPixelBuffer = nil
            videoLayer.flushAndRemoveImage()
        }

        guard let pixelBuffer else {
            lastPixelBuffer = nil
            submittedFrames = 0
            droppedFrames = 0
            videoLayer.flushAndRemoveImage()
            return false
        }
        guard renderingActive else { return false }

        // Check recovery before deduplicating frames: a failed layer may need
        // to display the same buffer again even if no new decoded frame arrives.
        if videoLayer.status == .failed || videoLayer.requiresFlushToResumeDecoding {
            NSLog("CrossDesk video layer failed: %@",
                  videoLayer.error?.localizedDescription ?? "unknown error")
            videoLayer.flush()
            lastPixelBuffer = nil
        }

        // SwiftUI can refresh for unrelated status fields. Only deduplicate a
        // frame after it was actually submitted to a healthy, active layer.
        guard lastPixelBuffer !== pixelBuffer else { return false }

        // Never let AVSampleBufferDisplayLayer turn temporary rendering
        // pressure into seconds of latency. Its queued buffers are already
        // stale by the time it stops accepting more data, so discard them and
        // present the newest decoded IOSurface instead.
        if !videoLayer.isReadyForMoreMediaData {
            droppedFrames &+= 1
            videoLayer.flush()
            if droppedFrames == 1 || droppedFrames.isMultiple(of: 300) {
                NSLog("CrossDesk dropped stale display frames: %llu",
                      droppedFrames)
            }
        }
        guard videoLayer.isReadyForMoreMediaData else { return false }

        var formatDescription: CMVideoFormatDescription?
        guard CMVideoFormatDescriptionCreateForImageBuffer(
            allocator: kCFAllocatorDefault,
            imageBuffer: pixelBuffer,
            formatDescriptionOut: &formatDescription
        ) == noErr, let formatDescription else {
            NSLog("CrossDesk could not create a video format description")
            return false
        }

        var timing = CMSampleTimingInfo(
            duration: .invalid,
            presentationTimeStamp: .invalid,
            decodeTimeStamp: .invalid
        )
        var sampleBuffer: CMSampleBuffer?
        guard CMSampleBufferCreateReadyWithImageBuffer(
            allocator: kCFAllocatorDefault,
            imageBuffer: pixelBuffer,
            formatDescription: formatDescription,
            sampleTiming: &timing,
            sampleBufferOut: &sampleBuffer
        ) == noErr, let sampleBuffer else {
            NSLog("CrossDesk could not create a video sample buffer")
            return false
        }

        if let attachments = CMSampleBufferGetSampleAttachmentsArray(
            sampleBuffer,
            createIfNecessary: true
        ), CFArrayGetCount(attachments) > 0 {
            let attachment = unsafeBitCast(
                CFArrayGetValueAtIndex(attachments, 0),
                to: CFMutableDictionary.self
            )
            CFDictionarySetValue(
                attachment,
                Unmanaged.passUnretained(kCMSampleAttachmentKey_DisplayImmediately).toOpaque(),
                Unmanaged.passUnretained(kCFBooleanTrue).toOpaque()
            )
        }

        videoLayer.enqueue(sampleBuffer)
        lastPixelBuffer = pixelBuffer
        submittedFrames &+= 1
        if submittedFrames == 1 || submittedFrames.isMultiple(of: 300) {
            NSLog("CrossDesk presented video frame %llu (%zu x %zu)",
                  submittedFrames,
                  CVPixelBufferGetWidth(pixelBuffer),
                  CVPixelBufferGetHeight(pixelBuffer))
        }
        if submittedFrames == 1 {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak self] in
                guard let self else { return }
                NSLog("CrossDesk video layer status=%ld error=%@",
                      self.videoLayer.status.rawValue,
                      self.videoLayer.error?.localizedDescription ?? "none")
            }
        }
        return true
    }
}
