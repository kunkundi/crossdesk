package cn.crossdesk.mobile;

/** Geometry shared by the video Surface, cursor and remote pointer input. */
final class RemoteViewport {
    private static final float MAXIMUM_SCALE = 10;
    private static final float MINIMUM_VISIBLE_HEIGHT_FRACTION = .2f;
    private int baseWidth, baseHeight, baseLeft, baseTop, containerHeight;
    private boolean edgePanned;
    private float scale = 1, offsetX, offsetY;

    void fit(int width, int height, int videoWidth, int videoHeight) {
        if (width <= 0 || height <= 0 || videoWidth <= 0 || videoHeight <= 0) return;
        containerHeight = height;
        double fitScale = Math.min((double) width / videoWidth, (double) height / videoHeight);
        baseWidth = Math.max(1, (int) (videoWidth * fitScale));
        baseHeight = Math.max(1, (int) (videoHeight * fitScale));
        // Match the integer positioning of Gravity.CENTER, including odd margins.
        baseLeft = (width - baseWidth) / 2;
        baseTop = (height - baseHeight) / 2;
        constrainOffset();
    }

    int baseWidth() { return baseWidth; }
    int baseHeight() { return baseHeight; }
    float scale() { return scale; }
    float offsetX() { return offsetX; }
    float offsetY() { return offsetY; }
    float width() { return baseWidth * scale; }
    float height() { return baseHeight * scale; }
    float left() { return baseLeft + baseWidth * (1 - scale) / 2 + offsetX; }
    float top() { return baseTop + baseHeight * (1 - scale) / 2 + offsetY; }
    boolean isZoomed() { return scale > 1.001f; }

    boolean contains(float x, float y) {
        return baseWidth > 0 && baseHeight > 0 &&
            x >= left() && x < left() + width() && y >= top() && y < top() + height();
    }

    float normalizedX(float x) { return clamp((x - left()) / Math.max(1, width()), 0, 1); }
    float normalizedY(float y) { return clamp((y - top()) / Math.max(1, height()), 0, 1); }

    void zoomBy(float factor, float focusX, float focusY) {
        if (baseWidth == 0 || baseHeight == 0 || !Float.isFinite(factor) || factor <= 0) return;
        float nextScale = clamp(scale * factor, 1, MAXIMUM_SCALE);
        // Returning to fit must clear cursor-edge panning too, including a
        // pinch that starts at 1x with an already shifted image.
        if (nextScale == 1) { reset(); return; }
        float ratio = nextScale / scale;
        float focusFromCenterX = focusX - (baseLeft + baseWidth / 2f);
        float focusFromCenterY = focusY - (baseTop + baseHeight / 2f);
        offsetX = focusFromCenterX - (focusFromCenterX - offsetX) * ratio;
        offsetY = focusFromCenterY - (focusFromCenterY - offsetY) * ratio;
        scale = nextScale;
        constrainOffset();
    }

    void panBy(float dx, float dy) {
        offsetX += dx;
        offsetY += dy;
        constrainOffset();
    }

    void panForPointer(float y, float dy) {
        if (baseHeight == 0 || !Float.isFinite(dy)) return;
        float overflowY = pointerOverflow(top() + y * height(), dy,
            Math.max(0, top()), Math.min(containerHeight, top() + height()));
        if (overflowY == 0) return;
        edgePanned = true;
        panBy(0, -overflowY);
    }

    // Only the outward portion of this movement pans; an offscreen cursor must
    // not make a tiny movement jump the viewport by its entire hidden distance.
    private static float pointerOverflow(float position, float delta, float low, float high) {
        if (delta > 0) return Math.min(delta, Math.max(0, position + delta - high));
        if (delta < 0) return Math.max(delta, Math.min(0, position + delta - low));
        return 0;
    }

    void finishGesture() {
        // Ordinary pointer gestures at 1x retain edge panning. Only a zoom
        // snapping back to its minimum restores the complete centered image.
        if (scale > 1 && !isZoomed()) reset();
    }

    void reset() { scale = 1; offsetX = offsetY = 0; edgePanned = false; }

    private void constrainOffset() {
        float maximumX = baseWidth * (scale - 1) / 2;
        float minimumY = -baseHeight * (scale - 1) / 2;
        float maximumY = -minimumY;
        if (edgePanned) {
            // Reserve 20% of the viewport height regardless of zoom. If the
            // whole image is shorter than that, keep its full height visible.
            float visibleHeight = Math.min(height(), containerHeight * MINIMUM_VISIBLE_HEIGHT_FRACTION);
            float centerY = baseTop + baseHeight / 2f;
            minimumY = visibleHeight - centerY - height() / 2;
            maximumY = containerHeight - visibleHeight - centerY + height() / 2;
        }
        offsetX = clamp(offsetX, -maximumX, maximumX);
        offsetY = clamp(offsetY, minimumY, maximumY);
    }

    private static float clamp(float value, float minimum, float maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }
}
