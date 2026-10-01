package cn.crossdesk.mobile;

import android.content.Context;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** Offline catalog; document bodies are loaded only when opened. */
final class LicenseCatalog {
    static final class Document {
        final String title,asset;
        Document(JSONObject value)throws JSONException{
            title=value.getString("title");asset=value.getString("asset");
            if(!asset.startsWith("about/")||asset.contains(".."))throw new JSONException("Invalid asset");
        }
    }
    static final class Component {
        final String id,name,version,license,sourceURL,buildSourceURL;
        final List<Document> documents,buildDocuments;
        Component(JSONObject value)throws JSONException{
            id=value.getString("id");name=value.getString("name");version=value.getString("version");license=value.getString("license");
            sourceURL=value.getString("sourceURL");buildSourceURL=value.optString("buildSourceURL","");
            documents=documents(value.getJSONArray("documents"));buildDocuments=documents(value.optJSONArray("buildDocuments"));
            if(documents.isEmpty())throw new JSONException("Missing documents");
        }
        String documentTitle(Document document){
            if(document.title.equals("Source copyright and license notices"))return "版权与许可声明";
            if(document.title.equals("apps/android/licenses/OPEN_SOURCE_NOTICE.txt"))return "开源软件权利说明";
            if(id.equals("crossdesk")&&document.title.equals("LICENSE"))return "GNU GPL 第 3 版";
            return document.title;
        }
    }
    final List<Component> components=new ArrayList<>();
    final JSONObject source;
    LicenseCatalog(Context context)throws IOException,JSONException{
        JSONObject data=new JSONObject(read(context,"ThirdPartyLicenses.json"));
        if(data.getInt("schemaVersion")!=1)throw new JSONException("Unknown catalog");
        JSONArray entries=data.getJSONArray("components");
        for(int i=0;i<entries.length();i++)components.add(new Component(entries.getJSONObject(i)));
        if(find("crossdesk")==null||find("openfec")==null)throw new JSONException("Incomplete catalog");
        JSONObject metadata;
        try{metadata=new JSONObject(read(context,"SourceMetadata.json"));if(metadata.getInt("schemaVersion")!=1)metadata=null;}catch(IOException|JSONException error){metadata=null;}
        source=metadata;
    }
    Component find(String id){for(Component component:components)if(component.id.equals(id))return component;return null;}
    static List<Document> documents(JSONArray values)throws JSONException{
        List<Document> result=new ArrayList<>();if(values!=null)for(int i=0;i<values.length();i++)result.add(new Document(values.getJSONObject(i)));return result;
    }
    static String read(Context context,String asset)throws IOException{
        try(var input=context.getAssets().open(asset);var output=new ByteArrayOutputStream()){
            byte[] buffer=new byte[8192];int count;while((count=input.read(buffer))!=-1)output.write(buffer,0,count);return output.toString("UTF-8");
        }
    }
}
