package cn.crossdesk.mobile;

import android.text.Spanned;
import android.text.style.URLSpan;
import androidx.test.platform.app.InstrumentationRegistry;
import org.json.*;
import org.junit.*;
import java.io.File;
import java.nio.file.Files;
import java.util.*;
import java.util.concurrent.FutureTask;
import static org.junit.Assert.*;

public class AnnouncementInboxTest {
    private AnnouncementInbox inbox;
    private File directory;
    private final ArrayDeque<JSONObject> sent=new ArrayDeque<>();
    private interface Checked { void run() throws Exception; }
    private void main(Checked action)throws Exception {
        FutureTask<Void> task=new FutureTask<>(()->{action.run();return null;});
        InstrumentationRegistry.getInstrumentation().runOnMainSync(task);task.get();
    }
    @Before public void setup()throws Exception {
        directory=Files.createTempDirectory(InstrumentationRegistry.getInstrumentation().getTargetContext().getCacheDir().toPath(),"announcements-").toFile();
        main(()->{inbox=new AnnouncementInbox(directory);inbox.sender=(message,id)->sent.add(message);inbox.configure("Example.test.",9099,"123456789");inbox.setConnected(true);});
    }
    @After public void cleanup()throws Exception {
        main(()->inbox.close());
        try(var paths=Files.walk(directory.toPath())){for(var path:paths.sorted(Comparator.reverseOrder()).toList())Files.delete(path);}
    }
    static JSONObject answer(JSONObject request,int count,long revision)throws Exception {
        int offset=request.getInt("offset");boolean summary=request.getBoolean("summary_only");JSONArray items=new JSONArray();
        for(int i=offset;i<Math.min(count,offset+(summary?200:20));i++){
            JSONObject item=new JSONObject().put("id",i+1).put("revision",revision);
            if(!summary)item.put("title",i==0?"欢迎使用 CrossDesk":"公告 "+(i+1)).put("body","安卓公告与 iOS 保持一致。\n\n访问 [CrossDesk 官网](https://crossdesk.cn) 了解更多。\n支持中文、换行与网页链接。").put("updated_at",1790812800L);
            items.put(item);
        }
        return new JSONObject().put("type","announcements").put("request_id",request.getString("request_id"))
            .put("summary_only",summary).put("offset",offset).put("total",count).put("catalog_revision",revision).put("items",items);
    }
    private void drain(int count,long revision)throws Exception {
        int limit=100;while(!sent.isEmpty()){assertTrue("Request loop",limit-->0);inbox.receive(answer(sent.remove(),count,revision));}
    }
    @Test public void summariesCountAllUnreadAndBodiesPageInTwenties()throws Exception {main(()->{
        JSONObject first=sent.remove();inbox.receive(answer(first,203,1));assertEquals(200,sent.peek().getInt("offset"));assertEquals(0,inbox.items.size());
        inbox.receive(answer(sent.remove(),203,1));assertEquals(203,inbox.unread);assertFalse(sent.peek().getBoolean("summary_only"));
        drain(203,1);assertEquals(20,inbox.items.size());assertEquals(203,inbox.total);assertFalse(inbox.loading);assertTrue(inbox.loaded);
        inbox.loadMore();assertEquals(20,sent.peek().getInt("offset"));drain(203,1);assertEquals(40,inbox.items.size());
    });}
    @Test public void readAndDeletePersistPerRevisionAndScope()throws Exception {main(()->{
        drain(3,1);inbox.markRead(inbox.items.get(0));inbox.dismiss(inbox.items.get(1));assertEquals(1,inbox.unread);assertEquals(2,inbox.total);
        inbox.close();inbox=new AnnouncementInbox(directory);inbox.sender=(message,id)->sent.add(message);
        inbox.configure("example.TEST",9099,"123456789");inbox.setConnected(true);drain(3,1);
        assertEquals(2,inbox.items.size());assertTrue(inbox.items.get(0).read);assertEquals(1,inbox.unread);
        inbox.refresh();drain(3,2);assertEquals(3,inbox.total);assertEquals(3,inbox.unread);assertEquals(3,inbox.items.size());
        inbox.configure("other.test",9099,"123456789");drain(3,1);assertEquals(3,inbox.unread);
        inbox.configure("example.test",9099,"different-device");drain(3,1);assertEquals(3,inbox.unread);
        inbox.configure("example.test",9999,"123456789");drain(3,1);assertEquals(3,inbox.unread);
        inbox.configure("example.test",9099,"123456789");drain(3,1);assertEquals(1,inbox.unread);
    });}
    @Test public void deletingAPageLoadsReplacementAndStaleConfirmationDoesNotDeleteRevision()throws Exception {main(()->{
        drain(45,1);AnnouncementInbox.Item old=inbox.items.get(0);
        for(int i=0;i<20;i++){inbox.dismiss(inbox.items.get(0));drain(45,1);}
        assertEquals(25,inbox.total);assertEquals(20,inbox.items.size());assertEquals(21,inbox.items.get(0).id);
        inbox.refresh();drain(45,2);inbox.dismiss(old);assertTrue(inbox.deleteFailed);assertEquals(45,inbox.total);
    });}
    @Test public void catalogChangesRestartPagingAndIgnoreSupersededRequests()throws Exception {main(()->{
        JSONObject old=sent.remove();inbox.refresh();JSONObject current=sent.remove();inbox.receive(answer(old,203,1));assertTrue(inbox.loading);assertEquals(0,inbox.total);
        inbox.receive(answer(current,203,1));inbox.receive(answer(sent.remove(),203,2));
        assertTrue(sent.peek().getBoolean("summary_only"));assertEquals(0,sent.peek().getInt("offset"));drain(203,2);assertEquals(203,inbox.unread);
        inbox.refresh();JSONObject refresh=sent.remove();inbox.setConnected(false);inbox.receive(answer(refresh,0,3));
        assertEquals(20,inbox.items.size());assertFalse(inbox.loading);
        inbox.setConnected(true);drain(0,3);assertEquals(0,inbox.items.size());assertTrue(inbox.loaded);
    });}
    @Test public void malformedResponseFailsWithoutLosingVisibleRowsAndCanRetry()throws Exception {main(()->{
        drain(2,1);inbox.refresh();JSONObject invalid=answer(sent.remove(),2,1);invalid.put("total","2");inbox.receive(invalid);
        assertTrue(inbox.failed);assertFalse(inbox.loading);assertEquals(2,inbox.items.size());
        inbox.refresh();drain(2,2);assertFalse(inbox.failed);
        inbox.refresh();JSONObject summary=answer(sent.remove(),2,3);summary.getJSONArray("items").getJSONObject(1).put("id",1);inbox.receive(summary);
        assertTrue(inbox.failed);inbox.refresh();drain(2,3);assertFalse(inbox.failed);
        inbox.refresh();inbox.receive(answer(sent.remove(),2,4));JSONObject body=answer(sent.remove(),2,4);
        body.getJSONArray("items").getJSONObject(0).put("body","中".repeat(2667));inbox.receive(body);assertTrue(inbox.failed);assertEquals(0,inbox.items.size());
        inbox.refresh();drain(2,4);assertFalse(inbox.failed);
    });}
    @Test public void duplicateBodiesAcrossPagesCannotCreateDuplicateRows()throws Exception {main(()->{
        drain(21,1);inbox.loadMore();JSONObject invalid=answer(sent.remove(),21,1);invalid.getJSONArray("items").getJSONObject(0).put("id",1);
        inbox.receive(invalid);assertTrue(inbox.failed);assertEquals(20,inbox.items.size());
    });}
    @Test public void sendFailureAndTimeoutAreMatchedToTheirRequest()throws Exception {main(()->{
        String obsolete=sent.remove().getString("request_id");
        var timeoutField=AnnouncementInbox.class.getDeclaredField("timeout");timeoutField.setAccessible(true);Runnable oldTimeout=(Runnable)timeoutField.get(inbox);
        inbox.refresh();oldTimeout.run();inbox.failed(obsolete);assertTrue(inbox.loading);assertFalse(inbox.failed);
        ((Runnable)timeoutField.get(inbox)).run();assertFalse(inbox.loading);assertTrue(inbox.failed);sent.clear();
        inbox.sender=(message,id)->inbox.failed(id);inbox.refresh();assertTrue(inbox.failed);assertFalse(inbox.loading);
        inbox.setConnected(false);inbox.refresh();assertFalse(inbox.loading);assertTrue(sent.isEmpty());
    });}
    @Test public void diskFailureKeepsUnreadAndVisibleState()throws Exception {main(()->{
        inbox.close();File blocked=new File(directory,"not-a-directory");assertTrue(blocked.createNewFile());
        inbox=new AnnouncementInbox(blocked);inbox.sender=(message,id)->sent.add(message);sent.clear();
        inbox.configure("example.test",9099,"123456789");inbox.setConnected(true);drain(1,1);
        AnnouncementInbox.Item item=inbox.items.get(0);inbox.markRead(item);assertTrue(inbox.readSaveFailed);assertFalse(item.read);assertEquals(1,inbox.unread);
        inbox.dismiss(item);assertTrue(inbox.deleteFailed);assertEquals(1,inbox.items.size());
        assertTrue(blocked.delete());inbox.markRead(item);assertFalse(inbox.readSaveFailed);assertTrue(item.read);
    });}
    @Test public void resetClearsMemoryAndRejectsOldServerResponses()throws Exception {main(()->{
        drain(1,1);inbox.refresh();JSONObject old=sent.remove();inbox.reset();inbox.receive(answer(old,1,1));
        assertEquals(0,inbox.unread);assertEquals(0,inbox.items.size());assertFalse(inbox.isConnected());assertFalse(inbox.loading);
        inbox.refresh();assertTrue(sent.isEmpty());
    });}
    @Test public void textKeepsPlainContentAndOnlyCreatesWebLinks(){
        String body="<b>普通文本</b>\r\n[官网](https://crossdesk.cn/path_(one))，https://example.com/a?b=c!\n[java](javascript:alert(1))";
        Spanned formatted=(Spanned)AnnouncementText.format(body);
        assertEquals("<b>普通文本</b>\n官网，https://example.com/a?b=c!\n[java](javascript:alert(1))",formatted.toString());
        URLSpan[] links=formatted.getSpans(0,formatted.length(),URLSpan.class);assertEquals(2,links.length);
        assertEquals("https://crossdesk.cn/path_(one)",links[0].getURL());assertEquals("https://example.com/a?b=c",links[1].getURL());
        for(String bad:new String[]{"javascript:alert(1)","file:///data/test","intent://example","https://","https://example.com\\evil","https://example.com\n","https://<bad>"})assertFalse(bad,AnnouncementText.isWebURL(bad));
        assertTrue(AnnouncementText.isWebURL("HTTPS://example.com/path"));
    }
}
