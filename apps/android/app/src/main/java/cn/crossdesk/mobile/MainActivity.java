package cn.crossdesk.mobile;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.ClipData;
import android.content.Intent;
import android.content.ClipboardManager;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.InsetDrawable;
import android.os.Bundle;
import android.text.*;
import android.text.method.PasswordTransformationMethod;
import android.view.*;
import android.view.inputmethod.InputMethodManager;
import android.widget.*;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static cn.crossdesk.mobile.MobileUi.*;

public final class MainActivity extends Activity implements NativeSession.Listener {
    private MobileUi ui;
    private PageHost pageHost;
    private AboutPages aboutPages;
    private AppUpdateChecker appUpdates;
    private View settingsUpdateDot, aboutUpdateDot, settingsUpdateButton, aboutUpdateRow;
    private TextView aboutUpdateLabel;
    private boolean navigatingBack;
    private FrameLayout canvas, safeArea;
    private LinearLayout root;
    private EditText remote;
    private TextView status, signalBadge, identitySummary;
    private AnnouncementInbox announcementInbox;
    private AnnouncementView announcementView;
    private TextView announcementBadge;
    private View announcementRefresh;
    private String announcementServer="";
    private SharedPreferences preferences;
    private RecentConnections history;
    private final RecentConnectionPresence recentPresence=new RecentConnectionPresence();
    private LinearLayout recentGrid;
    private TextView recentCount, recentFilterButton;
    private String recentFilter, recentFilterServer;
    private int recentColumns=2;
    private final Runnable presenceTick=new Runnable(){public void run(){
        if(!recentPresence.isConnected())return;
        recentPresence.maintain(android.os.SystemClock.elapsedRealtime());
        mainHandler.postDelayed(this,1000);
    }};
    private RemotePreviewStore previews;
    private RemotePreviewStore.Capture previewCapture;
    private boolean previewCopying,updatingPreviewSwitch;
    private int previewAttempts;
    private Switch previewSwitch;
    private TextView clearPreviewsButton,previewCleanupError;
    private AlertDialog previewDialog;
    private final java.util.List<Runnable> previewCards=new java.util.ArrayList<>();
    private final Runnable previewChanged=this::renderPreviews;
    private NativeSession session, signaling;
    private String signalingServer="", localIdentity="", signalError="";
    private int signalState=-1;
    private boolean foreground;
    private boolean pictureInPictureUi;
    private boolean enteredPictureInPicture;
    private BackgroundSession backgroundSession;
    private int videoWidth,videoHeight;
    private final RemotePictureInPicture pictureInPicture=new RemotePictureInPicture();
    private RemoteVideoView video;
    private RemoteOrientation remoteOrientation;
    private SessionControls controls;
    private Dialog passwordDialog, progressDialog, privacyDialog;
    private AlertDialog disconnectDialog;
    private AlertDialog connectionDialog;
    private String pendingConnectionMessage="";
    private AlertDialog remoteUpdateDialog;
    private AlertDialog privacyUnlockDialog;
    private RemoteVersionCheck remoteVersionCheck;
    private String host, connectedRemote="", hostPlatform="", remoteClipboard="", pendingPassword="", page="home";
    private int port,selectedDisplay;
    private JSONArray displays=new JSONArray();
    private boolean muted,ready,rememberPassword;
    private boolean remoteServiceAvailable;
    private static final int PICK_FILE=41,SAVE_FILE=42;
    private NativeSession documentSession;
    private java.io.File receivedFile,documentToSave;
    private final Runnable documentTimeout=()->{if(documentSession!=null&&!foreground)end(L10n.tr("文件选择已超时，会话已结束"));};
    private RemoteVideoSettings videoSettings=new RemoteVideoSettings(1);
    private final android.os.Handler mainHandler=new android.os.Handler(android.os.Looper.getMainLooper());
    private Runnable videoSettingsTimeout;
    private RemoteDisplayResolutions displayResolutions=new RemoteDisplayResolutions();
    private Runnable displayResolutionTimeout;
    private final Set<Integer> pressed=new HashSet<>();
    private Runnable backAction;

    @Override public void onCreate(Bundle state){
        super.onCreate(state);ui=new MobileUi(this);history=new RecentConnections(this);remoteOrientation=new RemoteOrientation(this);
        pageHost=new PageHost(this);setContentView(pageHost);
        preferences=getSharedPreferences("settings",MODE_PRIVATE);host=preferences.getString("host","api.crossdesk.cn");port=preferences.getInt("port",9099);
        recentPresence.sender=ids->{if(signaling!=null&&!signaling.isClosed())signaling.presenceRequest(ids);};
        recentPresence.changed=this::renderRecentConnections;
        remoteVersionCheck=new RemoteVersionCheck(this::showRemoteUpdate);
        appUpdates=new AppUpdateChecker(applicationVersion(),this::renderAppUpdates);
        previews=RemotePreviewStore.get(this);previews.addListener(previewChanged);previews.enforceConsent();
        announcementInbox=new AnnouncementInbox(new java.io.File(getNoBackupFilesDir(),"AnnouncementReads"));
        announcementInbox.sender=(request,id)->{if(signaling!=null&&!signaling.isClosed())signaling.announcementRequest(request,id);else announcementInbox.failed(id);};
        announcementInbox.changed=this::renderAnnouncements;
        if(android.os.Build.VERSION.SDK_INT>=33)getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,this::back);
        home("");
    }
    private int dp(float value){return ui.dp(value);}
    private LinearLayout column(){return ui.column();}
    private LinearLayout row(){return ui.row();}
    private TextView text(String title,int size,boolean bold){return ui.text(title,size,bold);}
    private String serverKey(){return host+":"+port;}
    private void toast(String value){Toast.makeText(this,value,Toast.LENGTH_SHORT).show();}
    private void hideKeyboard(){((InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(root.getWindowToken(),0);root.clearFocus();}
    // Legacy inset handling also includes the IME on non-session pages. The
    // decor-fitting call below is restricted to Android 11–14.
    @SuppressWarnings("deprecation")
    private void installRoot(String nextPage,boolean dark){
        PageHost.Motion motion=PageHost.Motion.NONE;
        if(canvas!=null&&foreground&&!page.equals(nextPage)){
            if(nextPage.equals("session"))motion=PageHost.Motion.SESSION_IN;
            else if(page.equals("session"))motion=PageHost.Motion.SESSION_OUT;
            else motion=navigatingBack||nextPage.equals("home")?PageHost.Motion.POP:PageHost.Motion.PUSH;
        }
        if(root!=null)hideKeyboard();page=nextPage;
        // Remote controls own the IME inset. Android 11–14 otherwise also resize
        // the content, unlike Android 15's enforced edge-to-edge layout.
        if(android.os.Build.VERSION.SDK_INT>=30&&android.os.Build.VERSION.SDK_INT<35)getWindow().setDecorFitsSystemWindows(!dark);
        signalBadge=null;identitySummary=null;announcementBadge=null;announcementView=null;announcementRefresh=null;
        settingsUpdateDot=null;aboutUpdateDot=null;settingsUpdateButton=null;aboutUpdateRow=null;aboutUpdateLabel=null;
        previewSwitch=null;clearPreviewsButton=null;previewCleanupError=null;previewCards.clear();recentGrid=null;
        canvas=new FrameLayout(this);canvas.setBackgroundColor(dark?Color.BLACK:BACKGROUND);
        safeArea=new FrameLayout(this);canvas.addView(safeArea,new FrameLayout.LayoutParams(-1,-1));
        root=ui.pageColumn(dark?0:nextPage.equals("home")?1200:720);
        safeArea.addView(root,new FrameLayout.LayoutParams(-1,-1,Gravity.TOP|Gravity.CENTER_HORIZONTAL));
        safeArea.setOnApplyWindowInsetsListener((view,insets)->{
            if(dark&&(pictureInPictureUi||isInPictureInPictureMode())){view.setPadding(0,0,0,0);return insets;}
            if(dark&&android.os.Build.VERSION.SDK_INT>=30){
                android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
                view.setPadding(bars.left,bars.top,bars.right,bars.bottom);
                if(controls!=null)controls.keyboardInsets(Math.max(0,insets.getInsets(WindowInsets.Type.ime()).bottom-bars.bottom),insets.isVisible(WindowInsets.Type.ime()),bars.bottom);
            }else view.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());return insets;
        });
        root.setFocusableInTouchMode(true);pageHost.show(canvas,motion);
        applySystemBars();
    }
    // Android 8–10 require system UI flags instead of WindowInsetsController.
    @SuppressWarnings("deprecation")
    private void applySystemBars() {
        boolean sessionPage = "session".equals(page);
        boolean immersive = sessionPage && !isInMultiWindowMode();
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller == null) return;
            int light = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                    | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
            controller.setSystemBarsAppearance(sessionPage ? 0 : light, light);
            int bars = WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars();
            if (immersive) {
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(bars);
            } else {
                controller.show(bars);
            }
        } else {
            int flags = sessionPage ? 0 : View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
            if (immersive) flags |= View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION;
            getWindow().getDecorView().setSystemUiVisibility(flags);
        }
    }
    private EditText field(String hint,int type){
        EditText v=new EditText(this);v.setHint(hint);v.setContentDescription(hint);v.setInputType(type);v.setSingleLine(true);v.setTextSize(17);v.setTextColor(INK);v.setHintTextColor(SECONDARY);
        v.setPadding(dp(14),0,dp(14),0);v.setBackground(ui.background(BACKGROUND,12));v.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO);return v;
    }
    private LinearLayout navigation(String title,Runnable back){
        LinearLayout bar=row();bar.setPadding(dp(12),0,dp(12),0);root.addView(bar,ui.size(-1,56));
        bar.addView(ui.iconButton("back",L10n.tr("返回"),this::navigateBack),ui.size(44,44));
        TextView label=text(title,17,true);label.setGravity(Gravity.CENTER);label.setSingleLine();label.setEllipsize(TextUtils.TruncateAt.END);bar.addView(label,new LinearLayout.LayoutParams(0,-1,1));
        bar.addView(new View(this),ui.size(44,44));backAction=back;return bar;
    }
    LinearLayout formPage(String name,String title,Runnable back){
        installRoot(name,false);navigation(title,back);ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout content=column();content.setPadding(dp(20),dp(16),dp(20),dp(28));scroll.addView(content);return content;
    }
    LinearLayout section(LinearLayout content,String title){
        return section(content,title,12);
    }
    private LinearLayout section(LinearLayout content,String title,int radius){
        if(!title.isEmpty()){TextView label=text(title,13,false);label.setTextColor(SECONDARY);label.setPadding(dp(16),dp(12),0,dp(8));content.addView(label);}
        LinearLayout card=column();ui.card(card,radius);content.addView(card);return card;
    }
    void footer(LinearLayout content,String title){TextView v=text(title,12,false);v.setTextColor(SECONDARY);v.setLineSpacing(dp(3),1);v.setPadding(dp(16),dp(8),dp(16),dp(16));content.addView(v);}
    LinearLayout link(LinearLayout parent,String title,String detail,Runnable action){
        LinearLayout item=row();item.setPadding(dp(16),dp(14),dp(12),dp(14));item.setMinimumHeight(dp(48));
        item.addView(text(title,16,false),new LinearLayout.LayoutParams(0,-2,1));TextView value=text(detail,15,false);value.setTextColor(SECONDARY);item.addView(value);
        TextView chevron=text(" ›",23,false);chevron.setTextColor(0xFFB7B7BD);item.addView(chevron);item.setContentDescription(title+(detail.isEmpty()?"":"，"+detail));item.setClickable(true);item.setFocusable(true);item.setOnClickListener(v->action.run());parent.addView(item);
        return item;
    }
    private boolean isTablet(){
        boolean tablet=getResources().getConfiguration().smallestScreenWidthDp>=600;
        if(android.os.Build.VERSION.SDK_INT>=30){
            android.graphics.Rect bounds=getWindowManager().getMaximumWindowMetrics().getBounds();
            tablet|=Math.min(bounds.width(),bounds.height())>=dp(600);
        }
        return tablet;
    }
    @android.annotation.SuppressLint("SourceLockedOrientationActivity") // Phones keep the portrait home; tablets and multi-window follow user rotation settings.
    private void updateHomeOrientation(){
        remoteOrientation.end();
        int orientation=isTablet()||isInMultiWindowMode()?ActivityInfo.SCREEN_ORIENTATION_FULL_USER:ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
        if(getRequestedOrientation()!=orientation)setRequestedOrientation(orientation);
    }
    private void home(String message){
        backAction=null;updateHomeOrientation();installRoot("home",false);
        LinearLayout toolbar=row();toolbar.setPadding(dp(20),0,dp(20),0);root.addView(toolbar,ui.size(-1,56));
        signalBadge=text("",12,true);signalBadge.setGravity(Gravity.CENTER);signalBadge.setPadding(dp(11),0,dp(11),0);signalBadge.setSingleLine(true);
        signalBadge.setOnClickListener(v->{
            if(!preferences.getBoolean("networkConsent",false))consent(this::renderSignaling);
            else if(signalState==1)toast(L10n.tr("已连接 ")+serverKey());
            else {stopSignaling();ensureSignaling();}
        });renderSignaling();toolbar.addView(signalBadge,ui.size(-2,34));toolbar.addView(new View(this),new LinearLayout.LayoutParams(0,1,1));
        FrameLayout announcementButton=(FrameLayout)ui.iconButton("envelope",L10n.tr("通知公告"),this::announcements);announcementButton.setClipChildren(false);toolbar.setClipChildren(false);toolbar.setClipToPadding(false);
        announcementBadge=text("",10,true);announcementBadge.setTextColor(Color.WHITE);announcementBadge.setGravity(Gravity.CENTER);announcementBadge.setMinWidth(dp(16));announcementBadge.setPadding(dp(4),0,dp(4),0);announcementBadge.setBackground(ui.background(RED,12));announcementBadge.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        FrameLayout.LayoutParams badgePosition=new FrameLayout.LayoutParams(-2,dp(16),Gravity.TOP|Gravity.END);badgePosition.topMargin=-dp(3);badgePosition.setMarginEnd(-dp(5));announcementButton.addView(announcementBadge,badgePosition);renderAnnouncements();
        toolbar.addView(announcementButton,ui.size(40,40));View spacer=new View(this);toolbar.addView(spacer,ui.size(12,1));
        FrameLayout settingsButton=(FrameLayout)ui.iconButton("settings",L10n.tr("设置"),this::settings);settingsButton.setClipChildren(false);settingsUpdateButton=settingsButton;
        TextView updateBadge=text("!",10,true);updateBadge.setTextColor(Color.WHITE);updateBadge.setGravity(Gravity.CENTER);updateBadge.setIncludeFontPadding(false);updateBadge.setBackground(ui.background(RED,8));updateBadge.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);settingsUpdateDot=updateBadge;
        FrameLayout.LayoutParams updatePosition=new FrameLayout.LayoutParams(dp(16),dp(16),Gravity.TOP|Gravity.END);updatePosition.topMargin=-dp(3);updatePosition.setMarginEnd(-dp(5));settingsButton.addView(settingsUpdateDot,updatePosition);
        toolbar.addView(settingsButton,ui.size(40,40));renderAppUpdates();
        HomeLayout layout=new HomeLayout(ui);root.addView(layout,new LinearLayout.LayoutParams(-1,0,1));LinearLayout content=layout.panels;
        LinearLayout connection=column();connection.setPadding(dp(18),dp(18),dp(18),dp(18));content.addView(ui.shadowCard(connection,20));
        connection.addView(text(L10n.tr("远程桌面"),20,true));ui.gap(connection,16);
        LinearLayout entry=row();connection.addView(entry,ui.size(-1,54));
        remote=field(L10n.tr("设备 ID 或 IP 地址"), InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_VARIATION_URI | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        remote.setImeOptions(remote.getImeOptions() | android.view.inputmethod.EditorInfo.IME_FLAG_FORCE_ASCII);
        remote.setTextSize(20);remote.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
        remote.setOnFocusChangeListener((view,focused)->{GradientDrawable shape=ui.background(BACKGROUND,12);shape.setStroke(dp(focused?2:1),focused?BLUE:LINE);remote.setBackground(shape);});
        remote.setFilters(new InputFilter[]{new InputFilter.LengthFilter(160)});remote.setText(formatId(preferences.getString("lastRemote","")));
        entry.addView(remote,new LinearLayout.LayoutParams(0,-1,1));
        TextView connect=ui.primary(L10n.tr("连接"),()->promptConnection(id()));connect.setContentDescription(L10n.tr("连接"));LinearLayout.LayoutParams buttonParams=ui.size(96,54);buttonParams.leftMargin=dp(10);entry.addView(connect,buttonParams);
        Runnable update=()->{connect.setEnabled(!id().isEmpty());connect.setAlpha(id().isEmpty()?.45f:1);};update.run();
        remote.addTextChangedListener(new TextWatcher(){boolean editing;public void beforeTextChanged(CharSequence s,int start,int count,int after){}public void onTextChanged(CharSequence s,int start,int before,int count){update.run();}public void afterTextChanged(Editable value){if(editing)return;String formatted=formatId(value.toString());if(!formatted.equals(value.toString())){editing=true;int cursor=remote.getSelectionStart(),digits=value.subSequence(0,Math.max(0,cursor)).toString().replaceAll("[^0-9]","").length();remote.setText(formatted);remote.setSelection(Math.min(formatted.length(),digits+Math.max(0,digits-1)/3));editing=false;}}});
        LinearLayout recent=column();recent.setPadding(dp(16),dp(16),dp(16),dp(16));ui.card(recent,18);LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,0,1);p.topMargin=dp(20);content.addView(recent,p);
        JSONArray records=history.list(serverKey());recentPresence.watch(records,android.os.SystemClock.elapsedRealtime());LinearLayout heading=row();heading.addView(text(L10n.tr("最近连接"),20,true),new LinearLayout.LayoutParams(0,-2,1));
        if(!serverKey().equals(recentFilterServer)){recentFilter=null;recentFilterServer=serverKey();}
        if(records.length()>0){
            recentCount=text("",12,false);recentCount.setTextColor(SECONDARY);heading.addView(recentCount);
            recentFilterButton=text("",13,true);recentFilterButton.setTextColor(BLUE);
            recentFilterButton.setSingleLine();recentFilterButton.setEllipsize(TextUtils.TruncateAt.END);
            recentFilterButton.setMaxWidth(dp(120));recentFilterButton.setPadding(dp(8),0,0,0);
            recentFilterButton.setGravity(Gravity.CENTER);recentFilterButton.setOnClickListener(v->showRecentFilter());
            heading.addView(recentFilterButton,ui.size(-2,44));
        }
        recent.addView(heading);
        if(records.length()==0){
            LinearLayout empty=column();empty.setGravity(Gravity.CENTER);recent.addView(empty,new LinearLayout.LayoutParams(-1,0,1));empty.addView(ui.icon("history",0xFFB6B6BB),ui.size(34,34));ui.gap(empty,12);
            TextView emptyTitle=text(L10n.tr("还没有连接记录"),17,true);emptyTitle.setGravity(Gravity.CENTER);empty.addView(emptyTitle);ui.gap(empty,8);TextView hint=text(L10n.tr("成功连接后，这里会显示最近的远程设备。"),13,false);hint.setTextColor(SECONDARY);hint.setGravity(Gravity.CENTER);empty.addView(hint);
        }else{
            ui.gap(recent,14);ScrollView scroll=new ScrollView(this);recent.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));recentGrid=column();scroll.addView(recentGrid);
            recentGrid.addOnLayoutChangeListener((view,left,top,right,bottom,oldLeft,oldTop,oldRight,oldBottom)->{
                int width=right-left;if(width<=0)return;
                int columns=width<dp(300)?1:Math.max(2,Math.min(4,(width+dp(12))/dp(192)));
                if(columns!=recentColumns){recentColumns=columns;view.post(()->{if(view==recentGrid)renderRecentConnections();});}
            });renderRecentConnections();
        }
        if(!message.isEmpty())pendingConnectionMessage=message;
        showConnectionMessage();
    }
    private void showConnectionMessage(){
        if(pendingConnectionMessage.isEmpty()||!foreground||isFinishing()||isDestroyed())return;
        if(connectionDialog!=null)connectionDialog.dismiss();
        connectionDialog=new AlertDialog.Builder(this).setTitle(L10n.tr("连接提示")).setMessage(pendingConnectionMessage).setPositiveButton(L10n.tr("确定"),null).create();
        connectionDialog.setOnDismissListener(dialog->connectionDialog=null);
        pendingConnectionMessage="";connectionDialog.show();
    }
    private String id(){return remote.getText().toString().replace(" ","");}
    static String formatId(String value) {
        String target = value.replace(" ", "");
        return target.matches("[0-9]{1,9}") ? target.replaceAll("(.{3})(?=.)", "$1 ") : target;
    }
    private void renderRecentConnections(){
        if(recentGrid==null)return;
        JSONArray all = history.list(serverKey());
        if (recentFilter != null && !recentFilter.isEmpty() && !RecentConnections.availableGroups(all).contains(recentFilter)) {
            recentFilter = null;
        }
        JSONArray records = recentPresence.ordered(all, android.os.SystemClock.elapsedRealtime());
        if (recentFilter != null) {
            JSONArray filtered = new JSONArray();
            for (int i = 0; i < records.length(); i++) {
                JSONObject item = records.optJSONObject(i);
                Set<String> groups = RecentConnections.groups(item);
                if (recentFilter.isEmpty() ? groups.isEmpty() : groups.contains(recentFilter)) filtered.put(item);
            }
            records = filtered;
        }
        recentCount.setText(L10n.format("{0} 台设备",records.length()));
        recentFilterButton.setText(recentGroupLabel(recentFilter)+" ▾");
        recentFilterButton.setContentDescription(L10n.tr("分组")+" · "+recentGroupLabel(recentFilter));
        recentGrid.removeAllViews();previewCards.clear();
        if(records.length()==0){
            TextView empty=text(L10n.tr("该分组暂无近期连接"),14,false);
            empty.setGravity(Gravity.CENTER);empty.setTextColor(SECONDARY);recentGrid.addView(empty,ui.size(-1,72));
        }
        for(int i=0;i<records.length();i+=recentColumns){LinearLayout line=row();line.setGravity(Gravity.TOP);recentGrid.addView(line);for(int j=i;j<i+recentColumns;j++){View card=j<records.length()?recentCard(records.optJSONObject(j)):new View(this);LinearLayout.LayoutParams cell=new LinearLayout.LayoutParams(0,-2,1);if(j>i)cell.setMarginStart(dp(12));line.addView(card,cell);}ui.gap(recentGrid,12);}
    }
    private String recentGroupLabel(String filter) {
        if (filter == null) return L10n.tr("全部设备");
        return filter.isEmpty() ? L10n.tr("未分组") : filter;
    }
    private void showRecentFilter() {
        String server = serverKey();
        List<String> filters = new ArrayList<>();
        filters.add(null);
        filters.add("");
        String[] groups = RecentConnections.availableGroups(history.list(server)).toArray(new String[0]);
        java.util.Collections.addAll(filters, groups);
        String[] labels = new String[filters.size()];
        for (int i = 0; i < labels.length; i++) labels[i] = recentGroupLabel(filters.get(i));
        AlertDialog.Builder picker = new AlertDialog.Builder(this).setTitle(L10n.tr("分组"))
            .setSingleChoiceItems(labels, filters.indexOf(recentFilter), (dialog, index) -> {
                if (!server.equals(serverKey())) return;
                recentFilter = filters.get(index);
                dialog.dismiss();
                renderRecentConnections();
            });
        if (groups.length > 0) {
            picker.setNeutralButton(L10n.tr("删除分组"), (dialog, which) ->
                new AlertDialog.Builder(this).setTitle(L10n.tr("删除分组"))
                    .setItems(groups, (list, index) -> confirmDeleteGroup(server, groups[index]))
                    .setNegativeButton(L10n.tr("取消"), null).show());
        }
        picker.show();
    }
    private void confirmDeleteGroup(String server, String group) {
        new AlertDialog.Builder(this).setTitle(group).setMessage(L10n.tr("组内设备和连接记录将保留。"))
            .setNegativeButton(L10n.tr("取消"), null)
            .setPositiveButton(L10n.tr("删除分组"), (dialog, which) -> {
                if (!server.equals(serverKey())) return;
                if (!history.deleteGroup(server, group)) toast(L10n.tr("保存失败，请重试"));
                renderRecentConnections();
            }).show();
    }
    private View recentCard(JSONObject item){
        if(item==null)return new View(this);String id=item.optString("id"),platform=item.optString("platform");
        LinearLayout card=column();ui.card(card,14);FrameLayout preview=new FrameLayout(this){@Override protected void onMeasure(int w,int h){super.onMeasure(w,MeasureSpec.makeMeasureSpec(MeasureSpec.getSize(w)*9/16,MeasureSpec.EXACTLY));}};
        int color=platform.equals("windows")?0xFF316AB7:platform.equals("macos")?0xFF8057B9:platform.equals("linux")?0xFFAE642B:0xFF677587;preview.setBackgroundColor(color);
        TextView label=text(platform.equals("windows")?"Windows":platform.equals("macos")?"macOS":platform.equals("linux")?"Linux":L10n.tr("远程电脑"),19,true);label.setGravity(Gravity.CENTER);label.setTextColor(Color.WHITE);preview.addView(label,new FrameLayout.LayoutParams(-1,-1));card.addView(preview,ui.size(-1,80));
        ImageView image=new ImageView(this);image.setScaleType(ImageView.ScaleType.CENTER_CROP);image.setVisibility(View.GONE);image.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);preview.addView(image,new FrameLayout.LayoutParams(-1,-1));
        String server=serverKey();Runnable load=()->{if(!image.isAttachedToWindow())return;Object binding=new Object();image.setTag(binding);image.setImageDrawable(null);image.setVisibility(View.GONE);label.setVisibility(View.VISIBLE);
            previews.load(server,id,bitmap->{if(image.getTag()!=binding||!image.isAttachedToWindow()){if(bitmap!=null)bitmap.recycle();return;}if(bitmap!=null){image.setImageBitmap(bitmap);image.setVisibility(View.VISIBLE);label.setVisibility(View.GONE);}});};previewCards.add(load);image.addOnAttachStateChangeListener(new View.OnAttachStateChangeListener(){public void onViewAttachedToWindow(View view){load.run();}public void onViewDetachedFromWindow(View view){image.setTag(null);}});if(image.isAttachedToWindow())load.run();
        Runnable actions=()->showRecentActions(server,item);
        boolean dark = (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)
                == Configuration.UI_MODE_NIGHT_YES;
        GradientDrawable capsule = ui.background(dark ? INK : BACKGROUND, 9);
        capsule.setStroke(Math.max(1, dp(0.75f)), dark ? 0xFF434348 : LINE);
        ImageButton more = new ImageButton(this);
        more.setImageResource(R.drawable.ic_more_horizontal);
        more.setScaleType(ImageView.ScaleType.CENTER);
        more.setBackground(new InsetDrawable(capsule, dp(9), dp(13.5f), dp(9), dp(13.5f)));
        more.setPadding(0, 0, 0, 0);
        more.setColorFilter(dark ? Color.WHITE : INK);
        more.setContentDescription(L10n.tr("更多操作"));
        more.setOnClickListener(v -> actions.run());
        preview.addView(more, new FrameLayout.LayoutParams(dp(44), dp(44), Gravity.TOP | Gravity.END));
        String title=RecentConnections.displayName(item);
        LinearLayout caption=column();caption.setPadding(dp(9),dp(9),dp(9),dp(9));TextView name=text(title,12,true);name.setSingleLine();name.setEllipsize(TextUtils.TruncateAt.END);caption.addView(name);ui.gap(caption,4);
        Set<String> groups=RecentConnections.groups(item);
        if(!groups.isEmpty()){
            TextView groupNames=text(TextUtils.join(" · ",groups),10,false);groupNames.setTextColor(SECONDARY);
            groupNames.setSingleLine();groupNames.setEllipsize(TextUtils.TruncateAt.END);caption.addView(groupNames);
        }
        LinearLayout details=row();TextView detail=text(id.contains(":") ? id : "ID "+id,10,false);detail.setTypeface(Typeface.create("sans-serif",Typeface.NORMAL));detail.setTextColor(SECONDARY);detail.setSingleLine();detail.setEllipsize(TextUtils.TruncateAt.END);details.addView(detail,new LinearLayout.LayoutParams(0,-2,1));
        if(item.optBoolean("remember")){View key=ui.icon("key",SECONDARY);key.setContentDescription(L10n.tr("已保存密码"));LinearLayout.LayoutParams keySize=ui.size(10,10);keySize.leftMargin=dp(4);details.addView(key,keySize);}
        boolean online=recentPresence.isOnline(id,android.os.SystemClock.elapsedRealtime());String availability=id.contains(":")?"IP":online?L10n.tr("在线"):L10n.tr("离线");TextView presence=text(availability,10,true);presence.setSingleLine();presence.setTextColor(online?GREEN:SECONDARY);LinearLayout.LayoutParams presenceSize=ui.size(-2,-2);presenceSize.leftMargin=dp(5);details.addView(presence,presenceSize);caption.addView(details);card.addView(caption);
        card.setContentDescription(L10n.tr("连接 ")+title+"，ID "+id+(item.optBoolean("remember")?L10n.tr("，已保存密码"):"")+"，"+availability);card.setClickable(true);card.setFocusable(true);card.setOnClickListener(v->promptConnection(id));
        card.setOnLongClickListener(v->{actions.run();return true;});return card;
    }
    private void showRecentActions(String server, JSONObject item) {
        String id = item.optString("id");
        String[] actions = {L10n.tr("连接"), L10n.tr("修改名称"), L10n.tr("修改分组"), L10n.tr("删除记录")};
        new AlertDialog.Builder(this).setTitle(RecentConnections.displayName(item)).setItems(actions, (dialog, index) -> {
            switch (index) {
                case 0: promptConnection(id); break;
                case 1: editRecentDevice(server, item, false); break;
                case 2: editRecentDevice(server, item, true); break;
                case 3: history.delete(server, id); home(""); break;
            }
        }).show();
    }
    private void editRecentDevice(String server, JSONObject item, boolean editGroups) {
        String id = item.optString("id");
        String title = RecentConnections.displayName(item);
        LinearLayout fields = column();
        fields.setPadding(dp(22), dp(12), dp(22), 0);
        fields.addView(text(editGroups ? title + " · ID " + id : "ID " + id, 13, false));
        EditText input = field(editGroups ? L10n.tr("新建分组（可选）") : item.optString("name", id), InputType.TYPE_CLASS_TEXT);
        Set<String> selected = RecentConnections.groups(item);
        if (editGroups) {
            addGroupChoices(fields, input, selected, RecentConnections.availableGroups(history.list(server)));
        } else {
            input.setText(title);
            input.setSelectAllOnFocus(true);
        }
        fields.addView(input, ui.size(-1, 48));
        ScrollView scroll = new ScrollView(this);
        scroll.addView(fields);
        new AlertDialog.Builder(this).setTitle(editGroups ? L10n.tr("修改分组") : L10n.tr("修改名称"))
            .setView(scroll).setNegativeButton(L10n.tr("取消"), null)
            .setPositiveButton(L10n.tr("保存"), (dialog, which) -> {
                String value = input.getText().toString().trim();
                if (editGroups) {
                    if (!value.isEmpty()) selected.add(value);
                    history.edit(server, id, null, selected);
                } else {
                    history.edit(server, id, value, null);
                }
                renderRecentConnections();
            }).show();
    }
    private void addGroupChoices(LinearLayout fields, EditText input, Set<String> selected, Set<String> groups) {
        CheckBox ungrouped = new CheckBox(this);
        ungrouped.setText(L10n.tr("未分组"));
        fields.addView(ungrouped);
        Runnable updateUngrouped = () -> ungrouped.setChecked(selected.isEmpty() && input.getText().toString().trim().isEmpty());
        List<CheckBox> choices = new ArrayList<>();
        for (String group : groups) {
            CheckBox choice = new CheckBox(this);
            choice.setText(group);
            choice.setChecked(selected.contains(group));
            choice.setOnClickListener(v -> {
                if (choice.isChecked()) selected.add(group);
                else selected.remove(group);
                updateUngrouped.run();
            });
            fields.addView(choice);
            choices.add(choice);
        }
        ungrouped.setOnClickListener(v -> {
            selected.clear();
            for (CheckBox choice : choices) choice.setChecked(false);
            input.setText("");
            updateUngrouped.run();
        });
        input.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            public void onTextChanged(CharSequence s, int start, int before, int count) { updateUngrouped.run(); }
            public void afterTextChanged(Editable s) {}
        });
        updateUngrouped.run();
    }
    private void promptConnection(String id){
        if (session != null || (passwordDialog != null && passwordDialog.isShowing()) ||
                (privacyDialog != null && privacyDialog.isShowing())) return;
        hideKeyboard();
        final String target = id.matches("[0-9]{9}") ? id : NativeSession.directEndpoint(id);
        if (target.isEmpty()) { remote.setError(L10n.tr("请输入设备 ID 或有效的 IP 地址")); remote.requestFocus(); return; }
        if (!target.equals(id())) remote.setText(formatId(target));
        if(!preferences.getBoolean("networkConsent",false)){consent(()->connectOrPrompt(target));return;}connectOrPrompt(target);
    }
    private void connectOrPrompt(String id){
        String stored;
        try{stored=history.password(serverKey(),id);}catch(Exception error){passwordSheet(id,L10n.tr("已保存密码无法读取，请重新输入"),true);return;}
        if(!stored.isEmpty()){rememberPassword=true;begin(id,stored);return;}
        boolean remember=history.remembersPassword(serverKey(),id);
        passwordSheet(id,remember?L10n.tr("已保存的密码未能保留，请重新输入一次"):"",remember);
    }
    private void consent(Runnable accepted){
        if(privacyDialog!=null&&privacyDialog.isShowing())return;
        Dialog dialog=new PrivacyConsentDialog(this,ui,()->{
            preferences.edit().putBoolean("networkConsent",true).apply();appUpdates.setEnabled(foreground);renderSignaling();ensureSignaling();accepted.run();
        });
        privacyDialog=dialog;dialog.setOnDismissListener(d->{if(privacyDialog==dialog)privacyDialog=null;});dialog.show();
    }
    private void passwordSheet(String id,String error,boolean rememberDefault){
        Dialog dialog=new Dialog(this);passwordDialog=dialog;LinearLayout sheet=column();sheet.setPadding(dp(22),dp(10),dp(22),dp(22));sheet.setBackground(ui.background(BACKGROUND,24));
        View handle=new View(this);handle.setBackground(ui.background(0xFFC6C6CB,3));LinearLayout.LayoutParams hp=ui.size(36,5);hp.gravity=Gravity.CENTER;sheet.addView(handle,hp);ui.gap(sheet,20);
        LinearLayout header=row(),titles=column();titles.addView(text(L10n.tr("连接远程桌面"),22,true));ui.gap(titles,5);TextView subtitle=text(id.contains(":") ? id : L10n.format("对端 ID  {0}",id),15,false);subtitle.setTextColor(SECONDARY);titles.addView(subtitle);header.addView(titles,new LinearLayout.LayoutParams(0,-2,1));header.addView(ui.iconButton("close",L10n.tr("关闭"),dialog::dismiss),ui.size(36,36));sheet.addView(header);ui.gap(sheet,18);
        LinearLayout passwordRow=row();ui.card(passwordRow,10);EditText password=field(L10n.tr(id.contains(":") ? "IP 直连密码" : "访问密码"),InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_PASSWORD);password.setBackgroundColor(Color.TRANSPARENT);passwordRow.addView(password,new LinearLayout.LayoutParams(0,dp(50),1));
        View eye=ui.iconButton("eye",L10n.tr("显示密码"),()->{boolean hidden=password.getTransformationMethod()!=null;password.setTransformationMethod(hidden?null:PasswordTransformationMethod.getInstance());password.setSelection(password.length());});passwordRow.addView(eye,ui.size(44,44));sheet.addView(passwordRow);ui.gap(sheet,18);
        LinearLayout save=row(),saveText=column();saveText.addView(text(L10n.tr("保存密码"),15,true));ui.gap(saveText,3);TextView security=text(L10n.tr("密码将加密保存在本机 Android Keystore 中"),11,false);security.setTextColor(SECONDARY);saveText.addView(security);save.addView(saveText,new LinearLayout.LayoutParams(0,-2,1));Switch remember=new Switch(this);remember.setContentDescription(L10n.tr("保存密码"));save.addView(remember);sheet.addView(save);
        remember.setChecked(rememberDefault);
        ui.gap(sheet,18);TextView connect=ui.primary(L10n.tr("连接"),()->{
            if (!dialog.isShowing() || session != null) return;
            String secret=password.getText().toString();if(secret.isEmpty()||secret.length()>128||secret.contains("@")||(id.contains(":")&&!secret.matches("[a-zA-Z0-9]{6}"))){password.setError(L10n.tr("请输入有效访问密码"));return;}
            rememberPassword=remember.isChecked();password.setText("");dialog.dismiss();begin(id,secret);
        });connect.setContentDescription(L10n.tr("确认连接"));sheet.addView(connect,ui.size(-1,48));
        ScrollView scroll=new ScrollView(this);scroll.addView(sheet);
        // This floating dialog uses window-managed IME resizing on all supported APIs.
        @SuppressWarnings("deprecation") int resizeMode=WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE;
        dialog.setContentView(scroll);Window window=dialog.getWindow();if(window!=null){window.setBackgroundDrawableResource(android.R.color.transparent);window.setSoftInputMode(resizeMode);window.setDimAmount(.22f);}dialog.show();sizePasswordDialog();password.requestFocus();if(!error.isEmpty())password.setError(error);
        dialog.setOnDismissListener(d->{password.setText("");if(passwordDialog==dialog)passwordDialog=null;});
    }
    private void sizePasswordDialog(){
        if(passwordDialog==null||passwordDialog.getWindow()==null)return;
        int width=getResources().getConfiguration().screenWidthDp;
        Window window=passwordDialog.getWindow();window.setLayout(width>=600?dp(Math.min(520,width-48)):-1,-2);
        window.setGravity(width>=600?Gravity.CENTER:Gravity.BOTTOM);
    }
    private void begin(String id,String secret){
        if (session != null) return;
        videoWidth=videoHeight=0;enteredPictureInPicture=false;
        pictureInPicture.disconnect();updatePictureInPicture();
        remoteServiceAvailable=false;receivedFile=null;
        resetRemoteVersionCheck();
        resetVideoSettings();
        previewCapture=previews.begin(serverKey(),id);previewCopying=false;previewAttempts=0;
        connectedRemote=id;pendingPassword=rememberPassword?secret:"";ready=false;muted=false;selectedDisplay=0;hostPlatform="";remoteClipboard="";displays=new JSONArray();preferences.edit().putString("lastRemote",id).apply();
        if (!id.contains(":")) {
            ensureSignaling();
            if(signaling==null){pendingPassword="";toast(L10n.tr("请先完成联网授权"));return;}
        }
        session=new NativeSession(this,host,port,id,secret,this);
        showProgress();
        session.start();
        if(signalState==1)session.connectAs(localIdentity);
    }
    private void showProgress(){
        Dialog dialog=new Dialog(this);progressDialog=dialog;LinearLayout card=column();ui.card(card,24);card.setGravity(Gravity.CENTER_HORIZONTAL);LinearLayout body=column();body.setGravity(Gravity.CENTER);body.setPadding(dp(26),dp(24),dp(26),dp(20));
        ProgressBar spinner=new ProgressBar(this);spinner.setIndeterminateTintList(ColorStateList.valueOf(BLUE));body.addView(spinner,ui.size(34,34));ui.gap(body,14);TextView title=text(L10n.tr("正在连接"),20,true);title.setGravity(Gravity.CENTER);body.addView(title);ui.gap(body,6);status=text(L10n.tr(connectedRemote.contains(":") ? "正在建立安全连接…" : "正在连接信令服务器…"),14,false);status.setGravity(Gravity.CENTER);status.setTextColor(SECONDARY);status.setMinHeight(dp(36));body.addView(status);card.addView(body,ui.size(-1,-2));ui.divider(card);card.addView(ui.action(L10n.tr("取消连接"),RED,()->end("")),ui.size(-1,46));
        dialog.setContentView(card);dialog.setCanceledOnTouchOutside(false);dialog.setOnCancelListener(d->end(""));Window w=dialog.getWindow();if(w!=null){w.setBackgroundDrawableResource(android.R.color.transparent);w.setDimAmount(.08f);}dialog.show();if(w!=null)w.setLayout(dp(292),-2);
    }
    private void sessionScreen(boolean autoLandscape){
        installRoot("session",true);remoteOrientation.begin(autoLandscape,!isTablet()&&!isInMultiWindowMode());video=new RemoteVideoView(this,session);video.setRelative(preferences.getBoolean("relativeMouse",true));
        // Video, cursor and touch input must share the area inside system bars
        // and display cutouts; fitting to the outer canvas hides image edges.
        safeArea.addView(video,0,new FrameLayout.LayoutParams(-1,-1));
        controls=new SessionControls(this,ui,video::setKeyboardInset,new SessionControls.Actions(){
            public void disconnect(){confirmDisconnect();}public void display(){selectDisplay();}public void clipboard(){MainActivity.this.clipboard();}
            public void sendFile(){chooseFile();}public void saveFile(){saveReceivedFile();}public void secureAttention(){MainActivity.this.secureAttention();}
            public void togglePrivacy(){if(ready&&session!=null)session.togglePrivacy();}
            public void key(int code){tapKey(code);}
            public boolean textEdit(int count,String value){return sendTextEdit(count,value);}
            public void chord(int code,int[] modifiers){if(ready&&session!=null)session.chord(code,modifiers);}public void shortcut(int code){MainActivity.this.shortcut(code);}public void text(String value){sendText(value);}
            public void mute(){muted=!muted;if(session!=null)session.control(2,muted?0:1);controls.setMuted(muted);}
            public void mouse(){boolean relative=!preferences.getBoolean("relativeMouse",true);preferences.edit().putBoolean("relativeMouse",relative).apply();video.setRelative(relative);controls.setRelative(relative);}
            public void displayResolution(RemoteDisplayResolutions.Mode mode){updateDisplayResolution(mode);}
            public void videoSettings(int field,int value){RemoteVideoSettings.Values old=MainActivity.this.videoSettings.selection;updateVideoSettings(new RemoteVideoSettings.Values(field==0?value:old.quality,field==1?value:old.frameRate,field==2?value:old.preference));}
        });controls.setHostPlatform(hostPlatform);controls.setRelative(preferences.getBoolean("relativeMouse",true));controls.setMuted(muted);controls.videoSettings(videoSettings);controls.displayResolutions(displayResolutions,selectedDisplay);root.addView(controls,new LinearLayout.LayoutParams(-1,-1));getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        controls.attachMouseBar(safeArea,safeArea.indexOfChild(root),new VirtualMouseBar(ui,video.mouseInput));
        privacyChanged();
        video.remoteFocusChanged=()->{if(controls!=null)controls.resetKeyboardContext();};
        controls.requestApplyInsets();
        video.addOnLayoutChangeListener((view,left,top,right,bottom,oldLeft,oldTop,oldRight,oldBottom)->{
            if(!pictureInPictureUi&&!isInPictureInPictureMode())updatePictureInPicture();
        });
        pictureInPicture.connected();updatePictureInPicture();
    }
    private void confirmDisconnect(){disconnectDialog=new AlertDialog.Builder(this).setTitle(L10n.tr("断开远程连接？")).setMessage(L10n.tr("断开后将返回首页")).setNegativeButton(L10n.tr("取消"),null).setPositiveButton(L10n.tr("断开连接"),(d,w)->end("")).show();}
    private void end(String reason){
        dismissPrivacyUnlockDialog();
        remoteOrientation.end();
        if(backgroundSession!=null){BackgroundSession retained=backgroundSession;backgroundSession=null;retained.detach(this);retained.finish(reason);}
        boolean closeWindow=isInPictureInPictureMode();
        pictureInPicture.disconnect();updatePictureInPicture();
        if(controls!=null)controls.cancelMouseInput();if(video!=null)video.releaseMouse();
        mainHandler.removeCallbacks(documentTimeout);documentSession=null;documentToSave=null;receivedFile=null;remoteServiceAvailable=false;
        resetRemoteVersionCheck();
        resetVideoSettings();
        previewCapture=null;previewCopying=false;
        if(progressDialog!=null){progressDialog.dismiss();progressDialog=null;}
        if(session!=null){releaseKeys();session.close();session=null;}
        if(!foreground)stopSignaling();
        pendingPassword="";remoteClipboard="";hostPlatform="";displays=new JSONArray();ready=false;video=null;controls=null;getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if(closeWindow){finish();return;}
        home(reason);
        appUpdates.checkNow();
    }
    private void settings(){
        LinearLayout content=formPage("settings",L10n.tr("设置"),()->home(""));
        LinearLayout mouse=section(content,L10n.tr("鼠标控制"),MobileUi.SETTINGS_CARD_RADIUS);mouse.setPadding(dp(12),dp(12),dp(12),dp(12));boolean relative=preferences.getBoolean("relativeMouse",true);
        mouse.addView(segments(new String[]{L10n.tr("相对位置"),L10n.tr("绝对位置")},relative?0:1,true,index->preferences.edit().putBoolean("relativeMouse",index==0).apply()));
        LinearLayout picture=section(content,L10n.tr("画面偏好"),MobileUi.SETTINGS_CARD_RADIUS);picture.setPadding(dp(12),dp(12),dp(12),dp(12));int preference=Math.max(0,Math.min(2,preferences.getInt("videoPreference",1)));
        picture.addView(segments(RemoteVideoSettings.preferences(),preference,true,index->preferences.edit().putInt("videoPreference",index).apply()));ui.gap(content,28);
        LinearLayout direction=section(content,L10n.tr("屏幕方向"),MobileUi.SETTINGS_CARD_RADIUS);LinearLayout directionRow=row();directionRow.setPadding(dp(16),dp(10),dp(16),dp(10));directionRow.addView(text(L10n.tr("连接建立后锁定横屏"),16,false),new LinearLayout.LayoutParams(0,-2,1));
        Switch autoLandscape=new Switch(this);autoLandscape.setContentDescription(L10n.tr("连接建立后锁定横屏"));autoLandscape.setChecked(preferences.getBoolean("autoLandscape",false));autoLandscape.setOnCheckedChangeListener((button,checked)->preferences.edit().putBoolean("autoLandscape",checked).apply());directionRow.addView(autoLandscape);direction.addView(directionRow);
        footer(content,L10n.tr("默认关闭。开启后，手机连接时进入并保持横屏；关闭时跟随设备方向。平板和多窗口不受影响。"));
        LinearLayout server=section(content,"",MobileUi.SETTINGS_CARD_RADIUS);LinearLayout fields=column();fields.setVisibility(View.GONE);boolean custom=!host.equals("api.crossdesk.cn")||port!=9099;
        link(server,L10n.tr("服务器"),custom?L10n.tr("自定义"):L10n.tr("默认"),()->fields.setVisibility(fields.getVisibility()==View.VISIBLE?View.GONE:View.VISIBLE));server.addView(fields);
        LinearLayout toggleRow=row();toggleRow.setPadding(dp(16),dp(8),dp(16),dp(8));toggleRow.addView(text(L10n.tr("使用自定义服务器"),16,false),new LinearLayout.LayoutParams(0,-2,1));Switch toggle=new Switch(this);toggle.setContentDescription(L10n.tr("使用自定义服务器"));toggle.setChecked(custom);toggleRow.addView(toggle);fields.addView(toggleRow);
        LinearLayout inputs=column();inputs.setPadding(dp(12),0,dp(12),dp(12));EditText address=field(L10n.tr("服务器地址"),InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_URI),number=field(L10n.tr("服务器端口"),InputType.TYPE_CLASS_NUMBER);address.setText(custom?host:"");number.setText(custom?String.valueOf(port):"");inputs.addView(address,ui.size(-1,48));ui.gap(inputs,8);inputs.addView(number,ui.size(-1,48));ui.gap(inputs,8);
        inputs.addView(ui.action(L10n.tr("完成"),BLUE,()->{if(saveServer(address,number)){hideKeyboard();settings();}}));fields.addView(inputs);inputs.setVisibility(custom?View.VISIBLE:View.GONE);
        toggle.setOnCheckedChangeListener((button,checked)->{inputs.setVisibility(checked?View.VISIBLE:View.GONE);if(!checked){host="api.crossdesk.cn";port=9099;preferences.edit().putString("host",host).putInt("port",port).apply();ensureSignaling();} });
        backAction=()->{if(toggle.isChecked()&&!saveServer(address,number))return;hideKeyboard();home("");};
        identitySummary=text("",12,false);identitySummary.setTextColor(SECONDARY);identitySummary.setGravity(Gravity.CENTER);identitySummary.setPadding(dp(16),dp(12),dp(16),dp(16));identitySummary.setTextIsSelectable(true);content.addView(identitySummary);renderSignaling();
        LinearLayout trust=section(content,L10n.tr("本机配对信息"),MobileUi.SETTINGS_CARD_RADIUS);
        link(trust,L10n.tr("查看指纹"),"",this::showPairingInfo);ui.gap(content,28);
        LinearLayout privacy=section(content,L10n.tr("隐私"),MobileUi.SETTINGS_CARD_RADIUS);link(privacy,L10n.tr("隐私与授权"),preferences.getBoolean("networkConsent",false)?L10n.tr("已授权"):L10n.tr("未授权"),this::privacyPage);ui.divider(privacy);
        LinearLayout preview=row();preview.setPadding(dp(16),dp(10),dp(16),dp(10));preview.addView(text(L10n.tr("保存远程画面预览"),16,false),new LinearLayout.LayoutParams(0,-2,1));previewSwitch=new Switch(this);previewSwitch.setContentDescription(L10n.tr("保存远程画面预览"));preview.addView(previewSwitch);privacy.addView(preview);
        previewSwitch.setOnCheckedChangeListener((button,checked)->{if(updatingPreviewSwitch)return;if(checked){renderPreviews();confirmPreviewSaving();}else previews.setEnabled(false);});
        ui.divider(privacy);clearPreviewsButton=ui.action(L10n.tr("清除预览图"),RED,this::confirmClearPreviews);clearPreviewsButton.setGravity(Gravity.START|Gravity.CENTER_VERTICAL);clearPreviewsButton.setTypeface(Typeface.DEFAULT);clearPreviewsButton.setPadding(dp(16),0,dp(16),0);privacy.addView(clearPreviewsButton,ui.size(-1,48));
        previewCleanupError=text("",12,false);previewCleanupError.setTextColor(RED);previewCleanupError.setPadding(dp(16),dp(8),dp(16),dp(12));privacy.addView(previewCleanupError);renderPreviews();
        ui.gap(content,28);LinearLayout about=section(content,"",MobileUi.SETTINGS_CARD_RADIUS);LinearLayout aboutLink=link(about,L10n.tr("关于"),L10n.tr("新版本"),this::about);aboutUpdateRow=aboutLink;
        aboutUpdateLabel=(TextView)aboutLink.getChildAt(1);aboutUpdateLabel.setPadding(0,0,dp(8),0);
        aboutUpdateDot=updateDot();LinearLayout.LayoutParams dotPosition=new LinearLayout.LayoutParams(dp(8),dp(8));dotPosition.setMarginEnd(dp(8));aboutLink.addView(aboutUpdateDot,2,dotPosition);renderAppUpdates();
    }
    private void showPairingInfo(){
        NativeSession.pairingInfo(this,localIdentity,info->{
            if(isFinishing()||isDestroyed()||!page.equals("settings"))return;
            if(info==null){toast(L10n.tr("安全存储不可用，请解锁设备后重试"));return;}
            TextView fingerprint=text(info.optString("fingerprint").replaceAll("(.{4})(?!$)","$1 "),14,false);
            fingerprint.setTypeface(android.graphics.Typeface.MONOSPACE);fingerprint.setTextIsSelectable(true);fingerprint.setPadding(dp(24),dp(16),dp(24),dp(16));
            new AlertDialog.Builder(this).setTitle(L10n.tr("本机配对信息"))
                .setMessage(L10n.tr("将配对信息添加到被控端的可信设备列表，并核对指纹。"))
                .setView(fingerprint).setNegativeButton(L10n.tr("取消"),null)
                .setPositiveButton(L10n.tr("复制配对信息"),(dialog,which)->{
                    try{((ClipboardManager)getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("CrossDesk",info.getString("pairing")));toast(L10n.tr("配对信息已复制。"));}
                    catch(Exception error){toast(L10n.tr("无法复制配对信息，请重试。"));}
                }).show();
        });
    }
    private void confirmPreviewSaving(){
        previewDialog=new AlertDialog.Builder(this).setTitle(L10n.tr("保存远程画面预览？"))
            .setMessage(L10n.tr("预览可能包含远程电脑上的个人或工作信息。开启后，每次连接会自动将一张画面保存在本机并显示在最近连接中。你可以随时关闭并清除。"))
            .setNegativeButton(L10n.tr("取消"),null).setPositiveButton(L10n.tr("开启保存"),(d,w)->previews.setEnabled(true)).show();
        previewDialog.setOnDismissListener(d->{previewDialog=null;renderPreviews();});
    }
    private void confirmClearPreviews(){
        previewDialog=new AlertDialog.Builder(this).setTitle(L10n.tr("清除预览图？"))
            .setMessage(L10n.tr("将删除本机保存的所有远程预览图，保留连接记录和密码。")+(previews.enabled()?L10n.tr("后续连接仍会保存新的预览图。"):""))
            .setNegativeButton(L10n.tr("取消"),null).setPositiveButton(L10n.tr("清除"),(d,w)->previews.clear()).show();
        previewDialog.setOnDismissListener(d->previewDialog=null);
    }
    private void renderPreviews(){
        if(previews==null)return;
        if(!previews.enabled()||previews.cleaning)previewCapture=null;
        if(previewSwitch!=null){updatingPreviewSwitch=true;previewSwitch.setChecked(previews.enabled());previewSwitch.setEnabled(preferences.getBoolean("networkConsent",false)&&!previews.cleaning);updatingPreviewSwitch=false;}
        if(clearPreviewsButton!=null){clearPreviewsButton.setText(previews.cleaning?L10n.tr("正在清除…"):L10n.tr("清除预览图"));clearPreviewsButton.setEnabled(!previews.cleaning);}
        if(previewCleanupError!=null){previewCleanupError.setText(previews.cleanupError);previewCleanupError.setVisibility(previews.cleanupError.isEmpty()?View.GONE:View.VISIBLE);}
        for(Runnable card:previewCards)card.run();
    }
    private void capturePreviewIfNeeded(){
        if(!ready||video==null||previewCapture==null||previewCopying||!previews.current(previewCapture))return;
        if(!history.contains(serverKey(),connectedRemote)){previewCapture=null;return;}
        RemotePreviewStore.Capture capture=previewCapture;NativeSession source=session;previewCopying=true;
        video.capturePreview(bitmap->{
            if(session!=source||previewCapture!=capture||!previews.current(capture)){if(bitmap!=null)bitmap.recycle();return;}
            previewCopying=false;
            if(bitmap!=null){previewCapture=null;previews.save(capture,bitmap);}
            else if(++previewAttempts>=3)previewCapture=null;
        });
    }
    private View segments(String[] titles,int selected,boolean enabled,java.util.function.IntConsumer changed){
        return new SegmentedControl(ui,titles,selected,enabled,changed);
    }
    private boolean saveServer(EditText address,EditText number){
        String name=address.getText().toString().trim();int value;try{value=Integer.parseInt(number.getText().toString());}catch(NumberFormatException e){number.setError(L10n.tr("请输入 1–65535 范围内的端口"));return false;}
        if(name.length()>253||!name.matches("[A-Za-z0-9.-]+")||name.startsWith(".")||name.endsWith(".")){address.setError(L10n.tr("填写主机名或 IPv4 地址，不含协议和路径"));return false;}if(value<1||value>65535){number.setError(L10n.tr("端口范围为 1–65535"));return false;}host=name;port=value;preferences.edit().putString("host",host).putInt("port",port).apply();ensureSignaling();return true;
    }
    private void announcements(){
        installRoot("announcements",false);LinearLayout bar=navigation(L10n.tr("通知公告"),()->home(""));
        bar.removeViewAt(bar.getChildCount()-1);announcementRefresh=ui.iconButton("refresh",L10n.tr("刷新公告"),announcementInbox::refresh);bar.addView(announcementRefresh,ui.size(44,44));
        announcementView=new AnnouncementView(ui,announcementInbox,null,this::announcementDetail,this::connectAnnouncements);
        root.addView(announcementView,new LinearLayout.LayoutParams(-1,0,1));announcementInbox.refresh();renderAnnouncements();
    }
    private void announcementDetail(AnnouncementInbox.Item item){
        installRoot("announcement-detail",false);navigation(L10n.tr("公告详情"),this::announcements);
        announcementView=new AnnouncementView(ui,announcementInbox,item,this::announcementDetail,this::connectAnnouncements);
        root.addView(announcementView,new LinearLayout.LayoutParams(-1,0,1));announcementInbox.markRead(item);
    }
    private void connectAnnouncements(){
        if(!preferences.getBoolean("networkConsent",false))consent(()->{ensureSignaling();renderAnnouncements();});
        else{stopSignaling();ensureSignaling();}
    }
    private void renderAnnouncements(){
        if(announcementBadge!=null){int unread=announcementInbox.unread;announcementBadge.setText(unread>99?"99+":String.valueOf(unread));announcementBadge.setVisibility(unread>0?View.VISIBLE:View.GONE);
            if(android.os.Build.VERSION.SDK_INT>=30)((View)announcementBadge.getParent()).setStateDescription(unread>0?L10n.format("{0} 条未读",unread):L10n.tr("无未读公告"));}
        if(announcementRefresh!=null){boolean enabled=announcementInbox.isConnected()&&!announcementInbox.loading;announcementRefresh.setEnabled(enabled);announcementRefresh.setAlpha(enabled?1:.4f);}
        if(announcementView!=null)announcementView.render();
    }
    @Override public void announcement(JSONObject message){
        if(!preferences.getBoolean("networkConsent",false)||signalState!=1)return;
        if("announcements_changed".equals(message.optString("type")))announcementInbox.catalogChanged(message.optBoolean("reset_pages"));else announcementInbox.receive(message);
    }
    @Override public void announcementFailed(String requestId){announcementInbox.failed(requestId);}
    private void privacyPage(){
        LinearLayout content=formPage("privacy",L10n.tr("隐私与授权"),this::settings);LinearLayout card=section(content,"");boolean consent=preferences.getBoolean("networkConsent",false);link(card,L10n.tr("授权状态"),consent?L10n.tr("已授权"):L10n.tr("未授权"),()->{});ui.divider(card);link(card,L10n.tr("隐私政策"),"",this::privacyPolicyPage);
        footer(content,L10n.tr("设备身份及选择保存的访问密码使用 Android Keystore 加密。连接记录仅保存在本机，不纳入设备备份。剪贴板内容在结束会话时清除。离开应用后通过画中画或连接通知保持会话，可在应用或通知中断开。"));
        if(consent)content.addView(ui.action(L10n.tr("撤回联网授权"),RED,()->new AlertDialog.Builder(this).setTitle(L10n.tr("撤回联网授权？")).setMessage(L10n.tr("撤回后将停止远程连接、关闭画面预览保存并清除已有预览。连接记录和密码会保留，重新连接前需再次授权。")).setNegativeButton(L10n.tr("取消"),null).setPositiveButton(L10n.tr("撤回"),(d,w)->{preferences.edit().putBoolean("networkConsent",false).apply();appUpdates.setEnabled(false);previews.setEnabled(false);stopSignaling();announcementInbox.reset();privacyPage();}).show()));else content.addView(ui.primary(L10n.tr("阅读并授权"),()->consent(this::privacyPage)));
    }
    private void privacyPolicyPage(){
        installRoot("privacy-policy",false);navigation(L10n.tr("隐私政策"),this::privacyPage);
        root.addView(new PrivacyPolicyDocument(this).view(ui),new LinearLayout.LayoutParams(-1,0,1));
    }
    String applicationVersion(){
        try{return getPackageManager().getPackageInfo(getPackageName(),0).versionName;}catch(android.content.pm.PackageManager.NameNotFoundException error){return "";}
    }
    View updateDot(){View dot=new View(this);dot.setBackground(ui.background(RED,4));dot.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);return dot;}
    boolean hasNetworkConsent(){return preferences.getBoolean("networkConsent",false);}
    void checkAppUpdates(){
        if(hasNetworkConsent())appUpdates.checkNow();
        else consent(appUpdates::checkNow);
    }
    private void renderAppUpdates(){
        boolean available=appUpdates.updateAvailable();
        if(settingsUpdateDot!=null)settingsUpdateDot.setVisibility(available?View.VISIBLE:View.GONE);
        if(aboutUpdateDot!=null)aboutUpdateDot.setVisibility(available?View.VISIBLE:View.GONE);
        if(aboutUpdateLabel!=null)aboutUpdateLabel.setVisibility(available?View.VISIBLE:View.GONE);
        if(settingsUpdateButton!=null)settingsUpdateButton.setContentDescription(available?L10n.tr("设置，有新版本"):L10n.tr("设置"));
        if(aboutUpdateRow!=null)aboutUpdateRow.setContentDescription(available?L10n.tr("关于，有新版本"):L10n.tr("关于"));
        if(aboutPages!=null&&page.equals("about"))aboutPages.renderUpdates();
    }
    private void about(){
        if(aboutPages==null)aboutPages=new AboutPages(this,ui,this::settings,appUpdates);aboutPages.show();
    }
    private void sendText(String value){sendTextEdit(0,value);}
    private boolean sendTextEdit(int count,String value){
        if(session==null)return false;
        if(session.textEdit(count,value))return true;
        toast(L10n.tr("当前远端不支持直接输入此文本，请使用支持文本输入的被控端版本，或通过剪贴板发送"));return false;
    }
    private void tapKey(int code){if(session!=null){session.key(code,true);session.key(code,false);}}
    private void shortcut(int code){if(ready&&session!=null)session.chord(code,new int[]{hostPlatform.equals("macos")?0x5B:0x11});}
    private void selectDisplay(){
        if(!ready)return;int count=Math.min(displays.length(),8);if(count==0){toast(L10n.tr("等待远端屏幕信息"));return;}String[] labels=new String[count];for(int i=0;i<count;i++){JSONObject display=displays.optJSONObject(i);labels[i]=(i+1)+" · "+(display==null?L10n.tr("显示器"):display.optString("name",L10n.tr("显示器")));}new AlertDialog.Builder(this).setTitle(L10n.tr("显示器")).setItems(labels,(d,w)->{if(session!=null){if(w!=selectedDisplay&&video!=null)video.resetViewport();selectedDisplay=w;session.control(4,w);if(controls!=null)controls.displayResolutions(displayResolutions,selectedDisplay);if(controls!=null)controls.resetVideoStatistics();}}).show();
    }
    private void secureAttention(){
        if(!ready||session==null||!hostPlatform.equals("windows"))return;
        if(!remoteServiceAvailable){controlFeedback(L10n.tr("请在远端启用 CrossDesk 系统服务后重试"));return;}
        session.secureAttention();
    }
    private void chooseFile(){
        if(!ready||session==null)return;
        openDocument(new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"),PICK_FILE);
    }
    private void saveReceivedFile(){
        if(!ready||session==null||receivedFile==null)return;
        documentToSave=receivedFile;
        openDocument(new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/octet-stream").putExtra(Intent.EXTRA_TITLE,receivedFile.getName()),SAVE_FILE);
    }
    private void openDocument(Intent intent,int request){
        if(documentSession!=null)return;
        documentSession=session;releaseKeys();
        pictureInPicture.choosingDocument(true);updatePictureInPicture();
        try{startActivityForResult(intent,request);mainHandler.postDelayed(documentTimeout,5*60*1000);}
        catch(android.content.ActivityNotFoundException error){documentSession=null;documentToSave=null;pictureInPicture.choosingDocument(false);updatePictureInPicture();controlFeedback(L10n.tr("此设备没有可用的文件选择器"));}
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);
        if(request!=PICK_FILE&&request!=SAVE_FILE)return;
        NativeSession source=documentSession;java.io.File saved=documentToSave;
        documentSession=null;documentToSave=null;mainHandler.removeCallbacks(documentTimeout);
        pictureInPicture.choosingDocument(false);updatePictureInPicture();
        if(source==null||source!=session||!ready||result!=RESULT_OK||data==null||data.getData()==null)return;
        if(request==PICK_FILE)source.sendFile(data.getData());else if(saved!=null)source.saveFile(saved,data.getData());
    }
    @Override public void fileTransfer(String name,double progress,boolean sending,java.io.File received){
        if(!ready)return;
        if(received!=null)receivedFile=received;
        String fileStatus=progress<0?L10n.format("{0} 传输失败",name):(sending?L10n.tr("发送 "):L10n.tr("接收 "))+name+(progress>=1?L10n.tr(" · 已完成"):" · "+(int)(progress*100)+"%");
        if(controls!=null)controls.fileStatus(fileStatus,receivedFile!=null);
        if(progress<0||progress>=1)toast(fileStatus);
    }
    @Override public void controlFeedback(String message){
        toast(message);
    }
    private void clipboard() {
        NativeSession target = session;
        if (!ready || target == null) return;
        EditText input = field(L10n.tr("要发送到远端的文本"),
                InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        input.setSingleLine(false);
        input.setMinLines(3);
        ClipboardManager clipboard = getSystemService(ClipboardManager.class);
        try {
            ClipData clip = clipboard.getPrimaryClip();
            if (clip != null) {
                for (int i = 0; i < clip.getItemCount(); i++) {
                    CharSequence text = clip.getItemAt(i).getText();
                    if (!TextUtils.isEmpty(text)) {
                        input.setText(text.toString());
                        break;
                    }
                }
            }
        } catch (SecurityException denied) {
            input.setError(L10n.tr("无法访问剪贴板，请重试。"));
        }
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(L10n.tr("剪贴板")).setView(input)
                .setMessage(L10n.tr("发送后，在远程键盘中点击“粘贴”。接收的远端文本可手动复制到本机。"))
                .setNegativeButton(L10n.tr("关闭"), null)
                .setNeutralButton(L10n.tr("复制远端文本"), (d, w) -> {
                    if (!remoteClipboard.isEmpty()) {
                        clipboard.setPrimaryClip(ClipData.newPlainText("CrossDesk", remoteClipboard));
                    }
                })
                .setPositiveButton(L10n.tr("发送文本"), null).show();
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String text = input.getText().toString();
            if (text.isEmpty()) {
                input.setError(L10n.tr("剪贴板中没有文本"));
                return;
            }
            byte[] bytes = text.getBytes(java.nio.charset.StandardCharsets.UTF_8);
            if (bytes.length > NativeSession.MAX_CLIPBOARD_BYTES) {
                input.setError(L10n.tr("文本超过 128 KB"));
                return;
            }
            if (text.indexOf('\0') >= 0 || !ready || session != target || target.isClosed()) {
                input.setError(L10n.tr("指令发送失败，请重试"));
                return;
            }
            target.sendClipboard(bytes);
            dialog.dismiss();
        });
    }
    void licenseDocument(String title,String value,Runnable back){
        installRoot("document:"+title,false);navigation(title,back);
        // Bound each selectable accessibility node while keeping every character of long notices.
        java.util.List<String> chunks=new java.util.ArrayList<>();
        for(int start=0;start<value.length();){int end=Math.min(start+4000,value.length());if(end<value.length()){int newline=value.lastIndexOf('\n',end-1);if(newline>start)end=newline+1;else if(Character.isHighSurrogate(value.charAt(end-1)))end--;}chunks.add(value.substring(start,end));start=end;}
        ListView paragraphs=new ListView(this);paragraphs.setBackgroundColor(Color.WHITE);paragraphs.setDivider(null);paragraphs.setClipToPadding(false);paragraphs.setPadding(0,dp(8),0,dp(8));
        paragraphs.setAdapter(new ArrayAdapter<String>(this,android.R.layout.simple_list_item_1,chunks){
            @Override public View getView(int position,View reuse,ViewGroup parent){
                TextView item=(TextView)super.getView(position,reuse,parent);item.setTextColor(INK);item.setTextSize(16);item.setAutoLinkMask(android.text.util.Linkify.WEB_URLS);item.setText(getItem(position));item.setTextIsSelectable(true);item.setLinksClickable(true);item.setLinkTextColor(BLUE);item.setPadding(dp(16),dp(8),dp(16),dp(8));return item;
            }
        });root.addView(paragraphs,new LinearLayout.LayoutParams(-1,0,1));
    }
    private void ensureSignaling(){
        if(!foreground||!preferences.getBoolean("networkConsent",false))return;
        if(signaling!=null&&!signaling.isClosed()&&signalingServer.equals(serverKey()))return;
        if(!announcementServer.equals(serverKey())){announcementInbox.reset();announcementServer=serverKey();}
        stopSignaling();signalState=0;signalError="";signalingServer=serverKey();
        signaling=new NativeSession(this,host,port,"","",this);
        renderSignaling();signaling.start();
    }
    private void stopSignaling(){
        mainHandler.removeCallbacks(presenceTick);recentPresence.setConnected(false,android.os.SystemClock.elapsedRealtime());
        announcementInbox.setConnected(false);
        if(signaling!=null)signaling.close();
        signaling=null;signalingServer="";localIdentity="";signalState=-1;signalError="";renderSignaling();
    }
    private void renderSignaling(){
        boolean consent=preferences.getBoolean("networkConsent",false);
        boolean connected=consent&&signalError.isEmpty()&&signalState==1;
        String label;int tint,background;
        if(!consent){label=L10n.tr("等待隐私授权");tint=0xFFB76A00;background=0xFFFFEBD2;}
        else if(!signalError.isEmpty()){label=signalError.contains("TLS")?L10n.tr("证书校验失败 · 点击重试"):L10n.tr("连接失败 · 点击重试");tint=RED;background=0xFFFFE8E6;}
        else if(connected){label=L10n.tr("已连接服务器");tint=GREEN;background=0xFFE5F4E8;}
        else{label=signalState==0?L10n.tr("正在连接服务器…"):signalState==4?L10n.tr("正在重连服务器…"):L10n.tr("未连接服务器 · 点击重试");tint=0xFFB76A00;background=0xFFFFEBD2;}
        if(signalBadge!=null){
            android.graphics.drawable.Drawable icon=connected?getDrawable(R.drawable.ic_check_circle):null;
            if(icon!=null){int size=dp(14);icon.setBounds(0,0,size,size);}
            signalBadge.setCompoundDrawablesRelative(icon,null,null,null);signalBadge.setCompoundDrawablePadding(dp(6));
            signalBadge.setText(label);signalBadge.setTextColor(tint);signalBadge.setBackground(ui.background(background,20));signalBadge.setContentDescription(signalError.isEmpty()?label:signalError+L10n.tr("，点击重试"));
        }
        if(identitySummary!=null)identitySummary.setText(!consent?L10n.tr("完成隐私授权后登记本机身份"):!localIdentity.isEmpty()?L10n.format("本机 ID  {0}",localIdentity):!signalError.isEmpty()?signalError:L10n.tr("正在获取本机 ID…"));
    }
    @Override public void signaling(int state,String deviceId){
        signalState=state;localIdentity=deviceId;signalError="";renderSignaling();
        if(state==1&&session!=null&&!ready)session.connectAs(deviceId);
        long now=android.os.SystemClock.elapsedRealtime();recentPresence.watch(history.list(serverKey()),now);
        recentPresence.setConnected(foreground&&preferences.getBoolean("networkConsent",false)&&state==1&&!deviceId.isEmpty(),now);
        mainHandler.removeCallbacks(presenceTick);if(recentPresence.isConnected())mainHandler.postDelayed(presenceTick,1000);
        if(preferences.getBoolean("networkConsent",false)){announcementInbox.configure(host,port,deviceId);announcementInbox.setConnected(state==1);}
    }
    @Override public void presence(JSONObject message){recentPresence.receive(message,android.os.SystemClock.elapsedRealtime());}
    @Override public void status(String value){if(session!=null&&status!=null)status.setText(value);}
    @Override public void connected(){
        // Transport readiness can be reported again after ICE changes its selected path.
        // The first success consumes pendingPassword; a duplicate must not save it again.
        if(session==null||ready)return;ready=true;if(progressDialog!=null){progressDialog.dismiss();progressDialog=null;}
        try{history.save(serverKey(),connectedRemote,connectedRemote,hostPlatform,rememberPassword,pendingPassword);}catch(Exception error){toast(L10n.tr("密码保存失败，本次连接仍可使用"));}pendingPassword="";sessionScreen(preferences.getBoolean("autoLandscape",false));
        if(!canUsePictureInPicture())prepareBackgroundSession(true);
        if(videoSettings.supported&&videoSettings.requestId==0)updateVideoSettings(videoSettings.selection);
        if(foreground&&preferences.getBoolean("networkConsent",false))remoteVersionCheck.connected();
    }
    @Override public void ended(String reason){
        if(session!=null)end(reason);
    }
    @Override public void signalingEnded(String reason){
        stopSignaling();signalError=reason;renderSignaling();
        if(session!=null&&!ready&&!connectedRemote.contains(":"))end(reason);
    }
    @Override public void passwordRejected(){
        if(session==null)return;
        String id=connectedRemote;boolean remember=rememberPassword;
        end("");
        // Let the user replace an expired credential; never retry it automatically.
        if(foreground)passwordSheet(id,L10n.tr("访问密码错误，请重新输入"),remember);
    }
    private void cancelVideoSettingsTimeout(){if(videoSettingsTimeout!=null)mainHandler.removeCallbacks(videoSettingsTimeout);videoSettingsTimeout=null;}
    private void showRemoteUpdate(){
        if(!ready||session==null||!foreground||pictureInPictureUi||isInPictureInPictureMode()||isFinishing()||!preferences.getBoolean("networkConsent",false))return;
        remoteUpdateDialog=new AlertDialog.Builder(this).setTitle(L10n.tr("升级提示")).setMessage(L10n.tr(RemoteVersionCheck.NOTICE)).setPositiveButton(L10n.tr("知道了"),null).create();
        remoteUpdateDialog.setOnDismissListener(dialog->remoteUpdateDialog=null);
        remoteUpdateDialog.setCanceledOnTouchOutside(false);
        remoteUpdateDialog.show();
    }
    private void resetRemoteVersionCheck(){
        remoteVersionCheck.reset();
        if(remoteUpdateDialog!=null){remoteUpdateDialog.dismiss();remoteUpdateDialog=null;}
    }
    private void cancelDisplayResolutionTimeout(){if(displayResolutionTimeout!=null)mainHandler.removeCallbacks(displayResolutionTimeout);displayResolutionTimeout=null;}
    private void updateDisplayResolution(RemoteDisplayResolutions.Mode mode){
        if(!ready||session==null||!hasNetworkConsent())return;
        RemoteDisplayResolutions.Display display=displayResolutions.display(selectedDisplay);
        if(!displayResolutions.pending&&display!=null&&display.supports(mode)&&display.current.same(mode)){
            session.rememberDisplayResolution(display);return;
        }
        RemoteDisplayResolutions.Request request=displayResolutions.begin(selectedDisplay,mode);if(request==null)return;
        cancelDisplayResolutionTimeout();session.displayResolution(request);
        if(controls!=null)controls.displayResolutions(displayResolutions,selectedDisplay);
        displayResolutionTimeout=()->{displayResolutions.expire(request.id);if(controls!=null)controls.displayResolutions(displayResolutions,selectedDisplay);};
        mainHandler.postDelayed(displayResolutionTimeout,10000);
    }
    @Override public void displayResolutionFailed(long requestId){
        displayResolutions.expire(requestId);
        if(!displayResolutions.pending)cancelDisplayResolutionTimeout();
        if(controls!=null)controls.displayResolutions(displayResolutions,selectedDisplay);
    }
    private void resetVideoSettings(){
        cancelDisplayResolutionTimeout();displayResolutions=new RemoteDisplayResolutions();
        cancelVideoSettingsTimeout();
        android.util.DisplayMetrics metrics=getResources().getDisplayMetrics();
        videoSettings=new RemoteVideoSettings(preferences.getInt("videoPreference",1),Math.max(metrics.widthPixels,metrics.heightPixels));
    }
    private void updateVideoSettings(RemoteVideoSettings.Values value){
        if(!ready||session==null||!preferences.getBoolean("networkConsent",false))return;
        long request=videoSettings.begin(value);if(request<0)return;
        cancelVideoSettingsTimeout();if(controls!=null)controls.videoSettings(videoSettings);session.videoSettings(value,request);
        videoSettingsTimeout=()->{videoSettings.expire(request);if(controls!=null)controls.videoSettings(videoSettings);};mainHandler.postDelayed(videoSettingsTimeout,5000);
    }
    @Override public void data(JSONObject message){
        if(session==null)return;
        if(message.optInt("type",-1)==3||message.optInt("type",-1)==16){
            RemoteDisplayResolutions.Display confirmed=displayResolutions.receive(message);
            if(confirmed!=null&&session!=null)session.rememberDisplayResolution(confirmed);
            if(!displayResolutions.pending)cancelDisplayResolutionTimeout();
            if(controls!=null)controls.displayResolutions(displayResolutions,selectedDisplay);
            if(message.optInt("type",-1)==16)return;
        }
        if(message.optInt("type",-1)==5){JSONObject service=message.optJSONObject("service_status");remoteServiceAvailable=service!=null&&service.optBoolean("available",false);return;}
        if(message.optInt("type",-1)==12){if(!ready)return;videoSettings.receive(message);if(!videoSettings.pending)cancelVideoSettingsTimeout();if(controls!=null)controls.videoSettings(videoSettings);return;}
        JSONObject info=message.optJSONObject("host_info");
        if(info!=null){
            hostPlatform=info.optString("platform","");JSONArray list=info.optJSONArray("displays");if(list!=null)displays=list;history.metadata(serverKey(),connectedRemote,info.optString("host_name"),hostPlatform);
            if(controls!=null)controls.setHostPlatform(hostPlatform);
            boolean wasSupported=videoSettings.supported;videoSettings.setSupported(info.optBoolean("supports_video_settings",false));
            if(!videoSettings.supported)cancelVideoSettingsTimeout();
            if(controls!=null)controls.videoSettings(videoSettings);
            if(videoSettings.supported&&!wasSupported)updateVideoSettings(videoSettings.selection);
            if(foreground&&preferences.getBoolean("networkConsent",false))remoteVersionCheck.hostInfo(info);
        }
    }
    private void dismissPrivacyUnlockDialog() {
        if (privacyUnlockDialog != null) { privacyUnlockDialog.dismiss(); privacyUnlockDialog = null; }
    }
    @Override public void privacyChanged() {
        if (controls != null && session != null) controls.privacyScreen(session.privacyScreen);
        if (!ready || session == null || !session.privacyScreen.unlockNotice) {
            dismissPrivacyUnlockDialog(); return;
        }
        if (!foreground || pictureInPictureUi || isInPictureInPictureMode() || isFinishing()) return;
        if (privacyUnlockDialog == null) {
            releaseKeys();
            if (controls != null) { controls.closeKeyboard(); controls.cancelMouseInput(); }
            if (video != null) video.releaseMouse();
            privacyUnlockDialog = new AlertDialog.Builder(this).setTitle(L10n.tr("隐私屏已被解锁"))
                .setMessage(L10n.tr("被控端已通过组合键解除隐私屏。点击“锁定”将重新开启隐私屏，并禁止本机使用组合键解锁。"))
                .setNegativeButton(L10n.tr("确认"), (dialog, which) -> {
                    if (session != null) session.privacyScreen.unlockNotice = false;
                    dismissPrivacyUnlockDialog();
                }).setPositiveButton(L10n.tr("锁定"), null).create();
            privacyUnlockDialog.setCancelable(false);
            privacyUnlockDialog.show();
            privacyUnlockDialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
                if (session != null) session.lockPrivacy();
            });
        }
        String feedback = session.privacyScreen.feedback(android.os.SystemClock.elapsedRealtime());
        privacyUnlockDialog.setMessage(L10n.tr("被控端已通过组合键解除隐私屏。点击“锁定”将重新开启隐私屏，并禁止本机使用组合键解锁。")
            + (feedback.isEmpty() ? "" : "\n\n" + feedback));
        privacyUnlockDialog.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(
            session.privacyScreen.canLock(android.os.SystemClock.elapsedRealtime()));
    }
    @Override public void videoSize(int width,int height){videoWidth=width;videoHeight=height;if(video!=null)video.videoSize(width,height);if(controls!=null)controls.videoSize(width,height);pictureInPicture.videoSize(width,height);updatePictureInPicture();}
    @Override public void clipboard(String value){remoteClipboard=value;}
    @Override public void statistics(RemoteNetworkStatistics.Snapshot value){if(controls!=null)controls.statistics(value);if(value.fps>0)capturePreviewIfNeeded();}
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        if (privacyUnlockDialog != null) return super.dispatchKeyEvent(event);
        int code=KeyMap.windowsCode(event.getKeyCode());View focus=getCurrentFocus();
        boolean remoteEditor=focus instanceof RemoteKeyboardInput;
        boolean desktopKey=event.isCtrlPressed()||event.isAltPressed()||event.isMetaPressed()||pressed.contains(code)
            ||code==17||code==18||code==91||code==27||code==9||code>=33&&code<=40||code==45||code==46||code>=112&&code<=123;
        if(ready&&session!=null&&!pictureInPictureUi&&hasWindowFocus()&&(!(focus instanceof EditText)||remoteEditor&&(!((RemoteKeyboardInput)focus).isAcceptingInput()||desktopKey))&&event.getKeyCode()!=KeyEvent.KEYCODE_BACK&&code!=0){
            boolean down=event.getAction()==KeyEvent.ACTION_DOWN;
            if(down){pressed.add(code);if(remoteEditor)controls.resetKeyboardContext();}else pressed.remove(code);
            session.key(code,down);return true;
        }
        return super.dispatchKeyEvent(event);
    }
    private void releaseKeys(){if(session!=null)for(int code:pressed)session.key(code,false);pressed.clear();}
    private void navigateBack(){if(backAction==null)return;navigatingBack=true;try{backAction.run();}finally{navigatingBack=false;}}
    private void back(){if(controls!=null&&controls.closeKeyboard())return;pageHost.finish();if(session!=null){if(ready)confirmDisconnect();else end("");}else if(backAction!=null)navigateBack();else finish();}
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(!focus)releaseKeys();else applySystemBars();}
    @Override public void onMultiWindowModeChanged(boolean multiWindow, Configuration config) {
        super.onMultiWindowModeChanged(multiWindow, config);
        applySystemBars();
    }
    // API 26–32 use this callback; API 33+ registers OnBackInvokedCallback in onCreate.
    @SuppressWarnings("deprecation")
    @android.annotation.SuppressLint("GestureBackNavigation") @Override public void onBackPressed(){back();}
    @Override public void onConfigurationChanged(Configuration config){
        super.onConfigurationChanged(config);
        releaseKeys();
        if(!page.equals("session"))updateHomeOrientation();
        else if(!isInPictureInPictureMode())remoteOrientation.update(!isTablet()&&!isInMultiWindowMode());
        sizePasswordDialog();
        if(privacyDialog instanceof PrivacyConsentDialog)((PrivacyConsentDialog)privacyDialog).updateSize();
        if(controls!=null&&!pictureInPictureUi)controls.post(controls::constrainPanels);
        if(video!=null)video.post(this::updatePictureInPicture);
    }
    private boolean supportsPictureInPicture(){return getPackageManager().hasSystemFeature(android.content.pm.PackageManager.FEATURE_PICTURE_IN_PICTURE);}
    private boolean canUsePictureInPicture(){
        if(!supportsPictureInPicture())return false;
        android.app.AppOpsManager ops=getSystemService(android.app.AppOpsManager.class);
        int mode=ops.checkOpNoThrow(android.app.AppOpsManager.OPSTR_PICTURE_IN_PICTURE,android.os.Process.myUid(),getPackageName());
        return mode==android.app.AppOpsManager.MODE_ALLOWED||mode==android.app.AppOpsManager.MODE_DEFAULT;
    }
    private void captureBackgroundSession(){
        BackgroundSession retained=backgroundSession;if(retained==null)return;
        retained.identity=localIdentity;retained.signalState=signalState;retained.platform=hostPlatform;retained.displays=displays;
        retained.muted=muted;retained.selectedDisplay=selectedDisplay;retained.remoteServiceAvailable=remoteServiceAvailable;
        retained.clipboard=remoteClipboard;retained.videoSettings=videoSettings;retained.displayResolutions=displayResolutions;retained.receivedFile=receivedFile;
        retained.width=videoWidth;retained.height=videoHeight;
        retained.autoLandscape=remoteOrientation.autoLandscape();
    }
    private boolean prepareBackgroundSession(boolean inform){
        if(!ready||session==null||session.isClosed()||isFinishing()||!hasNetworkConsent())return false;
        if(backgroundSession!=null&&!backgroundSession.ended)return RemoteSessionService.current()==backgroundSession;
        BackgroundSession retained=new BackgroundSession(this,session,host,port,connectedRemote);
        backgroundSession=retained;captureBackgroundSession();retained.attach(this);session.setListener(retained);
        if(!RemoteSessionService.retain(this,retained)){
            session.setListener(this);retained.detach(this);backgroundSession=null;
            if(inform)toast(L10n.tr("后台保活未能启动，请保持应用在前台"));return false;
        }
        if(inform){
            toast(L10n.tr("已启用后台保活，可从连接通知返回或断开"));
            requestBackgroundNotifications();
        }
        return true;
    }
    private void requestBackgroundNotifications(){
        if(android.os.Build.VERSION.SDK_INT>=33&&checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)!=android.content.pm.PackageManager.PERMISSION_GRANTED&&!preferences.getBoolean("notificationPermissionAsked",false)){
            preferences.edit().putBoolean("notificationPermissionAsked",true).apply();
            requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS},43);
        }
    }
    private boolean restoreBackgroundSession(){
        BackgroundSession retained=backgroundSession!=null?backgroundSession:RemoteSessionService.current();
        if(retained==null)return false;
        if(retained.ended||retained.session.isClosed()||!hasNetworkConsent()||!host.equals(retained.host)||port!=retained.port){
            retained.detach(this);retained.finish(L10n.tr("后台连接已结束，请重新连接"));backgroundSession=null;
            if(session==retained.session)end(retained.endReason);return false;
        }
        boolean rebuild=session!=retained.session||video==null||!page.equals("session");
        backgroundSession=retained;session=retained.session;
        if(retained.signaling!=null){
            signaling=retained.signaling;retained.signaling=null;signaling.setListener(this);
            signalingServer=serverKey();localIdentity=retained.identity;signalState=retained.signalState;
        }
        connectedRemote=retained.remote;
        hostPlatform=retained.platform;displays=retained.displays;muted=retained.muted;selectedDisplay=retained.selectedDisplay;
        remoteServiceAvailable=retained.remoteServiceAvailable;remoteClipboard=retained.clipboard;receivedFile=retained.receivedFile;
        videoSettings=retained.videoSettings;displayResolutions=retained.displayResolutions;ready=true;retained.attach(this);
        if(rebuild)sessionScreen(retained.autoLandscape);
        if(retained.width>0&&retained.height>0)videoSize(retained.width,retained.height);
        if(controls!=null){controls.setHostPlatform(hostPlatform);controls.setMuted(muted);controls.videoSettings(videoSettings);controls.displayResolutions(displayResolutions,selectedDisplay);controls.fileStatus(retained.fileStatus,receivedFile!=null);}
        retained.setBackground(false);signaling(signalState,localIdentity);
        return true;
    }
    private android.app.PictureInPictureParams pictureInPictureParams(){
        return pictureInPicture.parameters(video==null||pictureInPictureUi||isInPictureInPictureMode()?null:video.pictureInPictureBounds());
    }
    private void updatePictureInPicture(){
        if(!supportsPictureInPicture()||isDestroyed())return;
        try{setPictureInPictureParams(pictureInPictureParams());}
        catch(IllegalArgumentException|IllegalStateException error){android.util.Log.w("CrossDesk","Picture in Picture parameters unavailable",error);}
    }
    private void enterRemotePictureInPicture(){
        if(isInPictureInPictureMode())return;
        if(!canUsePictureInPicture()){
            prepareBackgroundSession(false);return;
        }
        if(!pictureInPicture.canEnter()||session==null||session.isClosed()||isFinishing())return;
        android.app.PictureInPictureParams params=pictureInPictureParams();
        setPictureInPictureUi(true);
        boolean entered=false;
        try{entered=enterPictureInPictureMode(params);}
        catch(IllegalArgumentException|IllegalStateException error){android.util.Log.w("CrossDesk","Picture in Picture could not start",error);}
        if(!entered){setPictureInPictureUi(false);prepareBackgroundSession(false);}
    }
    private void setPictureInPictureUi(boolean value){
        pictureInPictureUi=value;
        if(value){releaseKeys();hideKeyboard();pageHost.finish();if(disconnectDialog!=null)disconnectDialog.dismiss();if(remoteUpdateDialog!=null)remoteUpdateDialog.dismiss();}
        if(controls!=null)controls.setPictureInPicture(value);
        if(video!=null)video.setPictureInPicture(value);
        if(safeArea!=null)safeArea.requestApplyInsets();
    }
    @Override protected void onUserLeaveHint(){
        super.onUserLeaveHint();
        if(documentSession!=null)return;
        if(!canUsePictureInPicture()||!pictureInPicture.canEnter())prepareBackgroundSession(false);
        else if(android.os.Build.VERSION.SDK_INT<31)enterRemotePictureInPicture();
    }
    @Override public void onPictureInPictureModeChanged(boolean active,Configuration config){
        super.onPictureInPictureModeChanged(active,config);if(active)enteredPictureInPicture=true;setPictureInPictureUi(active);
        if(!active&&video!=null)video.post(this::updatePictureInPicture);
    }
    @android.annotation.TargetApi(31)
    @Override public void onPictureInPictureUiStateChanged(android.app.PictureInPictureUiState state){
        super.onPictureInPictureUiStateChanged(state);
        if(android.os.Build.VERSION.SDK_INT>=35&&state.isTransitioningToPip())setPictureInPictureUi(true);
    }
    @Override protected void onResume(){super.onResume();privacyChanged();if(!isInPictureInPictureMode())enteredPictureInPicture=false;setPictureInPictureUi(isInPictureInPictureMode());updatePictureInPicture();if(ready&&!canUsePictureInPicture())prepareBackgroundSession(true);if(backgroundSession!=null)requestBackgroundNotifications();}
    // onStart marks a new visit; focus/resume callbacks must not reopen a refused notice.
    @Override protected void onPause(){releaseKeys();if(controls!=null)controls.cancelMouseInput();if(video!=null)video.releaseMouse();super.onPause();}
    @Override protected void onStart(){super.onStart();foreground=true;restoreBackgroundSession();appUpdates.setEnabled(preferences.getBoolean("networkConsent",false));if(!preferences.getBoolean("networkConsent",false))consent(this::renderSignaling);ensureSignaling();showConnectionMessage();}
    @Override protected void onStop(){dismissPrivacyUnlockDialog();if(controls!=null)controls.closeKeyboard();foreground=false;appUpdates.setEnabled(false);if(privacyDialog!=null)privacyDialog.dismiss();pageHost.finish();
        if(connectionDialog!=null)connectionDialog.dismiss();
        // Closing native PiP is an explicit end, not a request for invisible
        // fallback. An unavailable/failed PiP can hand its live session off.
        if(!isFinishing()&&!enteredPictureInPicture&&ready&&(backgroundSession!=null||documentSession==null)&&prepareBackgroundSession(false)){
            captureBackgroundSession();backgroundSession.detach(this);backgroundSession.setBackground(true);
            backgroundSession.signaling=signaling;
            if(signaling!=null){signaling.setListener(backgroundSession);signaling=null;}
            mainHandler.removeCallbacks(presenceTick);recentPresence.setConnected(false,android.os.SystemClock.elapsedRealtime());announcementInbox.setConnected(false);
            cancelDisplayResolutionTimeout();if(displayResolutions.pending)displayResolutions.expire(displayResolutions.requested.id);
            cancelVideoSettingsTimeout();if(videoSettings.pending)videoSettings.expire(videoSettings.requestId);
            resetRemoteVersionCheck();if(disconnectDialog!=null)disconnectDialog.dismiss();
        }else if(documentSession==null||documentSession!=session||isFinishing()){if(session!=null)end(isFinishing()?"":L10n.tr("远程窗口已关闭，或后台保活未能启动，会话已结束"));stopSignaling();}
        super.onStop();}
    @Override protected void onDestroy(){dismissPrivacyUnlockDialog();mainHandler.removeCallbacks(documentTimeout);documentSession=null;documentToSave=null;appUpdates.setEnabled(false);pageHost.finish();resetRemoteVersionCheck();cancelVideoSettingsTimeout();cancelDisplayResolutionTimeout();previews.removeListener(previewChanged);if(privacyDialog!=null)privacyDialog.dismiss();if(previewDialog!=null)previewDialog.dismiss();announcementInbox.close();if(disconnectDialog!=null)disconnectDialog.dismiss();if(passwordDialog!=null)passwordDialog.dismiss();if(progressDialog!=null)progressDialog.dismiss();
        if(connectionDialog!=null)connectionDialog.dismiss();
        boolean retained=!isFinishing()&&backgroundSession!=null&&!backgroundSession.ended&&RemoteSessionService.current()==backgroundSession;
        if(backgroundSession!=null){
            backgroundSession.detach(this);
            if(!retained)backgroundSession.finish("");
        }
        if(!retained){if(session!=null)session.close();stopSignaling();}else{mainHandler.removeCallbacks(presenceTick);session=signaling=null;}
        remoteOrientation.end();super.onDestroy();}
}
