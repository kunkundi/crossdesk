package cn.crossdesk.mobile;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class RemoteVersionCheckTest {
    private static byte[] json(String value) { return value.getBytes(StandardCharsets.UTF_8); }
    private static JSONObject host(Object version) {
        try { return new JSONObject().put("app_version", version); }
        catch (Exception error) { throw new AssertionError(error); }
    }
    private static final class Fetches implements RemoteVersionCheck.Fetcher {
        final List<Consumer<byte[]>> replies = new ArrayList<>();
        int cancellations;
        public RemoteVersionCheck.Cancellation fetch(Consumer<byte[]> reply) {
            replies.add(reply);
            return () -> cancellations++;
        }
    }

    @Test public void sharedNativeComparisonMatchesIosReleaseOrdering() {
        byte[] release = json("{\"latest_version\":\"v1.5.3-20260928\",\"patch\":2}");
        for (String version : new String[]{"1.5.2", "v1.5.3-20260928", "1.5.3-1-20260929", "1.5.3-20260928-1"}) {
            assertTrue(NativeSession.nValidAppVersion(version));
            assertTrue(version, NativeSession.nHasAppUpdate(version, release));
        }
        for (String version : new String[]{"1.5.3-2-20260927", "1.5.3-3-20260928", "1.6.0"})
            assertFalse(version, NativeSession.nHasAppUpdate(version, release));
        assertFalse(NativeSession.nHasAppUpdate("1.5.3-20260927", json("{\"version\":\"1.5.3-20261001\"}")));
        assertTrue(NativeSession.nHasAppUpdate("1.5.3-9", json("{\"version\":\"1.5.3\",\"patch\":\"10\"}")));
    }

    @Test public void invalidReleaseMetadataNeverRaisesANotice() {
        for (String release : new String[]{"", "not JSON", "{}", "[]", "null", "{\"version\":false}",
                "{\"version\":\"unknown\"}", "{\"version\":\"1..2\"}", "{\"version\":\"999999999999999.2\"}"})
            assertFalse(release, NativeSession.nHasAppUpdate("1.0.0", json(release)));
        assertFalse(NativeSession.nHasAppUpdate("1.0.0", null));
        assertFalse(NativeSession.nHasAppUpdate("1.0.0", new byte[256 * 1024 + 1]));
    }

    @Test public void legacyVersionWaitsForConnectionAndNeverFetches() {
        for (Object version : new Object[]{null, JSONObject.NULL, 123, "", " ", "unknown", "ios-native", "1..5", "1.5-"}) {
            Fetches fetches = new Fetches(); int[] notices = {0};
            RemoteVersionCheck check = new RemoteVersionCheck(fetches, () -> notices[0]++);
            check.hostInfo(host(version)); assertEquals(0, notices[0]);
            check.connected(); assertEquals(1, notices[0]);
            check.hostInfo(host(version)); check.connected(); assertEquals(1, notices[0]);
            assertTrue(fetches.replies.isEmpty()); check.reset();
        }
    }

    @Test public void bothEventOrdersCheckOncePerSession() {
        for (boolean hostFirst : new boolean[]{true, false}) {
            Fetches fetches = new Fetches(); int[] notices = {0};
            RemoteVersionCheck check = new RemoteVersionCheck(fetches, () -> notices[0]++);
            if (hostFirst) check.hostInfo(host("1.0.0")); else check.connected();
            assertTrue(fetches.replies.isEmpty());
            if (hostFirst) check.connected(); else check.hostInfo(host("1.0.0"));
            check.hostInfo(host("1.0.0")); check.connected(); assertEquals(1, fetches.replies.size());
            fetches.replies.get(0).accept(json("{\"latest_version\":\"2.0.0\"}")); assertEquals(1, notices[0]);
            check.hostInfo(host("1.0.0")); assertEquals(1, notices[0]);
            check.reset(); check.connected(); assertEquals(1, fetches.replies.size());
            check.hostInfo(host("1.0.0")); assertEquals(2, fetches.replies.size()); check.reset();
        }
    }

    @Test public void disconnectCancelsAndOldReplyCannotAffectANewPeer() {
        Fetches fetches = new Fetches(); int[] notices = {0};
        RemoteVersionCheck check = new RemoteVersionCheck(fetches, () -> notices[0]++);
        check.hostInfo(host("1.0.0")); check.connected(); check.reset();
        assertEquals(1, fetches.cancellations);
        fetches.replies.get(0).accept(json("{\"version\":\"2.0.0\"}")); assertEquals(0, notices[0]);
        check.connected(); check.hostInfo(host("3.0.0"));
        fetches.replies.get(0).accept(json("{\"version\":\"2.0.0\"}"));
        fetches.replies.get(1).accept(json("{\"version\":\"2.0.0\"}")); assertEquals(0, notices[0]);
        check.reset(); assertEquals(1, fetches.cancellations);
    }

    @Test public void failedCheckIsSilentAndDoesNotRetryOnDuplicateHostInfo() {
        for (byte[] reply : new byte[][]{null, json("invalid"), json("{\"version\":\"1.0.0\"}")}) {
            Fetches fetches = new Fetches(); int[] notices = {0};
            RemoteVersionCheck check = new RemoteVersionCheck(fetches, () -> notices[0]++);
            check.connected(); check.hostInfo(host("1.0.0")); fetches.replies.get(0).accept(reply);
            check.hostInfo(host("1.0.0")); assertEquals(0, notices[0]); assertEquals(1, fetches.replies.size());
            check.reset();
        }
    }
}
