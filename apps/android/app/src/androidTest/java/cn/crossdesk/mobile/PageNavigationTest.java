package cn.crossdesk.mobile;

import android.animation.ValueAnimator;
import android.app.Instrumentation;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.widget.FrameLayout;
import android.widget.TextView;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.util.Map;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import org.junit.*;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class PageNavigationTest {
    private final Instrumentation instrumentation=InstrumentationRegistry.getInstrumentation();
    private MainActivity activity;
    private SharedPreferences settings;
    private Map<String,?> saved;
    private PageHost host;

    @Before public void setup()throws Exception{
        Context context=instrumentation.getTargetContext();settings=context.getSharedPreferences("settings",Context.MODE_PRIVATE);saved=settings.getAll();
        settings.edit().putBoolean("networkConsent",true).putString("host","navigation-test.invalid").putInt("port",1).commit();
        activity=(MainActivity)instrumentation.startActivitySync(new Intent(context,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));instrumentation.waitForIdleSync();host=(PageHost)field(activity,"pageHost");
    }
    @After public void cleanup()throws Exception{
        if(activity!=null)main(activity::finish);
        SharedPreferences.Editor editor=settings.edit().clear();for(var entry:saved.entrySet()){
            Object v=entry.getValue();if(v instanceof String)editor.putString(entry.getKey(),(String)v);else if(v instanceof Boolean)editor.putBoolean(entry.getKey(),(Boolean)v);else if(v instanceof Integer)editor.putInt(entry.getKey(),(Integer)v);
        }editor.commit();
    }
    private void main(Runnable action)throws Exception{FutureTask<Void> task=new FutureTask<>(action,null);instrumentation.runOnMainSync(task);task.get();instrumentation.waitForIdleSync();}
    private static Object field(Object object,String name){try{var f=object.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(object);}catch(Exception error){throw new RuntimeException(error);}}
    private void set(String name,Object value){try{var f=MainActivity.class.getDeclaredField(name);f.setAccessible(true);f.set(activity,value);}catch(Exception error){throw new RuntimeException(error);}}
    private void invoke(String name){try{var method=MainActivity.class.getDeclaredMethod(name);method.setAccessible(true);method.invoke(activity);}catch(Exception error){throw new RuntimeException(error);}}
    private FrameLayout page(){return (FrameLayout)field(activity,"canvas");}
    private View find(View root,String label){
        if(label.contentEquals(root.getContentDescription()==null?"":root.getContentDescription())||(root instanceof TextView&&label.contentEquals(((TextView)root).getText())))return root;
        if(root instanceof ViewGroup){var group=(ViewGroup)root;for(int i=0;i<group.getChildCount();i++){View match=find(group.getChildAt(i),label);if(match!=null)return match;}}return null;
    }
    private void click(String label){View item=find(page(),label);assertNotNull(label,item);assertTrue(item.performClick());}
    private ValueAnimator animator(){return (ValueAnimator)field(host,"animator");}
    // Stop at a real intermediate frame; no sleeps or dependence on emulator rendering speed.
    private void transition(Runnable navigation,long time)throws Exception{
        FutureTask<Void> positioned=new FutureTask<>(()->{assertNotNull("Transition must start",animator());animator().pause();animator().setCurrentPlayTime(time);return null;});
        main(()->{
            navigation.run();host.getViewTreeObserver().addOnPreDrawListener(new ViewTreeObserver.OnPreDrawListener(){
                @Override public boolean onPreDraw(){host.getViewTreeObserver().removeOnPreDrawListener(this);positioned.run();return true;}
            });
        });positioned.get(5,TimeUnit.SECONDS);
    }
    private void finishTransition()throws Exception{main(()->{assertNotNull(animator());animator().end();assertSettled();});}
    private void assertSettled(){
        assertEquals(1,host.getChildCount());assertSame(page(),host.getChildAt(0));assertNull(animator());
        assertEquals(0,page().getTranslationX(),.01f);assertEquals(1,page().getAlpha(),.01f);assertEquals(0,page().getElevation(),.01f);assertEquals(View.LAYER_TYPE_NONE,page().getLayerType());
        assertEquals(View.IMPORTANT_FOR_ACCESSIBILITY_AUTO,page().getImportantForAccessibility());assertNotEquals(ViewGroup.FOCUS_BLOCK_DESCENDANTS,page().getDescendantFocusability());
    }
    private void snapshot(String name)throws Exception{
        Bitmap image=instrumentation.getUiAutomation().takeScreenshot();assertNotNull(image);
        try(var output=new java.io.FileOutputStream(new java.io.File(activity.getExternalFilesDir(null),name+".png"))){image.compress(Bitmap.CompressFormat.PNG,100,output);}finally{image.recycle();}
    }

    @Test public void pushAndPopHaveOppositeMotionAndCleanUp()throws Exception{
        main(this::assertSettled);FrameLayout home=page();
        transition(()->{click("设置");assertEquals(host.getWidth(),page().getTranslationX(),.01f);assertEquals(0,home.getTranslationX(),.01f);},140);
        main(()->{assertEquals(350,animator().getDuration());assertEquals(2,host.getChildCount());assertSame(page(),host.getChildAt(1));assertTrue(page().getTranslationX()>0);assertTrue(home.getTranslationX()<0);});
        snapshot("navigation-push-middle");finishTransition();assertNull(home.getParent());FrameLayout settingsPage=page();
        transition(()->click("返回"),140);
        main(()->{assertSame(page(),host.getChildAt(0));assertTrue(page().getTranslationX()<0);assertTrue(settingsPage.getTranslationX()>0);});
        snapshot("navigation-pop-middle");finishTransition();assertNull(settingsPage.getParent());assertNotNull(find(page(),"远程桌面"));
    }

    @Test public void interruptedNavigationUsesNewestPageAndBackDirection()throws Exception{
        FrameLayout home=page();transition(()->click("设置"),90);FrameLayout settingsPage=page();
        transition(()->click("关于"),90);FrameLayout about=page();
        main(()->{assertNull(home.getParent());assertEquals(2,host.getChildCount());assertSame(settingsPage,host.getChildAt(0));assertTrue(about.getTranslationX()>0);});
        transition(()->invoke("back"),90);
        main(()->{assertNull(settingsPage.getParent());assertTrue(page().getTranslationX()<0);assertTrue(about.getTranslationX()>0);});finishTransition();
        transition(()->invoke("back"),90);finishTransition();assertNotNull(find(page(),"远程桌面"));
    }

    @Test public void normalPlaybackFinishesAndRestoresInteractions()throws Exception{
        transition(()->click("设置"),0);main(()->animator().resume());
        for(int i=0;i<100;i++){
            java.util.concurrent.atomic.AtomicBoolean done=new java.util.concurrent.atomic.AtomicBoolean();main(()->done.set(host.getChildCount()==1));
            if(done.get())break;Thread.sleep(30);
        }
        main(this::assertSettled);assertNotNull(find(page(),"鼠标控制"));snapshot("navigation-settings-finished");
    }

    @Test public void refreshAndBackgroundFinishWithoutExtraPages()throws Exception{
        transition(()->click("设置"),90);FrameLayout old=page();
        main(()->{invoke("settings");assertSettled();assertNull(old.getParent());});
        transition(()->click("关于"),90);
        main(()->{instrumentation.callActivityOnStop(activity);assertSettled();assertEquals("about",field(activity,"page"));});
        main(()->instrumentation.callActivityOnStart(activity));
    }

    @Test public void sessionFadeKeepsSurfaceOpaqueAndUnlayered()throws Exception{
        FrameLayout home=page();
        transition(()->{set("session",new NativeSession(activity,"navigation-test.invalid",1,"","",activity));invoke("sessionScreen");},100);
        FrameLayout session=page();
        main(()->{assertEquals(200,animator().getDuration());assertSame(session,host.getChildAt(0));assertEquals(1,session.getAlpha(),.01f);assertEquals(View.LAYER_TYPE_NONE,session.getLayerType());assertEquals(0,session.getTranslationX(),.01f);assertEquals(.5f,home.getAlpha(),.02f);});finishTransition();
        transition(()->activity.ended(""),100);
        main(()->{assertEquals(200,animator().getDuration());assertSame(session,host.getChildAt(0));assertEquals(1,session.getAlpha(),.01f);assertEquals(View.LAYER_TYPE_NONE,session.getLayerType());assertEquals(.5f,page().getAlpha(),.02f);});finishTransition();assertNull(session.getParent());
    }

    @Test public void touchIsBlockedDuringMotionAndResizeFinishesIt()throws Exception{
        transition(()->click("设置"),90);
        main(()->{
            MotionEvent event=MotionEvent.obtain(0,0,MotionEvent.ACTION_DOWN,40,100,0);
            try{assertTrue(host.onInterceptTouchEvent(event));assertTrue(host.onTouchEvent(event));}finally{event.recycle();}
            host.layout(0,0,host.getWidth()+1,host.getHeight());assertSettled();
            event=MotionEvent.obtain(0,0,MotionEvent.ACTION_DOWN,40,100,0);try{assertFalse(host.onInterceptTouchEvent(event));}finally{event.recycle();}
        });
    }

    @Test public void disabledSystemAnimationsSwapImmediately()throws Exception{
        String scale=shell("settings get global animator_duration_scale").trim();
        try{
            shell("settings put global animator_duration_scale 0");awaitAnimations(false);
            main(()->{click("设置");assertSettled();click("返回");assertSettled();});
        }finally{
            // Deleting an unset value alone leaves WindowManager's cached scale at zero.
            shell("settings put global animator_duration_scale "+(scale.equals("null")?"1":scale));awaitAnimations(scale.equals("null")||Float.parseFloat(scale)>0);
            if(scale.equals("null"))shell("settings delete global animator_duration_scale");
        }
    }
    private String shell(String command)throws Exception{try(var input=new android.os.ParcelFileDescriptor.AutoCloseInputStream(instrumentation.getUiAutomation().executeShellCommand(command))){return new String(input.readAllBytes(),java.nio.charset.StandardCharsets.UTF_8);}}
    private void awaitAnimations(boolean enabled)throws Exception{
        for(int i=0;i<100;i++){if(ValueAnimator.areAnimatorsEnabled()==enabled)return;Thread.sleep(30);}assertEquals(enabled,ValueAnimator.areAnimatorsEnabled());
    }
}
