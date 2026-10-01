package cn.crossdesk.mobile;

import android.content.Context;
import android.content.Intent;
import android.view.KeyEvent;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.lang.reflect.Field;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.TimeUnit;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class ControllerTest {
    @Test public void announcementsUseIdentitySignalingAndReportNativeSendFailure()throws Exception{
        var instrumentation=InstrumentationRegistry.getInstrumentation();var reply=new java.util.concurrent.atomic.AtomicReference<JSONObject>();var failed=new java.util.concurrent.atomic.AtomicReference<String>();
        NativeSession.Listener listener=new NativeSession.Listener(){
            public void status(String value){}public void connected(){}public void ended(String reason){}public void data(JSONObject value){}public void videoSize(int width,int height){}public void clipboard(String value){}public void statistics(RemoteNetworkStatistics.Snapshot value){}
            public void announcement(JSONObject value){reply.set(value);}public void announcementFailed(String id){failed.set(id);}
        };
        NativeSession session=new NativeSession(instrumentation.getTargetContext(),"127.0.0.1",1,"","",listener);
        Field workerField=NativeSession.class.getDeclaredField("RTC");workerField.setAccessible(true);ExecutorService worker=(ExecutorService)workerField.get(null);
        var event=NativeSession.class.getDeclaredMethod("onNativeEvent",int.class,byte[].class);event.setAccessible(true);
        byte[] changed="{\"type\":\"announcements_changed\"}".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        try{
            event.invoke(session,8,changed);worker.submit(()->{}).get(5,TimeUnit.SECONDS);instrumentation.waitForIdleSync();assertNull(reply.get());
            worker.submit(()->{try{Field ready=NativeSession.class.getDeclaredField("signalReady"),identity=NativeSession.class.getDeclaredField("identity");ready.setAccessible(true);identity.setAccessible(true);ready.setBoolean(session,true);identity.set(session,"test");}catch(Exception error){throw new RuntimeException(error);}}).get(5,TimeUnit.SECONDS);
            event.invoke(session,8,changed);session.announcementRequest(new JSONObject().put("type","announcements_list").put("request_id","request-test"),"request-test");
            worker.submit(()->{}).get(5,TimeUnit.SECONDS);instrumentation.waitForIdleSync();assertEquals("announcements_changed",reply.get().getString("type"));assertEquals("request-test",failed.get());
            reply.set(null);session.close();event.invoke(session,8,changed);worker.submit(()->{}).get(5,TimeUnit.SECONDS);instrumentation.waitForIdleSync();assertNull(reply.get());
        }finally{session.close();}
    }
    @Test public void videoSettingsSendFailureReturnsMatchingRejection()throws Exception{
        var instrumentation=InstrumentationRegistry.getInstrumentation();var reply=new java.util.concurrent.atomic.AtomicReference<JSONObject>();var received=new java.util.concurrent.CountDownLatch(1);
        NativeSession.Listener listener=new NativeSession.Listener(){
            public void status(String value){}public void connected(){}public void ended(String reason){}
            public void data(JSONObject value){reply.set(value);received.countDown();}public void videoSize(int width,int height){}public void clipboard(String value){}public void statistics(RemoteNetworkStatistics.Snapshot value){}
        };
        NativeSession session=new NativeSession(instrumentation.getTargetContext(),"127.0.0.1",1,"123456789","test",listener);
        try{
            Field connected=NativeSession.class.getDeclaredField("connected");connected.setAccessible(true);connected.setBoolean(session,true);
            // JNI has no peer, so its real send failure must carry the original request to the UI.
            session.videoSettings(new RemoteVideoSettings.Values(1,30,2),0xffffffffL);
            assertTrue(received.await(5,TimeUnit.SECONDS));assertEquals(12,reply.get().getInt("type"));var settings=reply.get().getJSONObject("video_settings");
            assertEquals(1,settings.getInt("quality"));assertEquals(30,settings.getInt("frame_rate"));assertEquals(2,settings.getInt("preference"));assertEquals(0xffffffffL,settings.getLong("request_id"));assertFalse(settings.getBoolean("accepted"));
        }finally{session.close();}
    }
    @Test public void nativeReportsAndSubmittedFramesReachTheStatisticsCallback()throws Exception{
        var instrumentation=InstrumentationRegistry.getInstrumentation();var sample=new java.util.concurrent.atomic.AtomicReference<RemoteNetworkStatistics.Snapshot>();
        NativeSession.Listener listener=new NativeSession.Listener(){
            public void status(String value){}public void connected(){}public void ended(String reason){}public void data(JSONObject value){}public void videoSize(int width,int height){}public void clipboard(String value){}public void statistics(RemoteNetworkStatistics.Snapshot value){sample.set(value);}
        };
        NativeSession session=new NativeSession(instrumentation.getTargetContext(),"127.0.0.1",1,"123456789","test",listener);
        try{
            var event=NativeSession.class.getDeclaredMethod("onNativeEvent",int.class,byte[].class);event.setAccessible(true);
            event.invoke(session,7,NetworkStatisticsTest.report(1,true,40).toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
            Field worker=NativeSession.class.getDeclaredField("RTC");worker.setAccessible(true);((ExecutorService)worker.get(null)).submit(()->{}).get(5,TimeUnit.SECONDS);
            var frame=NativeSession.class.getDeclaredMethod("onNativeVideoFrame",long.class,double.class,int.class,int.class);frame.setAccessible(true);frame.invoke(session,1L,25.0,1920,1080);
            Field tick=NativeSession.class.getDeclaredField("statisticsTick");tick.setAccessible(true);instrumentation.runOnMainSync((Runnable)tick.get(session));
            assertNotNull(sample.get());assertEquals("TURN 中继",sample.get().mode());assertEquals(8000000,sample.get().report.traffic[0].inbound);assertTrue(sample.get().report.srtp);assertEquals(40,sample.get().rtt,0);assertEquals(25,sample.get().latency,0);assertEquals(1,sample.get().fps);assertEquals("1920 × 1080",sample.get().resolution());
        }finally{session.close();}
    }
    @Test public void repeatedNativeConnectedEventsNotifyAndInitializeOnlyOnce()throws Exception{
        var instrumentation=InstrumentationRegistry.getInstrumentation();
        var notifications=new java.util.concurrent.atomic.AtomicInteger();
        NativeSession.Listener listener=new NativeSession.Listener(){
            public void status(String value){}public void connected(){notifications.incrementAndGet();}public void ended(String reason){}
            public void data(JSONObject value){}public void videoSize(int width,int height){}public void clipboard(String value){}public void statistics(RemoteNetworkStatistics.Snapshot value){}
        };
        NativeSession session=new NativeSession(instrumentation.getTargetContext(),"127.0.0.1",1,"123456789","test",listener);
        Field workerField=NativeSession.class.getDeclaredField("RTC"),audioField=NativeSession.class.getDeclaredField("audio");workerField.setAccessible(true);audioField.setAccessible(true);
        ExecutorService worker=(ExecutorService)workerField.get(null);
        var event=NativeSession.class.getDeclaredMethod("onNativeEvent",int.class,byte[].class);event.setAccessible(true);
        byte[] connected="{\"status\":1}".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        try{
            // Exercise the Java native-callback path without starting a network peer.
            event.invoke(session,2,connected);Object audio=worker.submit(()->audioField.get(session)).get(5,TimeUnit.SECONDS);
            instrumentation.waitForIdleSync();assertEquals(1,notifications.get());assertNotNull(audio);
            event.invoke(session,2,connected);event.invoke(session,2,connected);
            assertSame("Duplicate readiness must not replace the audio player",audio,worker.submit(()->audioField.get(session)).get(5,TimeUnit.SECONDS));
            instrumentation.waitForIdleSync();assertEquals("One desktop connection must notify the UI only once",1,notifications.get());
        }finally{session.close();worker.submit(()->{}).get(5,TimeUnit.SECONDS);}
    }
    @Test public void keyboardSupportsDesktopVirtualKeys() {
        assertEquals(0x41, KeyMap.windowsCode(KeyEvent.KEYCODE_A));
        assertEquals(0x2E, KeyMap.windowsCode(KeyEvent.KEYCODE_FORWARD_DEL));
        assertEquals(0x11, KeyMap.windowsCode(KeyEvent.KEYCODE_CTRL_LEFT));
        assertEquals(0x5B, KeyMap.windowsCode(KeyEvent.KEYCODE_META_LEFT));
        assertEquals(0xDB | 0x100, KeyMap.ascii('{'));
        assertEquals(0x31 | 0x100, KeyMap.ascii('!'));
        assertEquals(0, KeyMap.ascii('中'));
    }

    @Test public void credentialsRemainEncryptedAndServerScoped() throws Exception {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        SecretStore store = new SecretStore(context);
        String scope = "instrumentation.invalid:9099";
        try {
            store.put(scope, "test-id@temporary-test-password");
            assertEquals("test-id@temporary-test-password", store.get(scope));
            assertEquals("", store.get("instrumentation.invalid:9100"));
            String persisted = context.getSharedPreferences("identities", Context.MODE_PRIVATE).getString(scope, "");
            assertFalse(persisted.contains("temporary-test-password"));
        } finally {
            context.getSharedPreferences("identities", Context.MODE_PRIVATE).edit().remove(scope).commit();
        }
    }

    @Test public void nativePeerCanStartAndCancelRepeatedly() throws Exception {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        Field workerField = NativeSession.class.getDeclaredField("RTC"); workerField.setAccessible(true);
        ExecutorService worker = (ExecutorService) workerField.get(null);
        Field handleField = NativeSession.class.getDeclaredField("handle"); handleField.setAccessible(true);
        NativeSession.Listener listener = new NativeSession.Listener() {
            public void status(String value) { }
            public void connected() { fail("Loopback port 1 must not establish a session"); }
            public void ended(String reason) { }
            public void data(JSONObject data) { }
            public void videoSize(int width, int height) { }
            public void clipboard(String value) { }
            public void statistics(RemoteNetworkStatistics.Snapshot value) { }
        };
        for (int i = 0; i < 3; i++) {
            NativeSession session = new NativeSession(context, "127.0.0.1", 1, "invalid", "test", listener);
            session.start();
            assertTrue(worker.submit(() -> handleField.getLong(session)).get(15, TimeUnit.SECONDS) != 0);
            session.surface(null);
            session.close(); session.close();
            assertEquals(0L, (long) worker.submit(() -> handleField.getLong(session)).get(15, TimeUnit.SECONDS));
        }
    }

    @Test public void homeActivityStartsWithoutNetworkConsent() throws Exception {
        var instrumentation = InstrumentationRegistry.getInstrumentation();
        Context context = instrumentation.getTargetContext();
        var preferences = context.getSharedPreferences("settings", Context.MODE_PRIVATE);
        boolean consent = preferences.getBoolean("networkConsent", false);
        preferences.edit().putBoolean("networkConsent", false).commit();
        MainActivity activity = null;
        try {
            Intent launch = new Intent(context, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity = (MainActivity) instrumentation.startActivitySync(launch);
            assertNotNull(activity.findViewById(android.R.id.content));
            Field connection = MainActivity.class.getDeclaredField("signaling"); connection.setAccessible(true);
            assertNull("The home page must not start a peer without consent", connection.get(activity));
        } finally {
            if (activity != null) instrumentation.runOnMainSync(activity::finish);
            preferences.edit().putBoolean("networkConsent", consent).commit();
        }
    }
}
