package cn.crossdesk.mobile;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ValueAnimator;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.Gravity;
import android.view.View;
import android.view.animation.PathInterpolator;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.util.function.IntConsumer;

/** A single sliding selection highlight, matching the iOS segmented settings pickers. */
@android.annotation.SuppressLint("ViewConstructor") // Programmatic control with labels and a change callback.
final class SegmentedControl extends LinearLayout {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF highlight=new RectF(), destination=new RectF();
    private final float radius;
    private final IntConsumer changed;
    private int selected;
    private ValueAnimator animator;

    SegmentedControl(MobileUi ui,String[] titles,int selection,boolean enabled,IntConsumer changed){
        super(ui.context);this.changed=changed;selected=Math.max(0,Math.min(titles.length-1,selection));radius=ui.dp(6);
        setGravity(Gravity.CENTER_VERTICAL);setPadding(ui.dp(3),ui.dp(3),ui.dp(3),ui.dp(3));setBackground(ui.background(0xFFE9E9EF,8));paint.setColor(Color.WHITE);
        for(int i=0;i<titles.length;i++){
            final int index=i;TextView choice=ui.action(titles[i],MobileUi.INK,()->select(index));
            choice.setTextSize(13);choice.setMinHeight(ui.dp(30));choice.setSelected(i==selected);choice.setEnabled(enabled);choice.setAlpha(enabled?1:.45f);
            addView(choice,new LayoutParams(0,ui.dp(30),1));
        }
    }

    private RectF selectedBounds(){View choice=getChildAt(selected);return new RectF(choice.getLeft(),choice.getTop(),choice.getRight(),choice.getBottom());}
    private void select(int index){
        if(index==selected)return;
        setSelection(index);changed.accept(index);
    }
    void setSelection(int index){
        if(index==selected)return;
        selected=index;for(int i=0;i<getChildCount();i++)getChildAt(i).setSelected(i==selected);
        cancelAnimation();destination.set(selectedBounds());
        if(!isAttachedToWindow()||!isLaidOut()||highlight.isEmpty()||!ValueAnimator.areAnimatorsEnabled()){
            highlight.set(destination);invalidate();
        }else{
            // Restart from the displayed position, including when the user reverses mid-animation.
            RectF start=new RectF(highlight),end=new RectF(destination);ValueAnimator next=ValueAnimator.ofFloat(0,1);animator=next;
            next.setDuration(200);next.setInterpolator(new PathInterpolator(.25f,.1f,.25f,1));
            next.addUpdateListener(value->{
                if(animator!=value)return;float fraction=(float)value.getAnimatedValue();
                highlight.set(start.left+(end.left-start.left)*fraction,start.top+(end.top-start.top)*fraction,start.right+(end.right-start.right)*fraction,start.bottom+(end.bottom-start.bottom)*fraction);invalidate();
            });
            next.addListener(new AnimatorListenerAdapter(){@Override public void onAnimationEnd(Animator value){if(animator==value){animator=null;highlight.set(destination);invalidate();}}});next.start();
        }
    }
    void setOptionsEnabled(boolean enabled){
        for(int i=0;i<getChildCount();i++){getChildAt(i).setEnabled(enabled);getChildAt(i).setAlpha(enabled?1:.45f);}
        if(!enabled){cancelAnimation();highlight.set(destination);invalidate();}
    }
    private void cancelAnimation(){ValueAnimator old=animator;animator=null;if(old!=null)old.cancel();}
    @Override protected void onLayout(boolean changed,int left,int top,int right,int bottom){
        super.onLayout(changed,left,top,right,bottom);RectF bounds=selectedBounds();
        // Text below the picker can reflow without interrupting an unchanged highlight path.
        if(!destination.equals(bounds)){cancelAnimation();destination.set(bounds);highlight.set(bounds);}
    }
    @Override protected void dispatchDraw(Canvas canvas){canvas.drawRoundRect(highlight,radius,radius,paint);super.dispatchDraw(canvas);}
    @Override protected void onDetachedFromWindow(){cancelAnimation();highlight.set(destination);super.onDetachedFromWindow();}
}
