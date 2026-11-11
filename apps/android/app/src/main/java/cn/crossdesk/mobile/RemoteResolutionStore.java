package cn.crossdesk.mobile;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;
import org.json.JSONArray;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Locale;

/** Successful selections, scoped to the signaling server, device and native screen. */
final class RemoteResolutionStore {
    private final SharedPreferences preferences;
    RemoteResolutionStore(Context context) {
        preferences=context.getSharedPreferences("remote_display_resolutions",Context.MODE_PRIVATE);
    }
    private String key(String host,int port,String remote) {
        return new JSONArray().put(host.trim().toLowerCase(Locale.ROOT)).put(port).put(remote).toString();
    }
    private static boolean validKey(String key) {
        return !key.isEmpty()&&key.getBytes(StandardCharsets.UTF_8).length<=1024&&key.indexOf('\0')<0;
    }
    private static int dimension(JSONObject value,String key)throws Exception {
        Object raw=value.get(key);
        if(!(raw instanceof Integer)&&!(raw instanceof Long))throw new IllegalArgumentException();
        long number=((Number)raw).longValue();
        if(number<1||number>32768)throw new IllegalArgumentException();
        return (int)number;
    }
    synchronized JSONArray load(String host,int port,String remote) {
        if(remote.isEmpty())return new JSONArray();
        try {
            String text=preferences.getString(key(host,port,remote),"[]");
            if(text==null||text.length()>65536)return new JSONArray();
            JSONArray values=new JSONArray(text);
            if(values.length()>32)return new JSONArray();
            HashSet<String> keys=new HashSet<>();
            JSONArray result=new JSONArray();
            for(int i=0;i<values.length();i++) {
                JSONObject value=values.getJSONObject(i);
                Object identity=value.get("display_key");
                if(!(identity instanceof String)||!validKey((String)identity)||!keys.add((String)identity))return new JSONArray();
                result.put(new JSONObject().put("display_key",identity).put("width",dimension(value,"width")).put("height",dimension(value,"height")));
            }
            return result;
        } catch(Exception ignored) { return new JSONArray(); }
    }
    synchronized void remember(String host,int port,String remote,String displayKey,RemoteDisplayResolutions.Mode mode) {
        if(remote.isEmpty()||!validKey(displayKey)||mode==null||mode.width<1||mode.width>32768||mode.height<1||mode.height>32768)return;
        try {
            JSONArray saved=load(host,port,remote),updated=new JSONArray();
            for(int i=0;i<saved.length();i++) {
                JSONObject value=saved.getJSONObject(i);
                if(!displayKey.equals(value.getString("display_key")))updated.put(value);
            }
            if(updated.length()>=32)updated.remove(0);
            updated.put(new JSONObject().put("display_key",displayKey).put("width",mode.width).put("height",mode.height));
            preferences.edit().putString(key(host,port,remote),updated.toString()).apply();
        } catch(Exception error) { Log.w("CrossDesk","Could not save remote display resolution",error); }
    }
}
