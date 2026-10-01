package cn.crossdesk.mobile;

import android.os.Handler;
import android.os.Looper;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.URL;
import java.util.function.Consumer;
import javax.net.ssl.HttpsURLConnection;

/** One check per desktop session. State and completion callbacks run on main. */
final class RemoteVersionCheck {
    static final String NOTICE = "被控端版本过低，请升级到最新版本。";
    static final String RELEASE_URL = "https://version.crossdesk.cn/version.json";
    interface Cancellation { void cancel(); }
    interface Fetcher { Cancellation fetch(Consumer<byte[]> completion); }

    private final Fetcher fetcher;
    private final Runnable notify;
    private boolean connected, receivedHostInfo, attempted;
    private String version = "";
    private long generation;
    private Cancellation request;

    RemoteVersionCheck(Runnable notify) { this(ReleaseRequest::start, notify); }
    RemoteVersionCheck(Fetcher fetcher, Runnable notify) {
        this.fetcher = fetcher;
        this.notify = notify;
    }

    void connected() { connected = true; check(); }

    void hostInfo(JSONObject info) {
        receivedHostInfo = true;
        Object value = info.opt("app_version");
        version = value instanceof String && NativeSession.nValidAppVersion((String)value)
                ? (String)value : "";
        check();
    }

    private void check() {
        if (!connected || !receivedHostInfo || attempted) return;
        attempted = true;
        // iOS treats missing/invalid peer versions as legacy, without a request.
        if (version.isEmpty()) { notify.run(); return; }
        long currentGeneration = generation;
        String currentVersion = version;
        request = fetcher.fetch(data -> {
            if (generation != currentGeneration || !connected) return;
            request = null;
            if (data != null && NativeSession.nHasAppUpdate(currentVersion, data)) notify.run();
        });
    }

    void reset() {
        ++generation;
        connected = receivedHostInfo = attempted = false;
        version = "";
        if (request != null) request.cancel();
        request = null;
    }

    private static final class ReleaseRequest implements Cancellation, Runnable {
        private static final int MAX_BYTES = 256 * 1024;
        private final Handler main = new Handler(Looper.getMainLooper());
        private final Consumer<byte[]> completion;
        private final Runnable timeout = () -> finish(null);
        private volatile boolean cancelled;
        private HttpsURLConnection connection;

        private ReleaseRequest(Consumer<byte[]> completion) { this.completion = completion; }
        static Cancellation start(Consumer<byte[]> completion) {
            ReleaseRequest request = new ReleaseRequest(completion);
            request.main.postDelayed(request.timeout, 15000);
            Thread worker = new Thread(request, "CrossDesk version check");
            worker.setDaemon(true);
            worker.start();
            return request;
        }

        @Override public void run() {
            byte[] result = null;
            HttpsURLConnection opened = null;
            try {
                // Fetch public metadata only: no peer identity, version or credentials.
                opened = (HttpsURLConnection)new URL(RELEASE_URL).openConnection();
                synchronized (this) {
                    if (cancelled) return;
                    connection = opened;
                }
                opened.setConnectTimeout(5000);
                opened.setReadTimeout(10000);
                opened.setUseCaches(false);
                opened.setInstanceFollowRedirects(false);
                if (opened.getResponseCode() == 200 && opened.getContentLengthLong() <= MAX_BYTES) {
                    try (InputStream input = opened.getInputStream();
                         ByteArrayOutputStream output = new ByteArrayOutputStream()) {
                        byte[] buffer = new byte[4096];
                        int count;
                        while (!cancelled && (count = input.read(buffer)) != -1) {
                            if (output.size() + count > MAX_BYTES) return;
                            output.write(buffer, 0, count);
                        }
                        if (!cancelled) result = output.toByteArray();
                    }
                }
            } catch (Exception ignored) {
                // A failed check is silent, as on iOS; it never interrupts control.
            } finally {
                if (opened != null) opened.disconnect();
                byte[] response = result;
                main.post(() -> finish(response));
            }
        }

        private void finish(byte[] response) {
            if (cancelled) return;
            cancel();
            completion.accept(response);
        }

        @Override public void cancel() {
            HttpsURLConnection active;
            synchronized (this) { cancelled = true; active = connection; connection = null; }
            main.removeCallbacks(timeout);
            if (active != null) active.disconnect();
        }
    }
}
