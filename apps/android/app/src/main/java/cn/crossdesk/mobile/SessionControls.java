package cn.crossdesk.mobile;

import android.content.Context;
import android.graphics.Color;
import android.view.*;
import android.widget.*;
import static cn.crossdesk.mobile.MobileUi.*;

/** Safe-area overlay: a draggable 54 dp orb, an adaptive menu and a docked keyboard. */
@android.annotation.SuppressLint("ViewConstructor")
final class SessionControls extends FrameLayout {
    interface Actions {
        void disconnect(); void display(); void clipboard(); void key(int code);
        void chord(int code,int[] modifiers); void shortcut(int code); void text(String value); void mute(); void mouse();
        default boolean textEdit(int deletions,String value){return false;}
        void videoSettings(int field,int value);
        default void displayResolution(RemoteDisplayResolutions.Mode mode) { }
        void sendFile(); void saveFile(); void secureAttention();
        default void togglePrivacy() { }
    }
    private final MobileUi ui;
    private final Actions actions;
    private final View orb;
    private final LinearLayout waiting;
    private View shield;
    private LinearLayout menu;
    private final RemoteKeyboard keyboard;
    private VirtualMouseBar mouseBar;
    private FrameLayout mouseHost;
    private boolean mouseVisible,pictureInPicture;
    private ScrollView menuScroll;
    private int menuColumns=3;
    private final ViewTreeObserver.OnGlobalLayoutListener legacyKeyboardLayout=this::legacyKeyboardLayout;
    private void legacyKeyboardLayout(){
        if(android.os.Build.VERSION.SDK_INT>=30)return;
        android.graphics.Rect visible=new android.graphics.Rect();getWindowVisibleDisplayFrame(visible);
        int height=Math.max(0,getRootView().getHeight()-visible.bottom);
        keyboardInsets(height,height>ui.dp(80),0);
    }
    private TextView videoFeedback,resolutionButton,resolutionFeedback;
    private RemoteDisplayResolutions resolutions=new RemoteDisplayResolutions();
    private int resolutionDisplay;
    private android.app.AlertDialog resolutionDialog;
    private RemotePrivacyScreen privacy=new RemotePrivacyScreen();
    private View privacyButton;
    private MobileUi.Icon privacyIcon;
    private TextView privacyFeedback,privacyCaption;
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
    private boolean muted,relative=true,orbPositioned,secureAttentionVisible;
    SessionControls(Context context,MobileUi ui,Actions actions){
        super(context);this.ui=ui;this.actions=actions;
        keyboard=new RemoteKeyboard(context,ui,actions,()->{requestLayout();positionMouse();});
        addView(keyboard,new LayoutParams(-1,ui.dp(44),Gravity.BOTTOM));
        // This container must remain transparent to remote touch outside its controls.
        waiting=ui.column();waiting.setMinimumWidth(ui.dp(200));waiting.setGravity(Gravity.CENTER);waiting.setPadding(ui.dp(20),ui.dp(20),ui.dp(20),ui.dp(20));waiting.setBackground(ui.background(0xB0202024,14));
        ProgressBar progress=new ProgressBar(context);progress.setIndeterminateTintList(android.content.res.ColorStateList.valueOf(Color.WHITE));waiting.addView(progress,ui.size(28,28));ui.gap(waiting,12);TextView message=ui.text(L10n.tr("等待远端画面…"),14,false);message.setTextColor(Color.WHITE);message.setGravity(Gravity.CENTER);waiting.addView(message);addView(waiting,new LayoutParams(-2,-2,Gravity.CENTER));
        orb=ui.iconButton("display",L10n.tr("展开远程控制菜单"),()->{if(menu==null)openMenu("controls");else closeMenu();});
        ImageView brand=new ImageView(context);brand.setImageResource(R.drawable.mobile_brand);brand.setScaleType(ImageView.ScaleType.CENTER_CROP);brand.setBackground(ui.background(Color.WHITE,23));brand.setClipToOutline(true);brand.setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        ((FrameLayout)orb).removeAllViews();((FrameLayout)orb).addView(brand,new LayoutParams(ui.dp(46),ui.dp(46),Gravity.CENTER));
        orb.setBackground(ui.background(0xFFF5F5F7,27));orb.setElevation(ui.dp(7));addView(orb,new LayoutParams(ui.dp(54),ui.dp(54)));
        drag(orb,orb,()->{if(menu==null)openMenu("controls");else closeMenu();});
    }
    void setRelative(boolean value){relative=value;if(menu!=null&&menuPage.equals("controls"))openMenu("controls");}
    void setHostPlatform(String platform){
        keyboard.setPlatform(platform);
        boolean visible="windows".equals(platform);
        if(secureAttentionVisible==visible)return;
        secureAttentionVisible=visible;
        if(menu!=null&&menuPage.equals("controls"))openMenu("controls");
    }
    void attachMouseBar(FrameLayout host, VirtualMouseBar bar) {
        mouseHost=host;mouseBar=bar;
        // A direct, tightly bounded sibling of the video is essential: otherwise
        // an existing touch in this full-screen controls container captures the
        // second finger before Android can dispatch it to the remote surface.
        host.addView(bar,new FrameLayout.LayoutParams(ui.dp(140),ui.dp(48)));
        bar.setVisibility(GONE);
    }
    void cancelMouseInput(){if(mouseBar!=null)mouseBar.cancelInput();}
    void setPictureInPicture(boolean value){
        pictureInPicture=value;
        if(value){cancelMouseInput();closeMenu();closeKeyboard();}
        setVisibility(value?GONE:VISIBLE);positionMouse();
    }
    private void toggleMouse(){cancelMouseInput();mouseVisible=!mouseVisible;closeMenu();positionMouse();}
    private void positionMouse(){
        if(mouseBar==null)return;
        boolean visible=!pictureInPicture&&mouseVisible&&menu==null&&waiting.getVisibility()==GONE;
        if(!visible){if(mouseBar.getVisibility()==VISIBLE)cancelMouseInput();mouseBar.setVisibility(GONE);return;}
        int[] location=new int[2],hostLocation=new int[2];getLocationOnScreen(location);mouseHost.getLocationOnScreen(hostLocation);
        float width=ui.dp(140),height=ui.dp(48),gap=ui.dp(6),margin=ui.dp(8);
        float x=orb.getX()>=width+gap+margin?orb.getX()-gap-width:orb.getX()+orb.getWidth()+gap;
        float y=orb.getY()+(orb.getHeight()-height)/2;
        // When the orb is near the middle of a narrow phone, use the adjacent
        // row rather than letting the two controls overlap.
        if(x+width>getWidth()-margin){x=bound(orb.getX(),margin,getWidth()-width-margin);y=orb.getY()+orb.getHeight()+gap;if(y+height>getHeight()-margin)y=orb.getY()-gap-height;}
        if(keyboard.isOpen()&&x<keyboard.getX()+keyboard.getWidth()&&x+width>keyboard.getX()&&y<keyboard.getY()+keyboard.getHeight()&&y+height>keyboard.getY()){
            y=keyboard.getY()-gap-height;if(y<margin)y=keyboard.getY()+keyboard.getHeight()+gap;
        }
        mouseBar.setX(location[0]-hostLocation[0]+bound(x,margin,getWidth()-width-margin));
        mouseBar.setY(location[1]-hostLocation[1]+bound(y,margin,getHeight()-height-margin));
        mouseBar.setVisibility(VISIBLE);
    }
    void setMuted(boolean value){muted=value;if(menu!=null&&menuPage.equals("controls"))openMenu("controls");}
    void videoSize(int width,int height){waiting.setVisibility(GONE);positionMouse();}
    void resetVideoStatistics(){statistics(new RemoteNetworkStatistics.Snapshot(network.report,0,0,0,Double.NaN,network.rtt,network.remoteEncoder,network.localDecoder));}
    void statistics(RemoteNetworkStatistics.Snapshot value){network=value;renderNetwork();}
    void videoSettings(RemoteVideoSettings value){settings=value;renderVideoSettings();}
    void displayResolutions(RemoteDisplayResolutions value,int display){resolutions=value;resolutionDisplay=display;renderResolutions();}
    void privacyScreen(RemotePrivacyScreen value){privacy=value;renderPrivacy();}
    void fileStatus(String status,boolean received){transferStatus=status;hasReceivedFile=received;renderFileStatus();}
    private void renderFileStatus(){
        if(transferLabel!=null){transferLabel.setText(transferStatus);transferLabel.setVisibility(transferStatus.isEmpty()?GONE:VISIBLE);}
        if(saveFileButton!=null)saveFileButton.setVisibility(hasReceivedFile?VISIBLE:GONE);
    }
    private void closeMenu(){if(resolutionDialog!=null){resolutionDialog.dismiss();resolutionDialog=null;}resolutionButton=resolutionFeedback=null;if(menu!=null){removeView(menu);menu=null;menuScroll=null;}if(shield!=null){removeView(shield);shield=null;}videoFeedback=transferLabel=saveFileButton=null;privacyFeedback=privacyCaption=null;privacyButton=null;privacyIcon=null;trafficCells=null;trafficRows=null;networkDetails=null;encryptionLock=encryptionOpen=null;settingRows.clear();orb.setContentDescription(L10n.tr("展开远程控制菜单"));positionMouse();}
    private void openMenu(String page){
        cancelMouseInput();
        closeMenu();menuPage=page;shield=new View(getContext());shield.setOnClickListener(v->closeMenu());addView(shield,new LayoutParams(-1,-1));
        menu=ui.column();menu.setPadding(ui.dp(12),ui.dp(12),ui.dp(12),ui.dp(12));ui.card(menu,18);menu.setElevation(ui.dp(12));
        LinearLayout header=ui.row();
        if(!page.equals("controls"))header.addView(ui.iconButton("back",L10n.tr("返回控制栏"),()->openMenu("controls")),ui.size(28,32));
        TextView title=ui.text(page.equals("controls")?L10n.tr("●  已连接"):page.equals("video")?L10n.tr("画面设置"):L10n.tr("网络状态"),14,true);
        header.addView(title,new LinearLayout.LayoutParams(0,-2,1));header.addView(ui.iconButton("close",L10n.tr("关闭控制栏"),this::closeMenu),ui.size(30,32));menu.addView(header);ui.gap(menu,9);
        if(page.equals("video")){videoFeedback=ui.text("",12,false);videoFeedback.setTextColor(SECONDARY);videoFeedback.setPadding(0,0,0,ui.dp(9));menu.addView(videoFeedback);}
        ScrollView scroll=new ScrollView(getContext());menuScroll=scroll;menu.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));LinearLayout body=ui.column();scroll.addView(body);
        if(page.equals("network")){
            // Reserve the scrollbar's width plus a gap before right-aligned statistics.
            scroll.setScrollBarStyle(View.SCROLLBARS_OUTSIDE_INSET);body.setPadding(0,0,ui.dp(6),0);
        }
        if(page.equals("controls")){
            String[] labels={L10n.tr("键盘"),mouseVisible?L10n.tr("收起鼠标"):L10n.tr("虚拟鼠标"),relative?L10n.tr("相对鼠标"):L10n.tr("绝对鼠标"),L10n.tr("显示器"),L10n.tr("画面设置"),muted?L10n.tr("静音"):L10n.tr("声音"),L10n.tr("发送文件"),L10n.tr("网络状态"),"Ctrl+Alt+Del",L10n.tr("剪贴板"),L10n.tr("隐私屏")};
            String[] icons={"keyboard","mouse","mouse","display","sliders","audio","folder","chart","lock","clipboard","eye"};
            Runnable[] clicks={()->{closeMenu();toggleKeyboard();},this::toggleMouse,actions::mouse,actions::display,()->openMenu("video"),actions::mute,actions::sendFile,()->openMenu("network"),actions::secureAttention,actions::clipboard,actions::togglePrivacy};
            menuColumns=controlColumns();
            java.util.List<Integer> visibleControls=new java.util.ArrayList<>();
            for(int index=0;index<labels.length;index++){
                if(index==8&&!secureAttentionVisible||index==10&&!privacy.desktopAvailable)continue;
                visibleControls.add(index);
            }
            int controlCount=visibleControls.size();
            for(int row=0;row<(controlCount+menuColumns-1)/menuColumns;row++){LinearLayout line=ui.row();for(int col=0;col<menuColumns;col++){int position=row*menuColumns+col;if(position>=controlCount){View spacer=new View(getContext());LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,ui.dp(52),1);if(col>0)p.leftMargin=ui.dp(7);line.addView(spacer,p);continue;}int index=visibleControls.get(position);LinearLayout control=ui.column();control.setGravity(Gravity.CENTER);control.setBackground(ui.background(0xFFF0F0F2,10));MobileUi.Icon icon=ui.icon(icons[index],INK);control.addView(icon,ui.size(19,19));ui.gap(control,4);TextView caption=ui.text(labels[index],10,false);caption.setMaxLines(2);caption.setAutoSizeTextTypeUniformWithConfiguration(8,10,1,android.util.TypedValue.COMPLEX_UNIT_SP);caption.setGravity(Gravity.CENTER);control.addView(caption);control.setContentDescription(labels[index]);control.setClickable(true);control.setFocusable(true);control.setOnClickListener(v->clicks[index].run());
                    if(index==10){privacyButton=control;privacyIcon=icon;privacyCaption=caption;}
                    LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,ui.dp(52),1);if(col>0)p.leftMargin=ui.dp(7);line.addView(control,p);
                }body.addView(line);if((row+1)*menuColumns<controlCount)ui.gap(body,7);}
            privacyFeedback=ui.text("",11,false);privacyFeedback.setTextColor(SECONDARY);privacyFeedback.setPadding(0,ui.dp(7),0,0);privacyFeedback.setAccessibilityLiveRegion(ACCESSIBILITY_LIVE_REGION_POLITE);body.addView(privacyFeedback);renderPrivacy();
            ui.gap(body,7);transferLabel=ui.text(transferStatus,11,false);transferLabel.setTextColor(SECONDARY);transferLabel.setAccessibilityLiveRegion(ACCESSIBILITY_LIVE_REGION_POLITE);body.addView(transferLabel);
            saveFileButton=ui.action(L10n.tr("保存收到的文件"),BLUE,actions::saveFile);body.addView(saveFileButton);renderFileStatus();
        }else if(page.equals("network")){
            networkPage(body);
        }else{
            TextView resolutionTitle=ui.text(L10n.tr("被控端分辨率"),12,false);resolutionTitle.setTextColor(SECONDARY);body.addView(resolutionTitle);ui.gap(body,5);
            resolutionButton=ui.action("",INK,this::chooseResolution);resolutionButton.setTextSize(13);resolutionButton.setBackground(ui.background(0xFFF0F0F2,7));body.addView(resolutionButton,ui.size(-1,34));
            resolutionFeedback=ui.text("",12,false);resolutionFeedback.setTextColor(SECONDARY);resolutionFeedback.setAccessibilityLiveRegion(ACCESSIBILITY_LIVE_REGION_POLITE);body.addView(resolutionFeedback);ui.gap(body,12);renderResolutions();
            settingRows.add(new SettingRow(body,0,L10n.tr("画面质量"),new String[]{L10n.tr("低"),L10n.tr("中"),L10n.tr("高")},new int[]{0,1,2}));ui.gap(body,12);
            settingRows.add(new SettingRow(body,1,L10n.tr("画面采集帧率"),new String[]{"30 fps","60 fps"},new int[]{30,60}));ui.gap(body,12);
            settingRows.add(new SettingRow(body,2,L10n.tr("画面偏好"),RemoteVideoSettings.preferences(),new int[]{0,1,2}));renderVideoSettings();
        }
        ui.gap(menu,9);TextView disconnect=ui.action(L10n.tr("断开连接"),Color.WHITE,()->{closeMenu();closeKeyboard();actions.disconnect();});disconnect.setMinHeight(ui.dp(34));disconnect.setTextSize(14);disconnect.setBackground(ui.background(RED,8));menu.addView(disconnect,ui.size(-1,34));
        addView(menu,new LayoutParams(panelWidth(getWidth()),panelHeight(getHeight())));orb.bringToFront();orb.setContentDescription(L10n.tr("收起远程控制菜单"));
        positionMouse();
    }
    private void renderPrivacy(){
        if(menu!=null&&menuPage.equals("controls")&&(privacyButton!=null)!=privacy.desktopAvailable){
            openMenu("controls");return;
        }
        long now=android.os.SystemClock.elapsedRealtime();privacy.tick(now);
        if(privacyFeedback!=null){String feedback=privacy.feedback(now);privacyFeedback.setText(feedback);privacyFeedback.setVisibility(feedback.isEmpty()?GONE:VISIBLE);}
        if(privacyButton==null)return;
        boolean enabled=privacy.enabled(now);String action=privacy.active()?L10n.tr("关闭隐私屏"):L10n.tr("开启隐私屏");
        privacyIcon.setName(enabled?"eye_off":"eye");
        privacyCaption.setText(privacy.buttonTitle(now));
        privacyButton.setSelected(enabled);
        privacyButton.setContentDescription(action+"，"+privacy.status(now));
        privacyButton.setEnabled(privacy.canToggle(now));privacyButton.setAlpha(privacyButton.isEnabled()?1f:.45f);
    }
    private final class SettingRow {
        final int field;final int[] values;final SegmentedControl group;
        SettingRow(LinearLayout body,int field,String title,String[] labels,int[] values){
            this.field=field;this.values=values;
            TextView label=ui.text(title,12,false);label.setTextColor(SECONDARY);body.addView(label);ui.gap(body,5);
            group=new SegmentedControl(ui,labels,selectionIndex(),settings.supported,index->actions.videoSettings(field,values[index]));
            group.setPadding(ui.dp(2),ui.dp(2),ui.dp(2),ui.dp(2));body.addView(group);
            for(int i=0;i<labels.length;i++){
                TextView button=(TextView)group.getChildAt(i);button.setTextSize(12);button.setMinHeight(ui.dp(28));button.setSingleLine();button.setAutoSizeTextTypeUniformWithConfiguration(9,12,1,android.util.TypedValue.COMPLEX_UNIT_SP);button.setContentDescription(title+"，"+labels[i]);
                button.setLayoutParams(new LinearLayout.LayoutParams(0,ui.dp(28),1));
            }
        }
        int selectionIndex(){int selected=field==0?settings.selection.quality:field==1?settings.selection.frameRate:settings.selection.preference;
            for(int i=0;i<values.length;i++)if(values[i]==selected)return i;return 0;
        }
        void render(){group.setSelection(selectionIndex());group.setOptionsEnabled(settings.supported);}
    }
    private void renderResolutions(){
        if(resolutionButton==null)return;
        RemoteDisplayResolutions.Display display=resolutions.display(resolutionDisplay);
        boolean enabled=display!=null&&!display.modes.isEmpty()&&!resolutions.pending;
        resolutionButton.setText(display==null?L10n.tr("设备不支持"):display.current.label()+" ▾");
        resolutionButton.setEnabled(enabled);resolutionButton.setAlpha(enabled?1f:.45f);
        String feedback=resolutions.feedback();resolutionFeedback.setText(feedback);resolutionFeedback.setVisibility(feedback.isEmpty()?GONE:VISIBLE);
    }
    private void chooseResolution(){
        RemoteDisplayResolutions.Display display=resolutions.display(resolutionDisplay);
        if(display==null||resolutions.pending||display.modes.isEmpty())return;
        String[] labels=new String[display.modes.size()];int selected=-1;
        for(int i=0;i<labels.length;i++){labels[i]=display.modes.get(i).label();if(display.current.same(display.modes.get(i)))selected=i;}
        resolutionDialog=new android.app.AlertDialog.Builder(getContext()).setTitle(L10n.tr("被控端分辨率"))
            .setSingleChoiceItems(labels,selected,(dialog,index)->{
                dialog.dismiss();resolutionDialog=null;
                RemoteDisplayResolutions.Display current=resolutions.display(resolutionDisplay);
                if(current!=null&&current.id==display.id&&current.key.equals(display.key))actions.displayResolution(display.modes.get(index));
            }).setNegativeButton(L10n.tr("取消"),null).show();
    }
    private void renderVideoSettings(){
        if(videoFeedback!=null){String feedback=settings.feedback();videoFeedback.setText(feedback);videoFeedback.setVisibility(feedback.isEmpty()?GONE:VISIBLE);}
        for(SettingRow row:settingRows)row.render();
    }
    private void networkPage(LinearLayout body){
        trafficCells=new TextView[4][3];trafficRows=new LinearLayout[4];String[] names={L10n.tr("视频"),L10n.tr("音频"),L10n.tr("数据"),L10n.tr("合计")};
        LinearLayout heading=trafficRow(body,"",new TextView[3],false);((TextView)heading.getChildAt(1)).setText(L10n.tr("接收"));((TextView)heading.getChildAt(2)).setText(L10n.tr("发送"));((TextView)heading.getChildAt(3)).setText(L10n.tr("丢包率"));
        for(int i=0;i<heading.getChildCount();i++)((TextView)heading.getChildAt(i)).setTextColor(SECONDARY);
        for(int i=0;i<4;i++)trafficRows[i]=trafficRow(body,names[i],trafficCells[i],i==3);
        ui.gap(body,10);ui.divider(body);ui.gap(body,10);
        String[] titles={L10n.tr("帧率"),L10n.tr("分辨率"),L10n.tr("画面延时"),L10n.tr("连接延时（RTT）"),L10n.tr("连接方式"),L10n.tr("媒体加密"),L10n.tr("被控端编码器"),L10n.tr("控制端解码器")};networkDetails=new TextView[titles.length];
        for(int i=0;i<titles.length;i++){LinearLayout row=ui.row();TextView title=ui.text(titles[i],12,false);title.setTextColor(SECONDARY);row.addView(title,i>=6?new LinearLayout.LayoutParams(-2,-2):new LinearLayout.LayoutParams(0,-2,1));
            if(i==5){encryptionLock=ui.icon("lock",GREEN);encryptionOpen=ui.icon("unlock",SECONDARY);for(View icon:new View[]{encryptionLock,encryptionOpen}){LinearLayout.LayoutParams p=ui.size(14,14);p.rightMargin=ui.dp(4);row.addView(icon,p);}}
            TextView value=ui.text("—",12,false);value.setFontFeatureSettings("tnum");
            if(i>=6){value.setGravity(Gravity.END);value.setSingleLine();value.setEllipsize(android.text.TextUtils.TruncateAt.MARQUEE);value.setMarqueeRepeatLimit(-1);value.setSelected(true);LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,-2,1);p.leftMargin=ui.dp(8);row.addView(value,p);}else row.addView(value);
            networkDetails[i]=value;body.addView(row);if(i<titles.length-1)ui.gap(body,7);}
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
        if(trafficCells==null)return;
        String[] names={L10n.tr("视频"),L10n.tr("音频"),L10n.tr("数据"),L10n.tr("合计")};
        for(int i=0;i<4;i++){
            RemoteNetworkStatistics.Traffic traffic=network.report==null?null:network.report.traffic[i];
            String in=RemoteNetworkStatistics.bitrate(traffic==null?-1:traffic.inbound),out=RemoteNetworkStatistics.bitrate(traffic==null?-1:traffic.outbound),loss=RemoteNetworkStatistics.loss(traffic==null?Double.NaN:traffic.loss);
            trafficCells[i][0].setText(in);trafficCells[i][1].setText(out);trafficCells[i][2].setText(loss);trafficRows[i].setContentDescription(L10n.format("{0}，接收 {1}，发送 {2}，丢包率 {3}",names[i],in,out,loss));
        }
        String[] values={network.fps+" FPS",network.resolution(),RemoteNetworkStatistics.latency(network.latency),RemoteNetworkStatistics.latency(network.rtt),network.mode(),network.report==null?"—":network.report.srtp?L10n.tr("SRTP 已启用"):L10n.tr("未启用"),network.remoteEncoder.isEmpty()?"—":network.remoteEncoder,network.localDecoder.isEmpty()?"—":network.localDecoder};
        // Reassigning identical text every statistics tick restarts the marquee.
        for(int i=0;i<values.length;i++)if(!android.text.TextUtils.equals(networkDetails[i].getText(),values[i]))networkDetails[i].setText(values[i]);networkDetails[5].setTextColor(network.report!=null&&network.report.srtp?GREEN:SECONDARY);
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
    private void positionKeyboard(){positionMouse();}
    boolean closeKeyboard(){return keyboard.close();}
    void resetKeyboardContext(){keyboard.resetContext();}
    void keyboardInsets(int height,boolean visible,int navigation){keyboard.updateInsets(height,visible,navigation);}
    private void toggleKeyboard(){keyboard.bringToFront();keyboard.toggle();}
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
        if(menu!=null||keyboard.isOpen())requestLayout();
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
        if(keyboard.isOpen()){
            LayoutParams p=(LayoutParams)keyboard.getLayoutParams();
            int height=keyboard.preferredHeight(),inset=keyboard.bottomInset();
            if(p.height!=height||p.bottomMargin!=inset){p.height=height;p.bottomMargin=inset;resized=true;}
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
    @Override protected void onAttachedToWindow(){super.onAttachedToWindow();if(android.os.Build.VERSION.SDK_INT<30)getViewTreeObserver().addOnGlobalLayoutListener(legacyKeyboardLayout);}
    @Override protected void onDetachedFromWindow(){getViewTreeObserver().removeOnGlobalLayoutListener(legacyKeyboardLayout);cancelMouseInput();super.onDetachedFromWindow();}
    @Override protected void onSizeChanged(int width,int height,int oldWidth,int oldHeight){super.onSizeChanged(width,height,oldWidth,oldHeight);post(this::constrainPanels);}
    private static float bound(float value,float min,float max){return Math.max(min,Math.min(value,Math.max(min,max)));}
}
