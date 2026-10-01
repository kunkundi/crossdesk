package cn.crossdesk.mobile;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.widget.TextView;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.*;
import org.junit.runner.RunWith;
import java.lang.reflect.*;
import java.util.Map;
import java.util.concurrent.FutureTask;
import static org.junit.Assert.*;

/** Home authorization and lifecycle behavior; uses a local closed port, no public service. */
@RunWith(AndroidJUnit4.class)
public class SignalingLifecycleTest {
    private final android.app.Instrumentation instrumentation=InstrumentationRegistry.getInstrumentation();
    private MainActivity activity;
    private SharedPreferences preferences;
    private Map<String,?> saved;
    @Before public void start(){
        Context context=instrumentation.getTargetContext();preferences=context.getSharedPreferences("settings",Context.MODE_PRIVATE);saved=preferences.getAll();
        preferences.edit().clear().putString("host","127.0.0.1").putInt("port",1).putBoolean("networkConsent",false).commit();
        activity=(MainActivity)instrumentation.startActivitySync(new Intent(context,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
    }
    @After public void finish()throws Exception{
        main(activity::finish);
        SharedPreferences.Editor editor=preferences.edit().clear();
        for(var entry:saved.entrySet()){Object value=entry.getValue();if(value instanceof String)editor.putString(entry.getKey(),(String)value);else if(value instanceof Boolean)editor.putBoolean(entry.getKey(),(Boolean)value);else if(value instanceof Integer)editor.putInt(entry.getKey(),(Integer)value);}
        editor.commit();
    }
    private void main(Runnable action)throws Exception{
        FutureTask<Void> task=new FutureTask<>(()->{action.run();return null;});instrumentation.runOnMainSync(task);task.get();instrumentation.waitForIdleSync();
    }
    private Object field(String name){try{Field f=MainActivity.class.getDeclaredField(name);f.setAccessible(true);return f.get(activity);}catch(Exception error){throw new RuntimeException(error);}}
    private void call(String name){try{Method m=MainActivity.class.getDeclaredMethod(name);m.setAccessible(true);m.invoke(activity);}catch(Exception error){throw new RuntimeException(error);}}
    @Test public void authorizedHomeConnectsAndBackgroundClosesPeer()throws Exception{
        main(()->{
            assertNull(field("signaling"));
            preferences.edit().putBoolean("networkConsent",true).apply();call("ensureSignaling");
            NativeSession first=(NativeSession)field("signaling");assertNotNull(first);
            assertNull("Home startup must not create a desktop session",field("session"));
            call("ensureSignaling");assertSame("Navigation reuses the identity connection",first,field("signaling"));
            activity.onStop();assertTrue(first.isClosed());assertNull(field("signaling"));
            activity.onStart();NativeSession resumed=(NativeSession)field("signaling");assertNotNull(resumed);assertNotSame(first,resumed);
            try{Field host=MainActivity.class.getDeclaredField("host");host.setAccessible(true);host.set(activity,"127.0.0.2");}catch(ReflectiveOperationException e){throw new RuntimeException(e);}
            call("ensureSignaling");assertTrue("Server changes retire the old identity peer",resumed.isClosed());assertNotSame(resumed,field("signaling"));
            activity.onStop();preferences.edit().putBoolean("networkConsent",false).apply();activity.onStart();
            assertNull("Revoked consent must prevent foreground reconnect",field("signaling"));
        });
    }
    @Test public void badgeTracksCallbacksAndFailureWithoutOpeningDesktop()throws Exception{
        main(()->{
            preferences.edit().putBoolean("networkConsent",true).apply();
            activity.signaling(0,"");assertTrue(((TextView)field("signalBadge")).getText().toString().contains("正在连接"));
            activity.signaling(1,"123456789");assertTrue(((TextView)field("signalBadge")).getText().toString().contains("已连接服务器"));
            activity.signaling(4,"123456789");assertTrue(((TextView)field("signalBadge")).getText().toString().contains("正在重连"));
            activity.ended("服务器 TLS 证书校验失败");assertTrue(((TextView)field("signalBadge")).getText().toString().contains("证书校验失败"));
            assertNull(field("session"));assertEquals("home",field("page"));
        });
    }
}
