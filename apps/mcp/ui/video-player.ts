export type FrameInfo = { width: number; height: number; generation: number; fps: number; frames: number;
  sourceSize?: { width: number; height: number } };
type Cursor = { type: "cursor"; visible: boolean; x: number; y: number;
  generation: number; geometry_epoch: number; width: number; height: number };
function parameters(data: Uint8Array) {
  let codec = "", signature = "";
  for (let i = 0; i + 4 < data.length; i++) {
    if (data[i] || data[i + 1]) continue;
    const prefix = data[i + 2] === 1 ? 3 : data[i + 2] === 0 && data[i + 3] === 1 ? 4 : 0;
    if (!prefix) continue;
    const start = i + prefix, type = data[start]! & 31;
    if (type !== 7 && type !== 8) continue;
    let end = start + 1;
    while (end + 3 < data.length && !(data[end] === 0 && data[end + 1] === 0 &&
      (data[end + 2] === 1 || (data[end + 2] === 0 && data[end + 3] === 1)))) end++;
    if (end + 3 >= data.length) end = data.length;
    if (end - start > 65536) throw new Error("VIDEO_INVALID");
    const hex = [...data.subarray(start, end)].map((n) => n.toString(16).padStart(2, "0")).join("");
    signature += hex;
    if (type === 7 && end >= start + 4) codec = "avc1." + hex.slice(2, 8);
    i = end - 1;
  }
  return { codec, signature };
}

export class VideoPlayer {
  private socket?: WebSocket;
  private decoder?: VideoDecoder;
  private stopped = false;
  private failed = false;
  private waitingKey = true;
  private generation = -1;
  private sequence = -1;
  private signature = "";
  private queue: ArrayBuffer[] = [];
  private queuedBytes = 0;
  private pumping = false;
  private keyRequested = 0;
  private firstFrame?: ReturnType<typeof setTimeout>;
  private latest?: VideoFrame;
  private drawing = 0;
  private count = 0;
  private samples = 0;
  private sampledAt = performance.now();
  private fps = 0;
  private raw = false;
  private software = false;
  private automatic = true;
  private cursor?: Cursor;
  private cursorDrawn = "";
  private cursorHeading = -Math.PI * 3 / 4;
  private cursorMotion?: { x: number; y: number; started: number; duration: number };
  private cursorDrawing = 0;
  private reducedMotion?: MediaQueryList;
  private motionPreferenceChanged = () => {
    this.cursorMotion = undefined;
    this.drawCursor();
  };
  private resize?: ResizeObserver;
  private policyListener?: (event: SecurityPolicyViolationEvent) => void;
  private socketError?: ReturnType<typeof setTimeout>;
  constructor(private canvas: HTMLCanvasElement,
    private onFrame: (info: FrameInfo) => void,
    private onError: (error: Error) => void,
    private overlay?: HTMLCanvasElement,
    private onAudio?: (bytes: ArrayBuffer) => void) {
    if (overlay) {
      this.reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");
      this.reducedMotion.addEventListener("change", this.motionPreferenceChanged);
      this.resize = new ResizeObserver(() => this.drawCursor());
      this.resize.observe(overlay);
    }
  }

  start(url: string, ticket: string, protocol = "h264-annexb/1", decoder = "auto") {
    if (!["h264-annexb/1", "nv12/1"].includes(protocol) ||
        !["auto", "software"].includes(decoder)) throw new Error("VIDEO_INVALID");
    this.raw = protocol === "nv12/1";
    this.automatic = decoder === "auto";
    this.software = !this.automatic;
    if (typeof VideoFrame === "undefined" || (!this.raw && typeof VideoDecoder === "undefined"))
      throw new Error("VIDEO_UNSUPPORTED");
    if (!/^ws:\/\/127\.0\.0\.1:\d+\/api\/video$/.test(url)) throw new Error("VIDEO_INVALID");
    this.policyListener = (event) => {
      if (event.disposition === "enforce" &&
          ["connect-src", "default-src"].includes(event.effectiveDirective) &&
          [url, new URL(url).origin].includes(event.blockedURI))
        this.fail(new Error("VIDEO_POLICY_BLOCKED"));
    };
    document.addEventListener("securitypolicyviolation", this.policyListener);
    let socket: WebSocket;
    try { socket = this.socket = new WebSocket(url); }
    catch (error) {
      this.fail(new Error(error instanceof DOMException && error.name === "SecurityError"
        ? "VIDEO_POLICY_BLOCKED" : "VIDEO_UNAVAILABLE"));
      return;
    }
    socket.binaryType = "arraybuffer";
    socket.onopen = () => socket.send(JSON.stringify({ ticket }));
    // Give a queued CSP event a chance to identify the failure precisely.
    socket.onerror = socket.onclose = () => {
      if (!this.stopped) this.socketError ??= setTimeout(() => this.fail(new Error("VIDEO_UNAVAILABLE")), 0);
    };
    this.firstFrame = setTimeout(() => this.fail(new Error("VIDEO_UNAVAILABLE")), 10000);
    socket.onmessage = (event) => {
      if (this.stopped) return;
      if (typeof event.data === "string") {
        try {
          if (event.data.length > 1024) throw new Error();
          const p = JSON.parse(event.data) as Cursor;
          if (p.type !== "cursor" || typeof p.visible !== "boolean" ||
              !Number.isFinite(p.x) || !Number.isFinite(p.y) || p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1 ||
              !Number.isSafeInteger(p.generation) || p.generation < 0 ||
              !Number.isSafeInteger(p.geometry_epoch) || p.geometry_epoch < 0 ||
              !Number.isInteger(p.width) || !Number.isInteger(p.height) ||
              p.width < 0 || p.width > 8192 || p.height < 0 || p.height > 8192)
            throw new Error();
          this.updateCursor(p);
        } catch { this.fail(new Error("VIDEO_INVALID")); }
        return;
      }
      if (!(event.data instanceof ArrayBuffer)) return;
      const data = event.data;
      if (data.byteLength >= 4 && new DataView(data).getUint32(0) === 0x43444131) {
        try { this.onAudio?.(data); } catch { this.onError(new Error("AUDIO_INVALID")); }
        return;
      }
      if (data.byteLength < (this.raw ? 38 : 25) ||
          data.byteLength > (this.raw ? 16777216 * 3 / 2 + 32 : 4 * 1024 * 1024 + 24))
        return this.fail(new Error("VIDEO_INVALID"));
      const header = new DataView(data);
      if (header.getUint32(0) !== (this.raw ? 0x43444e31 : 0x43445631))
        return this.fail(new Error("VIDEO_INVALID"));
      // Receipt, not presentation: bounds native writes even when the decoder
      // or the canvas is slow. This message goes directly to the native CLI.
      if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({
        type: "ack", generation: header.getUint32(4), sequence: header.getUint32(8),
      }));
      if (this.raw || this.queue.length >= 8 || this.queuedBytes + data.byteLength > 8 * 1024 * 1024) {
        this.queue = [];
        this.queuedBytes = 0;
        this.waitingKey = true;
        this.requestKey();
      }
      this.queue.push(data);
      this.queuedBytes += data.byteLength;
      void this.pump();
    };
  }
  private requestKey() {
    if (this.raw) return;
    if (Date.now() - this.keyRequested < 1000 || this.socket?.readyState !== WebSocket.OPEN) return;
    this.keyRequested = Date.now();
    this.socket.send(JSON.stringify({ type: "keyframe" }));
  }
  private async pump() {
    if (this.pumping) return;
    this.pumping = true;
    try {
      while (!this.stopped && this.queue.length) {
        const bytes = this.queue.shift()!;
        this.queuedBytes -= bytes.byteLength;
        const packet = new DataView(bytes);
        if (packet.getUint32(0) !== (this.raw ? 0x43444e31 : 0x43445631) || packet.getUint32(20) > 1)
          throw new Error("VIDEO_INVALID");
        const generation = packet.getUint32(4), sequence = packet.getUint32(8);
        const key = packet.getUint32(20) === 1;
        if (generation < this.generation) continue;
        const timestamp = Number(packet.getBigUint64(12));
        if (!Number.isSafeInteger(timestamp)) throw new Error("VIDEO_INVALID");
        if (this.raw) {
          const width = packet.getUint32(24), height = packet.getUint32(28);
          if (!key || !width || !height || (width & 1) || (height & 1) ||
              width > 8192 || height > 8192 || width * height > 16777216 ||
              bytes.byteLength !== 32 + width * height * 3 / 2)
            throw new Error("VIDEO_INVALID");
          if (generation === this.generation && sequence <= this.sequence) continue;
          this.generation = generation;
          this.sequence = sequence;
          // Match the native NV12 screenshot conversion's limited-range BT.601.
          this.present(new VideoFrame(new Uint8Array(bytes, 32), {
            format: "NV12", codedWidth: width, codedHeight: height, timestamp,
            colorSpace: { matrix: "smpte170m", primaries: "smpte170m", transfer: "smpte170m", fullRange: false },
            transfer: [bytes],
          } as VideoFrameBufferInit & { transfer: ArrayBuffer[] }));
          continue;
        }
        if (generation !== this.generation || (this.sequence >= 0 && sequence !== this.sequence + 1))
          this.waitingKey = true;
        this.generation = generation;
        this.sequence = sequence;
        if ((this.decoder?.decodeQueueSize ?? 0) >= 4) this.waitingKey = true;
        if (this.waitingKey && !key) { this.requestKey(); continue; }
        const data = new Uint8Array(bytes, 24);
        if (key) {
          const { codec, signature } = parameters(data);
          if (!codec) { this.waitingKey = true; this.requestKey(); continue; }
          if (!this.decoder || this.waitingKey || signature !== this.signature) {
            const config: VideoDecoderConfig = { codec, optimizeForLatency: true,
              hardwareAcceleration: this.software ? "prefer-software" : "prefer-hardware" };
            if (!(await VideoDecoder.isConfigSupported(config)).supported) {
              if (this.software) throw new Error("VIDEO_UNSUPPORTED");
              this.software = true;
              config.hardwareAcceleration = "prefer-software";
              if (!(await VideoDecoder.isConfigSupported(config)).supported) throw new Error("VIDEO_UNSUPPORTED");
            }
            if (this.stopped) break;
            if (this.decoder && this.decoder.state !== "closed") this.decoder.close();
            const decoder = new VideoDecoder({
              output: (frame) => {
                if (this.stopped || this.decoder !== decoder) { frame.close(); return; }
                this.present(frame);
              },
              error: () => {
                if (this.stopped || this.decoder !== decoder) return;
                this.decoder = undefined;
                if (this.software || !this.automatic) {
                  this.fail(new Error("VIDEO_UNAVAILABLE"));
                  return;
                }
                this.software = true;
                this.waitingKey = true;
                this.requestKey();
              },
            });
            this.decoder = decoder;
            decoder.configure(config);
            this.signature = signature;
          }
          this.waitingKey = false;
        }
        this.decoder?.decode(new EncodedVideoChunk({ type: key ? "key" : "delta", timestamp, data }));
      }
    } catch (error) {
      this.fail(error instanceof Error ? error : new Error("VIDEO_INVALID"));
    } finally { this.pumping = false; }
  }
  private present(frame: VideoFrame) {
    this.latest?.close();
    this.latest = frame;
    if (!this.drawing) this.drawing = requestAnimationFrame(() => this.draw());
  }
  private draw() {
    this.drawing = 0;
    const frame = this.latest;
    this.latest = undefined;
    if (!frame) return;
    try {
      if (this.stopped) return;
      const width = frame.displayWidth, height = frame.displayHeight;
      if (width > 8192 || height > 8192 || width * height > 16777216) throw new Error("VIDEO_INVALID");
      if (this.canvas.width !== width || this.canvas.height !== height) {
        this.cursorMotion = undefined;
        this.cursorHeading = -Math.PI * 3 / 4;
        this.canvas.width = width;
        this.canvas.height = height;
      }
      this.canvas.getContext("2d", { alpha: false })!.drawImage(frame, 0, 0);
      clearTimeout(this.firstFrame);
      this.count++;
      this.drawCursor();
      this.samples++;
      const elapsed = performance.now() - this.sampledAt;
      if (elapsed >= 1000) {
        this.fps = this.samples * 1000 / elapsed;
        this.samples = 0;
        this.sampledAt = performance.now();
      }
      this.canvas.dataset.frames = String(this.count);
      this.onFrame({ width, height, generation: this.generation, frames: this.count, fps: this.fps });
    } catch { this.fail(new Error("VIDEO_INVALID")); }
    finally { frame.close(); }
  }
  private cursorPoint(now: number) {
    const p = this.cursor, motion = this.cursorMotion;
    if (!p || !motion) return p;
    const progress = Math.min(1, Math.max(0, (now - motion.started) / motion.duration));
    if (progress === 1) {
      this.cursorMotion = undefined;
      return p;
    }
    const eased = 1 - (1 - progress) ** 3;
    return { x: motion.x + (p.x - motion.x) * eased, y: motion.y + (p.y - motion.y) * eased };
  }
  private updateCursor(p: Cursor) {
    const now = performance.now(), previous = this.cursor, from = this.cursorPoint(now);
    const overlay = this.overlay;
    const compatible = previous?.visible && p.visible && previous.generation === p.generation &&
      previous.geometry_epoch === p.geometry_epoch && previous.width === p.width && previous.height === p.height;
    const moved = previous?.x !== p.x || previous?.y !== p.y;
    // Animate only the presentation. Native input and hit testing keep using
    // the exact target; new samples replace the target without queuing trails.
    if (!compatible || moved) {
      this.cursorMotion = undefined;
      if (!compatible) this.cursorHeading = -Math.PI * 3 / 4;
      if (compatible && moved && from && overlay && !this.stopped &&
          this.count && p.generation === this.generation &&
          p.width === this.canvas.width && p.height === this.canvas.height) {
        const scale = Math.min(overlay.clientWidth / p.width, overlay.clientHeight / p.height);
        const dx = (p.x - from.x) * p.width, dy = (p.y - from.y) * p.height;
        const distance = Math.hypot(dx, dy) * scale;
        if (distance > .5) {
          // Follow the displayed trajectory in screen pixels, not normalized
          // coordinates. Keep the last heading at rest and ignore subpixel noise.
          this.cursorHeading = Math.atan2(dy, dx);
          if (!document.hidden && !this.reducedMotion?.matches)
            this.cursorMotion = { x: from.x, y: from.y, started: now,
              duration: Math.min(140, 60 + distance * .12) };
        }
      }
    }
    this.cursor = p;
    this.drawCursor(now);
  }
  private drawCursor(now = performance.now()) {
    const overlay = this.overlay;
    if (!overlay) return;
    if (overlay.width !== this.canvas.width) overlay.width = this.canvas.width;
    if (overlay.height !== this.canvas.height) overlay.height = this.canvas.height;
    const p = this.cursor;
    // Match the video canvas's object-fit: contain, including letterboxing.
    const scale = Math.min(overlay.clientWidth / overlay.width, overlay.clientHeight / overlay.height);
    const visible = !this.stopped && !!this.count && p?.visible && p.generation === this.generation &&
      p.width === overlay.width && p.height === overlay.height && scale > 0;
    if (!visible || document.hidden || this.reducedMotion?.matches) this.cursorMotion = undefined;
    const point = this.cursorPoint(now);
    if (this.cursorMotion) {
      // Cursor animation is independent of video frames, including a static desktop.
      if (!this.cursorDrawing) this.cursorDrawing = requestAnimationFrame(() => {
        this.cursorDrawing = 0;
        this.drawCursor();
      });
    } else {
      cancelAnimationFrame(this.cursorDrawing);
      this.cursorDrawing = 0;
    }
    const state = JSON.stringify([this.stopped, !!this.count, this.generation,
      overlay.width, overlay.height, overlay.clientWidth, overlay.clientHeight, p, point, this.cursorHeading]);
    if (state === this.cursorDrawn) return;
    this.cursorDrawn = state;
    const context = overlay.getContext("2d")!;
    context.clearRect(0, 0, overlay.width, overlay.height);
    if (!visible || !point) return;
    context.save();
    context.translate(Math.min(overlay.width - 1, point.x * overlay.width), Math.min(overlay.height - 1, point.y * overlay.height));
    context.scale(1 / scale, 1 / scale);
    // The tip stays on the operation coordinate while the body turns behind it.
    context.rotate(this.cursorHeading);
    // Round the filled outline itself. The tip radius must remain visible after
    // the centered white stroke; offset the rounded tip back to the input point.
    const tipRadius = 1.8;
    const tip = tipRadius * (Math.hypot(23, 9.5) / 9.5 - 1);
    context.beginPath();
    context.moveTo(tip - 10, -10 * 9.5 / 23);
    context.arcTo(tip - 23, -9.5, tip - 18.5, 0, 1.8);
    context.arcTo(tip - 18.5, 0, tip - 23, 9.5, 1.4);
    context.arcTo(tip - 23, 9.5, tip, 0, 1.8);
    context.arcTo(tip, 0, tip - 23, -9.5, tipRadius);
    context.closePath();
    // Match desktop WorkbenchStyle.accent and the viewer's --accent color.
    context.fillStyle = "#3564e3";
    context.strokeStyle = "#fff";
    context.lineWidth = 1.7;
    context.lineJoin = "round";
    context.shadowColor = "#3564e380";
    context.shadowBlur = 6.5 / scale;
    context.fill();
    context.stroke();
    context.restore();
  }
  private fail(error: Error) {
    if (this.failed || this.stopped) return;
    this.failed = true;
    this.stop();
    this.onError(error);
  }
  stop() {
    this.stopped = true;
    this.cursor = undefined;
    this.cursorMotion = undefined;
    this.reducedMotion?.removeEventListener("change", this.motionPreferenceChanged);
    this.resize?.disconnect();
    this.drawCursor();
    clearTimeout(this.firstFrame);
    clearTimeout(this.socketError);
    if (this.policyListener) document.removeEventListener("securitypolicyviolation", this.policyListener);
    this.socket?.close();
    if (this.decoder && this.decoder.state !== "closed") this.decoder.close();
    this.decoder = undefined;
    cancelAnimationFrame(this.drawing);
    this.drawing = 0;
    this.latest?.close();
    this.latest = undefined;
    this.queue = [];
    this.queuedBytes = 0;
  }
}
