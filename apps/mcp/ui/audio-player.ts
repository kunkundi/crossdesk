export class AudioPlayer {
  private context?: AudioContext;
  private gain?: GainNode;
  private next = 0;
  private sequence = -1;
  private generation = -1;
  private sources = new Set<AudioBufferSourceNode>();
  enabled = false;
  async enable(enabled: boolean) {
    if (enabled) {
      this.context ??= new AudioContext();
      if (!this.gain) {
        this.gain = this.context.createGain();
        this.gain.connect(this.context.destination);
      }
      await this.context.resume();
    }
    this.enabled = enabled;
    if (!enabled) this.flush();
  }
  volume(value: number) { if (this.gain) this.gain.gain.value = Math.max(0, Math.min(1, value)); }
  flush() {
    for (const source of this.sources) { source.stop(); source.disconnect(); }
    this.sources.clear();
    this.next = 0;
    this.sequence = this.generation = -1;
  }
  accept(bytes: ArrayBuffer) {
    if (bytes.byteLength < 22 || bytes.byteLength > 11540 || (bytes.byteLength & 1)) throw new Error("AUDIO_INVALID");
    const data = new DataView(bytes);
    if (data.getUint32(0) !== 0x43444131 || data.getUint32(12) !== 48000 ||
        data.getUint16(16) !== 1 || data.getUint16(18) !== 16) throw new Error("AUDIO_INVALID");
    if (!this.enabled || !this.context || !this.gain || this.context.state !== "running") return;
    const generation = data.getUint32(4), sequence = data.getUint32(8);
    if (generation < this.generation || (generation === this.generation && sequence <= this.sequence)) return;
    const now = this.context.currentTime;
    if (generation !== this.generation || this.next > now + .25 || sequence !== this.sequence + 1) this.flush();
    this.generation = generation;
    this.sequence = sequence;
    const buffer = this.context.createBuffer(1, (bytes.byteLength - 20) / 2, 48000);
    const samples = buffer.getChannelData(0);
    for (let index = 0; index < samples.length; index++) samples[index] = data.getInt16(20 + index * 2, true) / 32768;
    const source = this.context.createBufferSource();
    source.buffer = buffer;
    source.connect(this.gain);
    source.onended = () => { source.disconnect(); this.sources.delete(source); };
    this.sources.add(source);
    this.next = Math.max(now + .03, this.next);
    source.start(this.next);
    this.next += buffer.duration;
  }
}
