package cn.crossdesk.mobile;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Base64;
import android.view.Surface;
import org.json.JSONObject;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.KeyStore;
import java.security.cert.X509Certificate;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

final class NativeSession implements AutoCloseable {
    static { System.loadLibrary("crossdesk_android"); }
    interface Listener {
        void status(String message);
        void connected();
        void ended(String reason);
        void data(JSONObject message);
        void videoSize(int width, int height);
        void clipboard(String text);
        void statistics(RemoteNetworkStatistics.Snapshot value);
        default void signaling(int state, String deviceId) { }
        default void announcement(JSONObject message) { }
        default void announcementFailed(String requestId) { }
        default void passwordRejected() { ended("远端密码错误"); }
    }
    // Serializes all peer creation/API calls/destruction, including across activities.
    private static final ExecutorService RTC = Executors.newSingleThreadExecutor(r -> new Thread(r, "CrossDesk RTC"));
    private final Handler main = new Handler(Looper.getMainLooper());
    private final Context context;
    private final Listener listener;
    private final SecretStore secrets;
    private final String host, scope;
    private volatile String remote;
    private String password, identity = "";
    private final int port;
    private long handle;
    private boolean signalReady, joining, connected, started;
    private int signalState;
    private volatile boolean closed;
    private Surface surface;
    private volatile AudioPlayer audio;
    private final RemoteNetworkStatistics networkStatistics=new RemoteNetworkStatistics();
    private final Runnable statisticsTick=new Runnable(){public void run(){
        if(closed)return;
        listener.statistics(networkStatistics.snapshot(RemoteNetworkStatistics.now()));
        if(!closed)main.postDelayed(this,1000);
    }};
    private final Runnable timeout = () -> fail(remote.isEmpty()
            ? "服务器连接超时，请检查服务器地址和网络"
            : "连接超时，请检查设备 ID、服务器及远端在线状态");

    NativeSession(Context context, String host, int port, String remote, String password, Listener listener) {
        this.context = context.getApplicationContext(); this.listener = listener;
        this.host = host; this.port = port; this.remote = remote; this.password = password;
        scope = host.toLowerCase(java.util.Locale.ROOT) + ":" + port;
        secrets = new SecretStore(this.context);
    }
    void start() {
        if (started || closed) return;
        started = true;
        main.postDelayed(timeout, 40000);
        execute(() -> {
            try {
                identity = secrets.get(scope);
                File certificates = exportTrust();
                File logs = new File(context.getNoBackupFilesDir(), "logs");
                if (!logs.isDirectory() && !logs.mkdirs()) throw new IllegalStateException("Log directory unavailable");
                handle = nCreate(this, host, port, identity, logs.getAbsolutePath(), certificates.getAbsolutePath());
                if (handle == 0) { fail("原生连接初始化失败"); return; }
                if (surface != null && surface.isValid()) nSurface(handle, surface);
            } catch (Exception error) { fail("安全存储或证书初始化失败，请重试或清除应用数据"); }
        });
    }
    /** Promote the already registered identity peer to an outgoing desktop session. */
    void connect(String remoteId, String secret) {
        execute(() -> {
            if (joining || connected) return;
            remote = remoteId;
            password = secret;
            // Serialize arming with identity callbacks, which cancel the idle
            // login timeout. Otherwise a just-completed login can cancel this
            // outgoing connection's timeout before tryJoin runs.
            main.removeCallbacks(timeout);
            main.postDelayed(timeout, 40000);
            tryJoin();
        });
    }
    boolean isClosed() { return closed; }
    private void publishSignaling() {
        int state = signalState;
        // Only expose the public device ID, never the identity's @password suffix.
        String deviceId = identity.split("@", 2)[0];
        ui(() -> listener.signaling(state, deviceId));
    }
    private File exportTrust() throws Exception {
        TrustManagerFactory factory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
        factory.init((KeyStore) null);
        StringBuilder pem = new StringBuilder();
        for (TrustManager manager : factory.getTrustManagers()) {
            if (manager instanceof X509TrustManager trust) {
                for (X509Certificate certificate : trust.getAcceptedIssuers()) {
                    pem.append("-----BEGIN CERTIFICATE-----\n")
                            .append(Base64.encodeToString(certificate.getEncoded(), Base64.NO_WRAP))
                            .append("\n-----END CERTIFICATE-----\n");
                }
            }
        }
        if (pem.length() == 0) throw new IllegalStateException("Empty Android trust store");
        File output = new File(context.getNoBackupFilesDir(), "trusted-roots.pem");
        Files.write(output.toPath(), pem.toString().getBytes(StandardCharsets.US_ASCII));
        return output;
    }
    private void execute(Runnable task) { RTC.execute(() -> { if (!closed) task.run(); }); }
    private void ui(Runnable task) { main.post(() -> { if (!closed) task.run(); }); }
    private void fail(String reason) { ui(() -> { listener.ended(reason); close(); }); }
    private void tryJoin() {
        if (handle == 0 || joining || remote.isEmpty() || !signalReady || identity.isEmpty()) return;
        joining = true;
        String base = identity.split("@", 2)[0];
        if (!nConnect(handle, base, remote, password)) fail("无法发起远程连接");
        password = "";
    }
    // Called on arbitrary native callback threads; borrowed payloads were copied by JNI.
    @SuppressWarnings("unused") private void onNativeEvent(int type, byte[] bytes) {
        execute(() -> {
            String value = new String(bytes, StandardCharsets.UTF_8);
            try {
                if (type == 3) {
                    if (value.contains("@")) {
                        identity = value;
                        secrets.put(scope, identity);
                    } else if (!identity.startsWith(value + "@")) identity = value;
                    publishSignaling(); tryJoin(); return;
                }
                if (type == 6) { ui(() -> listener.clipboard(value)); return; }
                JSONObject event = new JSONObject(value);
                if (type == 1) {
                    int status = event.getInt("status");
                    if (!event.getBoolean("controller")) {
                        signalState = status;
                        signalReady = status == 1;
                        if (signalReady && remote.isEmpty()) main.removeCallbacks(timeout);
                        publishSignaling();
                        tryJoin();
                    }
                    if (status == 6) fail("服务器 TLS 证书校验失败");
                    else if (status == 2) fail("服务器登录失败，请检查服务器地址和网络");
                    else if (status >= 3 && connected) fail("信令连接中断，请重新连接");
                    else if (status == 0 || status == 4) ui(() -> listener.status("正在连接信令服务器…"));
                } else if (type == 2) {
                    int status = event.getInt("status");
                    if (status == 1) {
                        if (connected) return; // Repeated transport readiness is the same desktop session.
                        connected = true; main.removeCallbacks(timeout); nReady(handle);
                        audio = new AudioPlayer();
                        nControl(handle, 2, 1);
                        ui(() -> { listener.connected(); if(!closed)statisticsTick.run(); });
                    } else if (status == 0 || status == 2) ui(() -> listener.status("正在建立安全连接…"));
                    else if (status == 6) ui(() -> { listener.passwordRejected(); close(); });
                    else {
                        String[] reasons = {"", "", "", "连接已断开", "连接失败", "远端已关闭连接",
                                "远端密码错误", "找不到该设备 ID", "远端当前不可用"};
                        fail(status >= 0 && status < reasons.length ? reasons[status] : "连接已结束");
                    }
                } else if (type == 4) ui(() -> listener.data(event));
                else if (type == 5) {
                    int width = event.getInt("width"), height = event.getInt("height");
                    ui(() -> listener.videoSize(width, height));
                } else if (type == 7) networkStatistics.receive(new RemoteNetworkStatistics.Report(event),RemoteNetworkStatistics.now());
                else if (type == 8 && signalReady) ui(() -> listener.announcement(event));
            } catch (Exception error) {
                if (type == 3) fail("设备身份保存失败，请检查安全存储");
            }
        });
    }
    @SuppressWarnings("unused") private void onNativeAudio(byte[] pcm) {
        AudioPlayer player = audio;
        if (!closed && player != null) player.offer(pcm);
    }
    @SuppressWarnings("unused") private void onNativeVideoFrame(long id,double latency,int width,int height){
        if(!closed)networkStatistics.frame(id,latency,width,height,RemoteNetworkStatistics.now());
    }
    void videoSettings(RemoteVideoSettings.Values settings,long requestId){
        if(!settings.valid()||requestId<0||requestId>0xffffffffL)return;
        execute(()->{if(connected&&!nVideoSettings(handle,settings.quality,settings.frameRate,settings.preference,requestId))ui(()->listener.data(settings.status(requestId,false)));});
    }
    void announcementRequest(JSONObject request,String requestId){
        byte[] bytes=request.toString().getBytes(StandardCharsets.UTF_8);
        execute(()->{if(!signalReady||identity.isEmpty()||!nAnnouncementRequest(handle,bytes))ui(()->listener.announcementFailed(requestId));});
    }
    void surface(Surface value) { execute(() -> { surface = value; if (handle != 0) nSurface(handle, value); }); }
    void pointer(float x, float y, int flag, int wheel) { execute(() -> { if (connected) nPointer(handle, x, y, flag, wheel); }); }
    void key(int code, boolean down) { execute(() -> { if (connected) nKey(handle, code, down); }); }
    void control(int type, int value) {
        AudioPlayer player = audio;
        if (type == 2 && player != null) player.setEnabled(value != 0);
        execute(() -> { if (connected){nControl(handle, type, value);if(type==4)networkStatistics.resetVideo();} });
    }
    void clipboard(String value) {
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        if (bytes.length == 0 || bytes.length > 128 * 1024) return;
        execute(() -> { if (connected) nClipboard(handle, bytes); });
    }
    @Override public void close() {
        if (closed) return;
        closed = true; main.removeCallbacks(timeout);main.removeCallbacks(statisticsTick);
        RTC.execute(() -> {
            main.removeCallbacks(timeout);
            if (audio != null) { audio.close(); audio = null; }
            password = "";
            if (handle != 0) { nDestroy(handle); handle = 0; }
            surface = null;
        });
    }
    static native boolean nValidAppVersion(String version);
    static native boolean nHasAppUpdate(String version, byte[] releaseJson);
    private static native long nCreate(NativeSession owner, String host, int port, String identity, String logs, String roots);
    private static native boolean nConnect(long handle, String identity, String remote, String password);
    private static native void nReady(long handle);
    private static native void nPointer(long handle, float x, float y, int flag, int wheel);
    private static native void nKey(long handle, int code, boolean down);
    private static native void nControl(long handle, int type, int value);
    private static native boolean nVideoSettings(long handle,int quality,int frameRate,int preference,long requestId);
    private static native boolean nAnnouncementRequest(long handle,byte[] data);
    private static native void nClipboard(long handle, byte[] data);
    private static native void nSurface(long handle, Surface surface);
    private static native void nDestroy(long handle);
}
