package cn.crossdesk.mobile;

import android.annotation.SuppressLint;
import android.graphics.Color;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.InsetDrawable;
import android.view.MotionEvent;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

/** A 140 × 48 dp bar. Touch ownership continues outside the original key bounds. */
@SuppressLint("ViewConstructor")
final class VirtualMouseBar extends LinearLayout {
    private final RemoteMouseInput input;
    private final MobileUi ui;
    private final TextView[] keys = new TextView[3];
    private int generation;

    VirtualMouseBar(MobileUi ui, RemoteMouseInput input) {
        super(ui.context); this.ui = ui; this.input = input;
        setOrientation(HORIZONTAL);
        setPadding(ui.dp(4), ui.dp(2), ui.dp(4), ui.dp(2));
        GradientDrawable shell = ui.background(Color.WHITE, 18);
        shell.setStroke(ui.dp(.5f), 0xFFD4DAE2);
        setBackground(shell);
        setMotionEventSplittingEnabled(true);
        keys[0] = key("鼠标左键，按住并拖动，松开结束拖拽", 0);
        keys[1] = key("鼠标滚轮，上下滑动，可滑出按键范围", 1);
        keys[2] = key("鼠标右键，按住并拖动，松开结束拖拽", 2);
        input.didCancel = this::resetTouches;
        setContentDescription("虚拟鼠标");
    }

    @SuppressLint("ClickableViewAccessibility") // Touch sends real down/up; performClick serves accessibility only.
    private TextView key(String description, int index) {
        TextView key = index == 1 ? new WheelKey(ui) : new TextView(ui.context);
        key.setGravity(android.view.Gravity.CENTER);
        key.setContentDescription(description);
        key.setClickable(true); key.setFocusable(true);
        highlight(key, index, false);
        addView(key, new LayoutParams(0, ui.dp(44), 1));
        key.setOnClickListener(v -> {
            if (index == 1) { ((WheelKey)key).advance(ui.dp(10)); input.scroll(-1); }
            else { input.virtualButton(index == 0, true); input.virtualButton(index == 0, false); }
        });
        if (index == 1) {
            key.setAccessibilityDelegate(new View.AccessibilityDelegate() {
                @Override public void onInitializeAccessibilityNodeInfo(View host, android.view.accessibility.AccessibilityNodeInfo info) {
                    super.onInitializeAccessibilityNodeInfo(host, info);
                    info.setScrollable(true);
                    info.addAction(android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD);
                    info.addAction(android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD);
                }
                @Override public boolean performAccessibilityAction(View host, int action, android.os.Bundle args) {
                    if (action == android.view.accessibility.AccessibilityNodeInfo.ACTION_SCROLL_FORWARD || action == android.view.accessibility.AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD) {
                        int direction = action == android.view.accessibility.AccessibilityNodeInfo.ACTION_SCROLL_FORWARD ? -1 : 1;
                        ((WheelKey)key).advance(-direction * ui.dp(10));
                        input.scroll(direction); return true;
                    }
                    return super.performAccessibilityAction(host, action, args);
                }
            });
        }
        key.setOnTouchListener(new View.OnTouchListener() {
            int pointer = -1, startedGeneration;
            float lastX, lastY, remainder;
            @Override public boolean onTouch(View v, MotionEvent event) {
                int action = event.getActionMasked();
                if (action == MotionEvent.ACTION_DOWN) {
                    pointer = event.getPointerId(0); startedGeneration = generation;
                    lastX = event.getX(); lastY = event.getY(); remainder = 0;
                    highlight(key, index, true);
                    if (index != 1) input.virtualButton(index == 0, true);
                    return true;
                }
                if (startedGeneration != generation || pointer == -1) return true;
                if (action == MotionEvent.ACTION_MOVE) {
                    int p = event.findPointerIndex(pointer);
                    if (p < 0) return true;
                    float dx = event.getX(p) - lastX, dy = event.getY(p) - lastY;
                    lastX = event.getX(p); lastY = event.getY(p);
                    if (index != 1) { input.drag(dx, dy); return true; }
                    ((WheelKey)key).advance(dy);
                    remainder += dy;
                    float step = ui.dp(10);
                    int ticks = (int)(remainder / step);
                    // Positive protocol wheel values scroll up; finger-down scrolls down.
                    for (int i = 0; i < Math.min(Math.abs(ticks), 32); i++) input.scroll(ticks > 0 ? -1 : 1);
                    remainder -= ticks * step;
                }
                if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL ||
                    (action == MotionEvent.ACTION_POINTER_UP && event.getPointerId(event.getActionIndex()) == pointer)) {
                    if (index != 1) input.virtualButton(index == 0, false);
                    pointer = -1; remainder = 0; highlight(key, index, false);
                }
                return true;
            }
        });
        return key;
    }
    private void highlight(TextView key, int index, boolean pressed) {
        key.setPressed(pressed);
        key.setTextColor(pressed ? Color.WHITE : 0xB8303640);
        // Narrow only the wheel's visible face; its 44 × 44 dp target remains.
        GradientDrawable face = ui.background(pressed ? MobileUi.BLUE : 0xFFEDF0F4, 6);
        float left = ui.dp(index == 0 ? 14 : 6), right = ui.dp(index == 2 ? 14 : 6);
        face.setCornerRadii(new float[]{left,left,right,right,right,right,left,left});
        int horizontal = ui.dp(index == 1 ? 13 : 1), vertical = ui.dp(index == 1 ? 7 : 2);
        key.setBackground(new InsetDrawable(face, horizontal, vertical, horizontal, vertical));
    }
    void cancelInput() {
        input.releaseAll();
    }
    private void resetTouches() {
        generation++;
        for (int i=0;i<keys.length;i++) if (keys[i] != null) highlight(keys[i], i, false);
    }
    /** Flat, uniformly colored treads follow the finger, wrapping every 5 dp. */
    private static final class WheelKey extends TextView {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path clip = new Path();
        private final float density;
        private float phase = 2;
        WheelKey(MobileUi ui) {
            super(ui.context); density = getResources().getDisplayMetrics().density;
            clip.addRoundRect(new RectF(0,0,12,24),2,2,Path.Direction.CW);
        }
        void advance(float distance) {
            phase = (phase + distance / density * .65f) % 5;
            invalidate();
        }
        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            canvas.save();
            canvas.translate((getWidth()-12*density)/2f, (getHeight()-24*density)/2f);
            canvas.scale(density,density);
            canvas.clipPath(clip);
            paint.setColor(getCurrentTextColor());
            for (int i=-1;i<=6;i++) {
                float y=i*5+phase;
                canvas.drawRect(0,y,12,y+1.5f,paint);
            }
            canvas.restore();
        }
    }
    @Override protected void onDetachedFromWindow() { cancelInput(); super.onDetachedFromWindow(); }
    @Override public void onWindowFocusChanged(boolean focused) { super.onWindowFocusChanged(focused); if (!focused) cancelInput(); }
}
