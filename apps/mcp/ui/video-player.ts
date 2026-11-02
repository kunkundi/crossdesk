type FrameInfo = { width: number; height: number; fps: number; frames: number };
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
  constructor(private canvas: HTMLCanvasElement,
    private onFrame: (info: FrameInfo) => void,
    private onError: (error: Error) => void) {}

  start(url: string, ticket: string, protocol = "h264-annexb/1", decoder = "auto") {
    if (!["h264-annexb/1", "nv12/1"].includes(protocol) ||
        !["auto", "software"].includes(decoder)) throw new Error("VIDEO_INVALID");
    this.raw = protocol === "nv12/1";
    this.automatic = decoder === "auto";
    this.software = !this.automatic;
    if (typeof VideoFrame === "undefined" || (!this.raw && typeof VideoDecoder === "undefined"))
      throw new Error("VIDEO_UNSUPPORTED");
    if (!/^ws:\/\/127\.0\.0\.1:\d+\/api\/video$/.test(url)) throw new Error("VIDEO_INVALID");
    const socket = this.socket = new WebSocket(url);
    socket.binaryType = "arraybuffer";
    socket.onopen = () => socket.send(JSON.stringify({ ticket }));
    socket.onerror = () => this.fail(new Error("VIDEO_UNAVAILABLE"));
    socket.onclose = () => { if (!this.stopped) this.fail(new Error("VIDEO_UNAVAILABLE")); };
    this.firstFrame = setTimeout(() => this.fail(new Error("VIDEO_UNAVAILABLE")), 10000);
    socket.onmessage = (event) => {
      if (this.stopped || !(event.data instanceof ArrayBuffer)) return;
      const data = event.data;
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
        this.canvas.width = width;
        this.canvas.height = height;
      }
      this.canvas.getContext("2d", { alpha: false })!.drawImage(frame, 0, 0);
      clearTimeout(this.firstFrame);
      this.count++;
      this.samples++;
      const elapsed = performance.now() - this.sampledAt;
      if (elapsed >= 1000) {
        this.fps = this.samples * 1000 / elapsed;
        this.samples = 0;
        this.sampledAt = performance.now();
      }
      this.canvas.dataset.frames = String(this.count);
      this.onFrame({ width, height, frames: this.count, fps: this.fps });
    } catch { this.fail(new Error("VIDEO_INVALID")); }
    finally { frame.close(); }
  }
  private fail(error: Error) {
    if (this.failed || this.stopped) return;
    this.failed = true;
    this.stop();
    this.onError(error);
  }
  stop() {
    this.stopped = true;
    clearTimeout(this.firstFrame);
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
