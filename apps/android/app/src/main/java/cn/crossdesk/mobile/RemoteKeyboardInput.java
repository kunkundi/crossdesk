package cn.crossdesk.mobile;

import android.content.Context;
import android.icu.text.BreakIterator;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.KeyEvent;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputConnectionWrapper;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import java.util.ArrayList;
import java.util.List;

/** A real editor keeps IME candidates, corrections and dictation local until committed. */
@android.annotation.SuppressLint("ViewConstructor")
final class RemoteKeyboardInput extends EditText {
    interface Sender {
        boolean edit(int deletions, String text);
        void key(int code);
    }
    private final Sender sender;
    private final android.os.Handler main=new android.os.Handler(android.os.Looper.getMainLooper());
    private String sent = "";
    private String committed = "";
    private boolean accepting, resetting, enterPending;
    private int batches, generation;
    private final Runnable synchronize = this::flush;
    private void flush() {
        if (!accepting || batches > 0 || !finishEdit()) return;
        if (enterPending || sent.length() > 2048 || sent.indexOf('\n') >= 0) resetContext();
    }

    RemoteKeyboardInput(Context context, Sender sender) {
        super(context);this.sender=sender;
        setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_AUTO_CORRECT | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        setImeOptions(EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_ACTION_NONE);
        setCursorVisible(false);setBackground(null);setPadding(0,0,0,0);setAlpha(0);
        setImportantForAutofill(IMPORTANT_FOR_AUTOFILL_NO);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s,int start,int count,int after) { }
            public void onTextChanged(CharSequence s,int start,int before,int count) { }
            public void afterTextChanged(Editable value) { schedule(); }
        });
    }
    void start() {
        if (!requestFocus() || accepting) return;
        accepting = true;
        // Android can still serve the connection invalidated by pause(), even
        // when the editor has lost and regained focus in the meantime.
        getContext().getSystemService(InputMethodManager.class).restartInput(this);
    }
    // Keep focus while replacing the IME, but reject its late edits without
    // restarting it. A restart here can show it again after the controlled hide.
    void pause() {
        if (accepting) {
            accepting = false;
            finishEdit();
        }
        resetContext();
    }
    void stop() { pause();clearFocus(); }
    boolean isAcceptingInput() { return accepting; }
    boolean hasPendingComposition() { return batches>0||composing(); }
    boolean prepareShortcut() {
        if (!accepting || !finishEdit()) return false;
        resetContext();
        return true;
    }
    private boolean finishEdit() {
        if (!synchronize()) return false;
        if (enterPending) sender.key(13);
        return true;
    }
    void finishInputContext() {
        if (accepting) finishEdit();
        resetContext();
    }
    private void resetContext() {
        generation++;batches=0;main.removeCallbacks(synchronize);resetting=true;
        // EditText.setText replaces the buffer and internally restarts the IME,
        // even while paused. Clear in place so only the explicit restart below
        // can renew the input connection.
        enterPending=false;sent=committed="";BaseInputConnection.removeComposingSpans(getText());getText().clear();setSelection(0);resetting=false;
        if(accepting&&hasFocus())getContext().getSystemService(InputMethodManager.class).restartInput(this);
    }
    private void schedule() { if(!resetting&&accepting){main.removeCallbacks(synchronize);main.post(synchronize);} }
    private boolean composing() { return BaseInputConnection.getComposingSpanStart(getText())>=0; }
    private boolean afterImeEdit(boolean applied) {
        // Another composition can start in the same batch, before the posted flush.
        if (applied) {
            if (!composing()) committed = getText().toString();
            schedule();
        }
        return applied;
    }
    private static List<String> characters(String text) {
        BreakIterator iterator=BreakIterator.getCharacterInstance();iterator.setText(text);
        List<String> result=new ArrayList<>();int start=iterator.first();
        for(int end=iterator.next();end!=BreakIterator.DONE;start=end,end=iterator.next())result.add(text.substring(start,end));
        return result;
    }
    private boolean synchronize() {
        if (resetting) return false;
        Editable value = getText();
        int markedStart = BaseInputConnection.getComposingSpanStart(value);
        boolean complete = batches == 0 && markedStart < 0;
        if (complete) {
            committed = value.toString();
        } else if (batches == 0 && markedStart > committed.length()) {
            // An IME can confirm a prefix by shrinking its composing region.
            // Re-marking an earlier word must preserve the already committed tail.
            String prefix = value.subSequence(0, markedStart).toString();
            if (prefix.startsWith(committed)) committed = prefix;
        }
        String current = committed;
        if (current.equals(sent)) return complete;
        List<String> old=characters(sent),next=characters(current);int prefix=0,offset=0;
        while(prefix<old.size()&&prefix<next.size()&&old.get(prefix).equals(next.get(prefix))){offset+=next.get(prefix).length();prefix++;}
        if(!sender.edit(old.size()-prefix,current.substring(offset))){resetContext();return false;}
        sent=current;
        return complete;
    }
    private void enter() {
        if(!accepting)return;
        BaseInputConnection.removeComposingSpans(getText());
        enterPending=true;schedule();
    }
    @Override public boolean onKeyDown(int code,KeyEvent event) {
        if(accepting&&code==KeyEvent.KEYCODE_ENTER){enter();return true;}
        if(accepting&&code==KeyEvent.KEYCODE_DEL&&length()==0){sender.key(8);return true;}
        return super.onKeyDown(code,event);
    }
    @Override public boolean onKeyUp(int code,KeyEvent event) {
        if(accepting&&(code==KeyEvent.KEYCODE_ENTER||code==KeyEvent.KEYCODE_DEL))return true;
        return super.onKeyUp(code,event);
    }
    @Override public InputConnection onCreateInputConnection(EditorInfo info) {
        InputConnection base=super.onCreateInputConnection(info);if(base==null)return null;
        int revision=++generation;batches=0;
        return new InputConnectionWrapper(base,false) {
            private boolean active(){return accepting&&generation==revision;}
            @Override public boolean beginBatchEdit(){if(!active())return false;batches++;return super.beginBatchEdit();}
            @Override public boolean endBatchEdit(){if(!active())return false;boolean result=super.endBatchEdit();batches=Math.max(0,batches-1);schedule();return result;}
            @Override public boolean setComposingText(CharSequence text,int cursor) {
                return active() && afterImeEdit(super.setComposingText(text,cursor));
            }
            @Override public boolean setComposingRegion(int start,int end) {
                return active() && afterImeEdit(super.setComposingRegion(start,end));
            }
            @Override public boolean commitText(CharSequence text,int cursor) {
                return active() && afterImeEdit(super.commitText(text,cursor));
            }
            @Override public boolean finishComposingText() {
                return active() && afterImeEdit(super.finishComposingText());
            }
            @Override public boolean deleteSurroundingText(int before,int after){
                if(!active())return false;
                if(length()==0&&before>0&&after==0){for(int i=0;i<Math.min(before,2048);i++)sender.key(8);return true;}
                return afterImeEdit(super.deleteSurroundingText(before,after));
            }
            @Override public boolean deleteSurroundingTextInCodePoints(int before,int after){
                if(!active())return false;
                if(length()==0&&before>0&&after==0){for(int i=0;i<Math.min(before,2048);i++)sender.key(8);return true;}
                return afterImeEdit(super.deleteSurroundingTextInCodePoints(before,after));
            }
            @Override public boolean performEditorAction(int action){if(!active())return false;enter();return true;}
            @Override public boolean sendKeyEvent(KeyEvent event){return active()&&super.sendKeyEvent(event);}
            @Override public boolean setSelection(int start,int end){return active()&&super.setSelection(start,end);}
            @Override public void closeConnection(){if(active())super.closeConnection();}
            @android.annotation.SuppressLint("NewApi") // Framework calls these overloads only on API 33+.
            @Override public boolean commitText(CharSequence text,int cursor,android.view.inputmethod.TextAttribute attributes){
                return active() && afterImeEdit(super.commitText(text,cursor,attributes));
            }
            @android.annotation.SuppressLint("NewApi") // Framework calls these overloads only on API 33+.
            @Override public boolean setComposingText(CharSequence text,int cursor,android.view.inputmethod.TextAttribute attributes){
                return active() && afterImeEdit(super.setComposingText(text,cursor,attributes));
            }
            @android.annotation.SuppressLint("NewApi") // Framework calls these overloads only on API 33+.
            @Override public boolean setComposingRegion(int start,int end,android.view.inputmethod.TextAttribute attributes) {
                return active() && afterImeEdit(super.setComposingRegion(start,end,attributes));
            }
            @android.annotation.SuppressLint("NewApi") // Framework calls this overload only on API 34+.
            @Override public boolean replaceText(int start,int end,CharSequence text,int cursor,android.view.inputmethod.TextAttribute attributes){
                return active() && afterImeEdit(super.replaceText(start,end,text,cursor,attributes));
            }
        };
    }
}
