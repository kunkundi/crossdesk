type Data = Record<string, any>;
export type InputFrame = { width: number; height: number; generation: number };
const keys: Record<string, string> = {
  Backspace: "BACKSPACE", Tab: "TAB", Enter: "ENTER", Escape: "ESC", Space: "SPACE",
  ShiftLeft: "SHIFT", ShiftRight: "SHIFT", ControlLeft: "CTRL", ControlRight: "CTRL",
  AltLeft: "ALT", AltRight: "ALT", MetaLeft: "META", MetaRight: "META",
  ArrowLeft: "LEFT", ArrowRight: "RIGHT", ArrowUp: "UP", ArrowDown: "DOWN",
  PageUp: "PAGE_UP", PageDown: "PAGE_DOWN", Home: "HOME", End: "END",
  Insert: "INSERT", Delete: "DELETE", CapsLock: "CAPS_LOCK", NumLock: "NUM_LOCK",
  ScrollLock: "SCROLL_LOCK", PrintScreen: "PRINT_SCREEN", ContextMenu: "MENU", Pause: "PAUSE",
  Semicolon: "SEMICOLON", Equal: "EQUAL", Comma: "COMMA", Minus: "MINUS", Period: "PERIOD",
  Slash: "SLASH", Backquote: "BACKQUOTE", BracketLeft: "BRACKET_LEFT", Backslash: "BACKSLASH",
  BracketRight: "BRACKET_RIGHT", Quote: "QUOTE", NumpadEnter: "ENTER",
  NumpadMultiply: "MULTIPLY", NumpadAdd: "ADD", NumpadSubtract: "SUBTRACT",
  NumpadDecimal: "DECIMAL", NumpadDivide: "DIVIDE",
};
function keyName(event: KeyboardEvent) {
  if (/^Key[A-Z]$/.test(event.code)) return event.code.slice(3);
  if (/^Digit[0-9]$/.test(event.code)) return event.code.slice(5);
  if (/^Numpad[0-9]$/.test(event.code)) return "NUMPAD" + event.code.slice(6);
  if (/^F([1-9]|1[0-9]|2[0-4])$/.test(event.code)) return event.code;
  return keys[event.code];
}

// Native ownership and epochs also fence queued events after a panel handoff.
export class HumanInput {
  private frame?: InputFrame;
  private frameAt = 0;
  private epoch?: number;
  private queue: Data[] = [];
  private sending = false;
  private pressed = new Set<string>();
  private move?: Data;
  private timer?: ReturnType<typeof setTimeout>;
  constructor(private surface: HTMLElement, private editor: HTMLTextAreaElement,
    private send: (args: Data) => Promise<unknown>, private error: (error: unknown) => void) {
    surface.addEventListener("pointerdown", (event) => {
      if (this.epoch === undefined || event.button > 2) return;
      const point = this.point(event, false);
      if (!point) return;
      event.preventDefault();
      editor.focus({ preventScroll: true });
      surface.setPointerCapture(event.pointerId);
      this.flushMove();
      this.modifiers(event);
      this.enqueue({ type: "mouse_down", ...point, button: this.button(event.button) });
    });
    surface.addEventListener("pointermove", (event) => {
      if (this.epoch === undefined) return;
      const point = this.point(event, surface.hasPointerCapture(event.pointerId));
      if (!point) return;
      this.move = { type: "move", ...point };
      this.timer ??= setTimeout(() => this.flushMove(), 33);
    });
    surface.addEventListener("pointerup", (event) => {
      if (this.epoch === undefined || event.button > 2) return;
      this.flushMove();
      const point = this.point(event, true);
      if (point) this.enqueue({ type: "mouse_up", ...point, button: this.button(event.button) });
      if (surface.hasPointerCapture(event.pointerId)) surface.releasePointerCapture(event.pointerId);
    });
    surface.addEventListener("pointercancel", () => this.release());
    surface.addEventListener("contextmenu", (event) => { if (this.epoch !== undefined) event.preventDefault(); });
    surface.addEventListener("wheel", (event) => {
      if (this.epoch === undefined) return;
      const point = this.point(event, false);
      if (!point) return;
      event.preventDefault();
      const horizontal = Math.abs(event.deltaX) > Math.abs(event.deltaY);
      const delta = horizontal ? event.deltaX : event.deltaY;
      if (!delta) return;
      this.flushMove();
      this.modifiers(event);
      this.enqueue({ type: "scroll", ...point,
        direction: horizontal ? delta > 0 ? "right" : "left" : delta > 0 ? "down" : "up",
        ticks: Math.min(10, Math.max(1, Math.round(Math.abs(delta) / (event.deltaMode === 0 ? 100 : 1)))) });
    }, { passive: false });
    editor.addEventListener("keydown", (event) => {
      if (this.epoch === undefined || event.isComposing) return;
      // Let the local input method produce committed Unicode text.
      if (event.key.length === 1 && ((!event.ctrlKey && !event.metaKey && !event.altKey) || event.getModifierState?.("AltGraph"))) return;
      const key = keyName(event);
      if (!key) return;
      event.preventDefault();
      this.modifiers(event, key);
      if (!this.pressed.has(key) || event.repeat) {
        this.pressed.add(key);
        this.enqueue({ type: "key_down", key });
      }
    });
    editor.addEventListener("keyup", (event) => {
      const key = keyName(event);
      const held = key === "CTRL" ? event.ctrlKey : key === "ALT" ? event.altKey :
        key === "META" ? event.metaKey : key === "SHIFT" ? event.shiftKey : false;
      if (key && !held && this.pressed.delete(key)) {
        event.preventDefault();
        this.enqueue({ type: "key_up", key });
      }
    });
    const commit = () => {
      const value = editor.value;
      editor.value = "";
      if (!value || this.epoch === undefined) return;
      if (new TextEncoder().encode(value).length > 16384) {
        this.release();
        this.error(new Error("INVALID_ARGUMENT"));
        return;
      }
      this.pressed.clear();
      this.enqueue({ type: "type_text", text: value });
    };
    editor.addEventListener("input", (event) => { if (!(event as InputEvent).isComposing) commit(); });
    editor.addEventListener("compositionend", commit);
    editor.addEventListener("blur", () => this.release());
    window.addEventListener("blur", () => this.release());
    window.addEventListener("pagehide", () => this.release());
    document.addEventListener("visibilitychange", () => { if (document.hidden) this.release(); });
  }
  update(epoch: number | undefined) {
    if (this.epoch === epoch) return;
    this.clear();
    this.epoch = epoch;
    this.surface.dataset.human = String(epoch !== undefined);
    if (epoch === undefined) this.editor.blur();
  }
  setFrame(frame?: InputFrame) {
    this.frame = frame ? { width: frame.width, height: frame.height, generation: frame.generation } : undefined;
    this.frameAt = Date.now();
  }
  private modifiers(event: MouseEvent | KeyboardEvent, skip?: string) {
    for (const [key, held] of [["CTRL", event.ctrlKey], ["ALT", event.altKey],
      ["META", event.metaKey], ["SHIFT", event.shiftKey]] as const) {
      if (key === skip) continue;
      if (held) {
        // Refresh modifiers after focus changes or native input-lease expiry.
        this.pressed.add(key);
        this.enqueue({ type: "key_down", key });
      } else if (this.pressed.delete(key)) this.enqueue({ type: "key_up", key });
    }
  }
  private button(index: number) { return ["left", "middle", "right"][index]; }
  private point(event: MouseEvent, clamp: boolean) {
    if (!this.frame) return;
    const rect = this.surface.getBoundingClientRect();
    const scale = Math.min(rect.width / this.frame.width, rect.height / this.frame.height);
    if (!scale) return;
    const width = this.frame.width * scale, height = this.frame.height * scale;
    let x = (event.clientX - rect.left - (rect.width - width) / 2) / width;
    let y = (event.clientY - rect.top - (rect.height - height) / 2) / height;
    if (!clamp && (x < 0 || x > 1 || y < 0 || y > 1)) return;
    return { x: Math.max(0, Math.min(1, x)), y: Math.max(0, Math.min(1, y)) };
  }
  private flushMove() {
    clearTimeout(this.timer);
    this.timer = undefined;
    const move = this.move;
    this.move = undefined;
    if (move) this.enqueue(move);
  }
  private clear() {
    clearTimeout(this.timer);
    this.timer = undefined;
    this.move = undefined;
    this.queue = [];
    this.pressed.clear();
    this.editor.value = "";
  }
  release() {
    this.clear();
    this.enqueue({ type: "release" });
  }
  private enqueue(action: Data) {
    if (this.epoch === undefined || !this.frame) return;
    if (action.type !== "release" && Date.now() - this.frameAt > 2000) {
      this.release();
      return;
    }
    if (this.queue.length >= 64) {
      this.clear();
      action = { type: "release" };
      this.error(new Error("INPUT_QUEUE_FULL"));
    }
    this.queue.push({ ...this.frame, input_epoch: this.epoch, action });
    void this.pump();
  }
  private async pump() {
    if (this.sending) return;
    this.sending = true;
    try {
      while (this.queue.length) {
        const packet = this.queue.shift()!;
        if (packet.input_epoch !== this.epoch) continue;
        try { await this.send(packet); }
        catch (error) {
          // Never replay clicks or text after an uncertain response.
          if (packet.input_epoch === this.epoch) {
            this.clear();
            try { await this.send({ ...packet, action: { type: "release" } }); } catch { /* Lease releases on lost transport. */ }
            this.error(error);
          }
        }
      }
    } finally { this.sending = false; }
  }
}
