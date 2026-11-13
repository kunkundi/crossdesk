package cn.crossdesk.mobile;

import android.content.Context;
import android.content.SharedPreferences;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;

/** Local controller preferences. Key codes use the existing cross-platform wire mapping. */
final class RemoteShortcuts {
    static final int MAX_CUSTOM=16,MAX_PINNED=8;
    static final String[] PLATFORMS={"windows","macos","linux"};
    static final int[] MODIFIERS={17,16,18,91};
    static final class Item {
        final String id,name,platform,group;
        final int key;
        final int[] modifiers;
        final boolean service,custom;
        Item(String id,String name,String platform,String group,int key,int[] modifiers,boolean service,boolean custom){
            this.id=id;this.name=name;this.platform=platform;this.group=group;this.key=key;
            this.modifiers=modifiers.clone();this.service=service;this.custom=custom;
        }
        String title(){return custom?name:L10n.tr(name);}
        String chord(){List<String> parts=new ArrayList<>();for(int modifier:modifiers)parts.add(modifierName(modifier,platform));parts.add(keyName(key,platform));return String.join(" + ",parts);}
    }
    private final SharedPreferences preferences;
    RemoteShortcuts(Context context){preferences=context.getSharedPreferences("remote_shortcuts_v1",Context.MODE_PRIVATE);}
    static String platform(String value){return Arrays.asList(PLATFORMS).contains(value)?value:"unknown";}
    static String platformName(String value){return switch(value){case "windows"->"Windows";case "macos"->"macOS";case "linux"->"Linux";default->L10n.tr("未知");};}
    String desktop(){String value=preferences.getString("linux_desktop","");return value.equals("gnome")||value.equals("kde")?value:"";}
    void desktop(String value){preferences.edit().putString("linux_desktop",value.equals("gnome")||value.equals("kde")?value:"").apply();}
    static String modifierName(int code,String platform){return switch(code){case 17->"Ctrl";case 16->"Shift";case 18->platform.equals("macos")?"Option":"Alt";case 91->platform.equals("macos")?"Cmd":platform.equals("linux")?"Super":"Win";default->"";};}
    static String keyName(int code,String platform){
        if(code>=65&&code<=90||code>=48&&code<=57)return Character.toString((char)code);
        if(code>=112&&code<=123)return "F"+(code-111);
        return switch(code){case 8->platform.equals("macos")?"Delete ⌫":"Backspace";case 9->"Tab";case 13->"Enter";case 27->"Esc";case 32->"Space";case 33->"PageUp";case 34->"PageDown";case 35->"End";case 36->"Home";case 37->"←";case 38->"↑";case 39->"→";case 40->"↓";case 45->"Insert";case 46->platform.equals("macos")?"Delete ⌦":"Delete";default->"";};
    }
    static List<Integer> keys(){List<Integer> result=new ArrayList<>();for(int i=65;i<=90;i++)result.add(i);for(int i=48;i<=57;i++)result.add(i);for(int i=112;i<=123;i++)result.add(i);for(int i:new int[]{27,9,13,32,8,46,45,36,35,33,34,37,38,39,40})result.add(i);return result;}
    static int[] normalize(int[] modifiers){return Arrays.stream(MODIFIERS).filter(code->Arrays.stream(modifiers).anyMatch(value->value==code)).toArray();}
    private static Item preset(String id,String name,String platform,String group,int key,int... modifiers){return new Item(id,name,platform,group,key,modifiers,false,false);}
    List<Item> items(String remote){
        String os=platform(remote);List<Item> result=new ArrayList<>();
        if(!os.equals("unknown")){
            int primary=os.equals("macos")?91:17;
            String[] ids={"copy","paste","cut","all","undo","save"},names={"复制","粘贴","剪切","全选","撤销","保存"};int[] keys={67,86,88,65,90,83};
            for(int i=0;i<ids.length;i++)result.add(preset(ids[i],names[i],os,"common",keys[i],primary));
            if(os.equals("linux")){result.add(preset("term-copy","终端复制",os,"common",67,17,16));result.add(preset("term-paste","终端粘贴",os,"common",86,17,16));}
        }
        for(int key:new int[]{27,9,13,8,46,36,35,33,34,37,38,39,40,112,113,114,115,116,117,118,119,120,121,122,123})
            result.add(preset(key==27?"esc":key==9?"tab":"key-"+key,keyName(key,os),os,"keys",key));
        if(os.equals("windows")){
            result.add(preset("switch","切换窗口",os,"system",9,18));result.add(preset("desktop","显示桌面",os,"system",68,91));result.add(preset("task","任务管理器",os,"system",27,17,16));
            result.add(new Item("sas","安全选项",os,"system",46,new int[]{17,18},true,false));
        }else if(os.equals("macos")){
            result.add(preset("switch","切换应用",os,"system",9,91));result.add(preset("search","聚焦搜索",os,"system",32,91));result.add(preset("forcequit","强制退出面板",os,"system",27,18,91));
        }else if(os.equals("linux")&&!desktop().isEmpty()){
            result.add(preset("switch",desktop().equals("gnome")?"切换应用":"切换窗口",os,"system",9,desktop().equals("gnome")?91:18));result.add(preset("run","运行窗口",os,"system",113,18));
            result.add(desktop().equals("gnome")?preset("apps","应用列表",os,"system",65,91):preset("window-menu","窗口菜单",os,"system",114,18));
        }
        for(Item item:custom())if(item.platform.equals(os))result.add(item);
        return result;
    }
    List<Item> custom(){
        List<Item> result=new ArrayList<>();
        try{JSONArray entries=new JSONArray(preferences.getString("custom","[]"));for(int i=0;i<entries.length()&&result.size()<MAX_CUSTOM;i++){
            JSONObject entry=entries.optJSONObject(i);if(entry==null)continue;String os=platform(entry.optString("platform")),id=entry.optString("id"),name=entry.optString("name").trim();int key=entry.optInt("key");JSONArray modifiers=entry.optJSONArray("modifiers");
            if(os.equals("unknown")||!id.startsWith("custom-")||name.isEmpty()||name.length()>64||!keys().contains(key)||modifiers==null||modifiers.length()>4)continue;
            int[] values=new int[modifiers.length()];boolean valid=true;for(int j=0;j<values.length;j++){int value=modifiers.optInt(j);values[j]=value;if(!Arrays.stream(MODIFIERS).anyMatch(code->code==value))valid=false;}
            if(!valid)continue;int[] normalized=normalize(values);if(os.equals("windows")&&key==46&&Arrays.stream(normalized).anyMatch(v->v==17)&&Arrays.stream(normalized).anyMatch(v->v==18))continue;
            if(result.stream().anyMatch(item->item.id.equals(id)))continue;
            result.add(new Item(id,name,os,"mine",key,normalized,false,true));
        }}catch(Exception ignored){ }
        return result;
    }
    List<String> pins(String os){
        List<String> result=new ArrayList<>();String stored=preferences.getString("pins_"+platform(os),null);
        if(stored==null)stored="[\"esc\",\"tab\",\"copy\",\"paste\"]";
        try{JSONArray array=new JSONArray(stored);for(int i=0;i<array.length()&&result.size()<MAX_PINNED;i++){String id=array.optString(i);if(!id.isEmpty()&&!result.contains(id))result.add(id);}}catch(Exception ignored){result.addAll(Arrays.asList("esc","tab","copy","paste"));}
        List<Item> available=items(os);result.removeIf(id->available.stream().noneMatch(item->item.id.equals(id)));
        return result;
    }
    List<Item> pinned(String os){List<Item> all=items(os),result=new ArrayList<>();for(String id:pins(os))for(Item item:all)if(item.id.equals(id)){result.add(item);break;}return result;}
    void pins(String os,List<String> values){preferences.edit().putString("pins_"+platform(os),new JSONArray(values.subList(0,Math.min(values.size(),MAX_PINNED))).toString()).apply();}
    void remove(String id){List<Item> values=custom();values.removeIf(item->item.id.equals(id));write(values);for(String os:PLATFORMS){List<String> selected=pins(os);selected.remove(id);pins(os,selected);}}
    String save(String id,String name,String os,int key,int[] modifiers,boolean pin){
        name=name.trim();if(name.isEmpty()||name.length()>64)return L10n.tr("请输入有效的快捷键名称");
        if(platform(os).equals("unknown")||!keys().contains(key))return L10n.tr("请选择有效的按键");
        int[] normalized=normalize(modifiers);if(os.equals("windows")&&key==46&&Arrays.stream(normalized).anyMatch(v->v==17)&&Arrays.stream(normalized).anyMatch(v->v==18))return L10n.tr("请使用系统预设中的安全选项");
        List<Item> values=custom();boolean exists=false;for(Item item:values){if(item.id.equals(id))exists=true;else if(item.platform.equals(os)&&item.key==key&&Arrays.equals(item.modifiers,normalized))return L10n.tr("已有相同组合的快捷键");}
        if(!exists&&values.size()>=MAX_CUSTOM)return L10n.tr("最多保存 16 个自定义快捷键");
        List<String> selected=pins(os);if(pin&&!selected.contains(id)&&selected.size()>=MAX_PINNED)return L10n.tr("最多固定 8 个快捷键");
        String itemId=id==null?"custom-"+UUID.randomUUID():id;values.removeIf(item->item.id.equals(itemId));values.add(new Item(itemId,name,os,"mine",key,normalized,false,true));write(values);
        for(String target:PLATFORMS){List<String> list=pins(target);int index=list.indexOf(itemId);list.remove(itemId);if(target.equals(os)&&pin)list.add(index>=0?Math.min(index,list.size()):list.size(),itemId);pins(target,list);}return "";
    }
    private void write(List<Item> items){JSONArray array=new JSONArray();try{for(Item item:items){JSONArray modifiers=new JSONArray();for(int code:item.modifiers)modifiers.put(code);array.put(new JSONObject().put("id",item.id).put("name",item.name).put("platform",item.platform).put("key",item.key).put("modifiers",modifiers));}}catch(Exception ignored){return;}preferences.edit().putString("custom",array.toString()).apply();}
}
