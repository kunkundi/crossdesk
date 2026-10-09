package cn.crossdesk.mobile;

import android.content.Context;
import android.graphics.Color;
import android.view.*;
import android.widget.*;
import static cn.crossdesk.mobile.MobileUi.*;

/** Safe-area overlay: a draggable 54 dp orb, an adaptive menu and a floating keyboard. */
@android.annotation.SuppressLint("ViewConstructor")
final class SessionControls extends FrameLayout {
    interface Actions {
        void disconnect(); void display(); void clipboard(); void key(int code);
        void chord(int code,int[] modifiers); void shortcut(int code); void text(String value); void mute(); void mouse();
        void videoSettings(int field,int value);
        void sendFile(); void saveFile(); void secureAttention();
    }
    private final MobileUi ui;
    private final Actions actions;
    private final View orb;
    private final LinearLayout waiting;
    private View shield;
    private LinearLayout menu,keyboard;
    private VirtualMouseBar mouseBar;
    private FrameLayout mouseHost;
    private boolean mouseVisible;
    private ScrollView keyboardKeys;
    private ScrollView menuScroll;
    private int keyboardPreferredHeight;
    private int menuColumns=3;
    private TextView networkLabel,videoFeedback;
    private TextView transferLabel,saveFileButton;
    private String transferStatus="";
    private boolean hasReceivedFile;
    private String menuPage="controls";
    private RemoteNetworkStatistics.Snapshot network=new RemoteNetworkStatistics().snapshot(0);
    private RemoteVideoSettings settings=new RemoteVideoSettings(1);
    private final java.util.List<SettingRow> settingRows=new java.util.ArrayList<>();
    private TextView[][] trafficCells;
    private LinearLayout[] trafficRows;
    private TextView[] networkDetails;
    private View encryptionLock,encryptionOpen;
    private boolean muted,relative=true,shift,computerKeyboard,orbPositioned;
    private final java.util.Set<Integer> modifiers=new java.util.LinkedHashSet<>();
    SessionControls(Context context,MobileUi ui,Actions actions){
        super(context);this.ui=ui;this.actions=actions;
        // This container must remain transparent to remote touch outside its controls.
        waiting=ui.column();waiting.setMinimumWidth(ui.dp(200));waiting.setGravity(Gravity.CENTER);waiting.setPadding(ui.dp(20),ui.dp(20),ui.dp(20),ui.dp(20));waiting.setBackground(ui.background(0xB0202024,14));
        ProgressBar progress=new ProgressBar(context);progress.setIndeterminateTintList(android.content.res.ColorStateList.valueOf(Color.WHITE));waiting.addView(progress,ui.size(28,28));ui.gap(waiting,12);TextView message=ui.text("等待远端画面…",14,false);message.setTextColor(Color.WHITE);message.setGravity(Gravity.CENTER);waiting.addView(message);addView(waiting,new LayoutParams(-2,-2,Gravity.CENTER));
        orb=ui.iconButton("display","展开远程控制菜单",()->{if(menu==null)openMenu("controls");else closeMenu();});
        ImageView brand=new ImageView(context);brand.setImageResource(R.drawable.mobile_brand);brand.setScaleType(ImageView.ScaleType.CENTER_CROP);brand.setBackground(ui.background(Color.WHITE,23));brand.setClipToOutline(true);brand.setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        ((FrameLayout)orb).removeAllViews();((FrameLayout)orb).addView(brand,new LayoutParams(ui.dp(46),ui.dp(46),Gravity.CENTER));
        orb.setBackground(ui.background(0xFFF5F5F7,27));orb.setElevation(ui.dp(7));addView(orb,new LayoutParams(ui.dp(54),ui.dp(54)));
        drag(orb,orb,()->{if(menu==null)openMenu("controls");else closeMenu();});
    }
    void setRelative(boolean value){relative=value;if(menu!=null&&menuPage.equals("controls"))openMenu("controls");}
    void attachMouseBar(FrameLayout host, VirtualMouseBar bar) {
        mouseHost=host;mouseBar=bar;
        // A direct, tightly bounded sibling of the video is essential: otherwise
        // an existing touch in this full-screen controls container captures the
        // second finger before Android can dispatch it to the remote surface.
        host.addView(bar,new FrameLayout.LayoutParams(ui.dp(140),ui.dp(48)));
        bar.setVisibility(GONE);
    }
    void cancelMouseInput(){if(mouseBar!=null)mouseBar.cancelInput();}
    private void toggleMouse(){cancelMouseInput();mouseVisible=!mouseVisible;closeMenu();positionMouse();}
    private void positionMouse(){
        if(mouseBar==null)return;
        boolean visible=mouseVisible&&menu==null&&waiting.getVisibility()==GONE;
        if(!visible){if(mouseBar.getVisibility()==VISIBLE)cancelMouseInput();mouseBar.setVisibility(GONE);return;}
        int[] location=new int[2],hostLocation=new int[2];getLocationOnScreen(location);mouseHost.getLocationOnScreen(hostLocation);
        float width=ui.dp(140),height=ui.dp(48),gap=ui.dp(6),margin=ui.dp(8);
        float x=orb.getX()>=width+gap+margin?orb.getX()-gap-width:orb.getX()+orb.getWidth()+gap;
        float y=orb.getY()+(orb.getHeight()-height)/2;
        // When the orb is near the middle of a narrow phone, use the adjacent
        // row rather than letting the two controls overlap.
        if(x+width>getWidth()-margin){x=bound(orb.getX(),margin,getWidth()-width-margin);y=orb.getY()+orb.getHeight()+gap;if(y+height>getHeight()-margin)y=orb.getY()-gap-height;}
        if(keyboard!=null&&x<keyboard.getX()+keyboard.getWidth()&&x+width>keyboard.getX()&&y<keyboard.getY()+keyboard.getHeight()&&y+height>keyboard.getY()){
            y=keyboard.getY()-gap-height;if(y<margin)y=keyboard.getY()+keyboard.getHeight()+gap;
        }
        mouseBar.setX(location[0]-hostLocation[0]+bound(x,margin,getWidth()-width-margin));
        mouseBar.setY(location[1]-hostLocation[1]+bound(y,margin,getHeight()-height-margin));
        mouseBar.setVisibility(VISIBLE);
    }
    void setMuted(boolean value){muted=value;if(menu!=null&&menuPage.equals("controls"))openMenu("controls");}
    void videoSize(int width,int height){waiting.setVisibility(GONE);positionMouse();}
    void resetVideoStatistics(){statistics(new RemoteNetworkStatistics.Snapshot(network.report,0,0,0,Double.NaN,network.rtt));}
    void statistics(RemoteNetworkStatistics.Snapshot value){network=value;renderNetwork();}
    void videoSettings(RemoteVideoSettings value){settings=value;renderVideoSettings();}
    void fileStatus(String status,boolean received){transferStatus=status;hasReceivedFile=received;renderFileStatus();}
    private void renderFileStatus(){
        if(transferLabel!=null){transferLabel.setText(transferStatus);transferLabel.setVisibility(transferStatus.isEmpty()?GONE:VISIBLE);}
        if(saveFileButton!=null)saveFileButton.setVisibility(hasReceivedFile?VISIBLE:GONE);
    }
    private void closeMenu(){if(menu!=null){removeView(menu);menu=null;menuScroll=null;}if(shield!=null){removeView(shield);shield=null;}networkLabel=videoFeedback=transferLabel=saveFileButton=null;trafficCells=null;trafficRows=null;networkDetails=null;encryptionLock=encryptionOpen=null;settingRows.clear();orb.setContentDescription("展开远程控制菜单");positionMouse();}
    private void openMenu(String page){
        cancelMouseInput();
        closeMenu();menuPage=page;shield=new View(getContext());shield.setOnClickListener(v->closeMenu());addView(shield,new LayoutParams(-1,-1));
        menu=ui.column();menu.setPadding(ui.dp(12),ui.dp(12),ui.dp(12),ui.dp(12));ui.card(menu,18);menu.setElevation(ui.dp(12));
        LinearLayout header=ui.row();
        if(!page.equals("controls"))header.addView(ui.iconButton("back","返回控制栏",()->openMenu("controls")),ui.size(28,32));
        LinearLayout titles=ui.column();TextView title=ui.text(page.equals("controls")?"●  已连接":page.equals("video")?"画面设置":"网络状态",14,true);titles.addView(title);
        if(page.equals("controls")){ui.gap(titles,3);networkLabel=ui.text(network.summary(),10,false);networkLabel.setFontFeatureSettings("tnum");networkLabel.setTextColor(SECONDARY);titles.addView(networkLabel);}header.addView(titles,new LinearLayout.LayoutParams(0,-2,1));header.addView(ui.iconButton("close","关闭控制栏",this::closeMenu),ui.size(30,32));menu.addView(header);ui.gap(menu,9);
        if(page.equals("video")){videoFeedback=ui.text("",12,false);videoFeedback.setTextColor(SECONDARY);videoFeedback.setPadding(0,0,0,ui.dp(9));menu.addView(videoFeedback);}
        ScrollView scroll=new ScrollView(getContext());menuScroll=scroll;menu.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));LinearLayout body=ui.column();scroll.addView(body);
        if(page.equals("network")){
            // Reserve the scrollbar's width plus a gap before right-aligned statistics.
            scroll.setScrollBarStyle(View.SCROLLBARS_OUTSIDE_INSET);body.setPadding(0,0,ui.dp(6),0);
        }
        if(page.equals("controls")){
            String[] labels={"键盘",relative?"相对鼠标":"绝对鼠标",mouseVisible?"收起鼠标":"虚拟鼠标","显示器","画面设置",muted?"静音":"声音","发送文件","网络状态","Ctrl+Alt+Del","剪贴板"};
            String[] icons={"keyboard","mouse","mouse","display","sliders","audio","folder","chart","lock","clipboard"};
            Runnable[] clicks={()->{closeMenu();toggleKeyboard();},actions::mouse,this::toggleMouse,actions::display,()->openMenu("video"),actions::mute,actions::sendFile,()->openMenu("network"),actions::secureAttention,actions::clipboard};
            menuColumns=controlColumns();
            for(int row=0;row<(labels.length+menuColumns-1)/menuColumns;row++){LinearLayout line=ui.row();for(int col=0;col<menuColumns;col++){int index=row*menuColumns+col;if(index>=labels.length){View spacer=new View(getContext());LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,ui.dp(52),1);if(col>0)p.leftMargin=ui.dp(7);line.addView(spacer,p);continue;}LinearLayout control=ui.column();control.setGravity(Gravity.CENTER);control.setBackground(ui.background(0xFFF0F0F2,10));control.addView(ui.icon(icons[index],INK),ui.size(19,19));ui.gap(control,4);TextView caption=ui.text(labels[index],10,false);caption.setSingleLine();caption.setGravity(Gravity.CENTER);control.addView(caption);control.setContentDescription(labels[index]);control.setClickable(true);control.setFocusable(true);control.setOnClickListener(v->clicks[index].run());
                    LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,ui.dp(52),1);if(col>0)p.leftMargin=ui.dp(7);line.addView(control,p);
                }body.addView(line);if((row+1)*menuColumns<labels.length)ui.gap(body,7);}
            ui.gap(body,7);transferLabel=ui.text(transferStatus,11,false);transferLabel.setTextColor(SECONDARY);transferLabel.setAccessibilityLiveRegion(ACCESSIBILITY_LIVE_REGION_POLITE);body.addView(transferLabel);
            saveFileButton=ui.action("保存收到的文件",BLUE,actions::saveFile);body.addView(saveFileButton);renderFileStatus();
        }else if(page.equals("network")){
            networkPage(body);
        }else{
            settingRows.add(new SettingRow(body,0,"画面质量",new String[]{"低","中","高"},new int[]{0,1,2}));ui.gap(body,12);
            settingRows.add(new SettingRow(body,1,"画面采集帧率",new String[]{"30 fps","60 fps"},new int[]{30,60}));ui.gap(body,12);
            settingRows.add(new SettingRow(body,2,"画面偏好",RemoteVideoSettings.PREFERENCES,new int[]{0,1,2}));renderVideoSettings();
        }
        ui.gap(menu,9);TextView disconnect=ui.action("断开连接",Color.WHITE,()->{closeMenu();closeKeyboard();actions.disconnect();});disconnect.setMinHeight(ui.dp(34));disconnect.setTextSize(14);disconnect.setBackground(ui.background(RED,8));menu.addView(disconnect,ui.size(-1,34));
        addView(menu,new LayoutParams(panelWidth(getWidth()),panelHeight(getHeight())));orb.bringToFront();orb.setContentDescription("收起远程控制菜单");
        positionMouse();
    }
    private final class SettingRow {
        final int field;final int[] values;final SegmentedControl group;
        SettingRow(LinearLayout body,int field,String title,String[] labels,int[] values){
            this.field=field;this.values=values;
            TextView label=ui.text(title,12,false);label.setTextColor(SECONDARY);body.addView(label);ui.gap(body,5);
            group=new SegmentedControl(ui,labels,selectionIndex(),settings.supported,index->actions.videoSettings(field,values[index]));
            group.setPadding(ui.dp(2),ui.dp(2),ui.dp(2),ui.dp(2));body.addView(group);
            for(int i=0;i<labels.length;i++){
                TextView button=(TextView)group.getChildAt(i);button.setTextSize(12);button.setMinHeight(ui.dp(28));button.setSingleLine();button.setContentDescription(title+"，"+labels[i]);
                button.setLayoutParams(new LinearLayout.LayoutParams(0,ui.dp(28),1));
            }
        }
        int selectionIndex(){int selected=field==0?settings.selection.quality:field==1?settings.selection.frameRate:settings.selection.preference;
            for(int i=0;i<values.length;i++)if(values[i]==selected)return i;return 0;
        }
        void render(){group.setSelection(selectionIndex());group.setOptionsEnabled(settings.supported);}
    }
    private void renderVideoSettings(){
        if(videoFeedback!=null){String feedback=settings.feedback();videoFeedback.setText(feedback);videoFeedback.setVisibility(feedback.isEmpty()?GONE:VISIBLE);}
        for(SettingRow row:settingRows)row.render();
    }
    private void networkPage(LinearLayout body){
        trafficCells=new TextView[4][3];trafficRows=new LinearLayout[4];String[] names={"视频","音频","数据","合计"};
        LinearLayout heading=trafficRow(body,"",new TextView[3],false);((TextView)heading.getChildAt(1)).setText("接收");((TextView)heading.getChildAt(2)).setText("发送");((TextView)heading.getChildAt(3)).setText("丢包率");
        for(int i=0;i<heading.getChildCount();i++)((TextView)heading.getChildAt(i)).setTextColor(SECONDARY);
        for(int i=0;i<4;i++)trafficRows[i]=trafficRow(body,names[i],trafficCells[i],i==3);
        ui.gap(body,10);ui.divider(body);ui.gap(body,10);
        String[] titles={"帧率","分辨率","画面延时","连接延时（RTT）","连接方式","媒体加密"};networkDetails=new TextView[titles.length];
        for(int i=0;i<titles.length;i++){LinearLayout row=ui.row();TextView title=ui.text(titles[i],12,false);title.setTextColor(SECONDARY);row.addView(title,new LinearLayout.LayoutParams(0,-2,1));
            if(i==5){encryptionLock=ui.icon("lock",GREEN);encryptionOpen=ui.icon("unlock",SECONDARY);for(View icon:new View[]{encryptionLock,encryptionOpen}){LinearLayout.LayoutParams p=ui.size(14,14);p.rightMargin=ui.dp(4);row.addView(icon,p);}}
            TextView value=ui.text("—",12,false);value.setFontFeatureSettings("tnum");row.addView(value);networkDetails[i]=value;body.addView(row);if(i<titles.length-1)ui.gap(body,7);}
        renderNetwork();
    }
    private LinearLayout trafficRow(LinearLayout body,String title,TextView[] cells,boolean total){
        LinearLayout row=ui.row();row.setPadding(ui.dp(4),ui.dp(5),ui.dp(4),ui.dp(5));body.addView(row);if(total)row.setBackground(ui.background(0x122E7ADB,5));
        TextView label=ui.text(title,11,total);row.addView(label,ui.size(28,-2));
        for(int i=0;i<3;i++){TextView cell=ui.text("—",11,total);cell.setGravity(Gravity.END);cell.setSingleLine();cell.setFontFeatureSettings("tnum");cell.setAutoSizeTextTypeUniformWithConfiguration(8,11,1,android.util.TypedValue.COMPLEX_UNIT_SP);
            LinearLayout.LayoutParams p=i==2?ui.size(46,16):new LinearLayout.LayoutParams(0,ui.dp(16),1);p.leftMargin=ui.dp(4);row.addView(cell,p);cells[i]=cell;}
        return row;
    }
    private void renderNetwork(){
        if(networkLabel!=null)networkLabel.setText(network.summary());
        if(trafficCells==null)return;
        String[] names={"视频","音频","数据","合计"};
        for(int i=0;i<4;i++){
            RemoteNetworkStatistics.Traffic traffic=network.report==null?null:network.report.traffic[i];
            String in=RemoteNetworkStatistics.bitrate(traffic==null?-1:traffic.inbound),out=RemoteNetworkStatistics.bitrate(traffic==null?-1:traffic.outbound),loss=RemoteNetworkStatistics.loss(traffic==null?Double.NaN:traffic.loss);
            trafficCells[i][0].setText(in);trafficCells[i][1].setText(out);trafficCells[i][2].setText(loss);trafficRows[i].setContentDescription(names[i]+"，接收 "+in+"，发送 "+out+"，丢包率 "+loss);
        }
        String[] values={network.fps+" FPS",network.resolution(),RemoteNetworkStatistics.latency(network.latency),RemoteNetworkStatistics.latency(network.rtt),network.mode(),network.report==null?"—":network.report.srtp?"SRTP 已启用":"未启用"};
        for(int i=0;i<values.length;i++)networkDetails[i].setText(values[i]);networkDetails[5].setTextColor(network.report!=null&&network.report.srtp?GREEN:SECONDARY);
        encryptionLock.setVisibility(network.report!=null&&network.report.srtp?VISIBLE:GONE);encryptionOpen.setVisibility(network.report!=null&&!network.report.srtp?VISIBLE:GONE);
    }
    private int controlColumns(){return getMeasuredHeight()<ui.dp(400)&&getMeasuredWidth()>=ui.dp(400)?4:3;}
    private int panelWidth(int width){return Math.max(1,Math.min(width-ui.dp(16),controlColumns()==4?ui.dp(380):Math.max(ui.dp(280),Math.min(ui.dp(340),width-ui.dp(160)))));}
    private int panelHeight(int height){
        // First measure every row at its natural height, then cap only to the
        // safe viewport. The weighted ScrollView takes any remaining space.
        LinearLayout.LayoutParams scrollParams=(LinearLayout.LayoutParams)menuScroll.getLayoutParams();
        scrollParams.height=LayoutParams.WRAP_CONTENT;scrollParams.weight=0;
        menu.measure(MeasureSpec.makeMeasureSpec(panelWidth(getMeasuredWidth()),MeasureSpec.EXACTLY),
                     MeasureSpec.makeMeasureSpec(0,MeasureSpec.UNSPECIFIED));
        int contentHeight=menu.getMeasuredHeight();
        scrollParams.height=0;scrollParams.weight=1;
        menu.forceLayout();menuScroll.forceLayout();
        return Math.min(contentHeight,Math.max(1,height-ui.dp(16)));
    }
    private void positionMenu(){if(menu==null)return;float ox=orb.getX()+orb.getWidth()/2f;float x=ox>getWidth()/2f?orb.getX()-ui.dp(12)-menu.getWidth():orb.getX()+orb.getWidth()+ui.dp(12);menu.setX(bound(x,ui.dp(8),getWidth()-menu.getWidth()-ui.dp(8)));menu.setY(bound(orb.getY(),ui.dp(8),getHeight()-menu.getHeight()-ui.dp(8)));}
    private int keyboardWidth(int width){return Math.min(ui.dp(computerKeyboard?520:440),Math.max(1,width-ui.dp(16)));}
    private int keyboardHeight(int height){return Math.min(keyboardPreferredHeight,Math.max(1,height-ui.dp(16)));}
    private void positionKeyboard(){if(keyboard!=null){keyboard.setX(bound(keyboard.getX(),ui.dp(8),getWidth()-keyboard.getWidth()-ui.dp(8)));keyboard.setY(bound(keyboard.getY(),ui.dp(8),getHeight()-keyboard.getHeight()-ui.dp(8)));}positionMouse();}
    private void closeKeyboard(){if(keyboard!=null){removeView(keyboard);keyboard=null;keyboardKeys=null;}}
    private void toggleKeyboard(){if(keyboard!=null){closeKeyboard();modifiers.clear();return;}modifiers.clear();showKeyboard();}
    private void showKeyboard(){
        float oldX=keyboard==null?-1:keyboard.getX(),oldY=keyboard==null?-1:keyboard.getY();int oldScroll=keyboardKeys==null?0:keyboardKeys.getScrollY();closeKeyboard();keyboard=ui.column();keyboard.setPadding(ui.dp(5),ui.dp(5),ui.dp(5),ui.dp(5));keyboard.setBackground(ui.background(0xFFD1D6DE,12));keyboard.setElevation(ui.dp(10));
        LinearLayout top=ui.row();TextView handle=ui.text("━",19,true);handle.setGravity(Gravity.CENTER);handle.setTextColor(SECONDARY);handle.setContentDescription("拖动键盘");top.addView(handle,new LinearLayout.LayoutParams(0,ui.dp(28),1));top.addView(ui.iconButton("close","收起键盘",this::closeKeyboard),ui.size(30,28));keyboard.addView(top);drag(handle,keyboard,()->{});
        // Keep the drag/close row fixed while short split windows can scroll every key into view.
        keyboardKeys=new ScrollView(getContext());keyboard.addView(keyboardKeys,new LinearLayout.LayoutParams(-1,0,1));LinearLayout keys=ui.column();keyboardKeys.addView(keys);
        String[][] rows=computerKeyboard?new String[][]{{"Esc","F1","F2","F3","F4","F5","F6"},{"Tab","Ctrl","Alt","Win","↑","⌫"},{"复制","粘贴","全选","←","↓","→"},{"ABC","空格","Enter"}}:new String[][]{{"1","2","3","4","5","6","7","8","9","0"},{"q","w","e","r","t","y","u","i","o","p"},{"a","s","d","f","g","h","j","k","l"},{"⇧","z","x","c","v","b","n","m","⌫"},{"电脑","剪贴板","空格",".","Enter"}};
        for(String[] values:rows){LinearLayout line=ui.row();for(String value:values){String shown=shift&&value.length()==1?value.toUpperCase(java.util.Locale.ROOT):value;TextView key=ui.action(shown,INK,()->keyboardKey(value));key.setTextSize(value.length()>2?11:14);key.setMinHeight(ui.dp(31));int modifier=value.equals("Ctrl")?17:value.equals("Alt")?18:value.equals("Win")?91:0;key.setBackground(ui.background(modifiers.contains(modifier)?0xFF9BC7F7:value.length()>1||value.equals("⇧")?0xFFAFB8C5:Color.WHITE,5));LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,ui.dp(31),value.equals("空格")?3:1);p.setMargins(ui.dp(1.5f),0,ui.dp(1.5f),0);line.addView(key,p);}keys.addView(line);ui.gap(keys,3);}
        keyboardPreferredHeight=ui.dp(5)*2+ui.dp(28)+rows.length*(ui.dp(31)+ui.dp(3));
        addView(keyboard,new LayoutParams(keyboardWidth(getWidth()),keyboardHeight(getHeight())));LinearLayout shownKeyboard=keyboard;ScrollView shownKeys=keyboardKeys;
        keyboard.post(()->{if(keyboard==shownKeyboard){keyboard.setX(oldX<0?(getWidth()-keyboard.getWidth())/2f:oldX);keyboard.setY(oldY<0?getHeight()-keyboard.getHeight()-ui.dp(20):oldY);shownKeys.scrollTo(0,oldScroll);constrainPanels();}});
    }
    private void keyboardKey(String value){
        switch(value){
            case "⇧":shift=!shift;showKeyboard();return;
            case "电脑":computerKeyboard=true;showKeyboard();return;
            case "ABC":computerKeyboard=false;showKeyboard();return;
            case "剪贴板":actions.clipboard();return;
            case "复制":actions.shortcut(0x43);return;case "粘贴":actions.shortcut(0x56);return;case "全选":actions.shortcut(0x41);return;
            case "空格":sendKey(32);return;case "⌫":sendKey(8);return;case "Enter":sendKey(13);return;case "Esc":sendKey(27);return;case "Tab":sendKey(9);return;
            case "↑":sendKey(38);return;case "↓":sendKey(40);return;case "←":sendKey(37);return;case "→":sendKey(39);return;
            // Latch locally; the next key sends a complete chord without leaving remote modifiers down.
            case "Ctrl":toggleModifier(17);return;case "Alt":toggleModifier(18);return;case "Win":toggleModifier(91);return;
            default:if(value.matches("F[1-6]")){sendKey(111+Integer.parseInt(value.substring(1)));return;}if(!modifiers.isEmpty())sendKey(KeyMap.ascii(value.charAt(0))&255);else actions.text(shift?value.toUpperCase(java.util.Locale.ROOT):value);
        }
    }
    private void toggleModifier(int code){if(!modifiers.add(code))modifiers.remove(code);showKeyboard();}
    private void sendKey(int code){if(modifiers.isEmpty())actions.key(code);else{actions.chord(code,modifiers.stream().mapToInt(Integer::intValue).toArray());modifiers.clear();showKeyboard();}}
    @android.annotation.SuppressLint("ClickableViewAccessibility") // Tap dispatches performClick; dragging is an additional action.
    private void drag(View handle,View target,Runnable tap){
        handle.setOnClickListener(v->tap.run());handle.setOnTouchListener(new View.OnTouchListener(){float startX,startY,originX,originY;boolean moved;
            public boolean onTouch(View v,MotionEvent event){
                switch(event.getActionMasked()){
                    case MotionEvent.ACTION_DOWN:if(target==orb)cancelMouseInput();startX=event.getRawX();startY=event.getRawY();originX=target.getX();originY=target.getY();moved=false;return true;
                    case MotionEvent.ACTION_MOVE:float dx=event.getRawX()-startX,dy=event.getRawY()-startY;if(Math.hypot(dx,dy)>ui.dp(4))moved=true;if(moved){if(target==orb)closeMenu();target.setX(bound(originX+dx,ui.dp(8),getWidth()-target.getWidth()-ui.dp(8)));target.setY(bound(originY+dy,ui.dp(8),getHeight()-target.getHeight()-ui.dp(8)));positionMouse();}return true;
                    case MotionEvent.ACTION_UP:if(!moved)v.performClick();return true;
                    case MotionEvent.ACTION_CANCEL:return true;
                    default:return false;
                }
            }
        });
    }
    void constrainPanels(){
        cancelMouseInput();
        if(menu!=null&&menuPage.equals("controls")&&menuColumns!=controlColumns())openMenu("controls");
        orb.setX(bound(orb.getX(),ui.dp(8),getWidth()-orb.getWidth()-ui.dp(8)));orb.setY(bound(orb.getY(),ui.dp(8),getHeight()-orb.getHeight()-ui.dp(8)));
        if(menu!=null||keyboard!=null)requestLayout();
        positionKeyboard();
        positionMouse();
    }
    @Override protected void onMeasure(int widthSpec,int heightSpec){
        super.onMeasure(widthSpec,heightSpec);
        boolean resized=false;
        if(menu!=null){
            // Use this traversal's viewport, including rotation/inset changes, before drawing.
            ViewGroup.LayoutParams p=menu.getLayoutParams();int width=panelWidth(getMeasuredWidth()),height=panelHeight(getMeasuredHeight());
            // panelHeight performed an unconstrained measurement; always
            // measure again with the final cap, even when it did not change.
            p.width=width;p.height=height;resized=true;
        }
        if(keyboard!=null){
            ViewGroup.LayoutParams p=keyboard.getLayoutParams();int width=keyboardWidth(getMeasuredWidth()),height=keyboardHeight(getMeasuredHeight());
            if(p.width!=width||p.height!=height){p.width=width;p.height=height;resized=true;}
        }
        if(resized)super.onMeasure(widthSpec,heightSpec);
    }
    @Override protected void onLayout(boolean changed,int left,int top,int right,int bottom){
        super.onLayout(changed,left,top,right,bottom);
        if(!orbPositioned){orb.setX(Math.max(ui.dp(12),getWidth()-ui.dp(66)));orb.setY(ui.dp(17));orbPositioned=true;}
        orb.setX(bound(orb.getX(),ui.dp(8),getWidth()-orb.getWidth()-ui.dp(8)));orb.setY(bound(orb.getY(),ui.dp(8),getHeight()-orb.getHeight()-ui.dp(8)));
        // New/rebuilt menus have real dimensions now; position them in the same frame.
        positionMenu();
        positionKeyboard();
        positionMouse();
    }
    @Override protected void onDetachedFromWindow(){cancelMouseInput();super.onDetachedFromWindow();}
    @Override protected void onSizeChanged(int width,int height,int oldWidth,int oldHeight){super.onSizeChanged(width,height,oldWidth,oldHeight);post(this::constrainPanels);}
    private static float bound(float value,float min,float max){return Math.max(min,Math.min(value,Math.max(min,max)));}
}
