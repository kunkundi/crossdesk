package cn.crossdesk.mobile;

import android.app.Dialog;
import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.*;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.function.Consumer;

/** The preset panel occupies the keyboard keybed; editing uses a separate local window. */
final class RemoteShortcutPanel {
    private final Context context;
    private final MobileUi ui;
    final RemoteShortcuts store;
    private final Consumer<RemoteShortcuts.Item> send;
    private final Runnable changed;
    private String platform="unknown";
    private int category,page;
    private Dialog manager;
    RemoteShortcutPanel(Context context,MobileUi ui,Consumer<RemoteShortcuts.Item> send,Runnable changed){this.context=context;this.ui=ui;this.send=send;this.changed=changed;store=new RemoteShortcuts(context);}
    void platform(String value){String next=RemoteShortcuts.platform(value);if(!platform.equals(next)){platform=next;page=0;close();}}
    private void hideEditor(){if(manager!=null&&manager.getWindow()!=null)context.getSystemService(android.view.inputmethod.InputMethodManager.class).hideSoftInputFromWindow(manager.getWindow().getDecorView().getWindowToken(),0);}
    void close(){if(manager!=null){hideEditor();manager.dismiss();manager=null;}}
    private boolean dark(){return (context.getResources().getConfiguration().uiMode&Configuration.UI_MODE_NIGHT_MASK)==Configuration.UI_MODE_NIGHT_YES;}
    private int ink(){return dark()?Color.WHITE:0xFF202124;}
    private int surface(){return dark()?0xFF202124:0xFFF1F3F6;}
    private TextView text(String value,int size){TextView text=ui.text(value,size,false);text.setTextColor(ink());return text;}
    private TextView button(String value,Runnable action){TextView button=ui.action(value,ink(),action);button.setTextSize(13);button.setMinHeight(ui.dp(44));button.setPadding(ui.dp(8),0,ui.dp(8),0);return button;}
    private void refresh(){changed.run();}
    View create(){
        LinearLayout root=ui.column();root.setPadding(ui.dp(6),0,ui.dp(6),0);
        String[] groups={"common","keys","system","mine"},labels={L10n.tr("常用"),L10n.tr("按键"),L10n.tr("系统"),L10n.tr("我的")};
        root.addView(new SegmentedControl(ui,labels,category,true,index->{category=index;page=0;refresh();}),new LinearLayout.LayoutParams(-1,ui.dp(36)));
        if(category==2&&platform.equals("linux")){
            Spinner desktop=spinner(new String[]{L10n.tr("选择桌面方案"),"GNOME","KDE Plasma"},store.desktop().equals("gnome")?1:store.desktop().equals("kde")?2:0,index->{String next=index==1?"gnome":index==2?"kde":"";if(!store.desktop().equals(next)){store.desktop(next);page=0;refresh();}});
            root.addView(desktop,new LinearLayout.LayoutParams(-1,ui.dp(40)));
        }
        List<RemoteShortcuts.Item> items=new ArrayList<>();for(RemoteShortcuts.Item item:store.items(platform))if(item.group.equals(groups[category]))items.add(item);
        int count=Math.max(1,(items.size()+5)/6);page=Math.min(page,count-1);
        ScrollView gridScroll=new ScrollView(context);gridScroll.setFillViewport(true);root.addView(gridScroll,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout grid=ui.column();grid.setMinimumHeight(ui.dp(items.isEmpty()?44:108));gridScroll.addView(grid,new FrameLayout.LayoutParams(-1,-2));
        if(items.isEmpty()){TextView empty=text(L10n.tr(platform.equals("unknown")?"等待远端平台信息":category==2?"选择桌面方案以加载系统预设":"还没有自定义快捷键"),13);empty.setGravity(Gravity.CENTER);grid.addView(empty,new LinearLayout.LayoutParams(-1,-1));}
        else for(int row=0;row<2;row++){
            LinearLayout line=ui.row();for(int col=0;col<3;col++){
                int index=page*6+row*3+col;LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(0,-1,1);params.setMargins(ui.dp(3),ui.dp(3),ui.dp(3),ui.dp(3));
                if(index>=items.size()){line.addView(new View(context),params);continue;}
                RemoteShortcuts.Item item=items.get(index);LinearLayout tile=ui.column();tile.setGravity(Gravity.CENTER);tile.setPadding(ui.dp(3),0,ui.dp(3),0);tile.setBackground(ui.background(dark()?0xFF353A43:Color.WHITE,7));
                TextView name=text(item.title(),13);name.setGravity(Gravity.CENTER);name.setMaxLines(2);tile.addView(name);TextView chord=text(item.chord(),11);chord.setGravity(Gravity.CENTER);chord.setMaxLines(2);tile.addView(chord);
                tile.setContentDescription(item.title()+", "+item.chord());tile.setClickable(true);tile.setFocusable(true);tile.setOnClickListener(v->send.accept(item));line.addView(tile,params);
            }grid.addView(line,new LinearLayout.LayoutParams(-1,0,1));
        }
        LinearLayout footer=ui.row();footer.setGravity(Gravity.CENTER_VERTICAL);
        if(count>1){TextView previous=button("‹",()->{page--;refresh();});previous.setEnabled(page>0);previous.setContentDescription(L10n.tr("上一页"));footer.addView(previous);footer.addView(text((page+1)+" / "+count,12));TextView next=button("›",()->{page++;refresh();});next.setEnabled(page+1<count);next.setContentDescription(L10n.tr("下一页"));footer.addView(next);}
        else footer.addView(text(RemoteShortcuts.platformName(platform),12));
        footer.addView(new View(context),new LinearLayout.LayoutParams(0,1,1));TextView edit=button(L10n.tr("管理与自定义"),this::manage);edit.setEnabled(!platform.equals("unknown"));footer.addView(edit);root.addView(footer,new LinearLayout.LayoutParams(-1,ui.dp(44)));
        return root;
    }
    private Spinner spinner(String[] labels,int selected,Consumer<Integer> changed){Spinner spinner=new Spinner(context);ArrayAdapter<String> adapter=new ArrayAdapter<>(context,android.R.layout.simple_spinner_dropdown_item,labels);spinner.setAdapter(adapter);spinner.setSelection(selected);spinner.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener(){public void onNothingSelected(android.widget.AdapterView<?> p){}public void onItemSelected(android.widget.AdapterView<?> p,View v,int position,long id){changed.accept(position);}});return spinner;}
    private LinearLayout screen(String title,Runnable back,String action,Runnable submit){
        LinearLayout root=ui.column();root.setBackgroundColor(surface());LinearLayout header=ui.row();header.setGravity(Gravity.CENTER_VERTICAL);header.addView(button(L10n.tr("返回"),back));TextView heading=text(title,17);header.addView(heading,new LinearLayout.LayoutParams(0,-2,1));if(action!=null)header.addView(button(action,submit));root.addView(header,new LinearLayout.LayoutParams(-1,ui.dp(52)));
        TextView note=text(L10n.tr("本地编辑，输入不会发送到远端"),12);note.setPadding(ui.dp(16),ui.dp(8),ui.dp(16),ui.dp(8));root.addView(note);return root;
    }
    private LinearLayout scrollBody(LinearLayout root){ScrollView scroll=new ScrollView(context);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));LinearLayout body=ui.column();body.setPadding(ui.dp(16),ui.dp(8),ui.dp(16),ui.dp(20));scroll.addView(body);return body;}
    private void present(View root){
        if(manager==null){manager=new Dialog(context);manager.requestWindowFeature(Window.FEATURE_NO_TITLE);manager.setOnDismissListener(d->{manager=null;refresh();});}
        hideEditor();manager.setContentView(root);manager.show();Window window=manager.getWindow();if(window!=null){window.setLayout(-1,-1);window.setBackgroundDrawableResource(android.R.color.transparent);window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);}
    }
    private void manage(){
        LinearLayout root=screen(L10n.tr("管理快捷键"),this::close,L10n.tr("新建"),()->edit(null));LinearLayout body=scrollBody(root);body.addView(text(RemoteShortcuts.platformName(platform)+" · "+L10n.tr("键盘上方常用栏"),14));
        List<String> selected=store.pins(platform);
        for(RemoteShortcuts.Item item:store.pinned(platform)){
            LinearLayout row=ui.row();row.setGravity(Gravity.CENTER_VERTICAL);TextView label=text(item.title()+"\n"+item.chord(),13);row.addView(label,new LinearLayout.LayoutParams(0,-2,1));
            TextView up=button("↑",()->{List<String> list=store.pins(platform);int index=list.indexOf(item.id);if(index>0){java.util.Collections.swap(list,index,index-1);store.pins(platform,list);manage();}});up.setContentDescription(L10n.tr("向上移动"));up.setEnabled(selected.indexOf(item.id)>0);row.addView(up);
            row.addView(button(L10n.tr("移除"),()->{List<String> list=store.pins(platform);list.remove(item.id);store.pins(platform,list);manage();}));body.addView(row);ui.divider(body);
        }
        ui.gap(body,16);body.addView(text(L10n.tr("添加到常用栏"),14));
        for(RemoteShortcuts.Item item:store.items(platform)){
            LinearLayout row=ui.row();row.setGravity(Gravity.CENTER_VERTICAL);TextView label=text(item.title()+"\n"+item.chord(),13);row.addView(label,new LinearLayout.LayoutParams(0,-2,1));
            if(!selected.contains(item.id))row.addView(button("＋",()->{List<String> list=store.pins(platform);if(list.size()>=RemoteShortcuts.MAX_PINNED){Toast.makeText(context,L10n.tr("最多固定 8 个快捷键"),Toast.LENGTH_SHORT).show();return;}list.add(item.id);store.pins(platform,list);manage();}));
            if(item.custom){row.addView(button(L10n.tr("编辑"),()->edit(item)));row.addView(button(L10n.tr("删除"),()->{store.remove(item.id);manage();}));}
            body.addView(row);
        }
        present(root);
    }
    private void edit(RemoteShortcuts.Item existing){
        EditText name=new EditText(context);name.setTextColor(ink());name.setSingleLine();name.setText(existing==null?"":existing.name);name.setHint(L10n.tr("名称"));name.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(64)});
        final String[] target={existing==null?platform:existing.platform};final int[] key={existing==null?83:existing.key};List<Integer> modifiers=new ArrayList<>();for(int code:existing==null?new int[]{platform.equals("macos")?91:17}:existing.modifiers)modifiers.add(code);
        CheckBox pin=new CheckBox(context);pin.setTextColor(ink());pin.setText(L10n.tr("固定到键盘上方"));pin.setChecked(existing==null?store.pins(target[0]).size()<RemoteShortcuts.MAX_PINNED:store.pins(existing.platform).contains(existing.id));
        TextView error=text("",13);error.setTextColor(0xFFE45757);TextView preview=text("",16);
        Runnable update=()->preview.setText(new RemoteShortcuts.Item("","",target[0],"mine",key[0],RemoteShortcuts.normalize(modifiers.stream().mapToInt(Integer::intValue).toArray()),false,true).chord());
        LinearLayout root=screen(L10n.tr(existing==null?"新建快捷键":"编辑快捷键"),this::manage,L10n.tr("保存"),()->{String result=store.save(existing==null?null:existing.id,name.getText().toString(),target[0],key[0],modifiers.stream().mapToInt(Integer::intValue).toArray(),pin.isChecked());if(result.isEmpty())manage();else error.setText(result);});
        LinearLayout body=scrollBody(root);body.addView(name,new LinearLayout.LayoutParams(-1,ui.dp(52)));ui.gap(body,12);
        LinearLayout modifierRow=ui.row();CheckBox[] toggles=new CheckBox[4];for(int i=0;i<4;i++){final int code=RemoteShortcuts.MODIFIERS[i];CheckBox toggle=new CheckBox(context);toggle.setTextColor(ink());toggle.setTextSize(12);toggle.setText(RemoteShortcuts.modifierName(code,target[0]));toggle.setChecked(modifiers.contains(code));toggle.setOnCheckedChangeListener((v,checked)->{if(checked){if(!modifiers.contains(code))modifiers.add(code);}else modifiers.remove(Integer.valueOf(code));update.run();});toggles[i]=toggle;modifierRow.addView(toggle,new LinearLayout.LayoutParams(0,ui.dp(48),1));}
        List<Integer> keys=RemoteShortcuts.keys();
        Spinner mainKey=spinner(keys.stream().map(code->RemoteShortcuts.keyName(code,target[0])).toArray(String[]::new),keys.indexOf(key[0]),index->{key[0]=keys.get(index);update.run();});
        body.addView(text(L10n.tr("适用远端"),13));body.addView(spinner(new String[]{"Windows","macOS","Linux"},Arrays.asList(RemoteShortcuts.PLATFORMS).indexOf(target[0]),index->{target[0]=RemoteShortcuts.PLATFORMS[index];mainKey.setAdapter(new ArrayAdapter<>(context,android.R.layout.simple_spinner_dropdown_item,keys.stream().map(code->RemoteShortcuts.keyName(code,target[0])).toArray(String[]::new)));mainKey.setSelection(keys.indexOf(key[0]));for(int i=0;i<4;i++)toggles[i].setText(RemoteShortcuts.modifierName(RemoteShortcuts.MODIFIERS[i],target[0]));update.run();}),new LinearLayout.LayoutParams(-1,ui.dp(48)));
        body.addView(text(L10n.tr("主按键"),13));body.addView(mainKey,new LinearLayout.LayoutParams(-1,ui.dp(48)));
        body.addView(text(L10n.tr("修饰键"),13));body.addView(modifierRow);body.addView(text(L10n.tr("发送组合"),13));body.addView(preview);body.addView(pin);body.addView(error);update.run();present(root);
    }
}
