package cn.crossdesk.mobile;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.os.Handler;
import android.os.Looper;
import android.view.PixelCopy;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.FrameLayout;

@android.annotation.SuppressLint("ViewConstructor") // Constructed only with a live session, never from XML.
final class RemoteVideoView extends FrameLayout implements SurfaceHolder.Callback {
    private final SurfaceView video;
    private final Cursor cursor;
    private final NativeSession session;
    private final GestureDetector gestures;
    private final ScaleGestureDetector scales;
    private final RemoteViewport viewport = new RemoteViewport();
    private int frameWidth = 16, frameHeight = 9;
    private float x = .5f, y = .5f, scrollY;
    private boolean dragging, twoFinger, relative, viewportGesture, viewportPanActive;
    private float lastX,lastY,twoFingerX,twoFingerY;
    void setRelative(boolean value) { relative=value; }
    RemoteVideoView(Context context, NativeSession session) {
        super(context); this.session = session;
        setBackgroundColor(Color.BLACK);
        video = new SurfaceView(context); video.getHolder().addCallback(this);
        addView(video, new LayoutParams(1, 1, android.view.Gravity.CENTER));
        cursor = new Cursor(context);
        // Keep input in container coordinates so transforms never move its hit area.
        addView(cursor, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
        gestures = new GestureDetector(context, new GestureDetector.SimpleOnGestureListener() {
            @Override public boolean onDown(MotionEvent e) { return point(e); }
            @Override public boolean onSingleTapUp(MotionEvent e) {
                if (!twoFinger && point(e)) cursor.performClick(); return true;
            }
            @Override public void onLongPress(MotionEvent e) { if (!twoFinger && !dragging && point(e)) click(3, 4); }
            @Override public boolean onScroll(MotionEvent first, MotionEvent current, float dx, float dy) {
                if (twoFinger) return true;
                if (!relative && !dragging) {
                    if (!point(first)) return true;
                    session.pointer(x, y, 1, 0); dragging = true;
                }
                point(current); return true;
            }
        });
        scales = new ScaleGestureDetector(context, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
            @Override public boolean onScaleBegin(ScaleGestureDetector detector) {
                if (!viewport.contains(detector.getFocusX(), detector.getFocusY())) return false;
                viewportGesture = true;
                viewportPanActive = true;
                return true;
            }
            @Override public boolean onScale(ScaleGestureDetector detector) {
                viewport.zoomBy(detector.getScaleFactor(), detector.getFocusX(), detector.getFocusY());
                applyViewport();
                return true;
            }
            @Override public void onScaleEnd(ScaleGestureDetector detector) {
                viewport.finishGesture();
                applyViewport();
            }
        });
        scales.setQuickScaleEnabled(false);
        scales.setStylusScaleEnabled(false);
        cursor.setOnGenericMotionListener((v, event) -> {
            if (!point(event)) return true;
            if (event.getActionMasked() == MotionEvent.ACTION_SCROLL) {
                float delta = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
                if (delta != 0) session.pointer(x, y, 7, delta > 0 ? 1 : -1);
            }
            return true;
        });
        setContentDescription("远程桌面：轻触点击，拖动移动，长按右键，双指捏合缩放，放大后双指拖动画面，原始比例下双指上下滑动滚动");
    }
    void videoSize(int width, int height) {
        if (width <= 0 || height <= 0) return;
        frameWidth = width; frameHeight = height; fit();
    }
    void resetViewport() { viewport.reset(); applyViewport(); }
    static Rect previewCrop(int width,int height){
        int w=width,h=height;
        if((long)width*RemotePreviewStore.HEIGHT>(long)height*RemotePreviewStore.WIDTH)w=Math.max(1,height*RemotePreviewStore.WIDTH/RemotePreviewStore.HEIGHT);
        else h=Math.max(1,width*RemotePreviewStore.HEIGHT/RemotePreviewStore.WIDTH);
        return new Rect((width-w)/2,(height-h)/2,(width+w)/2,(height+h)/2);
    }
    void capturePreview(java.util.function.Consumer<Bitmap> completion){
        // Copy only the decoded-video Surface, excluding cursor, controls, keyboard and black margins.
        if(!isAttachedToWindow()||!video.getHolder().getSurface().isValid()){completion.accept(null);return;}
        Bitmap image=Bitmap.createBitmap(RemotePreviewStore.WIDTH,RemotePreviewStore.HEIGHT,Bitmap.Config.ARGB_8888);
        try{
            PixelCopy.request(video.getHolder().getSurface(),previewCrop(frameWidth,frameHeight),image,result->{
                if(result==PixelCopy.SUCCESS)completion.accept(image);else{image.recycle();completion.accept(null);}
            },new Handler(Looper.getMainLooper()));
        }catch(IllegalArgumentException error){image.recycle();completion.accept(null);}
    }
    private void fit() {
        if (getWidth() == 0 || getHeight() == 0) return;
        viewport.fit(getWidth(), getHeight(), frameWidth, frameHeight);
        video.setLayoutParams(new LayoutParams(viewport.baseWidth(),viewport.baseHeight(),android.view.Gravity.CENTER));
        applyViewport();
    }
    private void applyViewport() {
        // Transform the existing Surface without enlarging its decoder buffers.
        video.setScaleX(viewport.scale()); video.setScaleY(viewport.scale());
        video.setTranslationX(viewport.offsetX()); video.setTranslationY(viewport.offsetY());
        cursor.invalidate();
    }
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh) { post(this::fit); }
    private boolean point(MotionEvent e) {
        boolean touchpad = relative && e.getToolType(0) != MotionEvent.TOOL_TYPE_MOUSE;
        float px=e.getX(),py=e.getY();
        if (!touchpad && !viewport.contains(px,py)) return false;
        if(touchpad){
            if(e.getActionMasked()==MotionEvent.ACTION_MOVE){
                x=Math.max(0,Math.min(1,x+(px-lastX)/Math.max(1,viewport.width())));
                y=Math.max(0,Math.min(1,y+(py-lastY)/Math.max(1,viewport.height())));
            }
        }else{
            x=viewport.normalizedX(px);
            y=viewport.normalizedY(py);
        }
        lastX=px;lastY=py;
        session.pointer(x,y,0,0); cursor.invalidate();
        return true;
    }
    private void click(int down, int up) { session.pointer(x,y,down,0); session.pointer(x,y,up,0); }
    private boolean touch(MotionEvent e) {
        int action = e.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            twoFinger = viewportGesture = viewportPanActive = false;
        }
        if (action == MotionEvent.ACTION_POINTER_DOWN) {
            if (dragging) { session.pointer(x,y,2,0); dragging = false; }
            twoFinger = true;
            twoFingerX = focus(e,true,-1); twoFingerY = scrollY = focus(e,false,-1);
            viewportPanActive = viewport.isZoomed() && viewport.contains(twoFingerX,twoFingerY);
            MotionEvent cancel = MotionEvent.obtain(e); cancel.setAction(MotionEvent.ACTION_CANCEL);
            gestures.onTouchEvent(cancel); cancel.recycle();
        }
        scales.onTouchEvent(e);
        if (twoFinger && action == MotionEvent.ACTION_MOVE && e.getPointerCount() >= 2) {
            float focusX = focus(e,true,-1), focusY = focus(e,false,-1);
            if (viewport.isZoomed() || viewportGesture || scales.isInProgress()) {
                viewportGesture = true;
                if (!viewportPanActive) viewportPanActive = viewport.contains(focusX,focusY);
                if (viewportPanActive) {
                    viewport.panBy(focusX-twoFingerX,focusY-twoFingerY);
                    applyViewport();
                }
            } else if (viewport.contains(focusX,focusY)) {
                float delta = focusY - scrollY;
                if (Math.abs(delta) > 16 * getResources().getDisplayMetrics().density) {
                    session.pointer(x,y,7,delta > 0 ? 1 : -1); scrollY = focusY;
                }
            }
            twoFingerX = focusX; twoFingerY = focusY;
        } else if (!twoFinger) {
            if ((e.getButtonState() & MotionEvent.BUTTON_SECONDARY) != 0 && action == MotionEvent.ACTION_DOWN) {
                if (point(e)) click(3,4); return true;
            }
            gestures.onTouchEvent(e);
        }
        if (action == MotionEvent.ACTION_POINTER_UP) {
            // Rebase when a finger lifts; never resume a click/drag until all lift.
            twoFingerX = focus(e,true,e.getActionIndex());
            twoFingerY = scrollY = focus(e,false,e.getActionIndex());
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            if (dragging) session.pointer(x,y,2,0);
            dragging = false;
            viewport.finishGesture(); applyViewport();
            viewportPanActive = false;
        }
        return true;
    }
    private static float focus(MotionEvent event, boolean horizontal, int excludedIndex) {
        float total = 0; int count = 0;
        for (int i=0;i<event.getPointerCount();i++) {
            if (i==excludedIndex) continue;
            total += horizontal ? event.getX(i) : event.getY(i); count++;
        }
        return total / Math.max(1,count);
    }
    @Override public boolean performClick() { super.performClick(); return true; }
    @Override public void surfaceCreated(SurfaceHolder holder) { session.surface(holder.getSurface()); }
    @Override public void surfaceChanged(SurfaceHolder holder,int format,int width,int height) { }
    @Override public void surfaceDestroyed(SurfaceHolder holder) { session.surface(null); }
    private final class Cursor extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path arrow = new Path();
        private final float arrowWidth = 12, arrowHeight = 15;
        Cursor(Context context) {
            super(context); setClickable(true); setContentDescription("远程桌面触控区域");
            // Match the iOS RemoteCursorArrowShape in density-independent units.
            arrow.moveTo(arrowWidth*.08f,arrowHeight*.04f);
            arrow.lineTo(arrowWidth*.08f,arrowHeight*.78f);
            arrow.lineTo(arrowWidth*.34f,arrowHeight*.60f);
            arrow.lineTo(arrowWidth*.55f,arrowHeight*.94f);
            arrow.lineTo(arrowWidth*.73f,arrowHeight*.84f);
            arrow.lineTo(arrowWidth*.52f,arrowHeight*.51f);
            arrow.lineTo(arrowWidth*.88f,arrowHeight*.49f);
            arrow.close();
        }
        // GestureDetector calls performClick only after resolving tap vs drag/long press.
        @android.annotation.SuppressLint("ClickableViewAccessibility")
        @Override public boolean onTouchEvent(MotionEvent event) { return touch(event); }
        @Override public boolean performClick() { super.performClick(); click(1, 2); return true; }
        @Override protected void onDraw(Canvas canvas) {
            int saved=canvas.save();
            canvas.translate(viewport.left()+x*viewport.width(),viewport.top()+y*viewport.height());
            float density=getResources().getDisplayMetrics().density;canvas.scale(density,density);
            // Anchor the arrow tip to the same normalized coordinate sent to the desktop.
            canvas.translate(-arrowWidth*.08f,-arrowHeight*.04f);
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(1.75f);paint.setColor(Color.BLACK);canvas.drawPath(arrow,paint);
            paint.setStyle(Paint.Style.FILL);paint.setColor(Color.WHITE);canvas.drawPath(arrow,paint);
            canvas.restoreToCount(saved);
        }
    }
}
