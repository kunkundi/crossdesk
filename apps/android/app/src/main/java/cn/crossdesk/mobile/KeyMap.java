package cn.crossdesk.mobile;

import android.view.KeyEvent;

final class KeyMap {
    static int windowsCode(int code) {
        if (code >= KeyEvent.KEYCODE_A && code <= KeyEvent.KEYCODE_Z) return 0x41 + code - KeyEvent.KEYCODE_A;
        if (code >= KeyEvent.KEYCODE_0 && code <= KeyEvent.KEYCODE_9) return 0x30 + code - KeyEvent.KEYCODE_0;
        if (code >= KeyEvent.KEYCODE_F1 && code <= KeyEvent.KEYCODE_F12) return 0x70 + code - KeyEvent.KEYCODE_F1;
        return switch (code) {
            case KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> 0x0D;
            case KeyEvent.KEYCODE_DEL -> 0x08;
            case KeyEvent.KEYCODE_FORWARD_DEL -> 0x2E;
            case KeyEvent.KEYCODE_TAB -> 0x09;
            case KeyEvent.KEYCODE_ESCAPE -> 0x1B;
            case KeyEvent.KEYCODE_SPACE -> 0x20;
            case KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> 0x10;
            case KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> 0x11;
            case KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> 0x12;
            case KeyEvent.KEYCODE_META_LEFT, KeyEvent.KEYCODE_META_RIGHT -> 0x5B;
            case KeyEvent.KEYCODE_DPAD_LEFT -> 0x25;
            case KeyEvent.KEYCODE_DPAD_UP -> 0x26;
            case KeyEvent.KEYCODE_DPAD_RIGHT -> 0x27;
            case KeyEvent.KEYCODE_DPAD_DOWN -> 0x28;
            case KeyEvent.KEYCODE_MOVE_HOME -> 0x24;
            case KeyEvent.KEYCODE_MOVE_END -> 0x23;
            case KeyEvent.KEYCODE_PAGE_UP -> 0x21;
            case KeyEvent.KEYCODE_PAGE_DOWN -> 0x22;
            case KeyEvent.KEYCODE_MINUS -> 0xBD;
            case KeyEvent.KEYCODE_EQUALS -> 0xBB;
            case KeyEvent.KEYCODE_LEFT_BRACKET -> 0xDB;
            case KeyEvent.KEYCODE_RIGHT_BRACKET -> 0xDD;
            case KeyEvent.KEYCODE_BACKSLASH -> 0xDC;
            case KeyEvent.KEYCODE_SEMICOLON -> 0xBA;
            case KeyEvent.KEYCODE_APOSTROPHE -> 0xDE;
            case KeyEvent.KEYCODE_COMMA -> 0xBC;
            case KeyEvent.KEYCODE_PERIOD -> 0xBE;
            case KeyEvent.KEYCODE_SLASH -> 0xBF;
            case KeyEvent.KEYCODE_GRAVE -> 0xC0;
            default -> 0;
        };
    }
    // High bit is the Shift modifier; zero means use the clipboard for this text.
    static int ascii(char ch) {
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 0x41;
        if (ch >= 'A' && ch <= 'Z') return ch | 0x100;
        if (ch >= '0' && ch <= '9') return ch;
        if (ch == '\n') return 0x0D;
        if (ch == '\t') return 0x09;
        if (ch == ' ') return 0x20;
        String plain = "-=[]\\;',./`";
        String shifted = "_+{}|:\"<>?~";
        int[] codes = {0xBD,0xBB,0xDB,0xDD,0xDC,0xBA,0xDE,0xBC,0xBE,0xBF,0xC0};
        int i = plain.indexOf(ch);
        if (i >= 0) return codes[i];
        i = shifted.indexOf(ch);
        if (i >= 0) return codes[i] | 0x100;
        i = ")!@#$%^&*(".indexOf(ch);
        return i < 0 ? 0 : (0x30 + i) | 0x100;
    }
}
