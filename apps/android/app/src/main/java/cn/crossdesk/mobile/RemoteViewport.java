package cn.crossdesk.mobile;

/** Geometry shared by the video Surface, cursor and remote pointer input. */
final class RemoteViewport {
    private static final float MAXIMUM_SCALE = 10;
    private int baseWidth, baseHeight, baseLeft, baseTop;
    private float scale = 1, offsetX, offsetY;

    void fit(int width, int height, int videoWidth, int videoHeight) {
        if (width <= 0 || height <= 0 || videoWidth <= 0 || videoHeight <= 0) return;
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

    void finishGesture() {
        if (!isZoomed()) reset();
    }

    void reset() { scale = 1; offsetX = offsetY = 0; }

    private void constrainOffset() {
        // Keep the original fitted area covered, as on iOS, even with letterboxing.
        float maximumX = baseWidth * (scale - 1) / 2;
        float maximumY = baseHeight * (scale - 1) / 2;
        offsetX = clamp(offsetX, -maximumX, maximumX);
        offsetY = clamp(offsetY, -maximumY, maximumY);
    }

    private static float clamp(float value, float minimum, float maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }
}
