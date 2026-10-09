import SwiftUI
import UIKit

struct VirtualMouseBar: UIViewRepresentable {
    let input: RemoteMouseInput

    func makeUIView(context: Context) -> VirtualMouseBarView {
        VirtualMouseBarView(input: input)
    }

    func updateUIView(_ view: VirtualMouseBarView, context: Context) {}

    static func dismantleUIView(_ view: VirtualMouseBarView, coordinator: ()) {
        view.input.releaseAll()
    }
}

final class VirtualMouseBarView: UIView {
    let input: RemoteMouseInput
    private let keys: [VirtualMouseKey]

    init(input: RemoteMouseInput) {
        self.input = input
        keys = [VirtualMouseKey(kind: .left, input: input),
                VirtualMouseKey(kind: .wheel, input: input),
                VirtualMouseKey(kind: .right, input: input)]
        super.init(frame: .zero)
        backgroundColor = .white
        layer.cornerRadius = 18
        layer.borderWidth = 0.5
        layer.borderColor = UIColor(red: 212 / 255, green: 218 / 255, blue: 226 / 255, alpha: 1).cgColor
        isMultipleTouchEnabled = true
        keys.forEach(addSubview)
        input.didCancel = { [weak self] in self?.keys.forEach { $0.cancelTracking() } }
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func layoutSubviews() {
        super.layoutSubviews()
        let width = max(0, bounds.width - 8) / 3
        for (index, key) in keys.enumerated() {
            key.frame = CGRect(x: 4 + CGFloat(index) * width, y: 2,
                               width: width, height: max(0, bounds.height - 4))
        }
    }
}

private final class VirtualMouseKey: UIView {
    enum Kind { case left, wheel, right }
    private let kind: Kind
    private let input: RemoteMouseInput
    private let face = UIView()
    private let faceMask = CAShapeLayer()
    private let wheelIcon = VirtualMouseWheelView()
    private var trackedTouch: UITouch?
    private var lastPoint = CGPoint.zero
    private var remainder: CGFloat = 0

    init(kind: Kind, input: RemoteMouseInput) {
        self.kind = kind
        self.input = input
        super.init(frame: .zero)
        isMultipleTouchEnabled = false
        isAccessibilityElement = true
        accessibilityTraits = kind == .wheel ? [.adjustable] : [.button]
        accessibilityLabel = kind == .left ? "鼠标左键" : kind == .right ? "鼠标右键" : "鼠标滚轮"
        accessibilityHint = kind == .wheel ? "上下滑动，可滑出按键范围" : "按住并拖动，可滑出按键范围，松开结束拖拽"
        face.isUserInteractionEnabled = false
        face.layer.mask = faceMask
        addSubview(face)
        wheelIcon.isHidden = kind != .wheel
        wheelIcon.isUserInteractionEnabled = false
        addSubview(wheelIcon)
        highlight(false)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    override func layoutSubviews() {
        super.layoutSubviews()
        // All three controls keep a 44 × 44 pt hit area; only the wheel's face
        // narrows to 18 pt. Empty gutters separate it from the two paddles.
        face.frame = kind == .wheel
            ? CGRect(x: (bounds.width - 18) / 2, y: (bounds.height - 30) / 2, width: 18, height: 30)
            : bounds.insetBy(dx: 1, dy: 2)
        let leftRadius: CGFloat = kind == .left ? 14 : 6
        let rightRadius: CGFloat = kind == .right ? 14 : 6
        faceMask.frame = face.bounds
        faceMask.path = facePath(in: face.bounds, leftRadius: leftRadius, rightRadius: rightRadius).cgPath
        wheelIcon.frame = face.frame.insetBy(dx: 3, dy: 3)
    }

    private func facePath(in rect: CGRect, leftRadius: CGFloat, rightRadius: CGFloat) -> UIBezierPath {
        let path = UIBezierPath()
        let left = min(leftRadius, rect.height / 2, rect.width / 2)
        let right = min(rightRadius, rect.height / 2, rect.width / 2)
        path.move(to: CGPoint(x: rect.minX + left, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX - right, y: rect.minY))
        path.addArc(withCenter: CGPoint(x: rect.maxX - right, y: rect.minY + right),
                    radius: right, startAngle: -.pi / 2, endAngle: 0, clockwise: true)
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - right))
        path.addArc(withCenter: CGPoint(x: rect.maxX - right, y: rect.maxY - right),
                    radius: right, startAngle: 0, endAngle: .pi / 2, clockwise: true)
        path.addLine(to: CGPoint(x: rect.minX + left, y: rect.maxY))
        path.addArc(withCenter: CGPoint(x: rect.minX + left, y: rect.maxY - left),
                    radius: left, startAngle: .pi / 2, endAngle: .pi, clockwise: true)
        path.addLine(to: CGPoint(x: rect.minX, y: rect.minY + left))
        path.addArc(withCenter: CGPoint(x: rect.minX + left, y: rect.minY + left),
                    radius: left, startAngle: .pi, endAngle: .pi * 1.5, clockwise: true)
        path.close()
        return path
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard trackedTouch == nil, let touch = touches.first else { return }
        trackedTouch = touch
        lastPoint = touch.location(in: window)
        remainder = 0
        highlight(true)
        if kind != .wheel { input.setVirtualButton(left: kind == .left, down: true) }
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = trackedTouch, touches.contains(touch) else { return }
        let point = touch.location(in: window)
        let delta = CGPoint(x: point.x - lastPoint.x, y: point.y - lastPoint.y)
        lastPoint = point
        if kind != .wheel {
            input.drag(dx: Float(delta.x), dy: Float(delta.y))
            return
        }
        wheelIcon.advance(by: delta.y)
        remainder += delta.y
        let ticks = Int(remainder / 10)
        if ticks != 0 {
            // UIKit keeps delivering this touch outside the 44 pt key. Finger
            // down maps to negative wheel units (scroll down) on the desktop.
            for _ in 0..<min(abs(ticks), 32) { input.scroll(ticks > 0 ? -1 : 1) }
            remainder -= CGFloat(ticks) * 10
        }
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = trackedTouch, touches.contains(touch) else { return }
        cancelTracking()
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { cancelTracking() }

    func cancelTracking() {
        if trackedTouch != nil, kind != .wheel {
            input.setVirtualButton(left: kind == .left, down: false)
        }
        trackedTouch = nil
        remainder = 0
        highlight(false)
    }

    override func accessibilityActivate() -> Bool {
        if kind == .wheel { accessibilityIncrement() }
        else {
            input.setVirtualButton(left: kind == .left, down: true)
            input.setVirtualButton(left: kind == .left, down: false)
        }
        return true
    }
    override func accessibilityIncrement() {
        if kind == .wheel { wheelIcon.advance(by: 10); input.scroll(-1) }
    }
    override func accessibilityDecrement() {
        if kind == .wheel { wheelIcon.advance(by: -10); input.scroll(1) }
    }

    private func highlight(_ pressed: Bool) {
        face.backgroundColor = pressed ? UIColor(red: 46 / 255, green: 122 / 255, blue: 219 / 255, alpha: 1)
            : UIColor(red: 237 / 255, green: 240 / 255, blue: 244 / 255, alpha: 1)
        wheelIcon.strokeColor = pressed ? .white
            : UIColor(red: 48 / 255, green: 54 / 255, blue: 64 / 255, alpha: 0.72)
    }
}

/// A clipped conveyor of wheel treads: follows touch movement and stops on lift.
private final class VirtualMouseWheelView: UIView {
    var strokeColor = UIColor.black { didSet { setNeedsDisplay() } }
    private var phase: CGFloat = 2
    private let pitch: CGFloat = 5

    init() {
        super.init(frame: .zero)
        isOpaque = false
        backgroundColor = .clear
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func advance(by distance: CGFloat) {
        phase = (phase + distance * 0.65).truncatingRemainder(dividingBy: pitch)
        setNeedsDisplay()
    }

    override func draw(_ rect: CGRect) {
        guard let context = UIGraphicsGetCurrentContext() else { return }
        UIBezierPath(roundedRect: bounds, cornerRadius: 2).addClip()
        context.setFillColor(strokeColor.cgColor)
        for index in -1...6 {
            let y = CGFloat(index) * pitch + phase
            context.fill(CGRect(x: 0, y: y, width: bounds.width, height: 1.5))
        }
    }
}
