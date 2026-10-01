package cn.crossdesk.mobile;

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
    private static JSONObject answer(JSONObject request,int count,long revision)throws Exception {
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

}
