import SwiftUI
import UIKit

enum RemoteVideoGeometry {
    private static func pixelAligned(_ rect: CGRect) -> CGRect {
        let scale = max(UIScreen.main.scale, 1)
        let minX = (rect.minX * scale).rounded() / scale
        let minY = (rect.minY * scale).rounded() / scale
        let maxX = (rect.maxX * scale).rounded() / scale
        let maxY = (rect.maxY * scale).rounded() / scale
        return CGRect(x: minX, y: minY,
                      width: max(0, maxX - minX),
                      height: max(0, maxY - minY))
    }

    static func aspectFitRect(containerSize: CGSize,
                              videoSize: CGSize) -> CGRect? {
        guard containerSize.width > 0, containerSize.height > 0,
              videoSize.width > 0, videoSize.height > 0 else { return nil }
        let videoAspect = videoSize.width / videoSize.height
        let containerAspect = containerSize.width / containerSize.height
        if videoAspect > containerAspect {
            let height = containerSize.width / videoAspect
            return pixelAligned(
                CGRect(x: 0, y: (containerSize.height - height) / 2,
                       width: containerSize.width, height: height)
            )
        }
        let width = containerSize.height * videoAspect
        return pixelAligned(
            CGRect(x: (containerSize.width - width) / 2, y: 0,
                   width: width, height: containerSize.height)
        )
    }

    static func transformedRect(containerSize: CGSize,
                                videoSize: CGSize,
                                scale: CGFloat,
                                offset: CGSize) -> CGRect? {
        guard let baseRect = aspectFitRect(containerSize: containerSize,
                                           videoSize: videoSize) else {
            return nil
        }
        // SwiftUI scales the video around the center of its own pixel-aligned
        // frame. That center can differ fractionally from the container center
        // after edge alignment, and the difference is amplified at 10x.
        let center = CGPoint(x: baseRect.midX, y: baseRect.midY)
        return CGRect(
            x: center.x + (baseRect.minX - center.x) * scale + offset.width,
            y: center.y + (baseRect.minY - center.y) * scale + offset.height,
            width: baseRect.width * scale,
            height: baseRect.height * scale
        )
    }
}

struct RemoteTouchInputView: UIViewRepresentable {
    let videoSize: CGSize
    let controlMode: MouseControlMode
    let remoteCursorPosition: CGPoint?
    let viewportScale: CGFloat
    let viewportOffset: CGSize
    let viewportChanged: (CGFloat, CGSize) -> Void
    let move: (Float, Float) -> Void
    let leftDown: (Float, Float) -> Void
    let leftUp: (Float, Float) -> Void
    let rightClick: (Float, Float) -> Void
    let scroll: (Float, Float, Int, Int) -> Void

    func makeUIView(context: Context) -> RemoteTouchSurface {
        RemoteTouchSurface()
    }

    func updateUIView(_ view: RemoteTouchSurface, context: Context) {
        view.videoSize = videoSize
        view.controlMode = controlMode
        view.setViewport(scale: viewportScale, offset: viewportOffset)
        view.onViewportChanged = viewportChanged
        view.onMove = move
        view.onLeftDown = leftDown
        view.onLeftUp = leftUp
        view.onRightClick = rightClick
        view.onScroll = scroll
        view.synchronizeRemoteCursor(remoteCursorPosition)
    }
}

final class RemoteTouchSurface: UIView, UIGestureRecognizerDelegate {
    var videoSize = CGSize.zero {
        didSet { setNeedsLayout() }
    }
    var controlMode: MouseControlMode = .absolute {
        didSet {
            guard oldValue != controlMode else { return }
            relativeCursorPoint = lastRemoteCursorPoint ?? (0.5, 0.5)
            relativePanActive = false
            heldDragPoint = nil
            heldDragLastLocation = nil
            hoverLastLocation = nil
        }
    }
    var onMove: ((Float, Float) -> Void)?
    var onLeftDown: ((Float, Float) -> Void)?
    var onLeftUp: ((Float, Float) -> Void)?
    var onRightClick: ((Float, Float) -> Void)?
    var onScroll: ((Float, Float, Int, Int) -> Void)?
    var onViewportChanged: ((CGFloat, CGSize) -> Void)?

    private var relativeCursorPoint: (Float, Float) = (0.5, 0.5)
    private var lastRemoteCursorPoint: (Float, Float)?
    private var relativePanActive = false
    private var heldDragPoint: (Float, Float)?
    private var heldDragLastLocation: CGPoint?
    private var hoverLastLocation: CGPoint?
    private var viewportScale: CGFloat = 1
    private var viewportOffset = CGSize.zero
    private var pinchActive = false
    private var viewportPanActive = false
    private let relativePointerSpeed: CGFloat = 1.35
    private let maximumViewportScale: CGFloat = 10

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        isMultipleTouchEnabled = true

        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        tap.numberOfTouchesRequired = 1
        addGestureRecognizer(tap)

        let rightTap = UITapGestureRecognizer(target: self, action: #selector(handleRightTap(_:)))
        rightTap.numberOfTouchesRequired = 2
        tap.require(toFail: rightTap)
        addGestureRecognizer(rightTap)

        let drag = UIPanGestureRecognizer(target: self, action: #selector(handleDrag(_:)))
        drag.minimumNumberOfTouches = 1
        drag.maximumNumberOfTouches = 1
        drag.delegate = self
        addGestureRecognizer(drag)

        let heldDrag = UILongPressGestureRecognizer(
            target: self,
            action: #selector(handleHeldDrag(_:))
        )
        heldDrag.minimumPressDuration = 0.35
        heldDrag.allowableMovement = 14
        heldDrag.numberOfTouchesRequired = 1
        heldDrag.delegate = self
        addGestureRecognizer(heldDrag)

        let pinch = UIPinchGestureRecognizer(target: self,
                                             action: #selector(handlePinch(_:)))
        pinch.delegate = self
        addGestureRecognizer(pinch)

        let wheel = UIPanGestureRecognizer(target: self, action: #selector(handleWheel(_:)))
        wheel.minimumNumberOfTouches = 2
        wheel.maximumNumberOfTouches = 2
        wheel.delegate = self
        addGestureRecognizer(wheel)

        let hover = UIHoverGestureRecognizer(target: self,
                                             action: #selector(handleHover(_:)))
        addGestureRecognizer(hover)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        let constrained = constrainedViewportOffset(viewportOffset,
                                                     scale: viewportScale)
        guard constrained != viewportOffset else { return }
        viewportOffset = constrained
        publishViewport()
    }

    func setViewport(scale: CGFloat, offset: CGSize) {
        let nextScale = min(max(scale, 1), maximumViewportScale)
        viewportScale = nextScale
        viewportOffset = constrainedViewportOffset(offset, scale: nextScale)
    }

    func synchronizeRemoteCursor(_ position: CGPoint?) {
        guard let position else {
            lastRemoteCursorPoint = nil
            return
        }
        let point = (
            Float(min(max(position.x, 0), 1)),
            Float(min(max(position.y, 0), 1))
        )
        lastRemoteCursorPoint = point
        applyRemoteCursorIfIdle()
    }

    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                           shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool {
        let recognizers = [gestureRecognizer, otherGestureRecognizer]
        let hasPinch = recognizers.contains { $0 is UIPinchGestureRecognizer }
        let hasTwoFingerPan = recognizers.contains { recognizer in
            guard let pan = recognizer as? UIPanGestureRecognizer else {
                return false
            }
            return pan.minimumNumberOfTouches == 2
        }
        return hasPinch && hasTwoFingerPan
    }

    @objc private func handleTap(_ recognizer: UITapGestureRecognizer) {
        let location = recognizer.location(in: self)
        guard recognizer.state == .ended,
              let point = pointerPoint(for: location) else { return }
#if DEBUG
        NSLog("CrossDesk tap location=%@ normalized=(%.5f, %.5f) " +
              "render=%@ scale=%.4f offset=%@ mode=%@",
              String(describing: location), point.0, point.1,
              renderRect().map { String(describing: $0) } ?? "nil",
              viewportScale, String(describing: viewportOffset),
              controlMode == .relative ? "relative" : "absolute")
#endif
        onMove?(point.0, point.1)
        onLeftDown?(point.0, point.1)
        onLeftUp?(point.0, point.1)
    }

    @objc private func handleRightTap(_ recognizer: UITapGestureRecognizer) {
        guard recognizer.state == .ended,
              let point = pointerPoint(for: recognizer.location(in: self)) else { return }
        onMove?(point.0, point.1)
        onRightClick?(point.0, point.1)
    }

    @objc private func handleDrag(_ recognizer: UIPanGestureRecognizer) {
        if controlMode == .relative {
            handleRelativeDrag(recognizer)
            return
        }

        switch recognizer.state {
        case .began, .changed:
            guard let point = normalized(recognizer.location(in: self)) else { return }
            onMove?(point.0, point.1)
        default:
            break
        }
    }

    @objc private func handleHeldDrag(_ recognizer: UILongPressGestureRecognizer) {
        if controlMode == .relative {
            handleRelativeHeldDrag(recognizer)
            return
        }

        let point = normalized(recognizer.location(in: self)) ?? heldDragPoint
        switch recognizer.state {
        case .began:
            guard let point else { return }
            heldDragPoint = point
            onMove?(point.0, point.1)
            onLeftDown?(point.0, point.1)
        case .changed:
            guard let point else { return }
            heldDragPoint = point
            onMove?(point.0, point.1)
        case .ended, .cancelled, .failed:
            if let point {
                onLeftUp?(point.0, point.1)
            }
            heldDragPoint = nil
        default:
            break
        }
    }

    @objc private func handlePinch(_ recognizer: UIPinchGestureRecognizer) {
        let focus = recognizer.location(in: self)
        switch recognizer.state {
        case .began:
            pinchActive = renderRect()?.contains(focus) == true
            recognizer.scale = 1
        case .changed:
            guard pinchActive else { return }
            let oldScale = viewportScale
            let newScale = min(max(oldScale * recognizer.scale, 1),
                               maximumViewportScale)
            recognizer.scale = 1
            guard abs(newScale - oldScale) > 0.0001 else { return }

            let center = CGPoint(x: bounds.midX, y: bounds.midY)
            let ratio = newScale / oldScale
            let proposedOffset = CGSize(
                width: focus.x - center.x -
                    (focus.x - center.x - viewportOffset.width) * ratio,
                height: focus.y - center.y -
                    (focus.y - center.y - viewportOffset.height) * ratio
            )
            viewportScale = newScale
            viewportOffset = constrainedViewportOffset(proposedOffset,
                                                       scale: newScale)
            publishViewport()
        case .ended, .cancelled, .failed:
            pinchActive = false
            if viewportScale <= 1.001 {
                viewportScale = 1
                viewportOffset = .zero
                publishViewport()
            }
        default:
            break
        }
    }

    @objc private func handleWheel(_ recognizer: UIPanGestureRecognizer) {
        if viewportScale > 1.001 || pinchActive {
            switch recognizer.state {
            case .began:
                viewportPanActive = renderRect()?.contains(
                    recognizer.location(in: self)
                ) == true
                recognizer.setTranslation(.zero, in: self)
            case .changed:
                if !viewportPanActive {
                    viewportPanActive = renderRect()?.contains(
                        recognizer.location(in: self)
                    ) == true
                }
                guard viewportPanActive else { return }
                let translation = recognizer.translation(in: self)
                recognizer.setTranslation(.zero, in: self)
                let proposedOffset = CGSize(
                    width: viewportOffset.width + translation.x,
                    height: viewportOffset.height + translation.y
                )
                viewportOffset = constrainedViewportOffset(proposedOffset,
                                                           scale: viewportScale)
                publishViewport()
            default:
                viewportPanActive = false
            }
            return
        }

        guard let point = pointerPoint(for: recognizer.location(in: self)) else { return }
        let translation = recognizer.translation(in: self)
        let deltaX = Int(-translation.x / 12)
        let deltaY = Int(-translation.y / 12)
        if deltaX != 0 || deltaY != 0 {
            onScroll?(point.0, point.1, deltaX, deltaY)
            recognizer.setTranslation(.zero, in: self)
        }
    }

    @objc private func handleHover(_ recognizer: UIHoverGestureRecognizer) {
        let location = recognizer.location(in: self)
        if controlMode == .relative {
            switch recognizer.state {
            case .began:
                guard renderRect()?.contains(location) == true else { return }
                hoverLastLocation = location
                onMove?(relativeCursorPoint.0, relativeCursorPoint.1)
            case .changed:
                guard let previous = hoverLastLocation else { return }
                hoverLastLocation = location
                if let point = updateRelativeCursor(
                    by: CGPoint(x: location.x - previous.x,
                                y: location.y - previous.y)
                ) {
                    onMove?(point.0, point.1)
                }
            default:
                hoverLastLocation = nil
            }
            return
        }

        guard recognizer.state == .began || recognizer.state == .changed,
              let point = normalized(location) else { return }
        onMove?(point.0, point.1)
    }

    private func handleRelativeDrag(_ recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began:
            let location = recognizer.location(in: self)
            relativePanActive = renderRect()?.contains(location) == true
            recognizer.setTranslation(.zero, in: self)
            if relativePanActive {
                onMove?(relativeCursorPoint.0, relativeCursorPoint.1)
            }
        case .changed:
            guard relativePanActive else { return }
            let translation = recognizer.translation(in: self)
            recognizer.setTranslation(.zero, in: self)
            if let point = updateRelativeCursor(by: translation) {
                onMove?(point.0, point.1)
            }
        default:
            relativePanActive = false
            applyRemoteCursorIfIdle()
        }
    }

    private func handleRelativeHeldDrag(_ recognizer: UILongPressGestureRecognizer) {
        let location = recognizer.location(in: self)
        switch recognizer.state {
        case .began:
            guard renderRect()?.contains(location) == true else { return }
            heldDragLastLocation = location
            heldDragPoint = relativeCursorPoint
            onMove?(relativeCursorPoint.0, relativeCursorPoint.1)
            onLeftDown?(relativeCursorPoint.0, relativeCursorPoint.1)
        case .changed:
            guard let previous = heldDragLastLocation else { return }
            heldDragLastLocation = location
            if let point = updateRelativeCursor(
                by: CGPoint(x: location.x - previous.x,
                            y: location.y - previous.y)
            ) {
                heldDragPoint = point
                onMove?(point.0, point.1)
            }
        case .ended, .cancelled, .failed:
            if let point = heldDragPoint {
                onLeftUp?(point.0, point.1)
            }
            heldDragPoint = nil
            heldDragLastLocation = nil
            applyRemoteCursorIfIdle()
        default:
            break
        }
    }

    private func pointerPoint(for location: CGPoint) -> (Float, Float)? {
        if controlMode == .relative {
            guard renderRect()?.contains(location) == true else { return nil }
            return relativeCursorPoint
        }
        return normalized(location)
    }

    private func updateRelativeCursor(by translation: CGPoint) -> (Float, Float)? {
        guard let renderRect = renderRect(),
              renderRect.width > 0, renderRect.height > 0 else { return nil }
        let nextX = CGFloat(relativeCursorPoint.0) +
            translation.x / renderRect.width * relativePointerSpeed
        let nextY = CGFloat(relativeCursorPoint.1) +
            translation.y / renderRect.height * relativePointerSpeed
        relativeCursorPoint = (
            Float(min(max(nextX, 0), 1)),
            Float(min(max(nextY, 0), 1))
        )
        return relativeCursorPoint
    }

    private func applyRemoteCursorIfIdle() {
        guard controlMode == .relative,
              !relativePanActive,
              heldDragPoint == nil,
              let point = lastRemoteCursorPoint else { return }
        relativeCursorPoint = point
    }

    private func normalized(_ point: CGPoint) -> (Float, Float)? {
        guard let renderRect = renderRect(), renderRect.contains(point) else { return nil }
        return (Float((point.x - renderRect.minX) / renderRect.width),
                Float((point.y - renderRect.minY) / renderRect.height))
    }

    private func renderRect() -> CGRect? {
        RemoteVideoGeometry.transformedRect(
            containerSize: bounds.size,
            videoSize: videoSize,
            scale: viewportScale,
            offset: viewportOffset
        )
    }

    private func baseRenderRect() -> CGRect? {
        RemoteVideoGeometry.aspectFitRect(containerSize: bounds.size,
                                          videoSize: videoSize)
    }

    private func constrainedViewportOffset(_ offset: CGSize,
                                           scale: CGFloat) -> CGSize {
        guard let baseRect = baseRenderRect() else { return offset }
        let maximumX = max(0, baseRect.width * (scale - 1) / 2)
        let maximumY = max(0, baseRect.height * (scale - 1) / 2)
        return CGSize(
            width: min(max(offset.width, -maximumX), maximumX),
            height: min(max(offset.height, -maximumY), maximumY)
        )
    }

    private func publishViewport() {
        onViewportChanged?(viewportScale, viewportOffset)
    }
}
