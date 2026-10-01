package cn.crossdesk.mobile;

import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.Test;
import java.util.ArrayList;
import static org.junit.Assert.*;

public class RecentConnectionPresenceTest {
    private static JSONArray records(String... ids)throws Exception{
        JSONArray result=new JSONArray();for(String id:ids)result.put(new JSONObject().put("id",id));return result;
    }
    private static JSONObject device(String id,boolean online)throws Exception{return new JSONObject().put("id",id).put("online",online);}
    private static JSONObject snapshot(JSONObject... devices)throws Exception{
        JSONArray values=new JSONArray();for(JSONObject device:devices)values.put(device);return new JSONObject().put("type","presence").put("devices",values);
    }
    private static JSONObject update(String id,boolean online)throws Exception{return device(id,online).put("type","presence_update");}

    @Test public void snapshotAndUpdatesOnlyAffectWatchedDevicesAndSortStably()throws Exception{
        RecentConnectionPresence presence=new RecentConnectionPresence();JSONArray history=records("111111111","222222222","333333333");
        presence.watch(history,0);presence.setConnected(true,0);
        presence.receive(update("111111111",true),1);assertFalse(presence.isOnline("111111111",1));
        presence.receive(snapshot(device("222222222",true),device("333333333",true),device("999999999",true)),2);
        assertFalse(presence.isOnline("999999999",2));assertTrue(presence.isOnline("222222222",2));
        JSONArray ordered=presence.ordered(history,2);assertEquals("222222222",ordered.getJSONObject(0).getString("id"));assertEquals("333333333",ordered.getJSONObject(1).getString("id"));
        presence.receive(update("222222222",false),3);assertFalse(presence.isOnline("222222222",3));
        assertEquals("333333333",presence.ordered(history,3).getJSONObject(0).getString("id"));
        presence.receive(snapshot(device("111111111",true)),4);assertFalse("A full snapshot replaces missing devices",presence.isOnline("333333333",4));
    }

    @Test public void refreshExpiryAndReconnectRequireFreshSnapshot()throws Exception{
        RecentConnectionPresence presence=new RecentConnectionPresence();ArrayList<JSONArray> requests=new ArrayList<>();presence.sender=requests::add;
        presence.watch(records("111111111"),0);assertTrue(requests.isEmpty());presence.setConnected(true,0);assertEquals(1,requests.size());
        presence.receive(snapshot(device("111111111",true)),0);presence.maintain(29_999);assertEquals(1,requests.size());
        presence.maintain(30_000);assertEquals(2,requests.size());assertTrue(presence.isOnline("111111111",59_999));
        presence.maintain(60_000);assertEquals(3,requests.size());assertFalse(presence.isOnline("111111111",60_000));
        presence.receive(update("111111111",true),60_001);assertTrue(presence.isOnline("111111111",60_001));
        presence.setConnected(false,60_002);presence.receive(snapshot(device("111111111",true)),60_003);presence.maintain(120_000);assertEquals(3,requests.size());assertFalse(presence.isOnline("111111111",120_000));
        presence.setConnected(true,120_001);assertEquals(4,requests.size());presence.receive(update("111111111",true),120_002);assertFalse(presence.isOnline("111111111",120_002));
        presence.receive(snapshot(device("111111111",true)),120_003);assertTrue(presence.isOnline("111111111",120_003));
    }

    @Test public void changedHistoryResubscribesAndMalformedStatesDoNotBecomeOnline()throws Exception{
        RecentConnectionPresence presence=new RecentConnectionPresence();ArrayList<JSONArray> requests=new ArrayList<>();presence.sender=requests::add;
        presence.watch(records("111111111","111111111"),0);presence.setConnected(true,0);assertEquals(1,requests.get(0).length());
        presence.receive(snapshot(device("111111111",true)),1);presence.watch(records("222222222"),2);assertEquals(2,requests.size());assertFalse(presence.isOnline("111111111",2));
        presence.receive(snapshot(new JSONObject().put("id","222222222").put("online","true")),3);assertFalse(presence.isOnline("222222222",3));
        presence.receive(snapshot(device("222222222",true)),4);presence.receive(new JSONObject().put("type","presence").put("devices","invalid"),5);assertTrue(presence.isOnline("222222222",5));
        presence.watch(records(),6);assertEquals(0,requests.get(2).length());assertFalse(presence.isOnline("222222222",6));
    }
}
