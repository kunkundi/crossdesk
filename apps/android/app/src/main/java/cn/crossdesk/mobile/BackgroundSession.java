package cn.crossdesk.mobile;

import android.content.Context;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.util.UUID;

/** Main-thread session state; never retains a stopped Activity or its Views. */
final class BackgroundSession implements NativeSession.Listener {
    final Context context;
    final NativeSession session;
    final String token = UUID.randomUUID().toString();
    final String host, remote;
    final int port;
    NativeSession.Listener view;
    String identity = "", platform = "", clipboard = "", fileStatus = "", endReason = "";
    JSONArray displays = new JSONArray();
    RemoteVideoSettings videoSettings;
    int signalState, selectedDisplay, width, height;
    boolean muted, remoteServiceAvailable, background, ended;
    File receivedFile;

    BackgroundSession(Context context, NativeSession session, String host, int port, String remote) {
        this.context = context.getApplicationContext();
        this.session = session; this.host = host; this.port = port; this.remote = remote;
    }
    void attach(NativeSession.Listener listener) { view = listener; }
    void detach(NativeSession.Listener listener) { if (view == listener) view = null; }
    void setBackground(boolean value) {
        if (background == value || ended) return;
        background = value;
        // With no visible window, release rendering buffers and stop remote
        // audio. The RTC/control connection and file transfers remain alive.
        if (value) session.surface(null);
        session.control(2, value || muted ? 0 : 1);
        RemoteSessionService.refresh(this);
    }
    void finish(String reason) {
        if (ended) return;
        ended = true; endReason = reason; clipboard = "";
        session.close();
        RemoteSessionService.release(context, this);
        NativeSession.Listener listener = view; view = null;
        if (listener != null) listener.ended(reason);
    }
    @Override public void ended(String reason) { finish(reason); }
    @Override public void passwordRejected() { finish("远端访问密码已失效，请重新连接"); }
    @Override public void connected() { /* Already connected before the service takes ownership. */ }
    @Override public void status(String message) { if (view != null) view.status(message); }
    @Override public void signaling(int state, String id) {
        signalState = state; identity = id;
        if (view != null) view.signaling(state, id);
    }
    @Override public void data(JSONObject message) {
        if (view != null) { view.data(message); return; }
        if (message.optInt("type", -1) == 12 && videoSettings != null) videoSettings.receive(message);
        if (message.optInt("type", -1) == 5) {
            JSONObject service = message.optJSONObject("service_status");
            remoteServiceAvailable = service != null && service.optBoolean("available", false);
        }
        JSONObject info = message.optJSONObject("host_info");
        if (info != null) {
            platform = info.optString("platform", "");
            JSONArray values = info.optJSONArray("displays"); if (values != null) displays = values;
            if (videoSettings != null) videoSettings.setSupported(info.optBoolean("supports_video_settings", false));
        }
    }
    @Override public void videoSize(int w, int h) {
        width = w; height = h;
        if (view != null) view.videoSize(w, h);
    }
    @Override public void clipboard(String text) { clipboard = text; if (view != null) view.clipboard(text); }
    @Override public void statistics(RemoteNetworkStatistics.Snapshot value) { if (view != null) view.statistics(value); }
    @Override public void fileTransfer(String name, double progress, boolean sending, File received) {
        if (received != null) receivedFile = received;
        fileStatus = progress < 0 ? name + " 传输失败" : (sending ? "发送 " : "接收 ") + name
                + (progress >= 1 ? " · 已完成" : " · " + (int)(progress * 100) + "%");
        if (view != null) view.fileTransfer(name, progress, sending, received);
    }
    @Override public void controlFeedback(String text) { if (view != null) view.controlFeedback(text); }
    @Override public void presence(JSONObject message) { if (view != null) view.presence(message); }
    @Override public void announcement(JSONObject message) { if (view != null) view.announcement(message); }
    @Override public void announcementFailed(String id) { if (view != null) view.announcementFailed(id); }
}
