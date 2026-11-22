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

    static func verticalPointerOverflow(y: CGFloat, dy: CGFloat, visibleRect: CGRect) -> CGFloat {
        guard !visibleRect.isNull, !visibleRect.isEmpty else { return 0 }
        if dy > 0 { return min(dy, max(0, y + dy - visibleRect.maxY)) }
        if dy < 0 { return max(dy, min(0, y + dy - visibleRect.minY)) }
        return 0
    }

    // Leave enough room for the cursor glyph around its hotspot.
    static let pointerInset: CGFloat = 18

    static func pointerPan(position: CGFloat, length: CGFloat) -> CGFloat {
        let margin = min(pointerInset, length / 2)
        return min(max(position, margin), length - margin) - position
    }
}

struct RemoteTouchInputView: UIViewRepresentable {
    let mouseInput: RemoteMouseInput
    let keyboardGeometry: RemoteKeyboardGeometry?
    let videoSize: CGSize
    let controlMode: MouseControlMode
    let remoteCursorPosition: CGPoint?
    let remoteCursorPositionRevision: UInt64
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
        view.mouseInput = mouseInput
        view.keyboardGeometry = keyboardGeometry
        view.videoSize = videoSize
        view.controlMode = controlMode
        view.setViewport(scale: viewportScale, offset: viewportOffset)
        view.onViewportChanged = viewportChanged
        view.onMove = move
        view.onLeftDown = leftDown
        view.onLeftUp = leftUp
        view.onRightClick = rightClick
        view.onScroll = scroll
        view.synchronizeRemoteCursor(remoteCursorPosition,
                                     revision: remoteCursorPositionRevision)
    }
}

/// Keep the initial finger spacing until a pinch is recognized, including short pinches.
private final class FingerPinchGestureRecognizer: UIGestureRecognizer {
    private let spanTolerance: CGFloat = 4
    private var fingers: [UITouch] = []
    private var referenceSpan: CGFloat = 0

    private var span: CGFloat {
        let first = fingers[0].location(in: view)
        let second = fingers[1].location(in: view)
        return max(spanTolerance, hypot(second.x - first.x, second.y - first.y))
    }

    func takeScaleFactor() -> CGFloat {
        let current = span
        let factor = current / referenceSpan
        referenceSpan = current
        return factor
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
        for touch in touches {
            if fingers.count < 2 {
                fingers.append(touch)
                if fingers.count == 2 { referenceSpan = span }
            } else {
                ignore(touch, for: event)
            }
        }
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent) {
        guard fingers.count == 2 else { return }
        if state == .possible {
            if abs(span - referenceSpan) >= spanTolerance { state = .began }
        } else if state == .began || state == .changed {
            state = .changed
        }
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent) {
        state = state == .possible ? .failed : .ended
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent) {
        state = state == .possible ? .failed : .cancelled
    }

    override func reset() {
        super.reset()
        fingers.removeAll(keepingCapacity: true)
    }
}

final class RemoteTouchSurface: UIView, UIGestureRecognizerDelegate {
    weak var keyboardGeometry: RemoteKeyboardGeometry?
    var mouseInput: RemoteMouseInput? {
        didSet {
            guard oldValue !== mouseInput else { return }
            oldValue?.relativeMotion = nil
            mouseInput?.relativeMotion = { [weak self] dx, dy in
                _ = self?.updateRelativeCursor(by: CGPoint(x: CGFloat(dx), y: CGFloat(dy)))
            }
        }
    }
    private var gestureRevisions: [ObjectIdentifier: UInt64] = [:]
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

    private var fallbackCursorPoint: (Float, Float) = (0.5, 0.5)
    private var relativeCursorPoint: (Float, Float) {
        get { mouseInput.map { ($0.x, $0.y) } ?? fallbackCursorPoint }
        set {
            fallbackCursorPoint = newValue
            mouseInput?.position(x: newValue.0, y: newValue.1)
        }
    }
    private var lastRemoteCursorPoint: (Float, Float)?
    private var lastRemoteCursorRevision: UInt64?
    private var relativePanActive = false
    private var heldDragPoint: (Float, Float)?
    private var heldDragLastLocation: CGPoint?
    private var hoverLastLocation: CGPoint?
    private var viewportScale: CGFloat = 1
    private var viewportOffset = CGSize.zero
    private var edgePanned = false
    private var needsViewportRestore = true
    private var pinchActive = false
    private var viewportPanActive = false
    private let relativePointerSpeed: CGFloat = 1.35
    private let maximumViewportScale: CGFloat = 10
    private let minimumVisibleHeightFraction: CGFloat = 0.2

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        isMultipleTouchEnabled = true
        keyboardLayoutGuide.followsUndockedKeyboard = true

        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        tap.delegate = self
        tap.numberOfTouchesRequired = 1
        addGestureRecognizer(tap)

        let rightTap = UITapGestureRecognizer(target: self, action: #selector(handleRightTap(_:)))
        rightTap.delegate = self
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

        let pinch = FingerPinchGestureRecognizer(target: self, action: #selector(handlePinch(_:)))
        pinch.allowedTouchTypes = [NSNumber(value: UITouch.TouchType.direct.rawValue)]
        pinch.delegate = self
        addGestureRecognizer(pinch)
        rightTap.require(toFail: pinch)

        // Trackpads deliver their pinch through UIKit's indirect gesture path.
        let trackpadPinch = UIPinchGestureRecognizer(target: self, action: #selector(handlePinch(_:)))
        trackpadPinch.allowedTouchTypes = [UITouch.TouchType.indirect, .indirectPointer].map { NSNumber(value: $0.rawValue) }
        trackpadPinch.delegate = self
        addGestureRecognizer(trackpadPinch)

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
        if needsViewportRestore { restoreEdgePanAllowance() }
        mouseInput?.viewportSize = renderRect()?.size ?? .zero
        let constrained = constrainedViewportOffset(viewportOffset,
                                                     scale: viewportScale)
        guard constrained != viewportOffset else { return }
        viewportOffset = constrained
        publishViewport()
    }

    func setViewport(scale: CGFloat, offset: CGSize) {
        let nextScale = min(max(scale, 1), maximumViewportScale)
        viewportScale = nextScale
        if nextScale == 1 && offset == .zero { edgePanned = false }
        viewportOffset = offset
        if needsViewportRestore { restoreEdgePanAllowance() }
        setNeedsLayout()
        mouseInput?.viewportSize = renderRect()?.size ?? .zero
    }

    private func restoreEdgePanAllowance() {
        // SwiftUI can restore state before assigning the new surface's bounds.
        guard let baseRect = baseRenderRect() else { return }
        needsViewportRestore = false
        if abs(viewportOffset.height) > baseRect.height * (viewportScale - 1) / 2 ||
            abs(viewportOffset.width) > baseRect.width * (viewportScale - 1) / 2 {
            edgePanned = true
        }
    }

    func synchronizeRemoteCursor(_ position: CGPoint?, revision: UInt64) {
        // SwiftUI refreshes this view for video frames and shape-only feedback
        // too. Consume each position update once, even when it arrives during
        // local movement, so a later refresh cannot restore an old coordinate.
        guard revision != lastRemoteCursorRevision else { return }
        lastRemoteCursorRevision = revision
        guard let position else {
            lastRemoteCursorPoint = nil
            return
        }
        let point = (
            Float(min(max(position.x, 0), 1)),
            Float(min(max(position.y, 0), 1))
        )
        lastRemoteCursorPoint = point
        guard controlMode == .relative,
              mouseInput?.hasVirtualButton != true,
              !relativePanActive,
              heldDragPoint == nil,
              hoverLastLocation == nil else { return }
        relativeCursorPoint = point
    }

    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                           shouldReceive touch: UITouch) -> Bool {
        if gestureRecognizer is UITapGestureRecognizer || gestureRecognizer is UILongPressGestureRecognizer {
            guard mouseInput?.hasVirtualButton != true else { return false }
            gestureRevisions[ObjectIdentifier(gestureRecognizer)] = mouseInput?.revision ?? 0
        }
        return true
    }

    private func allowsPointerGesture(_ recognizer: UIGestureRecognizer) -> Bool {
        mouseInput?.hasVirtualButton != true &&
            gestureRevisions[ObjectIdentifier(recognizer)] == (mouseInput?.revision ?? 0)
    }

    override func gestureRecognizerShouldBegin(_ gestureRecognizer: UIGestureRecognizer) -> Bool {
        if gestureRecognizer is UITapGestureRecognizer || gestureRecognizer is UILongPressGestureRecognizer {
            // Decline recognition before a stale long press can cancel the pan
            // used by the other finger to move a held virtual mouse button.
            return allowsPointerGesture(gestureRecognizer)
        }
        if gestureRecognizer is FingerPinchGestureRecognizer || gestureRecognizer is UIPinchGestureRecognizer {
            return mouseInput?.hasVirtualButton != true
        }
        return true
    }

    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                           shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool {
        let recognizers = [gestureRecognizer, otherGestureRecognizer]
        let hasPinch = recognizers.contains { $0 is FingerPinchGestureRecognizer || $0 is UIPinchGestureRecognizer }
        let hasTwoFingerPan = recognizers.contains { recognizer in
            guard let pan = recognizer as? UIPanGestureRecognizer else {
                return false
            }
            return pan.minimumNumberOfTouches == 2
        }
        return hasPinch && hasTwoFingerPan
    }

    @objc private func handleTap(_ recognizer: UITapGestureRecognizer) {
        guard allowsPointerGesture(recognizer) else { return }
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
        guard allowsPointerGesture(recognizer) else { return }
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
            let location = recognizer.location(in: self)
            if recognizer.state == .changed { panForAbsolutePointer(at: location) }
            guard let point = normalized(location) else { return }
            onMove?(point.0, point.1)
        default:
            break
        }
    }

    @objc private func handleHeldDrag(_ recognizer: UILongPressGestureRecognizer) {
        guard allowsPointerGesture(recognizer) else {
            // A virtual button took ownership while the surface gesture was in flight.
            heldDragPoint = nil
            heldDragLastLocation = nil
            return
        }
        if controlMode == .relative {
            handleRelativeHeldDrag(recognizer)
            return
        }

        let location = recognizer.location(in: self)
        if recognizer.state == .changed { panForAbsolutePointer(at: location) }
        let point = normalized(location) ?? heldDragPoint
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

    @objc private func handlePinch(_ recognizer: UIGestureRecognizer) {
        guard mouseInput?.hasVirtualButton != true else { pinchActive = false; return }
        let focus = recognizer.location(in: self)
        switch recognizer.state {
        case .began:
            // The full touch surface accepts pinches, including letterboxing.
            pinchActive = bounds.contains(focus) && renderRect() != nil
            fallthrough
        case .changed:
            guard pinchActive else { return }
            let factor: CGFloat
            if let pinch = recognizer as? FingerPinchGestureRecognizer {
                factor = pinch.takeScaleFactor()
            } else if let pinch = recognizer as? UIPinchGestureRecognizer {
                factor = pinch.scale
                pinch.scale = 1
            } else { return }
            let oldScale = viewportScale
            let newScale = min(max(oldScale * factor, 1),
                               maximumViewportScale)
            if newScale == 1 {
                // Pinching back to fit also ends any cursor-edge pan, even
                // when the gesture starts at 1x with an off-center image.
                viewportScale = 1
                viewportOffset = .zero
                edgePanned = false
                publishViewport()
                return
            }
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
                edgePanned = false
                publishViewport()
            }
        default:
            break
        }
    }

    @objc private func handleWheel(_ recognizer: UIPanGestureRecognizer) {
        guard mouseInput?.hasVirtualButton != true else { viewportPanActive = false; return }
        if viewportScale > 1.001 || pinchActive {
            switch recognizer.state {
            case .began:
                viewportPanActive = pinchActive || renderRect()?.contains(
                    recognizer.location(in: self)
                ) == true
                recognizer.setTranslation(.zero, in: self)
            case .changed:
                if !viewportPanActive {
                    viewportPanActive = pinchActive || renderRect()?.contains(
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
                guard canStartRelativePointerGesture(at: location) else { return }
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

        if recognizer.state == .changed { panForAbsolutePointer(at: location) }
        guard recognizer.state == .began || recognizer.state == .changed,
              let point = normalized(location) else { return }
        onMove?(point.0, point.1)
    }

    private func handleRelativeDrag(_ recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began:
            let location = recognizer.location(in: self)
            relativePanActive = canStartRelativePointerGesture(at: location)
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
        }
    }

    private func handleRelativeHeldDrag(_ recognizer: UILongPressGestureRecognizer) {
        let location = recognizer.location(in: self)
        switch recognizer.state {
        case .began:
            guard canStartRelativePointerGesture(at: location) else { return }
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
        default:
            break
        }
    }

    private func pointerPoint(for location: CGPoint) -> (Float, Float)? {
        if controlMode == .relative {
            guard canStartRelativePointerGesture(at: location) else { return nil }
            return relativeCursorPoint
        }
        return normalized(location)
    }

    private func canStartRelativePointerGesture(at location: CGPoint) -> Bool {
        // The entire touch surface acts as a trackpad, including letterboxing.
        // A valid video rect is still required to scale pointer movement.
        bounds.contains(location) && renderRect() != nil
    }

    private func updateRelativeCursor(by translation: CGPoint) -> (Float, Float)? {
        guard let renderRect = renderRect(),
              renderRect.width > 0, renderRect.height > 0 else { return nil }
        let delta = CGPoint(x: translation.x * relativePointerSpeed,
                            y: translation.y * relativePointerSpeed)
        guard delta.x.isFinite, delta.y.isFinite else { return nil }
        guard delta != .zero else { return relativeCursorPoint }
        let (x, y) = relativeCursorPoint
        let nextX = Float(min(max(CGFloat(x) + delta.x / renderRect.width, 0), 1))
        let nextY = Float(min(max(CGFloat(y) + delta.y / renderRect.height, 0), 1))
        let cursorY = renderRect.minY + CGFloat(y) * renderRect.height
        var panY = -RemoteVideoGeometry.verticalPointerOverflow(
            y: cursorY, dy: delta.y, visibleRect: renderRect.intersection(bounds))
        let cursorX = renderRect.minX + CGFloat(nextX) * renderRect.width
        let nextCursorY = renderRect.minY + CGFloat(nextY) * renderRect.height
        var panX: CGFloat = 0
        if viewportScale > 1.001 {
            panX = RemoteVideoGeometry.pointerPan(position: cursorX, length: bounds.width)
        }
        let upperPoint = CGPoint(x: cursorX + panX, y: min(cursorY, nextCursorY))
        let visibleHeight = keyboardGeometry?.visibleHeight(in: self, at: upperPoint) ?? bounds.height
        if visibleHeight > 0 && (viewportScale > 1.001 || visibleHeight < bounds.height) {
            panY += RemoteVideoGeometry.pointerPan(position: nextCursorY + panY, length: visibleHeight)
        }
        panForPointer(dx: panX, dy: panY)
        relativeCursorPoint = (nextX, nextY)
        return relativeCursorPoint
    }

    private func panForAbsolutePointer(at point: CGPoint) {
        guard let height = keyboardGeometry?.visibleHeight(in: self, at: point),
              height < bounds.height, point.y >= 0, point.y < height,
              let rect = renderRect(), rect.contains(point) else { return }
        let dy = RemoteVideoGeometry.pointerPan(position: point.y, length: height)
        // Keep the remote point under the finger or mouse as the image moves.
        panForPointer(dx: 0, dy: min(max(dy, min(0, height - rect.maxY)), max(0, -rect.minY)))
    }

    private func panForPointer(dx: CGFloat, dy: CGFloat) {
        guard dx != 0 || dy != 0 else { return }
        edgePanned = true
        let proposed = CGSize(width: viewportOffset.width + dx, height: viewportOffset.height + dy)
        let next = constrainedViewportOffset(proposed, scale: viewportScale)
        if next != viewportOffset {
            viewportOffset = next
            publishViewport()
        }
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
        let horizontalInset = edgePanned && scale > 1.001 ? RemoteVideoGeometry.pointerInset : 0
        let maximumX = max(0, baseRect.width * (scale - 1) / 2) + horizontalInset
        var minimumY = -max(0, baseRect.height * (scale - 1) / 2)
        var maximumY = -minimumY
        if edgePanned {
            // Reserve 20% of the unobscured height at any zoom; keep the entire
            // image visible when its height is below that minimum.
            let availableHeight = keyboardGeometry?.visibleHeight(in: self) ?? bounds.height
            let visibleHeight = min(baseRect.height * scale, availableHeight * minimumVisibleHeightFraction)
            let halfHeight = baseRect.height * scale / 2
            minimumY = visibleHeight - baseRect.midY - halfHeight
            maximumY = availableHeight - visibleHeight - baseRect.midY + halfHeight
        }
        return CGSize(
            width: min(max(offset.width, -maximumX), maximumX),
            height: min(max(offset.height, minimumY), maximumY)
        )
    }

    private func publishViewport() {
        onViewportChanged?(viewportScale, viewportOffset)
    }
}
