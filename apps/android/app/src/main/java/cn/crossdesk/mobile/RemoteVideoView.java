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
    private int frameWidth = 16, frameHeight = 9;
    private float x = .5f, y = .5f, scrollY;
    private boolean dragging, twoFinger, relative;
    private float lastX,lastY;
    void setRelative(boolean value) { relative=value; }
    RemoteVideoView(Context context, NativeSession session) {
        super(context); this.session = session;
        setBackgroundColor(Color.BLACK);
        video = new SurfaceView(context); video.getHolder().addCallback(this);
        addView(video, new LayoutParams(1, 1, android.view.Gravity.CENTER));
        cursor = new Cursor(context);
        addView(cursor, new LayoutParams(1, 1, android.view.Gravity.CENTER));
        gestures = new GestureDetector(context, new GestureDetector.SimpleOnGestureListener() {
            @Override public boolean onDown(MotionEvent e) { point(e); return true; }
            @Override public boolean onSingleTapUp(MotionEvent e) {
                if (!twoFinger) { point(e); cursor.performClick(); } return true;
            }
            @Override public void onLongPress(MotionEvent e) { if (!twoFinger && !dragging) { point(e); click(3, 4); } }
            @Override public boolean onScroll(MotionEvent first, MotionEvent current, float dx, float dy) {
                if (twoFinger) return true;
                if (!relative && !dragging) { point(first); session.pointer(x, y, 1, 0); dragging = true; }
                point(current); return true;
            }
        });
        cursor.setOnGenericMotionListener((v, event) -> {
            point(event);
            if (event.getActionMasked() == MotionEvent.ACTION_SCROLL) {
                float delta = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
                if (delta != 0) session.pointer(x, y, 7, delta > 0 ? 1 : -1);
            }
            return true;
        });
        setContentDescription("远程桌面：轻触点击，拖动移动，长按右键，双指上下滑动滚动");
    }
    void videoSize(int width, int height) { frameWidth = width; frameHeight = height; fit(); }
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
        double scale = Math.min((double)getWidth()/frameWidth, (double)getHeight()/frameHeight);
        int w = Math.max(1,(int)(frameWidth*scale)), h = Math.max(1,(int)(frameHeight*scale));
        video.setLayoutParams(new LayoutParams(w,h,android.view.Gravity.CENTER));
        cursor.setLayoutParams(new LayoutParams(w,h,android.view.Gravity.CENTER));
    }
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh) { post(this::fit); }
    private void point(MotionEvent e) {
        boolean touchpad = relative && e.getToolType(0) != MotionEvent.TOOL_TYPE_MOUSE;
        float px=e.getX(),py=e.getY();
        if(touchpad){
            if(e.getActionMasked()==MotionEvent.ACTION_MOVE){
                x=Math.max(0,Math.min(1,x+(px-lastX)/Math.max(1,cursor.getWidth())));
                y=Math.max(0,Math.min(1,y+(py-lastY)/Math.max(1,cursor.getHeight())));
            }
        }else{
            x=Math.max(0,Math.min(1,px/Math.max(1,cursor.getWidth())));
            y=Math.max(0,Math.min(1,py/Math.max(1,cursor.getHeight())));
        }
        lastX=px;lastY=py;
        session.pointer(x,y,0,0); cursor.invalidate();
    }
    private void click(int down, int up) { session.pointer(x,y,down,0); session.pointer(x,y,up,0); }
    private boolean touch(MotionEvent e) {
        int action = e.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) twoFinger = false;
        if (action == MotionEvent.ACTION_POINTER_DOWN) {
            if (dragging) { session.pointer(x,y,2,0); dragging = false; }
            twoFinger = true; scrollY = e.getY();
            MotionEvent cancel = MotionEvent.obtain(e); cancel.setAction(MotionEvent.ACTION_CANCEL);
            gestures.onTouchEvent(cancel); cancel.recycle();
        }
        if (twoFinger && action == MotionEvent.ACTION_MOVE) {
            float delta = e.getY() - scrollY;
            if (Math.abs(delta) > 16 * getResources().getDisplayMetrics().density) {
                session.pointer(x,y,7,delta > 0 ? 1 : -1); scrollY = e.getY();
            }
        } else if (!twoFinger) {
            if ((e.getButtonState() & MotionEvent.BUTTON_SECONDARY) != 0 && action == MotionEvent.ACTION_DOWN) {
                point(e); click(3,4); return true;
            }
            gestures.onTouchEvent(e);
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            if (dragging) session.pointer(x,y,2,0);
            dragging = false;
        }
        return true;
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
            canvas.translate(x*getWidth(),y*getHeight());
            float density=getResources().getDisplayMetrics().density;canvas.scale(density,density);
            // Anchor the arrow tip to the same normalized coordinate sent to the desktop.
            canvas.translate(-arrowWidth*.08f,-arrowHeight*.04f);
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(1.75f);paint.setColor(Color.BLACK);canvas.drawPath(arrow,paint);
            paint.setStyle(Paint.Style.FILL);paint.setColor(Color.WHITE);canvas.drawPath(arrow,paint);
            canvas.restoreToCount(saved);
        }
    }
}
