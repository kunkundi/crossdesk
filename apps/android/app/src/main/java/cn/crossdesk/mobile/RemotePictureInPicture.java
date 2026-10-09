package cn.crossdesk.mobile;

import android.app.PictureInPictureParams;
import android.graphics.Rect;
import android.os.Build;
import android.util.Rational;

/** Builds platform PiP parameters only for a live, visible desktop video. */
final class RemotePictureInPicture {
    private boolean connected, choosingDocument;
    private int width, height;

    void connected() { connected = true; }
    void videoSize(int width, int height) { this.width = width; this.height = height; }
    void choosingDocument(boolean value) { choosingDocument = value; }
    void clearVideo() { width = height = 0; }
    void disconnect() { connected = choosingDocument = false; clearVideo(); }

    boolean canEnter() { return connected && !choosingDocument && width > 0 && height > 0; }

    PictureInPictureParams parameters(Rect source) {
        PictureInPictureParams.Builder builder = new PictureInPictureParams.Builder()
                .setAspectRatio(aspectRatio(width, height));
        if (source != null && !source.isEmpty()) builder.setSourceRectHint(source);
        if (Build.VERSION.SDK_INT >= 31) {
            builder.setAutoEnterEnabled(canEnter()).setSeamlessResizeEnabled(true);
        }
        return builder.build();
    }

    static Rational aspectRatio(int width, int height) {
        if (width <= 0 || height <= 0) return new Rational(16, 9);
        // Android's standard PiP accepts only 1/2.39 through 2.39. Keep
        // ultrawide and portrait desktops valid, with letterboxing in the view.
        if ((long) width * 100 > (long) height * 239) return new Rational(239, 100);
        if ((long) height * 100 > (long) width * 239) return new Rational(100, 239);
        return new Rational(width, height);
    }
}
