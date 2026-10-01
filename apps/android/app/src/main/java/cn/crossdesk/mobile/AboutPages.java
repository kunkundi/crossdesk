package cn.crossdesk.mobile;

import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import java.util.List;
import static cn.crossdesk.mobile.MobileUi.*;

/** Mirrors the iOS About / component / source / document navigation. */
final class AboutPages {
    private final MainActivity activity;
    private final MobileUi ui;
    private final Runnable settings;
    private final LicenseCatalog catalog;

    AboutPages(MainActivity activity,MobileUi ui,Runnable settings){
        this.activity=activity;this.ui=ui;this.settings=settings;
        LicenseCatalog loaded;try{loaded=new LicenseCatalog(activity);}catch(Exception error){loaded=null;}catalog=loaded;
    }
    void show(){
        LinearLayout content=activity.formPage("about","关于 CrossDesk",settings);
        if(catalog==null){unavailable(content);return;}
        LinearLayout version=activity.section(content,"");value(version,"版本",applicationVersion());ui.gap(content,28);
        LinearLayout links=activity.section(content,"");
        activity.link(links,"软件许可","",()->component(catalog.find("crossdesk"),this::show));ui.divider(links);
        activity.link(links,"开源组件","",this::components);ui.divider(links);
        activity.link(links,"源码与构建说明","",this::source);
        activity.footer(content,"许可与版权声明可离线查阅。");
    }
    private String applicationVersion(){
        try{return activity.getPackageManager().getPackageInfo(activity.getPackageName(),0).versionName;}catch(android.content.pm.PackageManager.NameNotFoundException error){return "—";}
    }
    private void components(){
        LinearLayout content=activity.formPage("open-source","开源组件",this::show);
        LinearLayout notice=activity.section(content,"OpenFEC 告知");
        paragraph(notice,"本应用使用 OpenFEC 提供前向纠错功能，遵循 CeCILL-C 1.0 许可。该许可规定了有限保证及责任限制，详情见许可全文。",SECONDARY);
        ui.divider(notice);paragraph(notice,"(c) Copyright 2009 - 2012 INRIA - All rights reserved",INK);ui.divider(notice);
        activity.link(notice,"OpenFEC 许可与源码","",()->component(catalog.find("openfec"),this::components));ui.gap(content,28);
        LinearLayout list=activity.section(content,"");boolean first=true;
        for(LicenseCatalog.Component item:catalog.components){
            if(item.id.equals("crossdesk")||item.id.equals("openfec"))continue;
            if(!first)ui.divider(list);first=false;
            LinearLayout row=ui.row();row.setPadding(ui.dp(16),ui.dp(12),ui.dp(12),ui.dp(12));
            LinearLayout labels=ui.column();labels.addView(ui.text(item.name,16,false));ui.gap(labels,4);
            TextView license=ui.text(item.license,12,false);license.setTextColor(SECONDARY);labels.addView(license);
            row.addView(labels,new LinearLayout.LayoutParams(0,-2,1));TextView chevron=ui.text(" ›",23,false);chevron.setTextColor(0xFFB7B7BD);row.addView(chevron);
            row.setContentDescription(item.name+"，"+item.license);row.setClickable(true);row.setFocusable(true);row.setOnClickListener(v->component(item,this::components));list.addView(row);
        }
        activity.footer(content,"各组件遵循所列开源许可证，其授予的权利不受本应用普通使用条款限制。查看源码需要网络连接。");
    }
    private void component(LicenseCatalog.Component item,Runnable back){
        LinearLayout content=activity.formPage("license:"+item.id,item.name,back);LinearLayout info=activity.section(content,"");
        String version=item.id.equals("crossdesk")?applicationVersion():item.version.length()==40?item.version.substring(0,12):item.version;
        value(info,"版本",version);ui.divider(info);value(info,"许可",item.license);ui.divider(info);
        external(info,"查看源码",item.sourceURL);
        if(!item.buildDocuments.isEmpty()||!item.buildSourceURL.isEmpty()){
            ui.divider(info);
            if(!item.buildDocuments.isEmpty())activity.link(info,"构建配方与项目补丁","",()->buildSources(item,back));
            else external(info,"构建配方与项目补丁",item.buildSourceURL);
        }
        ui.gap(content,16);LinearLayout docs=activity.section(content,"许可与版权声明");
        for(int i=0;i<item.documents.size();i++){
            if(i>0)ui.divider(docs);LicenseCatalog.Document document=item.documents.get(i);
            activity.link(docs,item.documentTitle(document),"",()->document(item.documentTitle(document),document,()->component(item,back)));
        }
    }
    private void source(){
        LinearLayout content=activity.formPage("source","源码与构建说明",this::show);
        LinearLayout info=activity.section(content,"本版本源码与构建说明");
        if(catalog.source==null){paragraph(info,"本版本源码信息暂不可用。",SECONDARY);ui.divider(info);external(info,"项目主页","https://github.com/kunkundi/crossdesk");return;}
        String revision=catalog.source.optString("revision","");String tag=catalog.source.isNull("tag")?"":catalog.source.optString("tag","");
        value(info,"源码版本",tag.isEmpty()?revision.substring(0,Math.min(12,revision.length())):tag);ui.divider(info);
        if(catalog.source.optBoolean("isModified")){paragraph(info,"开发版本包含尚未发布的修改，以下链接为基础版本源码。",SECONDARY);ui.divider(info);}
        external(info,"查看源码",catalog.source.optString("sourceURL",catalog.find("crossdesk").sourceURL));ui.divider(info);
        try{
            List<LicenseCatalog.Document> documents=LicenseCatalog.documents(catalog.source.optJSONArray("buildDocuments"));
            if(!documents.isEmpty())actionLink(info,"构建说明",()->document("构建说明",documents.get(0),this::source));
            else external(info,"构建说明",catalog.source.optString("buildInstructionsURL","https://github.com/kunkundi/crossdesk"));
        }catch(org.json.JSONException error){paragraph(info,"本版本源码信息暂不可用。",SECONDARY);}
    }
    private void buildSources(LicenseCatalog.Component item,Runnable back){
        LinearLayout content=activity.formPage("build:"+item.id,"构建配方与项目补丁",()->component(item,back));
        if(!item.buildSourceURL.isEmpty()){LinearLayout upstream=activity.section(content,"");external(upstream,"查看源码",item.buildSourceURL);ui.gap(content,28);}
        LinearLayout docs=activity.section(content,"");
        for(int i=0;i<item.buildDocuments.size();i++){
            if(i>0)ui.divider(docs);LicenseCatalog.Document document=item.buildDocuments.get(i);
            activity.link(docs,document.title,"",()->document(document.title,document,()->buildSources(item,back)));
        }
    }
    private void document(String title,LicenseCatalog.Document document,Runnable back){
        try{activity.licenseDocument(title,LicenseCatalog.read(activity,document.asset),back);}catch(java.io.IOException error){unavailable(activity.formPage("license-error",title,back));}
    }
    private void value(LinearLayout parent,String label,String value){
        LinearLayout row=ui.row();row.setPadding(ui.dp(16),ui.dp(14),ui.dp(16),ui.dp(14));
        TextView key=ui.text(label,16,false);key.setPadding(0,0,ui.dp(16),0);row.addView(key);
        TextView content=ui.text(value,16,false);content.setTextColor(SECONDARY);content.setGravity(Gravity.END);content.setTextIsSelectable(true);row.addView(content,new LinearLayout.LayoutParams(0,-2,1));parent.addView(row);
    }
    private void paragraph(LinearLayout parent,String text,int color){TextView view=ui.text(text,13,false);view.setTextColor(color);view.setPadding(ui.dp(16),ui.dp(12),ui.dp(16),ui.dp(12));view.setLineSpacing(ui.dp(3),1);parent.addView(view);}
    private void external(LinearLayout parent,String title,String url){
        actionLink(parent,title,()->open(url));
    }
    private void actionLink(LinearLayout parent,String title,Runnable action){TextView link=ui.action(title,BLUE,action);link.setTypeface(android.graphics.Typeface.DEFAULT);link.setGravity(Gravity.START|Gravity.CENTER_VERTICAL);link.setPadding(ui.dp(16),ui.dp(12),ui.dp(16),ui.dp(12));parent.addView(link);}
    private void open(String url){
        Uri uri=Uri.parse(url);if(!"https".equals(uri.getScheme())&&!"http".equals(uri.getScheme()))return;
        try{activity.startActivity(new Intent(Intent.ACTION_VIEW,uri).addCategory(Intent.CATEGORY_BROWSABLE));}catch(ActivityNotFoundException error){Toast.makeText(activity,"未找到可打开链接的浏览器",Toast.LENGTH_SHORT).show();}
    }
    private void unavailable(LinearLayout content){
        content.setGravity(Gravity.CENTER);ui.gap(content,32);TextView icon=ui.text("▤",36,false);icon.setTextColor(SECONDARY);content.addView(icon);ui.gap(content,12);
        content.addView(ui.text("无法读取开源许可",17,true));TextView message=ui.text("应用中的许可文件缺失或损坏，请重新安装应用。",14,false);message.setGravity(Gravity.CENTER);message.setTextColor(SECONDARY);content.addView(message);
    }
}
