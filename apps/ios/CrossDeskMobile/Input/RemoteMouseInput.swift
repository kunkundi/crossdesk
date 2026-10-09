import Foundation

/// Shared by the video surface and the virtual buttons. Coordinates always
/// refer to the remote cursor, never to the floating controls' screen location.
final class RemoteMouseInput {
    var send: ((Float, Float, Int, Int) -> Void)?
    var didCancel: (() -> Void)?
    var viewportSize = CGSize(width: 0, height: 0)
    private(set) var x: Float = 0.5
    private(set) var y: Float = 0.5
    private(set) var revision: UInt64 = 0
    private var gestureLeft = false
    private var virtualLeft = false
    private var virtualRight = false
    var hasVirtualButton: Bool { virtualLeft || virtualRight }

    func position(x: Float, y: Float) {
        self.x = min(max(x, 0), 1)
        self.y = min(max(y, 0), 1)
    }

    func drag(dx: Float, dy: Float) {
        guard hasVirtualButton, viewportSize.width > 0, viewportSize.height > 0 else { return }
        position(x: x + dx / Float(viewportSize.width) * 1.35,
                 y: y + dy / Float(viewportSize.height) * 1.35)
        emit(0)
    }

    func setGestureLeft(_ down: Bool) {
        guard !(down && hasVirtualButton), gestureLeft != down else { return }
        gestureLeft = down
        emit(down ? 1 : 2)
    }

    func setVirtualButton(left: Bool, down: Bool) {
        guard (left ? virtualLeft : virtualRight) != down else { return }
        if down {
            setGestureLeft(false)
            revision &+= 1
        }
        if left { virtualLeft = down } else { virtualRight = down }
        emit(left ? (down ? 1 : 2) : (down ? 3 : 4))
    }

    func scroll(_ direction: Int) {
        guard direction != 0 else { return }
        emit(7, wheel: direction > 0 ? 1 : -1)
    }

    func releaseAll() {
        setGestureLeft(false)
        setVirtualButton(left: true, down: false)
        setVirtualButton(left: false, down: false)
        revision &+= 1
        didCancel?()
    }

    private func emit(_ flag: Int, wheel: Int = 0) { send?(x, y, flag, wheel) }
}
