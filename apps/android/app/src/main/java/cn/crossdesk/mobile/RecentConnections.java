package cn.crossdesk.mobile;

import android.content.Context;
import android.content.SharedPreferences;
import org.json.JSONArray;
import org.json.JSONObject;

/** History and encrypted passwords are isolated by server; history never contains secrets. */
final class RecentConnections {
    private final SharedPreferences preferences;
    private final SecretStore secrets;
    private final Context context;
    RecentConnections(Context context){this.context=context.getApplicationContext();preferences=context.getSharedPreferences("settings",Context.MODE_PRIVATE);secrets=new SecretStore(context);}
    private String key(String server){return "recentList:"+server;}
    private String credential(String server,String id){return "remote-password:"+server+":"+id;}
    JSONArray list(String server){
        try {
            if(!preferences.contains(key(server))){
                String legacy=preferences.getString("recent:"+server,"");
                if(!legacy.isEmpty())return new JSONArray().put(new JSONObject().put("id",legacy).put("name",legacy));
            }
            return new JSONArray(preferences.getString(key(server),"[]"));
        }
        catch(org.json.JSONException error){return new JSONArray();}
    }
    boolean remembersPassword(String server,String id){
        JSONArray records=list(server);
        for(int i=0;i<records.length();i++){JSONObject item=records.optJSONObject(i);if(item!=null&&id.equals(item.optString("id")))return item.optBoolean("remember");}
        return false;
    }
    String password(String server,String id)throws Exception{return secrets.get(credential(server,id));}
    boolean contains(String server,String id){JSONArray all=list(server);for(int i=0;i<all.length();i++){JSONObject item=all.optJSONObject(i);if(item!=null&&id.equals(item.optString("id")))return true;}return false;}
    void save(String server,String id,String name,String platform,boolean remember,String password)throws Exception{
        if(remember&&password.isEmpty())throw new IllegalArgumentException("Cannot remember an empty password");
        if(remember)secrets.put(credential(server,id),password);else secrets.remove(credential(server,id));
        JSONObject item=new JSONObject().put("id",id).put("name",name.isEmpty()?id:name).put("platform",platform).put("remember",remember);
        JSONArray result=new JSONArray().put(item),old=list(server);
        for(int i=0;i<old.length();i++){JSONObject entry=old.optJSONObject(i);if(entry!=null&&!id.equals(entry.optString("id"))){if(result.length()<20)result.put(entry);else RemotePreviewStore.get(context).remove(server,entry.optString("id"));}}
        preferences.edit().putString(key(server),result.toString()).apply();
    }
    void metadata(String server,String id,String name,String platform){
        JSONArray all=list(server);
        try{for(int i=0;i<all.length();i++){JSONObject item=all.optJSONObject(i);if(item!=null&&id.equals(item.optString("id"))){item.put("name",name.isEmpty()?id:name);item.put("platform",platform);}}}
        catch(org.json.JSONException ignored){return;}
        preferences.edit().putString(key(server),all.toString()).apply();
    }
    void delete(String server,String id){
        JSONArray result=new JSONArray(),old=list(server);for(int i=0;i<old.length();i++){JSONObject entry=old.optJSONObject(i);if(entry!=null&&!id.equals(entry.optString("id")))result.put(entry);}
        preferences.edit().putString(key(server),result.toString()).apply();secrets.remove(credential(server,id));
        RemotePreviewStore.get(context).remove(server,id);
    }
}
