package cn.crossdesk.mobile;

import org.json.JSONArray;
import org.json.JSONObject;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;
import java.util.function.Consumer;

/** Main-thread presence cache, with the same refresh and expiry intervals as iOS. */
final class RecentConnectionPresence {
    private static final long REFRESH_INTERVAL=30_000, MAX_AGE=60_000;
    private final Set<String> watched=new LinkedHashSet<>();
    private final Map<String,Boolean> online=new HashMap<>();
    private final Map<String,Long> updatedAt=new HashMap<>();
    private boolean connected,awaitingSnapshot=true;
    private long lastRefresh;
    Consumer<JSONArray> sender=ids->{};
    Runnable changed=()->{};

    void watch(JSONArray records,long now){
        Set<String> next=new LinkedHashSet<>();
        for(int i=0;i<records.length();i++){
            JSONObject record=records.optJSONObject(i);
            if(record!=null&&!record.optString("id").isEmpty())next.add(record.optString("id"));
        }
        if(watched.equals(next))return;
        watched.clear();watched.addAll(next);
        online.keySet().retainAll(watched);updatedAt.keySet().retainAll(watched);
        refresh(now);changed.run();
    }
    void setConnected(boolean value,long now){
        if(connected==value)return;
        connected=value;online.clear();updatedAt.clear();awaitingSnapshot=true;
        refresh(now);changed.run();
    }
    boolean isConnected(){return connected;}
    boolean isOnline(String id,long now){
        Long time=updatedAt.get(id);
        return connected&&!awaitingSnapshot&&Boolean.TRUE.equals(online.get(id))&&time!=null&&now-time<MAX_AGE;
    }
    void receive(JSONObject message,long now){
        if(!connected)return;
        boolean snapshot="presence".equals(message.optString("type"));
        Map<String,Boolean> values=new HashMap<>();
        if(snapshot){
            JSONArray devices=message.optJSONArray("devices");if(devices==null)return;
            for(int i=0;i<devices.length();i++){JSONObject device=devices.optJSONObject(i);if(device!=null)read(device,values);}
        }else{
            if(!"presence_update".equals(message.optString("type"))||awaitingSnapshot)return;
            read(message,values);
        }
        Map<String,Boolean> previous=new HashMap<>(online);
        if(snapshot){online.clear();updatedAt.clear();awaitingSnapshot=false;}
        for(var entry:values.entrySet()){online.put(entry.getKey(),entry.getValue());updatedAt.put(entry.getKey(),now);}
        if(!previous.equals(online))changed.run();
    }
    private void read(JSONObject device,Map<String,Boolean> values){
        Object id=device.opt("id"),value=device.opt("online");
        if(id instanceof String&&watched.contains(id)&&value instanceof Boolean)values.put((String)id,(Boolean)value);
    }
    private void refresh(long now){
        if(!connected)return;
        lastRefresh=now;JSONArray ids=new JSONArray();for(String id:watched)ids.put(id);sender.accept(ids);
    }
    void maintain(long now){
        if(!connected)return;
        boolean expired=updatedAt.entrySet().removeIf(entry->{
            if(now-entry.getValue()<MAX_AGE)return false;
            online.remove(entry.getKey());return true;
        });
        if(expired)changed.run();
        if(now-lastRefresh>=REFRESH_INTERVAL)refresh(now);
    }
    JSONArray ordered(JSONArray records,long now){
        JSONArray result=new JSONArray();
        // Preserve history order within each group, including when all devices are offline.
        for(boolean available:new boolean[]{true,false})for(int i=0;i<records.length();i++){
            JSONObject record=records.optJSONObject(i);
            if(record!=null&&isOnline(record.optString("id"),now)==available)result.put(record);
        }
        return result;
    }
}
