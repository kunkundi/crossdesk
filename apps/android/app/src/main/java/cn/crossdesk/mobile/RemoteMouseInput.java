package cn.crossdesk.mobile;

/** Shared button ownership and cursor position for the video surface and mouse bar. */
final class RemoteMouseInput {
    interface Sender { void send(float x, float y, int flag, int wheel); }
    private final Sender sender;
    private float x = .5f, y = .5f;
    private boolean gestureLeft, virtualLeft, virtualRight;
    private long revision;
    Runnable didCancel;
    float viewportWidth, viewportHeight;

    RemoteMouseInput(Sender sender) { this.sender = sender; }
    boolean hasVirtualButton() { return virtualLeft || virtualRight; }
    long revision() { return revision; }
    void move(float x, float y) {
        this.x = Math.max(0, Math.min(1, x)); this.y = Math.max(0, Math.min(1, y));
        send(0, 0);
    }
    void drag(float dx, float dy) {
        if (!hasVirtualButton() || viewportWidth <= 0 || viewportHeight <= 0) return;
        move(x + dx / viewportWidth, y + dy / viewportHeight);
    }
    void gestureLeft(boolean down) {
        if (down && hasVirtualButton()) return;
        if (gestureLeft == down) return;
        gestureLeft = down;
        send(down ? 1 : 2, 0);
    }
    void click(int down, int up) {
        if (hasVirtualButton()) return;
        send(down, 0); send(up, 0);
    }
    void virtualButton(boolean left, boolean down) {
        if ((left ? virtualLeft : virtualRight) == down) return;
        if (down) {
            gestureLeft(false);
            revision++;
        }
        if (left) virtualLeft = down; else virtualRight = down;
        send(left ? (down ? 1 : 2) : (down ? 3 : 4), 0);
    }
    void scroll(int direction) {
        if (direction != 0) send(7, direction > 0 ? 1 : -1);
    }
    void releaseAll() {
        gestureLeft(false);
        virtualButton(true, false);
        virtualButton(false, false);
        revision++;
        if (didCancel != null) didCancel.run();
    }
    private void send(int flag, int wheel) { sender.send(x, y, flag, wheel); }
}
