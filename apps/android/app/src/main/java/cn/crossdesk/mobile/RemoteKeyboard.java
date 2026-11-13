package cn.crossdesk.mobile;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.drawable.StateListDrawable;
import android.os.Build;
import android.os.CancellationSignal;
import android.util.SparseIntArray;
import android.view.*;
import android.view.inputmethod.InputMethodManager;
import android.widget.*;
import java.util.LinkedHashSet;
import java.util.Set;

/** Docked native IME accessory and two-page desktop keyboard share one toolbar. */
@android.annotation.SuppressLint("ViewConstructor")
final class RemoteKeyboard extends LinearLayout {
    private final MobileUi ui;
    private final SessionControls.Actions actions;
    final RemoteKeyboardInput editor;
    private final Runnable changed;
    private final LinearLayout toolbar,keys;
    private final TextView switchButton,lettersButton,functionsButton;
    private final LinearLayout quickKeys;
    private final HorizontalScrollView quickScroll;
    private final TextView panelTitle;
    private final RemoteShortcutPanel shortcutPanel;
    private String hostPlatform="unknown";
    private boolean shortcuts;
    private final Set<Integer> modifiers=new LinkedHashSet<>();
    private final SparseIntArray heights=new SparseIntArray();
    private boolean opened,computer,functions,imeVisible,measuring,switching,targetComputer;
    private int imeHeight,navigationBottom,layoutWidth,layoutHeight,revision,lastInsetsHeight=-1,lastInsetsKey;
    private CancellationSignal transition;

    RemoteKeyboard(Context context,MobileUi ui,SessionControls.Actions actions,Runnable changed) {
        super(context);this.ui=ui;this.actions=actions;this.changed=changed;setOrientation(VERTICAL);
        setVisibility(GONE);setClickable(true);
        editor=new RemoteKeyboardInput(context,new RemoteKeyboardInput.Sender(){
            public boolean edit(int count,String text){return actions.textEdit(count,text);}
            public void key(int code){actions.key(code);}
        });
        shortcutPanel=new RemoteShortcutPanel(context,ui,this::sendShortcut,()->{render();changed.run();});
        FrameLayout header=new FrameLayout(context);addView(header,new LayoutParams(-1,ui.dp(44)));
        header.addView(editor,new FrameLayout.LayoutParams(1,1,Gravity.BOTTOM));
        toolbar=ui.row();toolbar.setGravity(Gravity.CENTER_VERTICAL);toolbar.setPadding(ui.dp(6),0,ui.dp(6),0);
        header.addView(toolbar,new FrameLayout.LayoutParams(-1,-1));
        switchButton=button(L10n.tr("电脑键盘"),()->{shortcuts=false;switchKeyboard();});switchButton.setPadding(ui.dp(8),0,ui.dp(8),0);
        toolbar.addView(switchButton,new LayoutParams(-2,-1));
        LinearLayout pages=ui.row();pages.setGravity(Gravity.CENTER);toolbar.addView(pages,new LayoutParams(0,-1,1));
        quickScroll=new HorizontalScrollView(context);quickScroll.setHorizontalScrollBarEnabled(false);
        quickKeys=ui.row();quickScroll.addView(quickKeys);pages.addView(quickScroll,new LayoutParams(-1,-1));
        panelTitle=ui.text(L10n.tr("快捷键"),13,false);panelTitle.setGravity(Gravity.CENTER);pages.addView(panelTitle,new LayoutParams(-1,-1));
        lettersButton=button(L10n.tr("字母数字"),()->page(false));functionsButton=button(L10n.tr("符号功能"),()->page(true));
        pages.addView(lettersButton,new LayoutParams(-2,ui.dp(32)));pages.addView(functionsButton,new LayoutParams(-2,ui.dp(32)));
        TextView more=button("⋯",this::showShortcuts);more.setContentDescription(L10n.tr("更多快捷键"));toolbar.addView(more,new LayoutParams(ui.dp(44),ui.dp(44)));
        FrameLayout hide=new FrameLayout(context);hide.setContentDescription(L10n.tr("收起键盘"));hide.setClickable(true);hide.setFocusable(true);
        hide.addView(ui.icon("keyboard_hide",foreground()),new FrameLayout.LayoutParams(ui.dp(24),ui.dp(24),Gravity.CENTER));
        hide.setOnClickListener(v->{feedback(v);close();});toolbar.addView(hide,new LayoutParams(ui.dp(44),ui.dp(44)));
        keys=ui.column();keys.setPadding(ui.dp(3),ui.dp(4),ui.dp(3),ui.dp(4));addView(keys,new LayoutParams(-1,0,1));
        render();
    }
    private boolean dark(){return (getResources().getConfiguration().uiMode&Configuration.UI_MODE_NIGHT_MASK)==Configuration.UI_MODE_NIGHT_YES;}
    private int foreground(){return dark()?Color.WHITE:0xFF202124;}
    private TextView button(String label,Runnable action){
        TextView view=ui.action(label,foreground(),action);view.setTypeface(android.graphics.Typeface.DEFAULT);
        view.setTextSize(13);view.setMinHeight(0);view.setPadding(ui.dp(8),0,ui.dp(8),0);
        view.setOnClickListener(v->{feedback(v);action.run();});return view;
    }
    private void feedback(View view){view.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);}
    boolean isOpen(){return opened;}
    boolean isComputer(){return computer;}
    void setPlatform(String value){hostPlatform=RemoteShortcuts.platform(value);shortcutPanel.platform(hostPlatform);if(opened)render();}
    int bottomInset(){return opened&&!computer?imeHeight:0;}
    private int sizeKey(){Configuration c=getResources().getConfiguration();return c.screenWidthDp*10000+c.screenHeightDp;}
    private int keyAreaHeight(){
        int height=heights.get(sizeKey(),0);
        if(height==0){Configuration c=getResources().getConfiguration();height=ui.dp(c.screenWidthDp>c.screenHeightDp?200:280);}
        int available=getParent() instanceof View?((View)getParent()).getMeasuredHeight()-ui.dp(44):height;
        return available>0?Math.min(height,available):height;
    }
    int preferredHeight(){return ui.dp(44)+(computer?keyAreaHeight():0);}
    void toggle(){if(opened)close();else open();}
    void open(){
        opened=true;computer=shortcuts=false;modifiers.clear();editor.start();setVisibility(VISIBLE);render();changed.run();
        post(()->{if(opened&&!computer)changeIme(true,false,false);});
    }
    boolean close(){
        shortcutPanel.close();if(!opened)return false;opened=false;measuring=switching=false;cancelTransition();modifiers.clear();editor.stop();
        getContext().getSystemService(InputMethodManager.class).hideSoftInputFromWindow(getWindowToken(),0);
        setVisibility(GONE);changed.run();return true;
    }
    void resetContext(){editor.resetContext();}
    private void page(boolean next){functions=next;render();}
    private void switchKeyboard(){
        if(!opened||switching)return;
        if(!computer&&!prepareShortcut())return;
        targetComputer=!computer;switching=true;modifiers.clear();
        if(targetComputer){
            editor.pause();computer=true;render();changed.run();
        }else editor.start();
        // Lay out computer keys behind the still-visible IME before onReady,
        // which runs after layout. Keep them until the native IME is ready too.
        if(targetComputer&&Build.VERSION.SDK_INT>=30&&isHardwareAccelerated()){
            int token=revision;
            // Insets surfaces can update ahead of the app's render thread.
            // Submit the replacement frame before releasing the IME surface.
            getViewTreeObserver().registerFrameCommitCallback(()->post(()->{
                if(opened&&switching&&targetComputer&&token==revision)changeIme(false,true,false);
            }));
            invalidate();
        }else changeIme(!targetComputer,true,false);
    }
    private boolean prepareShortcut(){
        if(computer)return true;
        if(editor.hasPendingComposition()){Toast.makeText(getContext(),L10n.tr("请先选词或取消组词"),Toast.LENGTH_SHORT).show();return false;}
        return editor.prepareShortcut();
    }
    private void showShortcuts(){
        if(!opened||switching)return;
        if(computer){if(shortcuts)switchKeyboard();else{shortcuts=true;modifiers.clear();render();changed.run();}}
        else{shortcuts=true;switchKeyboard();}
    }
    private void sendShortcut(RemoteShortcuts.Item item){
        if(!opened||switching||!item.platform.equals(hostPlatform)||!prepareShortcut())return;
        modifiers.clear();if(item.service)actions.secureAttention();else actions.chord(item.key,item.modifiers);
    }
    private void finishSwitch(){
        if(!switching)return;switching=false;
        if(computer!=targetComputer){computer=targetComputer;render();}
        changed.run();
    }
    void updateInsets(int height,boolean visible,int navigation){
        int key=sizeKey();
        if(lastInsetsHeight==height&&lastInsetsKey==key&&imeVisible==visible&&navigationBottom==navigation)return;
        lastInsetsHeight=height;lastInsetsKey=key;
        navigationBottom=navigation;imeHeight=Build.VERSION.SDK_INT>=30?Math.max(0,height):0;
        boolean wasVisible=imeVisible;imeVisible=visible;
        if(visible&&height>ui.dp(80)){
            heights.put(sizeKey(),height);
            if(measuring){measuring=false;post(()->{if(opened&&computer)changeIme(false,true,false);});}
        }
        if(switching&&visible!=targetComputer&&(!visible||height>ui.dp(80)))finishSwitch();
        if(opened&&!computer&&!switching&&wasVisible&&!visible&&transition==null){close();return;}
        if(opened){renderKeysIfNeeded(getMeasuredWidth());changed.run();}
    }
    private void cancelTransition(){revision++;if(transition!=null){CancellationSignal old=transition;transition=null;old.cancel();}}
    private void changeIme(boolean show,boolean immediate,boolean measureOnly){
        cancelTransition();if(!opened)return;
        measuring=measureOnly;
        if(Build.VERSION.SDK_INT>=30&&immediate&&getWindowInsetsController()!=null){
            // Request control directly: showSoftInput first starts a competing
            // system animation before we can replace the computer keyboard.
            if(show)editor.start();else if(measureOnly)editor.requestFocus();
            int token=revision;transition=new CancellationSignal();
            getWindowInsetsController().controlWindowInsetsAnimation(WindowInsets.Type.ime(),0,null,transition,new WindowInsetsAnimationControlListener(){
                public void onReady(WindowInsetsAnimationController controller,int types){
                    if(token!=revision||!opened){controller.finish(false);return;}
                    int height=controller.getShownStateInsets().bottom-navigationBottom;
                    if(height>ui.dp(80)){
                        heights.put(sizeKey(),height);measuring=false;
                        if(!measureOnly){imeHeight=show?height:0;finishSwitch();}
                        controller.finish(show&&!measureOnly);
                    }else if(measureOnly){
                        // Some IMEs report zero until their new input view is laid out.
                        // The first nonzero inset completes measurement and hides it.
                        controller.finish(true);
                    }else controller.finish(show);
                    renderKeysIfNeeded(getMeasuredWidth());changed.run();
                }
                public void onFinished(WindowInsetsAnimationController controller){if(token==revision)transition=null;}
                public void onCancelled(WindowInsetsAnimationController controller){
                    if(token!=revision)return;transition=null;
                    if(opened)normalIme(show||measureOnly);
                }
            });
        }else normalIme(show||measureOnly);
    }
    private void normalIme(boolean show){
        InputMethodManager manager=getContext().getSystemService(InputMethodManager.class);
        if(show){if(computer)editor.requestFocus();else editor.start();manager.showSoftInput(editor,InputMethodManager.SHOW_IMPLICIT);}
        else manager.hideSoftInputFromWindow(editor.getWindowToken(),0);
    }
    private void render(){
        setBackgroundColor(dark()?0xFF202124:0xFFE8EAED);
        switchButton.setText(computer?L10n.tr("系统键盘"):L10n.tr("电脑键盘"));switchButton.setTextColor(foreground());
        lettersButton.setVisibility(computer&&!shortcuts?VISIBLE:GONE);functionsButton.setVisibility(computer&&!shortcuts?VISIBLE:GONE);
        quickScroll.setVisibility(computer?GONE:VISIBLE);panelTitle.setVisibility(computer&&shortcuts?VISIBLE:GONE);panelTitle.setTextColor(foreground());
        quickKeys.removeAllViews();for(RemoteShortcuts.Item item:shortcutPanel.store.pinned(hostPlatform)){TextView key=button(item.title(),()->sendShortcut(item));key.setContentDescription(item.title()+", "+item.chord());quickKeys.addView(key,new LayoutParams(-2,ui.dp(44)));}
        for(TextView view:new TextView[]{lettersButton,functionsButton}){
            boolean selected=(view==functionsButton)==functions;view.setSelected(selected);
            view.setTextColor(selected?(dark()?0xFFA8C7FA:0xFF185ABC):foreground());
            view.setBackground(ui.background(selected?(dark()?0xFF394457:0xFFD2E3FC):Color.TRANSPARENT,6));
        }
        keys.setVisibility(computer?VISIBLE:GONE);layoutWidth=layoutHeight=0;renderKeysIfNeeded(getMeasuredWidth());
    }
    private void renderKeysIfNeeded(int width){
        if(!computer)return;int height=keyAreaHeight();
        if(layoutWidth==width&&layoutHeight==height)return;layoutWidth=width;layoutHeight=height;keys.removeAllViews();
        if(shortcuts){keys.addView(shortcutPanel.create(),new LayoutParams(-1,-1));return;}
        boolean landscape=getResources().getConfiguration().screenWidthDp>getResources().getConfiguration().screenHeightDp;
        String[][] upper;
        if(!functions)upper=new String[][]{{"1","2","3","4","5","6","7","8","9","0"},{"q","w","e","r","t","y","u","i","o","p"},{"a","s","d","f","g","h","j","k","l"}};
        else if(landscape)upper=new String[][]{{"`","-","=","[","]","\\",";","'",",",".","/"},{"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"},{"Esc","Tab","Caps","Ins","Del","Home","End"}};
        else upper=new String[][]{{"`","-","=","[","]","\\",";","'",",",".","/"},{"F1","F2","F3","F4","F5","F6"},{"F7","F8","F9","F10","F11","F12"},{"Esc","Tab","Caps","Ins","Del","Home","End"}};
        LinearLayout upperKeys=ui.column();keys.addView(upperKeys,new LayoutParams(-1,0,1));
        for(String[] row:upper)upperKeys.addView(keyRow(row),new LayoutParams(-1,0,1));
        // The same bottom two rows keep their geometry on both pages.
        int rowHeight=Math.max(1,(height-ui.dp(8))/5);
        keys.addView(keyRow(functions?new String[]{"⇧","←","↓","↑","→","PgUp","PgDn","⌫"}:new String[]{"⇧","z","x","c","v","b","n","m","⌫"}),new LayoutParams(-1,rowHeight));
        keys.addView(keyRow(new String[]{"Ctrl","Win","Alt","空格","Enter"}),new LayoutParams(-1,rowHeight));
    }
    private LinearLayout keyRow(String[] values){
        LinearLayout line=ui.row();line.setGravity(Gravity.CENTER);
        for(String value:values){
            boolean shift=modifiers.contains(16);String shown=shift&&value.length()==1?shifted(value):value;
            if(value.equals("Win"))shown=RemoteShortcuts.modifierName(91,hostPlatform);else if(value.equals("Alt"))shown=RemoteShortcuts.modifierName(18,hostPlatform);
            TextView key=button(L10n.tr(shown),()->key(value));key.setPadding(0,0,0,0);
            key.setTextSize(value.length()>1?12:18);key.setSingleLine(true);
            int modifier=modifier(value);boolean selected=modifier!=0&&modifiers.contains(modifier);key.setSelected(selected);
            int color=selected?(dark()?0xFF596D8C:0xFFA8C7FA):value.length()>1||value.equals("⇧")||value.equals("⌫")?(dark()?0xFF30343B:0xFFD0D5DC):(dark()?0xFF3C4043:Color.WHITE);
            StateListDrawable background=new StateListDrawable();background.addState(new int[]{android.R.attr.state_pressed},ui.background(dark()?0xFF68717D:0xFFBDC9D8,7));background.addState(new int[]{},ui.background(color,7));key.setBackground(background);
            if(value.equals("⇧"))key.setContentDescription("Shift");else if(value.equals("⌫"))key.setContentDescription(L10n.tr("退格"));
            // Both penultimate rows allocate exactly 1/8 to Shift and Backspace.
            boolean penultimate=values[0].equals("⇧");float weight=value.equals("空格")?4:1;
            if(penultimate)weight=value.equals("⇧")||value.equals("⌫")?1:(6f/(values.length-2));
            boolean edge=penultimate&&(value.equals("⇧")||value.equals("⌫"));
            int edgeWidth=Math.max(1,(layoutWidth-ui.dp(6))/8-ui.dp(5));
            LayoutParams p=new LayoutParams(edge?edgeWidth:0,-1,edge?0:weight);p.setMargins(ui.dp(2.5f),ui.dp(3),ui.dp(2.5f),ui.dp(3));line.addView(key,p);
        }
        return line;
    }
    private static int modifier(String value){return switch(value){case "⇧"->16;case "Ctrl"->17;case "Alt"->18;case "Win"->91;default->0;};}
    private static String shifted(String value){
        String plain="1234567890`-=[]\\;',./",shifted="!@#$%^&*()~_+{}|:\"<>?";int index=plain.indexOf(value);
        return index>=0?shifted.substring(index,index+1):value.toUpperCase(java.util.Locale.ROOT);
    }
    private void key(String value){
        int modifier=modifier(value);if(modifier!=0){if(!modifiers.add(modifier))modifiers.remove(modifier);render();return;}
        int code=switch(value){case "空格"->32;case "Enter"->13;case "⌫"->8;case "Esc"->27;case "Tab"->9;case "Caps"->20;case "Ins"->45;case "Del"->46;case "Home"->36;case "End"->35;case "PgUp"->33;case "PgDn"->34;case "←"->37;case "↑"->38;case "→"->39;case "↓"->40;default->value.startsWith("F")?111+Integer.parseInt(value.substring(1)):KeyMap.ascii(value.charAt(0))&255;};
        actions.chord(code,modifiers.stream().mapToInt(Integer::intValue).toArray());
        modifiers.clear();render();
    }
    @Override protected void onMeasure(int widthSpec,int heightSpec){
        renderKeysIfNeeded(MeasureSpec.getSize(widthSpec));super.onMeasure(widthSpec,heightSpec);
    }
    @Override protected void onSizeChanged(int width,int height,int oldWidth,int oldHeight){
        super.onSizeChanged(width,height,oldWidth,oldHeight);
        if(opened&&computer&&(width!=oldWidth||height!=oldHeight)&&heights.get(sizeKey(),0)==0)post(()->{
            if(opened&&computer&&!switching&&!measuring&&transition==null&&heights.get(sizeKey(),0)==0)changeIme(false,true,true);
        });
    }
    @Override protected void onDetachedFromWindow(){close();super.onDetachedFromWindow();}
}
