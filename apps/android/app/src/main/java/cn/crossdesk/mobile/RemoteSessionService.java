package cn.crossdesk.mobile;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

/** Foreground fallback for a user-started RTC session when native PiP is unavailable. */
public final class RemoteSessionService extends Service {
    private static final String CHANNEL = "remote_connection";
    private static final String STOP = "cn.crossdesk.mobile.STOP_REMOTE_SESSION";
    private static final int NOTIFICATION = 41;
    private static BackgroundSession current;
    private static RemoteSessionService running;
    private BackgroundSession owned;
    private PowerManager.WakeLock wakeLock;

    static BackgroundSession current() { return current; }
    static boolean retain(Context context, BackgroundSession value) {
        if (value.ended || value.session.isClosed()) return false;
        if (current == value) return true;
        if (current != null) return false;
        current = value;
        try {
            context.startForegroundService(new Intent(context, RemoteSessionService.class).putExtra("session", value.token));
            return true;
        } catch (RuntimeException error) {
            current = null;
            Log.w("CrossDesk", "Background session service could not start", error);
            return false;
        }
    }
    static void release(Context context, BackgroundSession value) {
        if (current != value) return;
        current = null;
        if (running != null) running.releaseWakeLock();
        context.stopService(new Intent(context, RemoteSessionService.class));
    }
    static void refresh(BackgroundSession value) {
        if (current == value && running != null && running.owned == value) running.update();
    }
    @Override public IBinder onBind(Intent intent) { return null; }
    @Override public void onCreate() {
        super.onCreate(); running = this;
        getSystemService(NotificationManager.class).createNotificationChannel(
                new NotificationChannel(CHANNEL, "远程连接", NotificationManager.IMPORTANCE_LOW));
        wakeLock = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "CrossDesk:RemoteSession");
        wakeLock.setReferenceCounted(false);
    }
    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        BackgroundSession value = current;
        if (value == null || value.ended || value.session.isClosed()) { stopSelf(startId); return START_NOT_STICKY; }
        if (intent == null || !value.token.equals(intent.getStringExtra("session"))) return START_NOT_STICKY;
        owned = value;
        if (STOP.equals(intent.getAction())) { value.finish("已从通知断开远程连接"); return START_NOT_STICKY; }
        try {
            Notification notification = notification();
            if (Build.VERSION.SDK_INT >= 34) startForeground(NOTIFICATION, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
            else startForeground(NOTIFICATION, notification);
            update();
        } catch (RuntimeException error) {
            Log.w("CrossDesk", "Background session foreground promotion failed", error);
            value.finish("后台保活未能启动，请保持应用在前台后重新连接");
        }
        return START_NOT_STICKY;
    }
    private Notification notification() {
        PendingIntent open = PendingIntent.getActivity(this, 0,
                new Intent(this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP),
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Intent stopIntent = new Intent(this, RemoteSessionService.class).setAction(STOP)
                .setData(android.net.Uri.parse("crossdesk-session:" + owned.token)).putExtra("session", owned.token);
        PendingIntent stop = PendingIntent.getService(this, 1, stopIntent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this, CHANNEL).setSmallIcon(R.drawable.ic_remote_session)
                .setContentTitle("CrossDesk 远程连接")
                .setContentText(owned.background ? "后台保持连接，点按返回远程桌面" : "已启用后台保活，切换应用可继续保持连接")
                .setContentIntent(open).setOngoing(true).setOnlyAlertOnce(true).setShowWhen(false)
                .setCategory(Notification.CATEGORY_SERVICE).setVisibility(Notification.VISIBILITY_PRIVATE)
                .addAction(new Notification.Action.Builder(null, "返回", open).build())
                .addAction(new Notification.Action.Builder(null, "断开", stop).build()).build();
    }
    @android.annotation.SuppressLint("WakelockTimeout") // Released on foreground return, disconnect, task removal and service destruction.
    private void update() {
        if (owned == null || owned.ended) return;
        if (owned.background) { if (!wakeLock.isHeld()) wakeLock.acquire(); }
        else releaseWakeLock();
        getSystemService(NotificationManager.class).notify(NOTIFICATION, notification());
    }
    private void releaseWakeLock() { if (wakeLock != null && wakeLock.isHeld()) wakeLock.release(); }
    @Override public void onTaskRemoved(Intent rootIntent) {
        if (current == owned && owned != null) owned.finish("远程任务已关闭");
        stopSelf();
    }
    @Override public void onDestroy() {
        if (running == this) running = null;
        releaseWakeLock(); stopForeground(STOP_FOREGROUND_REMOVE);
        BackgroundSession value = owned;
        if (current == value && value != null) { current = null; value.finish("后台连接服务已停止，请重新连接"); }
        super.onDestroy();
    }
}
