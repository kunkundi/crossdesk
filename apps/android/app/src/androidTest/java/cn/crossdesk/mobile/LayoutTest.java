package cn.crossdesk.mobile;

import android.app.Dialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.graphics.Bitmap;
import android.graphics.RectF;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Switch;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.*;
import org.junit.runner.RunWith;
import java.lang.reflect.Field;
import java.util.Map;
import java.util.concurrent.FutureTask;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.TimeUnit;
import static org.junit.Assert.*;

/** Layout and navigation regressions, exercised without a signaling server. */
@RunWith(AndroidJUnit4.class)
public class LayoutTest {
    private final android.app.Instrumentation instrumentation=InstrumentationRegistry.getInstrumentation();
    private MainActivity activity;
    private SharedPreferences settings;
    private Map<String,?> saved;
    private java.io.File announcementDirectory;
    private boolean clearTestPreviews;
    private static final String SCOPE="layout-test.invalid:1", ID="123456789";
    @Before public void setup(){
        Context context=instrumentation.getTargetContext();settings=context.getSharedPreferences("settings",Context.MODE_PRIVATE);saved=settings.getAll();
        settings.edit().clear().putBoolean("networkConsent",true).putString("host","layout-test.invalid").putInt("port",1).commit();
        activity=(MainActivity)instrumentation.startActivitySync(new Intent(context,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));instrumentation.waitForIdleSync();
    }
    @After public void cleanup()throws Exception{
        if(activity!=null)main(activity::finish);new RecentConnections(instrumentation.getTargetContext()).delete(SCOPE,ID);
        if(clearTestPreviews){RemotePreviewStore store=RemotePreviewStore.get(instrumentation.getTargetContext());store.setEnabled(false);store.io.submit(()->{}).get(5,TimeUnit.SECONDS);instrumentation.waitForIdleSync();}
        if(announcementDirectory!=null)try(var paths=java.nio.file.Files.walk(announcementDirectory.toPath())){for(var path:paths.sorted(java.util.Comparator.reverseOrder()).toList())java.nio.file.Files.delete(path);}
        SharedPreferences.Editor editor=settings.edit().clear();for(Map.Entry<String,?> entry:saved.entrySet()){Object value=entry.getValue();if(value instanceof String)editor.putString(entry.getKey(),(String)value);else if(value instanceof Boolean)editor.putBoolean(entry.getKey(),(Boolean)value);else if(value instanceof Integer)editor.putInt(entry.getKey(),(Integer)value);}editor.commit();
    }
    private void main(Runnable action)throws Exception{FutureTask<Void> task=new FutureTask<>(()->{action.run();return null;});instrumentation.runOnMainSync(task);task.get();instrumentation.waitForIdleSync();}
    private View content(){try{return (View)field("canvas");}catch(Exception error){throw new RuntimeException(error);}}
    private View find(View view,String label){
        if(label.contentEquals(view.getContentDescription()==null?"":view.getContentDescription())||(view instanceof TextView&&label.contentEquals(((TextView)view).getText())))return view;
        if(view instanceof ViewGroup){ViewGroup group=(ViewGroup)view;for(int i=0;i<group.getChildCount();i++){View match=find(group.getChildAt(i),label);if(match!=null)return match;}}return null;
    }
    private void click(String label)throws Exception{main(()->{View v=find(content(),label);assertNotNull(label,v);assertTrue(label,v.performClick());});}
    private Object field(String name)throws Exception{Field f=MainActivity.class.getDeclaredField(name);f.setAccessible(true);return f.get(activity);}
    private void set(String name,Object value)throws Exception{Field f=MainActivity.class.getDeclaredField(name);f.setAccessible(true);f.set(activity,value);}
    private void localSignaling()throws Exception{main(()->{try{NativeSession old=(NativeSession)field("signaling");if(old!=null)old.close();set("signaling",new NativeSession(activity,"layout-test.invalid",1,"","",activity));set("signalingServer",SCOPE);}catch(Exception error){throw new RuntimeException(error);}});}
    private void refreshHome()throws Exception{localSignaling();main(()->{try{var method=MainActivity.class.getDeclaredMethod("home",String.class);method.setAccessible(true);method.invoke(activity,"");}catch(ReflectiveOperationException error){throw new RuntimeException(error);}});}
    private ExecutorService rtc()throws Exception{Field f=NativeSession.class.getDeclaredField("RTC");f.setAccessible(true);return (ExecutorService)f.get(null);}
    private void snapshot(String name)throws Exception{
        instrumentation.waitForIdleSync();Thread.sleep(650); // Allow system window/rotation animations to settle before visual review.
        PageHost host=(PageHost)field("pageHost");java.util.concurrent.atomic.AtomicBoolean settled=new java.util.concurrent.atomic.AtomicBoolean();
        for(int i=0;i<100;i++){main(()->settled.set(host.getChildCount()==1));if(settled.get())break;Thread.sleep(30);}assertTrue("Page transition must finish",settled.get());Thread.sleep(50);
        Bitmap bitmap=instrumentation.getUiAutomation().takeScreenshot();assertNotNull(bitmap);
        try(java.io.FileOutputStream output=new java.io.FileOutputStream(new java.io.File(instrumentation.getTargetContext().getExternalFilesDir(null),name+".png"))){bitmap.compress(Bitmap.CompressFormat.PNG,100,output);}finally{bitmap.recycle();}
    }
    @Test public void passwordIsSeparateAndSettingsReturnHome()throws Exception{
        assertNotNull(find(content(),"远程桌面"));assertNotNull(find(content(),"最近连接"));assertNull(find(content(),"访问密码"));snapshot("layout-home");
        main(()->((EditText)find(content(),"对端 ID")).setText(ID));assertEquals("123 456 789",((EditText)field("remote")).getText().toString());click("连接");
        Dialog sheet=(Dialog)field("passwordDialog");assertTrue(sheet.isShowing());View sheetRoot=sheet.getWindow().getDecorView();assertNotNull(find(sheetRoot,"访问密码"));assertNotNull(find(sheetRoot,"保存密码"));snapshot("layout-password");main(sheet::dismiss);
        assertEquals(0,new RecentConnections(activity).list(SCOPE).length());click("设置");assertNotNull(find(content(),"鼠标控制"));assertNotNull(find(content(),"隐私与授权，已授权"));snapshot("layout-settings");
        click("绝对位置");assertFalse(settings.getBoolean("relativeMouse",true));click("返回");assertNotNull(find(content(),"远程桌面"));
    }
    @Test public void aboutMatchesIosNavigationAndReadsCompleteOfflineDocuments()throws Exception{
        click("设置");click("关于");assertNotNull(find(content(),"关于 CrossDesk"));assertNull(find(content(),"使用说明"));
        assertNotNull(find(content(),activity.getPackageManager().getPackageInfo(activity.getPackageName(),0).versionName));snapshot("about-home");
        LicenseCatalog catalog=new LicenseCatalog(activity);assertEquals(29,catalog.components.size());
        click("软件许可");assertNotNull(find(content(),"CrossDesk"));assertNotNull(find(content(),"GPL-3.0-only"));snapshot("about-application");
        click("GNU GPL 第 3 版");assertLicenseDocument(LicenseCatalog.read(activity,catalog.find("crossdesk").documents.get(0).asset));snapshot("about-gpl");
        click("返回");click("开源软件权利说明");assertLicenseDocument(LicenseCatalog.read(activity,catalog.find("crossdesk").documents.get(1).asset));click("返回");click("返回");
        click("开源组件");assertNotNull(find(content(),"OpenFEC 告知"));assertNotNull(find(content(),"(c) Copyright 2009 - 2012 INRIA - All rights reserved"));snapshot("about-components");
        click("OpenFEC 许可与源码");assertNotNull(find(content(),"CeCILL-C 1.0 / BSD / CC-BY-SA-3.0"));click("LICENCE_CeCILL-C_V1-en.txt");assertLicenseDocument(LicenseCatalog.read(activity,catalog.find("openfec").documents.get(0).asset));click("返回");
        click("构建配方与项目补丁");click("MiniRTC 构建配方");assertLicenseDocument(LicenseCatalog.read(activity,"about/build/3.txt"));click("返回");click("返回");click("返回");
        click("GLib / GObject / GIO / GModule / GThread / GVDB，LGPL-2.1-or-later and embedded notices");click("版权与许可声明");
        assertLicenseDocument(LicenseCatalog.read(activity,catalog.find("glib").documents.get(8).asset));click("返回");click("返回");click("返回");
        click("源码与构建说明");assertNotNull(find(content(),"本版本源码与构建说明"));snapshot("about-source");
        java.util.concurrent.atomic.AtomicReference<Intent> opened=new java.util.concurrent.atomic.AtomicReference<>();
        android.app.Instrumentation.ActivityMonitor monitor=new android.app.Instrumentation.ActivityMonitor(){
            @Override public android.app.Instrumentation.ActivityResult onStartActivity(Intent intent){if(Intent.ACTION_VIEW.equals(intent.getAction())){opened.set(intent);return new android.app.Instrumentation.ActivityResult(android.app.Activity.RESULT_OK,null);}return null;}
        };
        instrumentation.addMonitor(monitor);try{click("查看源码");assertNotNull(opened.get());assertEquals(catalog.source.getString("sourceURL"),opened.get().getDataString());}finally{instrumentation.removeMonitor(monitor);}
        click("构建说明");assertLicenseDocument(LicenseCatalog.read(activity,"about/build/0.txt"));snapshot("about-build");click("返回");click("返回");click("返回");assertNotNull(find(content(),"鼠标控制"));
    }
    private void assertLicenseDocument(String expected)throws Exception{
        main(()->{
            android.widget.ListView list=(android.widget.ListView)((android.widget.LinearLayout)((ViewGroup)content()).getChildAt(0)).getChildAt(1);
            StringBuilder actual=new StringBuilder();for(int i=0;i<list.getAdapter().getCount();i++){String chunk=(String)list.getAdapter().getItem(i);assertTrue(chunk.length()<=4000);actual.append(chunk);}
            assertEquals("The complete license must be available offline",expected,actual.toString());list.setSelection(list.getAdapter().getCount()-1);
        });
        main(()->{
            android.widget.ListView list=(android.widget.ListView)((android.widget.LinearLayout)((ViewGroup)content()).getChildAt(0)).getChildAt(1);
            assertEquals(list.getAdapter().getCount()-1,list.getLastVisiblePosition());assertTrue(((TextView)list.getChildAt(0)).isTextSelectable());list.setSelection(0);
        });
    }
    @Test public void floatingControlsStayWithinPortraitAndLandscape()throws Exception{
        SessionControls overlay=connectedOverlay();click("展开远程控制菜单");assertNotNull(find(content(),"断开连接"));assertFalse(find(content(),"发送文件，暂不支持").isEnabled());snapshot("layout-session-menu");
        click("键盘");assertNotNull(find(content(),"收起键盘"));snapshot("layout-session-keyboard");
        main(()->activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE));
        for(int i=0;i<30&&overlay.getWidth()<overlay.getHeight();i++){Thread.sleep(100);instrumentation.waitForIdleSync();}
        assertTrue(overlay.getWidth()>overlay.getHeight());main(overlay::constrainPanels);snapshot("layout-session-landscape");
        main(()->{for(int i=0;i<overlay.getChildCount();i++){View child=overlay.getChildAt(i);if(child.getVisibility()!=View.VISIBLE)continue;assertTrue(child.getX()>=0);assertTrue(child.getY()>=0);assertTrue(child.getX()+child.getWidth()<=overlay.getWidth()+1);assertTrue(child.getY()+child.getHeight()<=overlay.getHeight()+1);}});
        assertMenuFirstFrame(overlay,()->find(overlay,"展开远程控制菜单").performClick());click("断开连接");assertTrue((Boolean)field("ready")); // Confirmation must not disconnect immediately.
    }
    private SessionControls connectedOverlay()throws Exception{
        // No peer is started. Only inject a local session object to exercise the connected UI.
        main(()->{try{var stop=MainActivity.class.getDeclaredMethod("stopSignaling");stop.setAccessible(true);stop.invoke(activity);}catch(ReflectiveOperationException e){throw new RuntimeException(e);}});
        NativeSession session=new NativeSession(activity,"127.0.0.1",1,ID,"test",activity);set("session",session);set("connectedRemote",ID);main(activity::connected);
        return (SessionControls)field("controls");
    }
    @Test public void remoteUpgradeNoticeMatchesIosAndKeepsTheSessionConnected()throws Exception{
        connectedOverlay();
        main(()->activity.data(new org.json.JSONObject()));
        assertNull(field("remoteUpdateDialog"));
        main(()->{try{activity.data(new org.json.JSONObject().put("host_info",new org.json.JSONObject()));}catch(Exception error){throw new RuntimeException(error);}});
        android.app.AlertDialog dialog=(android.app.AlertDialog)field("remoteUpdateDialog");assertNotNull(dialog);assertTrue(dialog.isShowing());
        assertNotNull(find(dialog.getWindow().getDecorView(),"升级提示"));assertNotNull(find(dialog.getWindow().getDecorView(),RemoteVersionCheck.NOTICE));
        assertEquals("知道了",dialog.getButton(android.content.DialogInterface.BUTTON_POSITIVE).getText().toString());snapshot("remote-upgrade-notice");
        main(()->dialog.getButton(android.content.DialogInterface.BUTTON_POSITIVE).performClick());assertNull(field("remoteUpdateDialog"));assertTrue((Boolean)field("ready"));
        main(()->{try{activity.data(new org.json.JSONObject().put("host_info",new org.json.JSONObject()));activity.connected();}catch(Exception error){throw new RuntimeException(error);}});
        assertNull(field("remoteUpdateDialog"));assertTrue((Boolean)field("ready"));
    }
    @Test public void remoteVersionRequestIsCancelledWhenSessionEnds()throws Exception{
        var reply=new java.util.concurrent.atomic.AtomicReference<java.util.function.Consumer<byte[]>>();var cancelled=new java.util.concurrent.atomic.AtomicBoolean();
        main(()->{try{set("remoteVersionCheck",new RemoteVersionCheck(callback->{reply.set(callback);return ()->cancelled.set(true);},()->{throw new AssertionError("Stale result must not open a dialog");}));}catch(Exception error){throw new RuntimeException(error);}});
        connectedOverlay();
        main(()->{try{activity.data(new org.json.JSONObject().put("host_info",new org.json.JSONObject().put("app_version","1.0.0")));}catch(Exception error){throw new RuntimeException(error);}});
        assertNotNull(reply.get());main(()->activity.ended(""));assertTrue(cancelled.get());
        main(()->reply.get().accept("{\"version\":\"2.0.0\"}".getBytes(java.nio.charset.StandardCharsets.UTF_8)));
        assertNull(field("remoteUpdateDialog"));assertFalse((Boolean)field("ready"));
    }
    @Test public void remoteVersionCheckRespectsNetworkConsent()throws Exception{
        main(()->{try{set("remoteVersionCheck",new RemoteVersionCheck(callback->{throw new AssertionError("No request without consent");},()->{throw new AssertionError("No notice without consent");}));}catch(Exception error){throw new RuntimeException(error);}});
        settings.edit().putBoolean("networkConsent",false).commit();connectedOverlay();
        main(()->{try{activity.data(new org.json.JSONObject().put("host_info",new org.json.JSONObject().put("app_version","1.0.0")));}catch(Exception error){throw new RuntimeException(error);}});
        assertNull(field("remoteUpdateDialog"));
    }
    @Test public void floatingMenuIsAnchoredOnItsFirstFrame()throws Exception{
        SessionControls overlay=connectedOverlay();
        assertMenuFirstFrame(overlay,()->find(overlay,"展开远程控制菜单").performClick());
        assertMenuFirstFrame(overlay,()->find(overlay,"网络状态").performClick());
        assertMenuFirstFrame(overlay,()->find(overlay,"返回控制栏").performClick());
        assertMenuFirstFrame(overlay,()->overlay.setMuted(true));
        click("关闭控制栏");
        // Reopen beside a dragged orb at the opposite, bottom edge.
        main(()->{View orb=find(overlay,"展开远程控制菜单");orb.setX(24);orb.setY(overlay.getHeight()-orb.getHeight()-24);});
        assertMenuFirstFrame(overlay,()->find(overlay,"展开远程控制菜单").performClick());
    }
    @Test public void videoSettingsFollowCapabilitiesAndRemoteAcknowledgements()throws Exception{
        SessionControls overlay=connectedOverlay();click("展开远程控制菜单");click("画面设置");
        assertNotNull(find(content(),"连接建立后可调整，需被控端支持。"));assertFalse(find(content(),"画面质量，高").isEnabled());
        main(()->{try{activity.data(new org.json.JSONObject().put("host_info",new org.json.JSONObject().put("supports_video_settings",true)));}catch(Exception error){throw new RuntimeException(error);}});
        main(((Dialog)field("remoteUpdateDialog"))::dismiss); // This fixture omits the legacy host's version.
        RemoteVideoSettings state=(RemoteVideoSettings)field("videoSettings");assertTrue(state.pending);assertTrue(find(content(),"画面质量，中").isSelected());assertTrue(find(content(),"画面采集帧率，30 fps").isSelected());assertTrue(find(content(),"画面偏好，画质优先").isSelected());
        main(()->activity.data(state.selection.status(state.requestId,true)));click("画面质量，低");long first=state.requestId;RemoteVideoSettings.Values accepted=state.selection;
        click("画面采集帧率，60 fps");assertTrue(state.pending);assertNull(find(content(),"正在应用…"));
        main(()->activity.data(accepted.status(first,true)));assertTrue(find(content(),"画面采集帧率，60 fps").isSelected());
        main(()->activity.data(state.selection.status(state.requestId,false)));assertTrue(find(content(),"画面采集帧率，30 fps").isSelected());assertTrue(find(content(),"画面质量，低").isSelected());assertNotNull(find(content(),"调整失败，请重试。"));
        click("画面质量，高");click("画面偏好，平衡");main(()->activity.data(state.selection.status(state.requestId,true)));snapshot("layout-video-settings");
        View panel=menuPanel(overlay);assertMenuFirstFrame(overlay,()->{try{activity.data(new org.json.JSONObject().put("host_info",new org.json.JSONObject().put("supports_video_settings",false)));}catch(Exception error){throw new RuntimeException(error);}});
        assertSame(panel,menuPanel(overlay));assertFalse(find(content(),"画面质量，高").isEnabled());assertFalse(state.pending);
    }
    @Test public void videoSettingsTimeoutRollsBackWithoutMovingMenu()throws Exception{
        SessionControls overlay=connectedOverlay();click("展开远程控制菜单");click("画面设置");
        main(()->{try{activity.data(new org.json.JSONObject().put("host_info",new org.json.JSONObject().put("supports_video_settings",true)));}catch(Exception error){throw new RuntimeException(error);}});
        main(((Dialog)field("remoteUpdateDialog"))::dismiss);
        RemoteVideoSettings state=(RemoteVideoSettings)field("videoSettings");main(()->activity.data(state.selection.status(state.requestId,true)));
        click("画面质量，低");RectF before=bounds(menuPanel(overlay));Thread.sleep(5300);instrumentation.waitForIdleSync();
        assertNotNull(find(content(),"调整失败，请重试。"));assertTrue(find(content(),"画面质量，中").isSelected());assertEquals(before,bounds(menuPanel(overlay)));
    }
    @Test public void networkStatisticsShowIosRowsAndRefreshWithoutRebuilding()throws Exception{
        SessionControls overlay=connectedOverlay();RemoteNetworkStatistics stats=new RemoteNetworkStatistics();
        stats.receive(new RemoteNetworkStatistics.Report(NetworkStatisticsTest.report(1,true,40)),10);
        for(int i=0;i<60;i++)stats.frame(i+1,25,1920,1080,10+i/60.0);
        main(()->activity.statistics(stats.snapshot(10.99)));click("展开远程控制菜单");click("网络状态");
        // Keep the indicator visible in both screenshots so text overlap can be reviewed.
        View ancestor=find(content(),"媒体加密");while(!(ancestor instanceof android.widget.ScrollView))ancestor=(View)ancestor.getParent();
        android.widget.ScrollView scroll=(android.widget.ScrollView)ancestor;main(()->scroll.setScrollbarFadingEnabled(false));
        assertNotNull(find(content(),"视频，接收 8.0 Mbps，发送 0 bps，丢包率 2.0%"));assertNotNull(find(content(),"音频，接收 64 Kbps，发送 0 bps，丢包率 1.0%"));assertNotNull(find(content(),"数据，接收 2 Kbps，发送 3 Kbps，丢包率 3.0%"));assertNotNull(find(content(),"合计，接收 8.1 Mbps，发送 3 Kbps，丢包率 6.0%"));assertNotNull(find(content(),"60 FPS"));snapshot("layout-network-statistics");
        main(()->scroll.fullScroll(View.FOCUS_DOWN));
        assertNotNull(find(content(),"1920 × 1080"));assertNotNull(find(content(),"25 ms"));assertNotNull(find(content(),"40 ms"));assertNotNull(find(content(),"TURN 中继"));assertNotNull(find(content(),"SRTP 已启用"));snapshot("layout-network-details");
        View panel=menuPanel(overlay);RectF position=bounds(panel);stats.receive(new RemoteNetworkStatistics.Report(NetworkStatisticsTest.report(0,false,-1)),12);
        main(()->activity.statistics(stats.snapshot(12)));assertSame(panel,menuPanel(overlay));assertEquals(position,bounds(panel));assertNotNull(find(content(),"0 FPS"));assertNotNull(find(content(),"P2P 直连"));assertNotNull(find(content(),"未启用"));
        main(()->activity.statistics(stats.snapshot(15)));assertNotNull(find(content(),"合计，接收 —，发送 —，丢包率 —"));assertNull(find(content(),"P2P 直连"));
    }
    @Test public void homeVideoPreferenceSeedsNextConnection()throws Exception{
        click("设置");click("帧率优先");assertEquals(0,settings.getInt("videoPreference",1));assertNotNull(find(content(),RemoteVideoSettings.DETAILS[0]));click("返回");refreshHome();
        main(()->((EditText)find(content(),"对端 ID")).setText(ID));click("连接");Dialog sheet=(Dialog)field("passwordDialog");View sheetRoot=sheet.getWindow().getDecorView();
        main(()->{((EditText)find(sheetRoot,"访问密码")).setText("preference-test");find(sheetRoot,"确认连接").performClick();});main(activity::connected);
        RemoteVideoSettings state=(RemoteVideoSettings)field("videoSettings");assertEquals(0,state.selection.preference);
    }
    private View menuPanel(SessionControls overlay){
        View panel=find(overlay,"关闭控制栏");assertNotNull(panel);
        while(panel.getParent()!=overlay)panel=(View)panel.getParent();return panel;
    }
    private RectF bounds(View view){return new RectF(view.getX(),view.getY(),view.getX()+view.getWidth(),view.getY()+view.getHeight());}
    private void assertMenuFirstFrame(SessionControls overlay,Runnable open)throws Exception{
        RectF first=new RectF();
        main(()->{
            open.run();
            // A frame may be laid out and drawn before the message queue runs any posted positioning.
            overlay.measure(View.MeasureSpec.makeMeasureSpec(overlay.getWidth(),View.MeasureSpec.EXACTLY),View.MeasureSpec.makeMeasureSpec(overlay.getHeight(),View.MeasureSpec.EXACTLY));
            overlay.layout(overlay.getLeft(),overlay.getTop(),overlay.getRight(),overlay.getBottom());
            View panel=menuPanel(overlay),orb=find(overlay,"收起远程控制菜单");first.set(bounds(panel));
            float density=activity.getResources().getDisplayMetrics().density,margin=Math.round(8*density),gap=Math.round(12*density);
            float x=orb.getX()+orb.getWidth()/2f>overlay.getWidth()/2f?orb.getX()-gap-panel.getWidth():orb.getX()+orb.getWidth()+gap;
            assertEquals("First frame must already be anchored horizontally",Math.max(margin,Math.min(x,overlay.getWidth()-panel.getWidth()-margin)),first.left,.5f);
            assertEquals("First frame must already be anchored vertically",Math.max(margin,Math.min(orb.getY(),overlay.getHeight()-panel.getHeight()-margin)),first.top,.5f);
            assertTrue(first.right<=overlay.getWidth()-margin+.5f);assertTrue(first.bottom<=overlay.getHeight()-margin+.5f);
        });
        main(()->assertEquals("Posted work must not move the menu after its first frame",first,bounds(menuPanel(overlay))));
    }
    @Test public void savedPasswordsNeverEnterHistoryAndDeleteWithRecord()throws Exception{
        RecentConnections history=new RecentConnections(activity);
        try{
            history.save(SCOPE,ID,"测试电脑","windows",true,"temporary-layout-test");
            assertEquals("temporary-layout-test",history.password(SCOPE,ID));assertEquals("",history.password("different.invalid:1",ID));
            assertFalse(history.list(SCOPE).toString().contains("temporary-layout-test"));history.delete(SCOPE,ID);assertEquals("",history.password(SCOPE,ID));assertEquals(0,history.list(SCOPE).length());
        }finally{history.delete(SCOPE,ID);}
    }
    @Test public void savedPasswordConnectsFromRecentAndAfterRestart()throws Exception{
        RecentConnections history=new RecentConnections(activity);history.save(SCOPE,ID,"密码测试电脑","windows",true,"saved-flow-test");refreshHome();
        click("连接 密码测试电脑");assertSavedAttempt("saved-flow-test");
        main(activity::finish);
        activity=(MainActivity)instrumentation.startActivitySync(new Intent(instrumentation.getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        refreshHome();
        main(()->((EditText)find(content(),"对端 ID")).setText(ID));click("连接");assertSavedAttempt("saved-flow-test");
        Dialog progress=(Dialog)field("progressDialog");main(()->find(progress.getWindow().getDecorView(),"取消连接").performClick());
        assertEquals("Cancellation must retain saved credentials","saved-flow-test",history.password(SCOPE,ID));
    }
    @Test public void savedPasswordSurvivesRepeatedConnectedCallbacks()throws Exception{
        refreshHome();main(()->((EditText)find(content(),"对端 ID")).setText(ID));click("连接");
        Dialog sheet=(Dialog)field("passwordDialog");View sheetRoot=sheet.getWindow().getDecorView();
        main(()->{((EditText)find(sheetRoot,"访问密码")).setText("duplicate-callback-test");rememberSwitch(sheetRoot).setChecked(true);find(sheetRoot,"确认连接").performClick();});
        RecentConnections history=new RecentConnections(activity);
        main(activity::connected);assertEquals("duplicate-callback-test",history.password(SCOPE,ID));
        Object controls=field("controls");
        main(activity::connected);
        assertEquals("Repeated success must not overwrite the saved password with the cleared pending value","duplicate-callback-test",history.password(SCOPE,ID));
        assertSame("Repeated success must not rebuild the active session UI",controls,field("controls"));
        main(()->activity.ended(""));refreshHome();click("连接 "+ID);assertSavedAttempt("duplicate-callback-test");
    }
    @Test public void emptyPasswordCannotOverwriteSavedCredential()throws Exception{
        RecentConnections history=new RecentConnections(activity);history.save(SCOPE,ID,"密码测试电脑","windows",true,"saved-flow-test");
        assertThrows(IllegalArgumentException.class,()->history.save(SCOPE,ID,"密码测试电脑","windows",true,""));
        assertEquals("saved-flow-test",history.password(SCOPE,ID));
    }
    @Test public void legacyEmptyPasswordKeepsRememberChoiceWhenRequestingRepair()throws Exception{
        new RecentConnections(activity).save(SCOPE,ID,"密码测试电脑","windows",true,"saved-flow-test");
        new SecretStore(activity).put("remote-password:"+SCOPE+":"+ID,""); // Reproduce the old duplicate-callback overwrite.
        refreshHome();click("连接 密码测试电脑");
        Dialog sheet=(Dialog)field("passwordDialog");assertNotNull(sheet);View sheetRoot=sheet.getWindow().getDecorView();
        assertEquals("",((EditText)find(sheetRoot,"访问密码")).getText().toString());
        assertNotNull("Explain why the previously remembered credential needs repair",((EditText)find(sheetRoot,"访问密码")).getError());
        assertTrue("Preserve the user's existing choice to remember this device",rememberSwitch(sheetRoot).isChecked());assertNull(field("session"));
    }
    private void assertSavedAttempt(String secret)throws Exception{
        assertNull("A saved password must bypass the password sheet",field("passwordDialog"));
        assertTrue(((Dialog)field("progressDialog")).isShowing());assertEquals(true,field("rememberPassword"));
        NativeSession session=(NativeSession)field("session");assertNotNull(session);
        rtc().submit(()->{try{Field remote=NativeSession.class.getDeclaredField("remote"),password=NativeSession.class.getDeclaredField("password");remote.setAccessible(true);password.setAccessible(true);assertEquals(ID,remote.get(session));assertEquals("The connection must receive the decrypted credential",secret,password.get(session));}catch(ReflectiveOperationException error){throw new RuntimeException(error);}}).get(5,TimeUnit.SECONDS);
    }
    @Test public void unreadableSavedPasswordFallsBackToInput()throws Exception{
        new RecentConnections(activity).save(SCOPE,ID,"密码测试电脑","windows",true,"saved-flow-test");
        // Damage this test credential only; never alter the device's Keystore key.
        activity.getSharedPreferences("identities",Context.MODE_PRIVATE).edit().putString("remote-password:"+SCOPE+":"+ID,"invalid-test-record").commit();
        refreshHome();click("连接 密码测试电脑");
        Dialog sheet=(Dialog)field("passwordDialog");assertNotNull(sheet);assertTrue(sheet.isShowing());
        assertEquals("",((EditText)find(sheet.getWindow().getDecorView(),"访问密码")).getText().toString());assertNull(field("session"));
    }
    @Test public void rejectedSavedPasswordCanBeReplacedAfterSuccessfulRetry()throws Exception{
        RecentConnections history=new RecentConnections(activity);history.save(SCOPE,ID,"密码测试电脑","windows",true,"saved-flow-test");refreshHome();
        click("连接 密码测试电脑");assertSavedAttempt("saved-flow-test");
        NativeSession session=(NativeSession)field("session");var event=NativeSession.class.getDeclaredMethod("onNativeEvent",int.class,byte[].class);event.setAccessible(true);
        event.invoke(session,2,"{\"status\":6}".getBytes(java.nio.charset.StandardCharsets.UTF_8));rtc().submit(()->{}).get(5,TimeUnit.SECONDS);main(()->{});
        Dialog sheet=(Dialog)field("passwordDialog");assertNotNull("A rejected saved password must offer correction",sheet);
        View sheetRoot=sheet.getWindow().getDecorView();EditText input=(EditText)find(sheetRoot,"访问密码");
        assertEquals("",input.getText().toString());assertNotNull(input.getError());assertTrue(rememberSwitch(sheetRoot).isChecked());assertNull(field("session"));
        localSignaling();
        main(()->{input.setText("replacement-flow-test");find(sheetRoot,"确认连接").performClick();});
        assertEquals("Failed/unconfirmed attempts must not replace stored credentials","saved-flow-test",history.password(SCOPE,ID));
        main(activity::connected);assertEquals("replacement-flow-test",history.password(SCOPE,ID));
    }
    @Test public void announcementsSupportBadgeDetailRefreshSwipeDeleteAndEmptyState()throws Exception{
        refreshHome();java.util.ArrayDeque<org.json.JSONObject> requests=new java.util.ArrayDeque<>();
        announcementDirectory=java.nio.file.Files.createTempDirectory(activity.getCacheDir().toPath(),"announcement-layout-").toFile();
        main(()->{try{
            ((AnnouncementInbox)field("announcementInbox")).close();AnnouncementInbox inbox=new AnnouncementInbox(announcementDirectory);
            inbox.sender=(message,id)->requests.add(message);inbox.changed=()->{try{var render=MainActivity.class.getDeclaredMethod("renderAnnouncements");render.setAccessible(true);render.invoke(activity);}catch(Exception e){throw new RuntimeException(e);}};
            set("announcementInbox",inbox);inbox.configure("layout-test.invalid",1,ID);inbox.setConnected(true);drainAnnouncements(inbox,requests,3);
        }catch(Exception error){throw new RuntimeException(error);}});
        AnnouncementInbox inbox=(AnnouncementInbox)field("announcementInbox");assertEquals("3",((TextView)field("announcementBadge")).getText().toString());snapshot("layout-announcement-badge");
        click("通知公告");main(()->drainAnnouncements(inbox,requests,3));assertNotNull(find(content(),"共 3 条 · 3 条未读"));snapshot("layout-announcements");
        main(()->{View title=find(content(),"欢迎使用 CrossDesk");while(!title.isClickable())title=(View)title.getParent();title.performClick();});
        assertNotNull(find(content(),"公告详情"));assertEquals(2,inbox.unread);snapshot("layout-announcement-detail");
        click("返回");main(()->drainAnnouncements(inbox,requests,3));assertNotNull(find(content(),"共 3 条 · 2 条未读"));
        main(()->{try{AnnouncementView scroll=(AnnouncementView)field("announcementView");gesture(scroll,scroll.getWidth()/2f,30,scroll.getWidth()/2f,scroll.getHeight()/2f);}catch(Exception e){throw new RuntimeException(e);}});
        assertFalse("Pull to refresh must request the catalog",requests.isEmpty());main(()->drainAnnouncements(inbox,requests,3));
        main(()->{View title=find(content(),"欢迎使用 CrossDesk");while(!title.isClickable())title=(View)title.getParent();View swipe=(View)title.getParent();gesture(swipe,swipe.getWidth()-30,swipe.getHeight()/2f,30,swipe.getHeight()/2f);});snapshot("layout-announcement-swipe");
        main(()->{View delete=find(content(),"删除");assertNotNull(delete);delete.performClick();});
        var dialogField=AnnouncementView.class.getDeclaredField("deleteDialog");dialogField.setAccessible(true);android.app.AlertDialog dialog=(android.app.AlertDialog)dialogField.get(field("announcementView"));assertTrue(dialog.isShowing());
        main(()->dialog.getButton(android.content.DialogInterface.BUTTON_POSITIVE).performClick());assertEquals(2,inbox.total);assertEquals(2,inbox.unread);assertNull(find(content(),"欢迎使用 CrossDesk"));
        click("刷新公告");main(()->drainAnnouncements(inbox,requests,0));assertNotNull(find(content(),"暂无公告"));snapshot("layout-announcement-empty");
        click("刷新公告");main(()->inbox.failed(requests.remove().optString("request_id")));assertNotNull(find(content(),"公告加载失败，请重试。"));click("重试");main(()->drainAnnouncements(inbox,requests,0));assertFalse(inbox.failed);
        click("返回");assertEquals(View.GONE,((View)field("announcementBadge")).getVisibility());
    }
    private void drainAnnouncements(AnnouncementInbox inbox,java.util.ArrayDeque<org.json.JSONObject> requests,int count){
        try{int limit=10;while(!requests.isEmpty()){assertTrue(limit-->0);inbox.receive(AnnouncementInboxTest.answer(requests.remove(),count,count==0?2:1));}}catch(Exception error){throw new RuntimeException(error);}
    }
    private void gesture(View view,float fromX,float fromY,float toX,float toY){
        long now=android.os.SystemClock.uptimeMillis();
        for(int i=0;i<4;i++){int action=i==0?android.view.MotionEvent.ACTION_DOWN:i==3?android.view.MotionEvent.ACTION_UP:android.view.MotionEvent.ACTION_MOVE;
            android.view.MotionEvent event=android.view.MotionEvent.obtain(now,now+i*50,action,fromX+(toX-fromX)*i/3,fromY+(toY-fromY)*i/3,0);view.dispatchTouchEvent(event);event.recycle();}
    }
    @Test public void previewOptionRequiresConfirmationAndClearingPreservesHistoryAndPassword()throws Exception{
        clearTestPreviews=true;refreshHome();awaitPreviews();RemotePreviewStore store=RemotePreviewStore.get(activity);assertFalse(store.enabled());click("设置");
        togglePreviews();assertFalse(store.enabled());main(()->previewDialogUnchecked().getButton(android.content.DialogInterface.BUTTON_NEGATIVE).performClick());assertFalse(store.enabled());
        togglePreviews();main(()->previewDialogUnchecked().getButton(android.content.DialogInterface.BUTTON_POSITIVE).performClick());awaitPreviews();assertTrue(store.enabled());assertNull(field("previewCapture"));
        main(()->find(content(),"保存远程画面预览").requestRectangleOnScreen(new android.graphics.Rect(0,0,100,400),true));snapshot("layout-preview-settings");
        RecentConnections history=new RecentConnections(activity);history.save(SCOPE,ID,"预览测试电脑","windows",true,"preview-test-password");
        store.save(store.begin(SCOPE,ID),RemotePreviewStoreTest.solid(android.graphics.Color.GREEN));awaitPreviews();click("返回");awaitPreviews();assertPreviewCard();snapshot("layout-preview-card");
        click("设置");click("清除预览图");main(()->previewDialogUnchecked().getButton(android.content.DialogInterface.BUTTON_POSITIVE).performClick());awaitPreviews();
        assertTrue(store.enabled());assertEquals("preview-test-password",history.password(SCOPE,ID));assertTrue(history.contains(SCOPE,ID));assertFalse(store.directory.exists());
        store.save(store.begin(SCOPE,ID),RemotePreviewStoreTest.solid(android.graphics.Color.BLUE));awaitPreviews();togglePreviews();awaitPreviews();assertFalse(store.enabled());assertFalse(store.directory.exists());
        togglePreviews();main(()->previewDialogUnchecked().getButton(android.content.DialogInterface.BUTTON_POSITIVE).performClick());awaitPreviews();
        store.save(store.begin(SCOPE,ID),RemotePreviewStoreTest.solid(android.graphics.Color.RED));awaitPreviews();click("隐私与授权，已授权");click("撤回联网授权");
        var nodes=instrumentation.getUiAutomation().getRootInActiveWindow().findAccessibilityNodeInfosByText("撤回");boolean confirmed=false;
        for(var node:nodes)if("撤回".contentEquals(node.getText()==null?"":node.getText())&&node.isClickable()){confirmed=node.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK);break;}
        assertTrue(confirmed);instrumentation.waitForIdleSync();awaitPreviews();assertFalse(store.enabled());assertFalse(settings.getBoolean(RemotePreviewStore.OPTION,true));assertFalse(store.directory.exists());assertTrue(history.contains(SCOPE,ID));assertEquals("preview-test-password",history.password(SCOPE,ID));
    }
    @Test public void previewCopiesOnlyVideoOncePerConnectionAndSurvivesRestart()throws Exception{
        clearTestPreviews=true;refreshHome();awaitPreviews();RemotePreviewStore store=RemotePreviewStore.get(activity);store.setEnabled(true);awaitPreviews();
        main(()->{try{var begin=MainActivity.class.getDeclaredMethod("begin",String.class,String.class);begin.setAccessible(true);begin.invoke(activity,ID,"");}catch(Exception error){throw new RuntimeException(error);}});main(activity::connected);
        RemoteVideoView video=(RemoteVideoView)field("video");RemotePreviewStore.Capture capture=(RemotePreviewStore.Capture)field("previewCapture");assertNotNull(capture);
        var surfaceField=RemoteVideoView.class.getDeclaredField("video");surfaceField.setAccessible(true);android.view.SurfaceView surface=(android.view.SurfaceView)surfaceField.get(video);
        main(()->{video.videoSize(800,600);surface.getHolder().setFixedSize(800,600);});
        for(int i=0;i<30&&surface.getHolder().getSurfaceFrame().height()!=600;i++){Thread.sleep(50);instrumentation.waitForIdleSync();}
        assertEquals(600,surface.getHolder().getSurfaceFrame().height());
        main(()->{
            android.graphics.Canvas canvas=surface.getHolder().lockCanvas();assertNotNull(canvas);android.graphics.Paint paint=new android.graphics.Paint();canvas.drawColor(android.graphics.Color.YELLOW);
            paint.setColor(android.graphics.Color.GREEN);canvas.drawRect(0,75,800,525,paint);paint.setColor(android.graphics.Color.RED);canvas.drawRect(0,75,200,525,paint);paint.setColor(android.graphics.Color.BLUE);canvas.drawRect(600,75,800,525,paint);surface.getHolder().unlockCanvasAndPost(canvas);
            View overlay=new View(activity);overlay.setBackgroundColor(android.graphics.Color.MAGENTA);video.addView(overlay,new android.widget.FrameLayout.LayoutParams(120,120,android.view.Gravity.CENTER));
        });
        RemoteNetworkStatistics.Snapshot sample=new RemoteNetworkStatistics.Snapshot(null,1,800,600,10,3);main(()->activity.statistics(sample));
        java.io.File file=new java.io.File(store.directory,capture.key+".jpg");for(int i=0;i<60&&!file.exists();i++){Thread.sleep(50);instrumentation.waitForIdleSync();}awaitPreviews();assertTrue("Rendered frame should be saved",file.isFile());
        Bitmap image=android.graphics.BitmapFactory.decodeFile(file.getAbsolutePath());assertEquals(640,image.getWidth());assertEquals(360,image.getHeight());
        assertTrue(android.graphics.Color.green(image.getPixel(320,180))>240);assertTrue(android.graphics.Color.red(image.getPixel(320,180))<20);assertTrue(android.graphics.Color.red(image.getPixel(320,10))<20);assertTrue(android.graphics.Color.red(image.getPixel(10,180))>240);assertTrue(android.graphics.Color.blue(image.getPixel(630,180))>240);image.recycle();
        byte[] first=java.nio.file.Files.readAllBytes(file.toPath());main(()->{android.graphics.Canvas canvas=surface.getHolder().lockCanvas();canvas.drawColor(android.graphics.Color.BLUE);surface.getHolder().unlockCanvasAndPost(canvas);activity.statistics(sample);});awaitPreviews();assertArrayEquals(first,java.nio.file.Files.readAllBytes(file.toPath()));
        main(()->activity.ended(""));awaitPreviews();assertPreviewCard();snapshot("layout-preview-captured");
        main(activity::finish);activity=(MainActivity)instrumentation.startActivitySync(new Intent(instrumentation.getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));awaitPreviews();assertPreviewCard();
        new RecentConnections(activity).delete(SCOPE,ID);awaitPreviews();assertFalse("Deleting history also deletes its preview",file.exists());
    }
    private void togglePreviews()throws Exception{main(()->{try{Switch toggle=(Switch)field("previewSwitch");assertTrue(toggle.isEnabled());toggle.performClick();}catch(Exception error){throw new RuntimeException(error);}});}
    private android.app.AlertDialog previewDialog()throws Exception{return (android.app.AlertDialog)field("previewDialog");}
    private android.app.AlertDialog previewDialogUnchecked(){try{return previewDialog();}catch(Exception error){throw new RuntimeException(error);}}
    private void awaitPreviews()throws Exception{RemotePreviewStore store=RemotePreviewStore.get(activity);for(int i=0;i<2;i++){store.io.submit(()->{}).get(5,TimeUnit.SECONDS);instrumentation.waitForIdleSync();}}
    private void assertPreviewCard(){android.widget.ImageView image=findPreviewImage(content());assertNotNull("Preview card image",image);assertEquals(View.VISIBLE,image.getVisibility());assertNotNull(image.getDrawable());}
    private android.widget.ImageView findPreviewImage(View view){if(view instanceof android.widget.ImageView)return (android.widget.ImageView)view;if(view instanceof ViewGroup){ViewGroup group=(ViewGroup)view;for(int i=0;i<group.getChildCount();i++){var image=findPreviewImage(group.getChildAt(i));if(image!=null)return image;}}return null;}
    private Switch rememberSwitch(View view){
        if(view instanceof Switch)return (Switch)view;
        if(view instanceof ViewGroup){ViewGroup group=(ViewGroup)view;for(int i=0;i<group.getChildCount();i++){Switch match=rememberSwitch(group.getChildAt(i));if(match!=null)return match;}}return null;
    }
}
