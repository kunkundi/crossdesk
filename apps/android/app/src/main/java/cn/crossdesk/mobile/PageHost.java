package cn.crossdesk.mobile;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ValueAnimator;
import android.content.Context;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.animation.PathInterpolator;
import android.widget.FrameLayout;

/** Retains only the current page and, during a transition, the outgoing page. */
final class PageHost extends FrameLayout {
    enum Motion { NONE, PUSH, POP, SESSION_IN, SESSION_OUT }

    private FrameLayout current, outgoing;
    private Motion motion=Motion.NONE;
    private ValueAnimator animator;
    private ViewTreeObserver pendingObserver;
    private ViewTreeObserver.OnPreDrawListener pendingStart;
    private int accessibility, focusability;
    private float distance;
    private long generation;

    PageHost(Context context){super(context);setBackgroundColor(MobileUi.BACKGROUND);}

    void show(FrameLayout page,Motion requested){
        finish();
        FrameLayout previous=current;current=page;
        if(previous==null||requested==Motion.NONE||!ValueAnimator.areAnimatorsEnabled()||!isAttachedToWindow()){
            removeAllViews();addView(page,new LayoutParams(-1,-1));page.requestApplyInsets();return;
        }
        outgoing=previous;motion=requested;
        accessibility=page.getImportantForAccessibility();
        focusability=page.getDescendantFocusability();
        previous.clearFocus();previous.setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
        page.setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
        page.setDescendantFocusability(FOCUS_BLOCK_DESCENDANTS);
        boolean underneath=motion==Motion.POP||motion==Motion.SESSION_IN;
        addView(page,underneath?0:getChildCount(),new LayoutParams(-1,-1));
        // SurfaceView pages stay opaque and unlayered. Fade the ordinary UI above them.
        if(motion==Motion.PUSH||motion==Motion.POP){
            previous.setLayerType(LAYER_TYPE_HARDWARE,null);page.setLayerType(LAYER_TYPE_HARDWARE,null);
            (motion==Motion.PUSH?page:previous).setElevation(8*getResources().getDisplayMetrics().density);
        }else (motion==Motion.SESSION_IN?previous:page).setLayerType(LAYER_TYPE_HARDWARE,null);
        distance=getWidth()*(getLayoutDirection()==LAYOUT_DIRECTION_RTL?-1:1);
        progress(0);page.requestApplyInsets();
        // Wait for content and insets, so the first drawn frame already has the right position.
        long transition=generation;
        pendingObserver=getViewTreeObserver();
        pendingStart=()->{
            if(transition!=generation||outgoing==null)return true;
            clearPendingStart();distance=getWidth()*(getLayoutDirection()==LAYOUT_DIRECTION_RTL?-1:1);progress(0);
            ValueAnimator next=ValueAnimator.ofFloat(0,1);animator=next;
            boolean session=motion==Motion.SESSION_IN||motion==Motion.SESSION_OUT;
            next.setDuration(session?200:350);
            next.setInterpolator(session?new PathInterpolator(.42f,0,.58f,1):new PathInterpolator(.25f,.1f,.25f,1));
            next.addUpdateListener(value->{if(animator==value)progress((float)value.getAnimatedValue());});
            next.addListener(new AnimatorListenerAdapter(){@Override public void onAnimationEnd(Animator value){if(animator==value)finish();}});
            next.start();return true;
        };
        pendingObserver.addOnPreDrawListener(pendingStart);
    }

    private void progress(float value){
        switch(motion){
            case PUSH -> {current.setTranslationX(distance*(1-value));outgoing.setTranslationX(-distance*.3f*value);}
            case POP -> {current.setTranslationX(-distance*.3f*(1-value));outgoing.setTranslationX(distance*value);}
            case SESSION_IN -> outgoing.setAlpha(1-value);
            case SESSION_OUT -> current.setAlpha(value);
            default -> {}
        }
    }

    void finish(){
        generation++;
        clearPendingStart();
        ValueAnimator running=animator;animator=null;if(running!=null)running.cancel();
        if(outgoing==null)return;
        reset(outgoing);removeView(outgoing);outgoing=null;
        reset(current);current.setImportantForAccessibility(accessibility);
        current.setDescendantFocusability(focusability);motion=Motion.NONE;
    }
    private void clearPendingStart(){
        if(pendingObserver!=null&&pendingObserver.isAlive()&&pendingStart!=null)pendingObserver.removeOnPreDrawListener(pendingStart);
        pendingObserver=null;pendingStart=null;
    }
    private static void reset(View page){page.setTranslationX(0);page.setAlpha(1);page.setElevation(0);page.setLayerType(LAYER_TYPE_NONE,null);}
    @Override public boolean onInterceptTouchEvent(MotionEvent event){return outgoing!=null||super.onInterceptTouchEvent(event);}
    @android.annotation.SuppressLint("ClickableViewAccessibility") // A temporary input shield; the pages own all click actions.
    @Override public boolean onTouchEvent(MotionEvent event){return outgoing!=null||super.onTouchEvent(event);}
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh){super.onSizeChanged(w,h,oldw,oldh);if(oldw>0&&oldh>0&&(w!=oldw||h!=oldh))finish();}
    @Override protected void onDetachedFromWindow(){finish();super.onDetachedFromWindow();}
}
