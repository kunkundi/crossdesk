import type { FrameInfo } from "./video-player.js";

type Capture = (args: { generation: number; after_frame_id?: number }) => Promise<Record<string, any>>;

// A bounded, app-only fallback for hosts that cannot reach the native video
// socket. Decode bytes directly; no fetch, data URL, or extra network permission.
export class PreviewPlayer {
  private stopped = false;
  private timer?: ReturnType<typeof setTimeout>;
  private frameId?: number;
  private count = 0;
  private presentedAt = 0;
  constructor(private canvas: HTMLCanvasElement, private generation: number,
    private capture: Capture, private onFrame: (info: FrameInfo) => void,
    private onError: (error: Error) => void) {}

  start() { void this.poll(); }
  private async poll() {
    if (this.stopped) return;
    const started = performance.now();
    try {
      const result = await this.capture({ generation: this.generation,
        ...(this.frameId === undefined ? {} : { after_frame_id: this.frameId }) });
      if (this.stopped) return;
      if (result.unchanged) return;
      const meta = result.metadata;
      if (result.mime_type !== "image/png" || typeof result.data !== "string" ||
          result.data.length > 349528 || !meta || meta.generation !== this.generation ||
          !Number.isSafeInteger(meta.frame_id) || meta.frame_id < 0 ||
          !Number.isFinite(meta.frame_age_ms) || meta.frame_age_ms < 0 ||
          ![meta.source_size?.width, meta.source_size?.height].every(
            (n) => Number.isInteger(n) && n > 0 && n <= 8192))
        throw new Error("VIDEO_INVALID");
      if (meta.frame_age_ms > 2000) throw new Error("FRAME_TIMEOUT");
      if (this.frameId !== undefined && meta.frame_id <= this.frameId) return;
      const bytes = Uint8Array.from(atob(result.data), (c) => c.charCodeAt(0));
      const frame = await createImageBitmap(new Blob([bytes], { type: "image/png" }));
      try {
        if (this.stopped) return;
        if (!frame.width || !frame.height || frame.width > 960 || frame.height > 960)
          throw new Error("VIDEO_INVALID");
        this.canvas.width = frame.width;
        this.canvas.height = frame.height;
        this.canvas.getContext("2d", { alpha: false })!.drawImage(frame, 0, 0);
        this.frameId = meta.frame_id;
        this.canvas.dataset.frames = String(++this.count);
        const now = performance.now();
        const fps = this.presentedAt ? 1000 / (now - this.presentedAt) : 0;
        this.presentedAt = now;
        this.onFrame({ width: frame.width, height: frame.height, generation: meta.generation,
          sourceSize: meta.source_size, fps, frames: this.count });
      } finally { frame.close(); }
    } catch (error) {
      if (this.stopped) return;
      if (!(error instanceof Error) || !["BUSY", "FRAME_TIMEOUT"].includes(error.message)) {
        this.stop();
        this.onError(error instanceof Error ? error : new Error("VIDEO_UNAVAILABLE"));
      }
    } finally {
      if (!this.stopped)
        this.timer = setTimeout(() => void this.poll(), Math.max(0, 1000 - (performance.now() - started)));
    }
  }
  stop() {
    this.stopped = true;
    clearTimeout(this.timer);
  }
}
