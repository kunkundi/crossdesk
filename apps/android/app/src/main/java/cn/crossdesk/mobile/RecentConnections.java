package cn.crossdesk.mobile;

import android.content.Context;
import android.content.SharedPreferences;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import java.util.Set;
import java.util.TreeSet;

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
        JSONObject item = find(list(server), id);
        return item != null && item.optBoolean("remember");
    }
    String fingerprint(String server,String id) {
        JSONObject item=find(list(server),id);
        String key=item==null?"":item.optString("spki");
        return key.matches("[0-9a-f]{64}")?key:"";
    }
    private String boundCredential(String server,String id,String ref) {
        return credential(server,id)+":domain-v1:"+ref;
    }
    String password(String server,String id)throws Exception {
        JSONObject item=find(list(server),id);
        if(!id.contains(":") || !NativeSession.isDomainEndpoint(id)) return secrets.get(credential(server,id));
        if(item==null || !item.optBoolean("remember") || item.optString("credentialRef").isEmpty())return "";
        String key=fingerprint(server,id);
        String value=secrets.get(boundCredential(server,id,item.getString("credentialRef")));
        return !key.isEmpty() && value.startsWith(key+":") && value.substring(65).matches("[A-Za-z0-9]{6}")
            ? value.substring(65) : "";
    }
    private void removeBound(String server,JSONObject item) {
        String ref=item.optString("credentialRef");
        if(!ref.isEmpty())secrets.remove(boundCredential(server,item.optString("id"),ref));
    }
    boolean contains(String server,String id){return find(list(server), id) != null;}
    static String displayName(JSONObject item) {
        String alias = item.optString("alias");
        return alias.isEmpty() ? item.optString("name", item.optString("id")) : alias;
    }
    static Set<String> groups(JSONObject item) {
        Set<String> names = new TreeSet<>();
        JSONArray groups = item.optJSONArray("groups");
        if (groups == null) return names;
        for (int i = 0; i < groups.length(); i++) {
            String name = groups.optString(i).trim();
            if (!name.isEmpty()) names.add(name);
        }
        return names;
    }
    static Set<String> availableGroups(JSONArray records) {
        Set<String> names = new TreeSet<>();
        for (int i = 0; i < records.length(); i++) {
            JSONObject item = records.optJSONObject(i);
            if (item != null) names.addAll(groups(item));
        }
        return names;
    }
    private JSONObject find(JSONArray records, String id) {
        for (int i = 0; i < records.length(); i++) {
            JSONObject item = records.optJSONObject(i);
            if (item != null && id.equals(item.optString("id"))) return item;
        }
        return null;
    }
    boolean deleteGroup(String server, String name) {
        if (name.isEmpty()) return true;
        JSONArray records = list(server);
        boolean changed = false;
        for (int i = 0; i < records.length(); i++) {
            JSONObject item = records.optJSONObject(i);
            if (item == null) continue;
            JSONArray groups = item.optJSONArray("groups");
            if (groups == null) continue;
            for (int j = groups.length() - 1; j >= 0; j--) {
                if (name.equals(groups.optString(j).trim())) {
                    groups.remove(j);
                    changed = true;
                }
            }
        }
        return !changed || preferences.edit().putString(key(server), records.toString()).commit();
    }

    void edit(String server, String id, String alias, Set<String> groups) {
        JSONArray records = list(server);
        JSONObject item = find(records, id);
        if (item == null) return;
        try {
            if (alias != null) item.put("alias", alias.trim());
            if (groups != null) item.put("groups", new JSONArray(groups));
        } catch (JSONException error) {
            throw new IllegalStateException(error);
        }
        preferences.edit().putString(key(server), records.toString()).apply();
    }
    void save(String server,String id,String name,String platform,boolean remember,String password)throws Exception{
        save(server,id,name,platform,remember,password,"");
    }
    void save(String server,String id,String name,String platform,boolean remember,String password,String pin)throws Exception{
        if(remember&&password.isEmpty())throw new IllegalArgumentException("Cannot remember an empty password");
        boolean domain=id.contains(":") && NativeSession.isDomainEndpoint(id);
        if(domain && remember && !pin.matches("[0-9a-f]{64}"))throw new IllegalStateException("Missing peer key");
        if(!domain){if(remember)secrets.put(credential(server,id),password);else secrets.remove(credential(server,id));}
        JSONArray old = list(server);
        JSONObject item = find(old, id);
        if (item == null) item = new JSONObject().put("id", id).put("name", id);
        String oldRef=item.optString("credentialRef");
        String newRef = "";
        if (domain && remember) {
            newRef = java.util.UUID.randomUUID().toString();
            secrets.put(boundCredential(server, id, newRef), pin + ":" + password);
            item.put("credentialRef", newRef).put("spki", pin);
        } else {
            item.remove("credentialRef");
            item.remove("spki");
        }
        if (!name.isEmpty() && !name.equals(id)) item.put("name", name);
        item.put("platform", platform).put("remember", remember);
        JSONArray result = new JSONArray().put(item);
        for (int i = 0; i < old.length() && result.length() < 20; i++) {
            JSONObject entry = old.optJSONObject(i);
            if (entry != null && !id.equals(entry.optString("id"))) result.put(entry);
        }
        if (!preferences.edit().putString(key(server), result.toString()).commit()) {
            if (!newRef.isEmpty()) secrets.remove(boundCredential(server, id, newRef));
            throw new IllegalStateException("Cannot save recent connection");
        }
        if (!oldRef.isEmpty()) secrets.remove(boundCredential(server, id, oldRef));
        for (int i = 0; i < old.length(); i++) {
            JSONObject dropped = old.optJSONObject(i);
            if (dropped != null && find(result, dropped.optString("id")) == null) {
                removeBound(server, dropped);
                RemotePreviewStore.get(context).remove(server, dropped.optString("id"));
            }
        }
    }
    void metadata(String server,String id,String name,String platform){
        JSONArray all=list(server);
        JSONObject item = find(all, id);
        if (item == null) return;
        try {
            if (!name.trim().isEmpty()) item.put("name", name.trim());
            item.put("platform", platform);
        } catch (JSONException ignored) { return; }
        preferences.edit().putString(key(server),all.toString()).apply();
    }
    void delete(String server,String id){
        JSONArray result=new JSONArray(),old=list(server);for(int i=0;i<old.length();i++){JSONObject entry=old.optJSONObject(i);if(entry!=null&&!id.equals(entry.optString("id")))result.put(entry);}
        if(!preferences.edit().putString(key(server),result.toString()).commit())return;
        JSONObject removed=find(old,id);if(removed!=null)removeBound(server,removed);
        secrets.remove(credential(server,id));
        RemotePreviewStore.get(context).remove(server,id);
    }
}
