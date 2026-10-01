package cn.crossdesk.mobile;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.text.*;
import android.text.method.PasswordTransformationMethod;
import android.view.*;
import android.view.inputmethod.InputMethodManager;
import android.widget.*;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.HashSet;
import java.util.Set;

import static cn.crossdesk.mobile.MobileUi.*;

public final class MainActivity extends Activity implements NativeSession.Listener {
    private MobileUi ui;
    private PageHost pageHost;
    private AboutPages aboutPages;
    private boolean navigatingBack;
    private FrameLayout canvas;
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
    private RemoteVideoView video;
    private SessionControls controls;
    private Dialog passwordDialog, progressDialog;
    private AlertDialog disconnectDialog;
    private AlertDialog remoteUpdateDialog;
    private RemoteVersionCheck remoteVersionCheck;
    private String host, connectedRemote="", hostPlatform="", remoteClipboard="", pendingPassword="", page="home";
    private int port;
    private JSONArray displays=new JSONArray();
    private boolean muted,ready,rememberPassword;
    private RemoteVideoSettings videoSettings=new RemoteVideoSettings(1);
    private final android.os.Handler mainHandler=new android.os.Handler(android.os.Looper.getMainLooper());
    private Runnable videoSettingsTimeout;
    private final Set<Integer> pressed=new HashSet<>();
    private Runnable backAction;

    @Override public void onCreate(Bundle state){
        super.onCreate(state);ui=new MobileUi(this);history=new RecentConnections(this);
        pageHost=new PageHost(this);setContentView(pageHost);
        preferences=getSharedPreferences("settings",MODE_PRIVATE);host=preferences.getString("host","api.crossdesk.cn");port=preferences.getInt("port",9099);
        recentPresence.sender=ids->{if(signaling!=null&&!signaling.isClosed())signaling.presenceRequest(ids);};
        recentPresence.changed=this::renderRecentConnections;
        remoteVersionCheck=new RemoteVersionCheck(this::showRemoteUpdate);
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
    private void installRoot(String nextPage,boolean dark){
        PageHost.Motion motion=PageHost.Motion.NONE;
        if(canvas!=null&&foreground&&!page.equals(nextPage)){
            if(nextPage.equals("session"))motion=PageHost.Motion.SESSION_IN;
            else if(page.equals("session"))motion=PageHost.Motion.SESSION_OUT;
            else motion=navigatingBack||nextPage.equals("home")?PageHost.Motion.POP:PageHost.Motion.PUSH;
        }
        if(root!=null)hideKeyboard();page=nextPage;
        signalBadge=null;identitySummary=null;announcementBadge=null;announcementView=null;announcementRefresh=null;
        previewSwitch=null;clearPreviewsButton=null;previewCleanupError=null;previewCards.clear();recentGrid=null;
        canvas=new FrameLayout(this);canvas.setBackgroundColor(dark?Color.BLACK:BACKGROUND);
        root=column();canvas.addView(root,new FrameLayout.LayoutParams(-1,-1));
        root.setOnApplyWindowInsetsListener((view,insets)->{
            view.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());return insets;
        });
        root.setFocusableInTouchMode(true);pageHost.show(canvas,motion);
        applySystemBars(dark);
    }
    private void applySystemBars(boolean dark){
        if(android.os.Build.VERSION.SDK_INT>=30){
            android.view.WindowInsetsController controller=getWindow().getInsetsController();
            if(controller!=null){int light=android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS|android.view.WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;controller.setSystemBarsAppearance(dark?0:light,light);}
        }else getWindow().getDecorView().setSystemUiVisibility(dark?0:View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR|View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
    }
    private EditText field(String hint,int type){
        EditText v=new EditText(this);v.setHint(hint);v.setContentDescription(hint);v.setInputType(type);v.setSingleLine(true);v.setTextSize(17);v.setTextColor(INK);v.setHintTextColor(SECONDARY);
        v.setPadding(dp(14),0,dp(14),0);v.setBackground(ui.background(BACKGROUND,12));v.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO);return v;
    }
    private LinearLayout navigation(String title,Runnable back){
        LinearLayout bar=row();bar.setPadding(dp(12),0,dp(12),0);root.addView(bar,ui.size(-1,56));
        bar.addView(ui.iconButton("back","返回",this::navigateBack),ui.size(44,44));
        TextView label=text(title,17,true);label.setGravity(Gravity.CENTER);label.setSingleLine();label.setEllipsize(TextUtils.TruncateAt.END);bar.addView(label,new LinearLayout.LayoutParams(0,-1,1));
        bar.addView(new View(this),ui.size(44,44));backAction=back;return bar;
    }
    LinearLayout formPage(String name,String title,Runnable back){
        installRoot(name,false);navigation(title,back);ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout content=column();content.setPadding(dp(20),dp(16),dp(20),dp(28));scroll.addView(content);return content;
    }
    LinearLayout section(LinearLayout content,String title){
        if(!title.isEmpty()){TextView label=text(title,13,false);label.setTextColor(SECONDARY);label.setPadding(dp(16),dp(12),0,dp(8));content.addView(label);}
        LinearLayout card=column();ui.card(card,12);content.addView(card);return card;
    }
    void footer(LinearLayout content,String title){TextView v=text(title,12,false);v.setTextColor(SECONDARY);v.setLineSpacing(dp(3),1);v.setPadding(dp(16),dp(8),dp(16),dp(16));content.addView(v);}
    void link(LinearLayout parent,String title,String detail,Runnable action){
        LinearLayout item=row();item.setPadding(dp(16),dp(14),dp(12),dp(14));item.setMinimumHeight(dp(48));
        item.addView(text(title,16,false),new LinearLayout.LayoutParams(0,-2,1));TextView value=text(detail,15,false);value.setTextColor(SECONDARY);item.addView(value);
        TextView chevron=text(" ›",23,false);chevron.setTextColor(0xFFB7B7BD);item.addView(chevron);item.setContentDescription(title+(detail.isEmpty()?"":"，"+detail));item.setClickable(true);item.setFocusable(true);item.setOnClickListener(v->action.run());parent.addView(item);
    }
    @android.annotation.SuppressLint("SourceLockedOrientationActivity") // Match the iOS portrait home; sessions support both orientations.
    private void home(String message){
        backAction=null;setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);installRoot("home",false);
        LinearLayout toolbar=row();toolbar.setPadding(dp(20),0,dp(20),0);root.addView(toolbar,ui.size(-1,56));
        signalBadge=text("",12,true);signalBadge.setGravity(Gravity.CENTER);signalBadge.setPadding(dp(11),0,dp(11),0);signalBadge.setSingleLine(true);
        signalBadge.setOnClickListener(v->{
            if(!preferences.getBoolean("networkConsent",false))privacyPage();
            else if(signalState==1)toast("已连接 "+serverKey());
            else {stopSignaling();ensureSignaling();}
        });renderSignaling();toolbar.addView(signalBadge,ui.size(-2,34));toolbar.addView(new View(this),new LinearLayout.LayoutParams(0,1,1));
        FrameLayout bell=(FrameLayout)ui.iconButton("bell","通知公告",this::announcements);bell.setClipChildren(false);toolbar.setClipChildren(false);
        announcementBadge=text("",10,true);announcementBadge.setTextColor(Color.WHITE);announcementBadge.setGravity(Gravity.CENTER);announcementBadge.setMinWidth(dp(16));announcementBadge.setPadding(dp(4),0,dp(4),0);announcementBadge.setBackground(ui.background(RED,12));announcementBadge.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        FrameLayout.LayoutParams badgePosition=new FrameLayout.LayoutParams(-2,dp(16),Gravity.TOP|Gravity.END);badgePosition.topMargin=-dp(3);badgePosition.setMarginEnd(-dp(5));bell.addView(announcementBadge,badgePosition);renderAnnouncements();
        toolbar.addView(bell,ui.size(40,40));View spacer=new View(this);toolbar.addView(spacer,ui.size(12,1));toolbar.addView(ui.iconButton("settings","设置",this::settings),ui.size(40,40));
        LinearLayout content=column();content.setPadding(dp(20),dp(24),dp(20),dp(12));root.addView(content,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout connection=column();connection.setPadding(dp(18),dp(18),dp(18),dp(18));ui.card(connection,20);connection.setElevation(dp(2));content.addView(connection);
        connection.addView(text("远程桌面",20,true));ui.gap(connection,16);
        LinearLayout entry=row();connection.addView(entry,ui.size(-1,54));
        remote=field("对端 ID",InputType.TYPE_CLASS_NUMBER);remote.setTextSize(20);remote.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
        remote.setOnFocusChangeListener((view,focused)->{GradientDrawable shape=ui.background(BACKGROUND,12);shape.setStroke(dp(focused?2:1),focused?BLUE:LINE);remote.setBackground(shape);});
        remote.setFilters(new InputFilter[]{new InputFilter.LengthFilter(11)});remote.setText(formatId(preferences.getString("lastRemote","")));
        entry.addView(remote,new LinearLayout.LayoutParams(0,-1,1));
        TextView connect=ui.primary("连接  →",()->promptConnection(id()));connect.setContentDescription("连接");LinearLayout.LayoutParams buttonParams=ui.size(96,54);buttonParams.leftMargin=dp(10);entry.addView(connect,buttonParams);
        Runnable update=()->{connect.setEnabled(!id().isEmpty());connect.setAlpha(id().isEmpty()?.45f:1);};update.run();
        remote.addTextChangedListener(new TextWatcher(){boolean editing;public void beforeTextChanged(CharSequence s,int start,int count,int after){}public void onTextChanged(CharSequence s,int start,int before,int count){update.run();}public void afterTextChanged(Editable value){if(editing)return;String formatted=formatId(value.toString());if(!formatted.equals(value.toString())){editing=true;int cursor=remote.getSelectionStart(),digits=value.subSequence(0,Math.max(0,cursor)).toString().replaceAll("[^0-9]","").length();remote.setText(formatted);remote.setSelection(Math.min(formatted.length(),digits+Math.max(0,digits-1)/3));editing=false;}}});
        LinearLayout recent=column();recent.setPadding(dp(16),dp(16),dp(16),dp(16));ui.card(recent,18);LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,0,1);p.topMargin=dp(20);content.addView(recent,p);
        JSONArray records=history.list(serverKey());recentPresence.watch(records,android.os.SystemClock.elapsedRealtime());LinearLayout heading=row();heading.addView(text("最近连接",20,true),new LinearLayout.LayoutParams(0,-2,1));
        if(records.length()>0){TextView count=text(records.length()+" 台设备",12,false);count.setTextColor(SECONDARY);heading.addView(count);}recent.addView(heading);
        if(records.length()==0){
            LinearLayout empty=column();empty.setGravity(Gravity.CENTER);recent.addView(empty,new LinearLayout.LayoutParams(-1,0,1));empty.addView(ui.icon("history",0xFFB6B6BB),ui.size(34,34));ui.gap(empty,12);
            TextView emptyTitle=text("还没有连接记录",17,true);emptyTitle.setGravity(Gravity.CENTER);empty.addView(emptyTitle);ui.gap(empty,8);TextView hint=text("成功连接后，这里会显示最近的远程设备。",13,false);hint.setTextColor(SECONDARY);hint.setGravity(Gravity.CENTER);empty.addView(hint);
        }else{
            ui.gap(recent,14);ScrollView scroll=new ScrollView(this);recent.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));recentGrid=column();scroll.addView(recentGrid);renderRecentConnections();
        }
        ensureSignaling();
        if(!message.isEmpty())new AlertDialog.Builder(this).setTitle("连接提示").setMessage(message).setPositiveButton("确定",null).show();
    }
    private String id(){return remote.getText().toString().replace(" ","");}
    static String formatId(String value){String digits=value.replaceAll("[^0-9]","");if(digits.length()>9)digits=digits.substring(0,9);return digits.replaceAll("(.{3})(?=.)","$1 ");}
    private void renderRecentConnections(){
        if(recentGrid==null)return;
        JSONArray records=recentPresence.ordered(history.list(serverKey()),android.os.SystemClock.elapsedRealtime());
        recentGrid.removeAllViews();previewCards.clear();
        for(int i=0;i<records.length();i+=2){LinearLayout line=row();line.setGravity(Gravity.TOP);recentGrid.addView(line);for(int j=i;j<i+2;j++){View card=j<records.length()?recentCard(records.optJSONObject(j)):new View(this);LinearLayout.LayoutParams cell=new LinearLayout.LayoutParams(0,-2,1);if(j>i)cell.leftMargin=dp(12);line.addView(card,cell);}ui.gap(recentGrid,12);}
    }
    private View recentCard(JSONObject item){
        if(item==null)return new View(this);String id=item.optString("id"),platform=item.optString("platform");
        LinearLayout card=column();ui.card(card,14);FrameLayout preview=new FrameLayout(this){@Override protected void onMeasure(int w,int h){super.onMeasure(w,MeasureSpec.makeMeasureSpec(MeasureSpec.getSize(w)*9/16,MeasureSpec.EXACTLY));}};
        int color=platform.equals("windows")?0xFF316AB7:platform.equals("macos")?0xFF8057B9:platform.equals("linux")?0xFFAE642B:0xFF677587;preview.setBackgroundColor(color);
        TextView label=text(platform.equals("windows")?"Windows":platform.equals("macos")?"macOS":platform.equals("linux")?"Linux":"远程电脑",19,true);label.setGravity(Gravity.CENTER);label.setTextColor(Color.WHITE);preview.addView(label,new FrameLayout.LayoutParams(-1,-1));card.addView(preview,ui.size(-1,80));
        ImageView image=new ImageView(this);image.setScaleType(ImageView.ScaleType.CENTER_CROP);image.setVisibility(View.GONE);image.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);preview.addView(image,new FrameLayout.LayoutParams(-1,-1));
        String server=serverKey();Runnable load=()->{if(!image.isAttachedToWindow())return;Object binding=new Object();image.setTag(binding);image.setImageDrawable(null);image.setVisibility(View.GONE);label.setVisibility(View.VISIBLE);
            previews.load(server,id,bitmap->{if(image.getTag()!=binding||!image.isAttachedToWindow()){if(bitmap!=null)bitmap.recycle();return;}if(bitmap!=null){image.setImageBitmap(bitmap);image.setVisibility(View.VISIBLE);label.setVisibility(View.GONE);}});};previewCards.add(load);image.addOnAttachStateChangeListener(new View.OnAttachStateChangeListener(){public void onViewAttachedToWindow(View view){load.run();}public void onViewDetachedFromWindow(View view){image.setTag(null);}});if(image.isAttachedToWindow())load.run();
        LinearLayout caption=column();caption.setPadding(dp(9),dp(9),dp(9),dp(9));TextView name=text(item.optString("name",id),12,true);name.setSingleLine();name.setEllipsize(TextUtils.TruncateAt.END);caption.addView(name);ui.gap(caption,4);
        LinearLayout details=row();TextView detail=text("ID "+id,10,false);detail.setTypeface(Typeface.create("sans-serif",Typeface.NORMAL));detail.setTextColor(SECONDARY);detail.setSingleLine();detail.setEllipsize(TextUtils.TruncateAt.END);details.addView(detail,new LinearLayout.LayoutParams(0,-2,1));
        if(item.optBoolean("remember")){View key=ui.icon("key",SECONDARY);key.setContentDescription("已保存密码");LinearLayout.LayoutParams keySize=ui.size(10,10);keySize.leftMargin=dp(4);details.addView(key,keySize);}
        boolean online=recentPresence.isOnline(id,android.os.SystemClock.elapsedRealtime());String availability=online?"在线":"离线";TextView presence=text(availability,10,true);presence.setSingleLine();presence.setTextColor(online?GREEN:SECONDARY);LinearLayout.LayoutParams presenceSize=ui.size(-2,-2);presenceSize.leftMargin=dp(5);details.addView(presence,presenceSize);caption.addView(details);card.addView(caption);
        card.setContentDescription("连接 "+item.optString("name",id)+"，ID "+id+(item.optBoolean("remember")?"，已保存密码":"")+"，"+availability);card.setClickable(true);card.setFocusable(true);card.setOnClickListener(v->{remote.setText(formatId(id));promptConnection(id);});
        card.setOnLongClickListener(v->{new AlertDialog.Builder(this).setTitle(item.optString("name",id)).setItems(new String[]{"连接","删除记录"},(d,w)->{if(w==0)promptConnection(id);else{history.delete(serverKey(),id);home("");}}).show();return true;});return card;
    }
    private void promptConnection(String id){
        hideKeyboard();if(!id.matches("[0-9]{9}")){remote.setError("请输入 9 位设备 ID");remote.requestFocus();return;}
        if(!preferences.getBoolean("networkConsent",false)){consent(()->connectOrPrompt(id));return;}connectOrPrompt(id);
    }
    private void connectOrPrompt(String id){
        String stored;
        try{stored=history.password(serverKey(),id);}catch(Exception error){passwordSheet(id,"已保存密码无法读取，请重新输入",true);return;}
        if(!stored.isEmpty()){rememberPassword=true;begin(id,stored);return;}
        boolean remember=history.remembersPassword(serverKey(),id);
        passwordSheet(id,remember?"已保存的密码未能保留，请重新输入一次":"",remember);
    }
    private void consent(Runnable accepted){
        new AlertDialog.Builder(this).setTitle("隐私与授权").setMessage("连接时会向所选服务器发送设备身份和认证信息，并传输远程画面、声音与操作指令。设备身份加密保存在本机；仅在你开启“保存密码”时加密保存访问密码。剪贴板内容仅在你操作时发送或写入本机。仅连接你有权访问的电脑。")
            .setNegativeButton("暂不同意",null).setPositiveButton("同意并继续",(d,w)->{preferences.edit().putBoolean("networkConsent",true).apply();ensureSignaling();accepted.run();}).show();
    }
    private void passwordSheet(String id,String error,boolean rememberDefault){
        Dialog dialog=new Dialog(this);passwordDialog=dialog;LinearLayout sheet=column();sheet.setPadding(dp(22),dp(10),dp(22),dp(22));sheet.setBackground(ui.background(BACKGROUND,24));
        View handle=new View(this);handle.setBackground(ui.background(0xFFC6C6CB,3));LinearLayout.LayoutParams hp=ui.size(36,5);hp.gravity=Gravity.CENTER;sheet.addView(handle,hp);ui.gap(sheet,20);
        LinearLayout header=row(),titles=column();titles.addView(text("连接远程桌面",22,true));ui.gap(titles,5);TextView subtitle=text("对端 ID  "+id,15,false);subtitle.setTextColor(SECONDARY);titles.addView(subtitle);header.addView(titles,new LinearLayout.LayoutParams(0,-2,1));header.addView(ui.iconButton("close","关闭",dialog::dismiss),ui.size(36,36));sheet.addView(header);ui.gap(sheet,18);
        LinearLayout passwordRow=row();ui.card(passwordRow,10);EditText password=field("访问密码",InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_PASSWORD);password.setBackgroundColor(Color.TRANSPARENT);passwordRow.addView(password,new LinearLayout.LayoutParams(0,dp(50),1));
        View eye=ui.iconButton("eye","显示密码",()->{boolean hidden=password.getTransformationMethod()!=null;password.setTransformationMethod(hidden?null:PasswordTransformationMethod.getInstance());password.setSelection(password.length());});passwordRow.addView(eye,ui.size(44,44));sheet.addView(passwordRow);ui.gap(sheet,18);
        LinearLayout save=row(),saveText=column();saveText.addView(text("保存密码",15,true));ui.gap(saveText,3);TextView security=text("密码将加密保存在本机 Android Keystore 中",11,false);security.setTextColor(SECONDARY);saveText.addView(security);save.addView(saveText,new LinearLayout.LayoutParams(0,-2,1));Switch remember=new Switch(this);remember.setContentDescription("保存密码");save.addView(remember);sheet.addView(save);
        remember.setChecked(rememberDefault);
        ui.gap(sheet,18);TextView connect=ui.primary("连接  →",()->{
            String secret=password.getText().toString();if(secret.isEmpty()||secret.length()>128||secret.contains("@")){password.setError("请输入有效访问密码");return;}
            rememberPassword=remember.isChecked();password.setText("");dialog.dismiss();begin(id,secret);
        });connect.setContentDescription("确认连接");sheet.addView(connect,ui.size(-1,48));
        dialog.setContentView(sheet);Window window=dialog.getWindow();if(window!=null){window.setBackgroundDrawableResource(android.R.color.transparent);window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);window.setDimAmount(.22f);}dialog.show();if(window!=null){window.setLayout(-1,-2);window.setGravity(Gravity.BOTTOM);}password.requestFocus();if(!error.isEmpty())password.setError(error);
        dialog.setOnDismissListener(d->{password.setText("");passwordDialog=null;});
    }
    private void begin(String id,String secret){
        resetRemoteVersionCheck();
        resetVideoSettings();
        previewCapture=previews.begin(serverKey(),id);previewCopying=false;previewAttempts=0;
        connectedRemote=id;pendingPassword=rememberPassword?secret:"";ready=false;muted=false;hostPlatform="";remoteClipboard="";displays=new JSONArray();preferences.edit().putString("lastRemote",id).apply();
        ensureSignaling();
        if(signaling==null){pendingPassword="";toast("请先完成联网授权");return;}
        session=signaling;showProgress();session.connect(id,secret);
    }
    private void showProgress(){
        Dialog dialog=new Dialog(this);progressDialog=dialog;LinearLayout card=column();ui.card(card,24);card.setGravity(Gravity.CENTER_HORIZONTAL);LinearLayout body=column();body.setGravity(Gravity.CENTER);body.setPadding(dp(26),dp(24),dp(26),dp(20));
        ProgressBar spinner=new ProgressBar(this);spinner.setIndeterminateTintList(ColorStateList.valueOf(BLUE));body.addView(spinner,ui.size(34,34));ui.gap(body,14);TextView title=text("正在连接",20,true);title.setGravity(Gravity.CENTER);body.addView(title);ui.gap(body,6);status=text("正在连接信令服务器…",14,false);status.setGravity(Gravity.CENTER);status.setTextColor(SECONDARY);status.setMinHeight(dp(36));body.addView(status);card.addView(body,ui.size(-1,-2));ui.divider(card);card.addView(ui.action("取消连接",RED,()->end("")),ui.size(-1,46));
        dialog.setContentView(card);dialog.setCanceledOnTouchOutside(false);dialog.setOnCancelListener(d->end(""));Window w=dialog.getWindow();if(w!=null){w.setBackgroundDrawableResource(android.R.color.transparent);w.setDimAmount(.08f);}dialog.show();if(w!=null)w.setLayout(dp(292),-2);
    }
    private void sessionScreen(){
        installRoot("session",true);setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR);video=new RemoteVideoView(this,session);video.setRelative(preferences.getBoolean("relativeMouse",true));canvas.addView(video,0,new FrameLayout.LayoutParams(-1,-1));
        controls=new SessionControls(this,ui,new SessionControls.Actions(){
            public void disconnect(){confirmDisconnect();}public void display(){selectDisplay();}public void clipboard(){MainActivity.this.clipboard();}
            public void key(int code){tapKey(code);}
            public void chord(int code,int[] modifiers){if(session==null)return;for(int modifier:modifiers)session.key(modifier,true);tapKey(code);for(int i=modifiers.length-1;i>=0;i--)session.key(modifiers[i],false);}public void shortcut(int code){MainActivity.this.shortcut(code);}public void text(String value){sendText(value);}
            public void mute(){muted=!muted;if(session!=null)session.control(2,muted?0:1);controls.setMuted(muted);}
            public void mouse(){boolean relative=!preferences.getBoolean("relativeMouse",true);preferences.edit().putBoolean("relativeMouse",relative).apply();video.setRelative(relative);controls.setRelative(relative);}
            public void videoSettings(int field,int value){RemoteVideoSettings.Values old=MainActivity.this.videoSettings.selection;updateVideoSettings(new RemoteVideoSettings.Values(field==0?value:old.quality,field==1?value:old.frameRate,field==2?value:old.preference));}
        });controls.setRelative(preferences.getBoolean("relativeMouse",true));controls.videoSettings(videoSettings);root.addView(controls,new LinearLayout.LayoutParams(-1,-1));getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }
    private void confirmDisconnect(){disconnectDialog=new AlertDialog.Builder(this).setTitle("断开远程连接？").setMessage("断开后将返回首页").setNegativeButton("取消",null).setPositiveButton("断开连接",(d,w)->end("")).show();}
    private void end(String reason){
        resetRemoteVersionCheck();
        resetVideoSettings();
        previewCapture=null;previewCopying=false;
        if(progressDialog!=null){progressDialog.dismiss();progressDialog=null;}if(session!=null){releaseKeys();session.close();session=null;}stopSignaling();pendingPassword="";remoteClipboard="";hostPlatform="";displays=new JSONArray();ready=false;video=null;controls=null;getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);home(reason);
    }
    private void settings(){
        LinearLayout content=formPage("settings","设置",()->home(""));
        LinearLayout mouse=section(content,"鼠标控制");mouse.setPadding(dp(12),dp(12),dp(12),dp(12));boolean relative=preferences.getBoolean("relativeMouse",true);
        TextView detail=text(relative?"像触控板一样滑动光标，点击时操作当前光标位置。":"触摸位置直接对应远端屏幕位置，适合快速定位。",13,false);detail.setTextColor(SECONDARY);
        mouse.addView(segments(new String[]{"相对位置","绝对位置"},relative?0:1,true,index->{preferences.edit().putBoolean("relativeMouse",index==0).apply();detail.setText(index==0?"像触控板一样滑动光标，点击时操作当前光标位置。":"触摸位置直接对应远端屏幕位置，适合快速定位。");}));ui.gap(mouse,10);mouse.addView(detail);
        LinearLayout picture=section(content,"画面偏好");picture.setPadding(dp(12),dp(12),dp(12),dp(12));int preference=Math.max(0,Math.min(2,preferences.getInt("videoPreference",1)));TextView preferenceDetail=text(RemoteVideoSettings.DETAILS[preference],13,false);preferenceDetail.setTextColor(SECONDARY);
        picture.addView(segments(RemoteVideoSettings.PREFERENCES,preference,true,index->{preferences.edit().putInt("videoPreference",index).apply();preferenceDetail.setText(RemoteVideoSettings.DETAILS[index]);}));ui.gap(picture,10);picture.addView(preferenceDetail);ui.gap(content,28);
        LinearLayout server=section(content,"");LinearLayout fields=column();fields.setVisibility(View.GONE);boolean custom=!host.equals("api.crossdesk.cn")||port!=9099;
        link(server,"服务器",custom?"自定义":"默认",()->fields.setVisibility(fields.getVisibility()==View.VISIBLE?View.GONE:View.VISIBLE));server.addView(fields);
        LinearLayout toggleRow=row();toggleRow.setPadding(dp(16),dp(8),dp(16),dp(8));toggleRow.addView(text("使用自定义服务器",16,false),new LinearLayout.LayoutParams(0,-2,1));Switch toggle=new Switch(this);toggle.setContentDescription("使用自定义服务器");toggle.setChecked(custom);toggleRow.addView(toggle);fields.addView(toggleRow);
        LinearLayout inputs=column();inputs.setPadding(dp(12),0,dp(12),dp(12));EditText address=field("服务器地址",InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_URI),number=field("服务器端口",InputType.TYPE_CLASS_NUMBER);address.setText(custom?host:"");number.setText(custom?String.valueOf(port):"");inputs.addView(address,ui.size(-1,48));ui.gap(inputs,8);inputs.addView(number,ui.size(-1,48));ui.gap(inputs,8);
        inputs.addView(ui.action("完成",BLUE,()->{if(saveServer(address,number)){hideKeyboard();settings();}}));fields.addView(inputs);inputs.setVisibility(custom?View.VISIBLE:View.GONE);
        toggle.setOnCheckedChangeListener((button,checked)->{inputs.setVisibility(checked?View.VISIBLE:View.GONE);if(!checked){host="api.crossdesk.cn";port=9099;preferences.edit().putString("host",host).putInt("port",port).apply();ensureSignaling();} });
        backAction=()->{if(toggle.isChecked()&&!saveServer(address,number))return;hideKeyboard();home("");};
        identitySummary=text("",12,false);identitySummary.setTextColor(SECONDARY);identitySummary.setGravity(Gravity.CENTER);identitySummary.setPadding(dp(16),dp(12),dp(16),dp(16));identitySummary.setTextIsSelectable(true);content.addView(identitySummary);renderSignaling();
        LinearLayout privacy=section(content,"隐私");link(privacy,"隐私与授权",preferences.getBoolean("networkConsent",false)?"已授权":"未授权",this::privacyPage);ui.divider(privacy);
        LinearLayout preview=row();preview.setPadding(dp(16),dp(10),dp(16),dp(10));preview.addView(text("保存远程画面预览",16,false),new LinearLayout.LayoutParams(0,-2,1));previewSwitch=new Switch(this);previewSwitch.setContentDescription("保存远程画面预览");preview.addView(previewSwitch);privacy.addView(preview);
        previewSwitch.setOnCheckedChangeListener((button,checked)->{if(updatingPreviewSwitch)return;if(checked){renderPreviews();confirmPreviewSaving();}else previews.setEnabled(false);});
        ui.divider(privacy);clearPreviewsButton=ui.action("清除预览图",RED,this::confirmClearPreviews);clearPreviewsButton.setGravity(Gravity.START|Gravity.CENTER_VERTICAL);clearPreviewsButton.setTypeface(Typeface.DEFAULT);clearPreviewsButton.setPadding(dp(16),0,dp(16),0);privacy.addView(clearPreviewsButton,ui.size(-1,48));
        previewCleanupError=text("",12,false);previewCleanupError.setTextColor(RED);previewCleanupError.setPadding(dp(16),dp(8),dp(16),dp(12));privacy.addView(previewCleanupError);renderPreviews();
        footer(content,"画面预览默认关闭。开启后，从下一次连接起保存一张远程画面到本机，显示在最近连接中，不上传且不纳入设备备份。关闭开关会清除已有预览；连接记录和密码不受影响。");LinearLayout about=section(content,"");link(about,"关于","",this::about);
    }
    private void confirmPreviewSaving(){
        previewDialog=new AlertDialog.Builder(this).setTitle("保存远程画面预览？")
            .setMessage("预览可能包含远程电脑上的个人或工作信息。开启后，每次连接会自动将一张画面保存在本机并显示在最近连接中。你可以随时关闭并清除。")
            .setNegativeButton("取消",null).setPositiveButton("开启保存",(d,w)->previews.setEnabled(true)).show();
        previewDialog.setOnDismissListener(d->{previewDialog=null;renderPreviews();});
    }
    private void confirmClearPreviews(){
        previewDialog=new AlertDialog.Builder(this).setTitle("清除预览图？")
            .setMessage("将删除本机保存的所有远程预览图，保留连接记录和密码。"+(previews.enabled()?"后续连接仍会保存新的预览图。":""))
            .setNegativeButton("取消",null).setPositiveButton("清除",(d,w)->previews.clear()).show();
        previewDialog.setOnDismissListener(d->previewDialog=null);
    }
    private void renderPreviews(){
        if(previews==null)return;
        if(!previews.enabled()||previews.cleaning)previewCapture=null;
        if(previewSwitch!=null){updatingPreviewSwitch=true;previewSwitch.setChecked(previews.enabled());previewSwitch.setEnabled(preferences.getBoolean("networkConsent",false)&&!previews.cleaning);updatingPreviewSwitch=false;}
        if(clearPreviewsButton!=null){clearPreviewsButton.setText(previews.cleaning?"正在清除…":"清除预览图");clearPreviewsButton.setEnabled(!previews.cleaning);}
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
        String name=address.getText().toString().trim();int value;try{value=Integer.parseInt(number.getText().toString());}catch(NumberFormatException e){number.setError("请输入 1–65535 范围内的端口");return false;}
        if(name.length()>253||!name.matches("[A-Za-z0-9.-]+")||name.startsWith(".")||name.endsWith(".")){address.setError("填写主机名或 IPv4 地址，不含协议和路径");return false;}if(value<1||value>65535){number.setError("端口范围为 1–65535");return false;}host=name;port=value;preferences.edit().putString("host",host).putInt("port",port).apply();ensureSignaling();return true;
    }
    private void announcements(){
        installRoot("announcements",false);LinearLayout bar=navigation("通知公告",()->home(""));
        bar.removeViewAt(bar.getChildCount()-1);announcementRefresh=ui.iconButton("refresh","刷新公告",announcementInbox::refresh);bar.addView(announcementRefresh,ui.size(44,44));
        announcementView=new AnnouncementView(ui,announcementInbox,null,this::announcementDetail,this::connectAnnouncements);
        root.addView(announcementView,new LinearLayout.LayoutParams(-1,0,1));announcementInbox.refresh();renderAnnouncements();
    }
    private void announcementDetail(AnnouncementInbox.Item item){
        installRoot("announcement-detail",false);navigation("公告详情",this::announcements);
        announcementView=new AnnouncementView(ui,announcementInbox,item,this::announcementDetail,this::connectAnnouncements);
        root.addView(announcementView,new LinearLayout.LayoutParams(-1,0,1));announcementInbox.markRead(item);
    }
    private void connectAnnouncements(){
        if(!preferences.getBoolean("networkConsent",false))consent(()->{ensureSignaling();renderAnnouncements();});
        else{stopSignaling();ensureSignaling();}
    }
    private void renderAnnouncements(){
        if(announcementBadge!=null){int unread=announcementInbox.unread;announcementBadge.setText(unread>99?"99+":String.valueOf(unread));announcementBadge.setVisibility(unread>0?View.VISIBLE:View.GONE);
            if(android.os.Build.VERSION.SDK_INT>=30)((View)announcementBadge.getParent()).setStateDescription(unread>0?unread+" 条未读":"无未读公告");}
        if(announcementRefresh!=null){boolean enabled=announcementInbox.isConnected()&&!announcementInbox.loading;announcementRefresh.setEnabled(enabled);announcementRefresh.setAlpha(enabled?1:.4f);}
        if(announcementView!=null)announcementView.render();
    }
    @Override public void announcement(JSONObject message){
        if(!preferences.getBoolean("networkConsent",false)||signalState!=1)return;
        if("announcements_changed".equals(message.optString("type")))announcementInbox.refresh();else announcementInbox.receive(message);
    }
    @Override public void announcementFailed(String requestId){announcementInbox.failed(requestId);}
    private void privacyPage(){
        LinearLayout content=formPage("privacy","隐私与授权",this::settings);LinearLayout card=section(content,"");boolean consent=preferences.getBoolean("networkConsent",false);link(card,"授权状态",consent?"已授权":"未授权",()->{});ui.divider(card);link(card,"隐私说明","",()->consent(this::privacyPage));
        footer(content,"设备身份及选择保存的访问密码使用 Android Keystore 加密。连接记录仅保存在本机，不纳入设备备份。剪贴板内容在结束会话时清除。离开应用会结束当前会话。");
        if(consent)content.addView(ui.action("撤回联网授权",RED,()->new AlertDialog.Builder(this).setTitle("撤回联网授权？").setMessage("撤回后将停止远程连接、关闭画面预览保存并清除已有预览。连接记录和密码会保留，重新连接前需再次授权。").setNegativeButton("取消",null).setPositiveButton("撤回",(d,w)->{preferences.edit().putBoolean("networkConsent",false).apply();previews.setEnabled(false);stopSignaling();announcementInbox.reset();privacyPage();}).show()));else content.addView(ui.primary("阅读并授权",()->consent(this::privacyPage)));
    }
    private void about(){
        if(aboutPages==null)aboutPages=new AboutPages(this,ui,this::settings);aboutPages.show();
    }
    private void sendText(String value){if(session==null)return;if(value.length()>2048){toast("单次最多 2048 个字符");return;}for(char ch:value.toCharArray())if(KeyMap.ascii(ch)==0){toast("中文等文本请通过剪贴板发送，再点击粘贴");return;}for(char ch:value.toCharArray()){int code=KeyMap.ascii(ch);boolean shift=(code&0x100)!=0;if(shift)session.key(0x10,true);tapKey(code&0xFF);if(shift)session.key(0x10,false);}}
    private void tapKey(int code){if(session!=null){session.key(code,true);session.key(code,false);}}
    private void shortcut(int code){if(session!=null){int modifier=hostPlatform.equals("macos")?0x5B:0x11;session.key(modifier,true);tapKey(code);session.key(modifier,false);}}
    private void selectDisplay(){
        if(!ready)return;int count=Math.min(displays.length(),8);if(count==0){toast("等待远端屏幕信息");return;}String[] labels=new String[count];for(int i=0;i<count;i++){JSONObject display=displays.optJSONObject(i);labels[i]=(i+1)+" · "+(display==null?"显示器":display.optString("name","显示器"));}new AlertDialog.Builder(this).setTitle("显示器").setItems(labels,(d,w)->{if(session!=null){session.control(4,w);if(controls!=null)controls.resetVideoStatistics();}}).show();
    }
    private void clipboard(){
        if(!ready)return;
        EditText input=field("要发送到远端的文本",InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_MULTI_LINE);input.setSingleLine(false);input.setMinLines(3);
        new AlertDialog.Builder(this).setTitle("剪贴板").setView(input)
                .setMessage("发送后，在远程键盘中点击“粘贴”。接收的远端文本可手动复制到本机。")
                .setNegativeButton("关闭",null).setNeutralButton("复制远端文本",(d,w)->{
                    if(!remoteClipboard.isEmpty())((ClipboardManager)getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("CrossDesk",remoteClipboard));
                }).setPositiveButton("发送文本",(d,w)->{
                    String value=input.getText().toString();
                    if(value.getBytes(java.nio.charset.StandardCharsets.UTF_8).length>128*1024){Toast.makeText(this,"文本超过 128 KB",Toast.LENGTH_SHORT).show();return;}
                    if(session!=null)session.clipboard(value);
                }).show();
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
        if(!consent){label="等待隐私授权";tint=0xFFB76A00;background=0xFFFFEBD2;}
        else if(!signalError.isEmpty()){label=signalError.contains("TLS")?"证书校验失败 · 点击重试":"连接失败 · 点击重试";tint=RED;background=0xFFFFE8E6;}
        else if(connected){label="已连接服务器";tint=GREEN;background=0xFFE5F4E8;}
        else{label=signalState==0?"正在连接服务器…":signalState==4?"正在重连服务器…":"未连接服务器 · 点击重试";tint=0xFFB76A00;background=0xFFFFEBD2;}
        if(signalBadge!=null){
            android.graphics.drawable.Drawable icon=connected?getDrawable(R.drawable.ic_check_circle):null;
            if(icon!=null){int size=dp(14);icon.setBounds(0,0,size,size);}
            signalBadge.setCompoundDrawablesRelative(icon,null,null,null);signalBadge.setCompoundDrawablePadding(dp(6));
            signalBadge.setText(label);signalBadge.setTextColor(tint);signalBadge.setBackground(ui.background(background,20));signalBadge.setContentDescription(signalError.isEmpty()?label:signalError+"，点击重试");
        }
        if(identitySummary!=null)identitySummary.setText(!consent?"完成隐私授权后登记本机身份":!localIdentity.isEmpty()?"本机 ID  "+localIdentity:!signalError.isEmpty()?signalError:"正在获取本机 ID…");
    }
    @Override public void signaling(int state,String deviceId){
        signalState=state;localIdentity=deviceId;signalError="";renderSignaling();
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
        try{history.save(serverKey(),connectedRemote,connectedRemote,hostPlatform,rememberPassword,pendingPassword);}catch(Exception error){toast("密码保存失败，本次连接仍可使用");}pendingPassword="";sessionScreen();
        if(videoSettings.supported&&videoSettings.requestId==0)updateVideoSettings(videoSettings.selection);
        if(foreground&&preferences.getBoolean("networkConsent",false))remoteVersionCheck.connected();
    }
    @Override public void ended(String reason){
        if(session!=null){end(reason);return;}
        stopSignaling();signalError=reason;renderSignaling();
    }
    @Override public void passwordRejected(){
        if(session==null)return;
        String id=connectedRemote;boolean remember=rememberPassword;
        end("");
        // Let the user replace an expired credential; never retry it automatically.
        if(foreground)passwordSheet(id,"访问密码错误，请重新输入",remember);
    }
    private void cancelVideoSettingsTimeout(){if(videoSettingsTimeout!=null)mainHandler.removeCallbacks(videoSettingsTimeout);videoSettingsTimeout=null;}
    private void showRemoteUpdate(){
        if(!ready||session==null||!foreground||isFinishing()||!preferences.getBoolean("networkConsent",false))return;
        remoteUpdateDialog=new AlertDialog.Builder(this).setTitle("升级提示").setMessage(RemoteVersionCheck.NOTICE).setPositiveButton("知道了",null).create();
        remoteUpdateDialog.setOnDismissListener(dialog->remoteUpdateDialog=null);
        remoteUpdateDialog.setCanceledOnTouchOutside(false);
        remoteUpdateDialog.show();
    }
    private void resetRemoteVersionCheck(){
        remoteVersionCheck.reset();
        if(remoteUpdateDialog!=null){remoteUpdateDialog.dismiss();remoteUpdateDialog=null;}
    }
    private void resetVideoSettings(){
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
        if(message.optInt("type",-1)==12){if(!ready)return;videoSettings.receive(message);if(!videoSettings.pending)cancelVideoSettingsTimeout();if(controls!=null)controls.videoSettings(videoSettings);return;}
        JSONObject info=message.optJSONObject("host_info");
        if(info!=null){
            hostPlatform=info.optString("platform","");JSONArray list=info.optJSONArray("displays");if(list!=null)displays=list;history.metadata(serverKey(),connectedRemote,info.optString("host_name",connectedRemote),hostPlatform);
            boolean wasSupported=videoSettings.supported;videoSettings.setSupported(info.optBoolean("supports_video_settings",false));
            if(!videoSettings.supported)cancelVideoSettingsTimeout();
            if(controls!=null)controls.videoSettings(videoSettings);
            if(videoSettings.supported&&!wasSupported)updateVideoSettings(videoSettings.selection);
            if(foreground&&preferences.getBoolean("networkConsent",false))remoteVersionCheck.hostInfo(info);
        }
    }
    @Override public void videoSize(int width,int height){if(video!=null)video.videoSize(width,height);if(controls!=null)controls.videoSize(width,height);}
    @Override public void clipboard(String value){remoteClipboard=value;}
    @Override public void statistics(RemoteNetworkStatistics.Snapshot value){if(controls!=null)controls.statistics(value);if(value.fps>0)capturePreviewIfNeeded();}
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        if(ready&&session!=null&&hasWindowFocus()&&!(getCurrentFocus() instanceof EditText)&&event.getKeyCode()!=KeyEvent.KEYCODE_BACK){int code=KeyMap.windowsCode(event.getKeyCode());if(code!=0){boolean down=event.getAction()==KeyEvent.ACTION_DOWN;if(down)pressed.add(code);else pressed.remove(code);session.key(code,down);return true;}}return super.dispatchKeyEvent(event);
    }
    private void releaseKeys(){if(session!=null)for(int code:pressed)session.key(code,false);pressed.clear();}
    private void navigateBack(){if(backAction==null)return;navigatingBack=true;try{backAction.run();}finally{navigatingBack=false;}}
    private void back(){pageHost.finish();if(session!=null){if(ready)confirmDisconnect();else end("");}else if(backAction!=null)navigateBack();else finish();}
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(!focus)releaseKeys();else applySystemBars(ready);}
    @android.annotation.SuppressLint("GestureBackNavigation") @Override public void onBackPressed(){back();}
    @Override public void onConfigurationChanged(Configuration config){super.onConfigurationChanged(config);if(controls!=null)controls.post(controls::constrainPanels);}
    @Override protected void onStart(){super.onStart();foreground=true;ensureSignaling();}
    @Override protected void onStop(){foreground=false;pageHost.finish();if(session!=null)end(isFinishing()?"":"应用已进入后台，会话已结束");stopSignaling();super.onStop();}
    @Override protected void onDestroy(){pageHost.finish();resetRemoteVersionCheck();cancelVideoSettingsTimeout();previews.removeListener(previewChanged);if(previewDialog!=null)previewDialog.dismiss();announcementInbox.close();if(disconnectDialog!=null)disconnectDialog.dismiss();if(passwordDialog!=null)passwordDialog.dismiss();if(progressDialog!=null)progressDialog.dismiss();if(session!=null)session.close();stopSignaling();super.onDestroy();}
}
