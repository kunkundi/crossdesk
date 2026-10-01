package cn.crossdesk.mobile;

import android.os.Handler;
import android.os.Looper;
import android.util.AtomicFile;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;

/** Main-thread inbox. The server owns content; this device owns read/dismissed revisions. */
final class AnnouncementInbox implements AutoCloseable {
    interface Sender { void send(JSONObject request, String requestId); }
    static final class Item {
        final long id, revision, updatedAt;
        final String title, body;
        boolean read;
        Item(JSONObject json) throws Exception {
            id=integer(json,"id");revision=integer(json,"revision");updatedAt=integer(json,"updated_at");
            title=string(json,"title");body=string(json,"body");
            if(id<=0||revision<=0||updatedAt<=0||updatedAt>32_503_680_000L||
                title.getBytes(StandardCharsets.UTF_8).length>240||body.getBytes(StandardCharsets.UTF_8).length>8000)
                throw new IllegalArgumentException("Invalid announcement");
        }
    }
    final List<Item> items=new ArrayList<>();
    int total, unread;
    boolean loaded, loading, failed, readSaveFailed, deleteFailed;
    Sender sender;
    Runnable changed=()->{};
    private final File directory;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final Map<Long,Long> reads=new HashMap<>(), dismissed=new HashMap<>();
    private Map<Long,Long> catalog=new LinkedHashMap<>(), pendingCatalog=new LinkedHashMap<>();
    private final Map<Integer,List<Long>> pageIds=new HashMap<>();
    private final Map<Integer,List<Item>> pageBodies=new HashMap<>();
    private long catalogRevision=-1, pendingRevision=-1;
    private int pendingTotal, pageOffset, visibleLimit=20;
    private boolean summary=true, connected;
    private String scope="", requestId;
    private AtomicFile file;
    private Runnable timeout;

    AnnouncementInbox(File directory){this.directory=directory;}
    boolean isConnected(){return connected;}
    void configure(String host,int port,String deviceId){
        if(host.isEmpty()||port<=0||deviceId.isEmpty())return;
        String canonical=host.toLowerCase(Locale.ROOT);
        if(canonical.endsWith("."))canonical=canonical.substring(0,canonical.length()-1);
        String next=new JSONArray().put(canonical).put(port).put(deviceId).toString();
        if(scope.equals(next))return;
        boolean wasConnected=connected;reset();connected=wasConnected;scope=next;
        try{
            byte[] hash=MessageDigest.getInstance("SHA-256").digest(scope.getBytes(StandardCharsets.UTF_8));
            StringBuilder name=new StringBuilder();for(byte b:hash)name.append(String.format(Locale.ROOT,"%02x",b&255));
            file=new AtomicFile(new File(directory,name+".json"));loadLocalState();
        }catch(Exception error){file=null;}
        refresh();
    }
    void reset(){
        cancelRequest();scope="";file=null;connected=false;items.clear();reads.clear();dismissed.clear();
        catalog.clear();pendingCatalog.clear();pageIds.clear();pageBodies.clear();
        catalogRevision=pendingRevision=-1;pendingTotal=pageOffset=total=unread=0;visibleLimit=20;
        loaded=failed=readSaveFailed=deleteFailed=false;changed.run();
    }
    void setConnected(boolean value){
        if(connected==value)return;connected=value;
        if(value)refresh();else{cancelRequest();changed.run();}
    }
    void refresh(){
        cancelRequest();pendingCatalog=new LinkedHashMap<>();pendingRevision=-1;pendingTotal=0;summary=true;
        request(0);changed.run();
    }
    void loadMore(){
        if(!connected||loading||failed||catalogRevision<0||visibleLimit>=total)return;
        visibleLimit+=Math.min(20,total-visibleLimit);rebuild();changed.run();
    }
    Item find(long id,long revision){
        for(Item item:items)if(item.id==id&&item.revision==revision)return item;return null;
    }
    void markRead(Item item){
        if(find(item.id,item.revision)==null||Objects.equals(reads.get(item.id),item.revision))return;
        Map<Long,Long> updated=new HashMap<>(reads);updated.put(item.id,item.revision);
        if(!saveLocalState(updated,dismissed)){readSaveFailed=true;changed.run();return;}
        reads.clear();reads.putAll(updated);readSaveFailed=deleteFailed=false;updateReadingState();changed.run();
    }
    void dismiss(Item item){
        if(find(item.id,item.revision)==null){deleteFailed=true;changed.run();return;}
        Map<Long,Long> updated=new HashMap<>(dismissed);updated.put(item.id,item.revision);
        if(!saveLocalState(reads,updated)){deleteFailed=true;changed.run();return;}
        dismissed.clear();dismissed.putAll(updated);deleteFailed=readSaveFailed=false;
        if(summary&&loading){items.removeIf(this::isDismissed);updateReadingState();}
        else{cancelRequest();rebuild();}
        changed.run();
    }
    void failed(String id){
        if(!Objects.equals(requestId,id)||id==null)return;
        cancelRequest();failed=true;changed.run();
    }
    void receive(JSONObject message){
        Object identifier=message.opt("request_id");
        if(!(identifier instanceof String)||!identifier.equals(requestId))return;
        String id=(String)identifier;
        try{
            if(!"announcements".equals(string(message,"type"))||message.has("error"))throw new IllegalArgumentException();
            Object summaryFlag=message.get("summary_only");
            long offsetValue=integer(message,"offset"), totalValue=integer(message,"total");
            long revision=integer(message,"catalog_revision");JSONArray entries=message.getJSONArray("items");
            if(!(summaryFlag instanceof Boolean)||((Boolean)summaryFlag)!=summary||offsetValue<0||
                totalValue<offsetValue||totalValue>Integer.MAX_VALUE||revision<0||
                entries.length()!=Math.min(summary?200:20,totalValue-offsetValue))throw new IllegalArgumentException();
            int offset=(int)offsetValue, count=(int)totalValue;
            LinkedHashMap<Long,Long> entriesById=new LinkedHashMap<>();
            for(int i=0;i<entries.length();i++){
                JSONObject entry=entries.getJSONObject(i);long key=integer(entry,"id"), version=integer(entry,"revision");
                if(key<=0||version<=0||entriesById.put(key,version)!=null)throw new IllegalArgumentException();
            }
            long expected=summary?pendingRevision:catalogRevision;
            if(expected>=0&&expected!=revision){refresh();return;}
            if(summary){
                if(offset!=pendingCatalog.size()||(pendingRevision>=0&&pendingTotal!=count))throw new IllegalArgumentException();
                for(Map.Entry<Long,Long> entry:entriesById.entrySet())
                    if(pendingCatalog.put(entry.getKey(),entry.getValue())!=null)throw new IllegalArgumentException();
                pendingRevision=revision;pendingTotal=count;cancelRequest();
                if(pendingCatalog.size()<pendingTotal){request(pendingCatalog.size());changed.run();return;}
                if(catalogRevision!=pendingRevision){pageIds.clear();pageBodies.clear();}
                catalog=new LinkedHashMap<>(pendingCatalog);catalogRevision=pendingRevision;
                reads.keySet().retainAll(catalog.keySet());dismissed.keySet().retainAll(catalog.keySet());
            }else{
                if(offset!=pageOffset||count!=catalog.size())throw new IllegalArgumentException();
                List<Item> bodies=new ArrayList<>();
                for(int i=0;i<entries.length();i++){
                    Item item=new Item(entries.getJSONObject(i));
                    if(!Objects.equals(catalog.get(item.id),item.revision))throw new IllegalArgumentException();
                    // A repeated ID on another body page must not create duplicate rows.
                    for(Map.Entry<Integer,List<Long>> page:pageIds.entrySet())
                        if(page.getKey()!=offset&&page.getValue().contains(item.id))throw new IllegalArgumentException();
                    bodies.add(item);
                }
                cancelRequest();pageIds.put(offset,new ArrayList<>(entriesById.keySet()));pageBodies.put(offset,bodies);
            }
            failed=false;rebuild();changed.run();
        }catch(Exception error){failed(id);}
    }
    private void request(int offset){
        if(!connected||scope.isEmpty())return;
        String id=UUID.randomUUID().toString();
        try{
            JSONObject payload=new JSONObject().put("type","announcements_list").put("request_id",id)
                .put("summary_only",summary).put("offset",offset);
            requestId=id;loading=true;failed=false;timeout=()->failed(id);handler.postDelayed(timeout,12_000);
            if(sender!=null)sender.send(payload,id);else failed(id);
        }catch(Exception error){failed(id);}
    }
    private void cancelRequest(){
        if(timeout!=null)handler.removeCallbacks(timeout);timeout=null;requestId=null;loading=false;
    }
    private boolean isDismissed(Item item){return Objects.equals(dismissed.get(item.id),item.revision);}
    private void updateReadingState(){
        total=unread=0;
        for(Map.Entry<Long,Long> entry:catalog.entrySet()){
            if(Objects.equals(dismissed.get(entry.getKey()),entry.getValue()))continue;
            total++;if(!Objects.equals(reads.get(entry.getKey()),entry.getValue()))unread++;
        }
        for(Item item:items)item.read=Objects.equals(reads.get(item.id),item.revision);
    }
    private void rebuild(){
        updateReadingState();int wanted=Math.min(visibleLimit,total), position=0;Integer nextOffset=null;
        List<Item> visible=new ArrayList<>();Set<Integer> retainedPages=new HashSet<>();summary=false;
        if(wanted>0)for(int offset=0;offset<catalog.size();offset+=20){
            List<Long> ids=pageIds.get(offset);if(ids==null){nextOffset=offset;break;}
            Set<Long> selection=new HashSet<>();
            for(long id:ids){
                Long revision=catalog.get(id);if(revision==null||Objects.equals(dismissed.get(id),revision))continue;
                if(position<wanted)selection.add(id);position++;
            }
            if(selection.isEmpty()){pageBodies.remove(offset);continue;}
            retainedPages.add(offset);List<Item> bodies=pageBodies.get(offset);
            if(bodies==null){nextOffset=offset;break;}
            for(Item item:bodies)if(selection.contains(item.id))visible.add(item);
            if(visible.size()==wanted)break;
        }
        if(visible.size()==wanted)pageBodies.keySet().retainAll(retainedPages);
        else{
            Set<Long> retained=new HashSet<>();for(Item item:visible)retained.add(item.id);
            for(Item item:items)if(Objects.equals(catalog.get(item.id),item.revision)&&!isDismissed(item)&&retained.add(item.id)){
                visible.add(item);if(visible.size()==wanted)break;
            }
        }
        items.clear();items.addAll(visible);loaded=true;updateReadingState();
        if(nextOffset!=null){pageOffset=nextOffset;request(nextOffset);}
    }
    private void loadLocalState(){
        if(file==null||file.getBaseFile().length()>16*1024*1024)return;
        try{
            JSONObject saved=new JSONObject(new String(file.readFully(),StandardCharsets.UTF_8));
            long version=integer(saved,"version");
            if(!scope.equals(string(saved,"scope"))||(version!=1&&version!=2))return;
            loadRevisions(saved.optJSONObject("reads"),reads);loadRevisions(saved.optJSONObject("dismissed"),dismissed);
        }catch(Exception ignored){}
    }
    private static void loadRevisions(JSONObject json,Map<Long,Long> target){
        if(json==null)return;
        for(Iterator<String> keys=json.keys();keys.hasNext();){
            String key=keys.next();
            try{long id=Long.parseLong(key), revision=integer(json,key);if(id>0&&revision>0)target.put(id,revision);}catch(Exception ignored){}
        }
    }
    private boolean saveLocalState(Map<Long,Long> readValues,Map<Long,Long> dismissedValues){
        if(file==null)return false;FileOutputStream output=null;
        try{
            if(!directory.isDirectory()&&!directory.mkdirs())return false;
            JSONObject saved=new JSONObject().put("version",2).put("scope",scope)
                .put("reads",revisions(readValues)).put("dismissed",revisions(dismissedValues));
            byte[] bytes=saved.toString().getBytes(StandardCharsets.UTF_8);
            output=file.startWrite();output.write(bytes);output.getFD().sync();file.finishWrite(output);output=null;
            // AtomicFile logs some rename failures instead of throwing; verify the committed file.
            return Arrays.equals(bytes,file.readFully());
        }catch(Exception error){if(output!=null)file.failWrite(output);return false;}
    }
    private static JSONObject revisions(Map<Long,Long> values)throws Exception{
        JSONObject json=new JSONObject();for(Map.Entry<Long,Long> entry:values.entrySet())json.put(entry.getKey().toString(),entry.getValue());return json;
    }
    private static long integer(JSONObject json,String key)throws Exception{
        Object value=json.get(key);if(!(value instanceof Integer)&&!(value instanceof Long))throw new IllegalArgumentException(key);
        return ((Number)value).longValue();
    }
    private static String string(JSONObject json,String key)throws Exception{
        Object value=json.get(key);if(!(value instanceof String))throw new IllegalArgumentException(key);return (String)value;
    }
    @Override public void close(){cancelRequest();connected=false;sender=null;changed=()->{};}
}
