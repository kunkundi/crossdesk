package cn.crossdesk.mobile;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import java.util.concurrent.ArrayBlockingQueue;

/** Bounded audio queue: network callbacks never block on the speaker. */
final class AudioPlayer implements AutoCloseable {
    private final ArrayBlockingQueue<byte[]> queue = new ArrayBlockingQueue<>(20);
    private volatile boolean closed;
    private volatile boolean enabled = true;
    private final Thread thread = new Thread(this::play, "CrossDesk audio");
    AudioPlayer() { thread.start(); }
    void offer(byte[] pcm) {
        if (!closed && enabled && !queue.offer(pcm)) { queue.poll(); queue.offer(pcm); }
    }
    void setEnabled(boolean value) { enabled = value; queue.clear(); }
    private void play() {
        AudioTrack track = null;
        try {
            int minimum = AudioTrack.getMinBufferSize(48000, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
            track = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE).build())
                    .setAudioFormat(new AudioFormat.Builder().setSampleRate(48000)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                    .setBufferSizeInBytes(Math.max(minimum, 9600)).setTransferMode(AudioTrack.MODE_STREAM).build();
            track.play();
            while (!closed) {
                byte[] data = queue.take();
                if (!enabled) continue;
                int offset = 0;
                while (!closed && enabled && offset < data.length) {
                    int written = track.write(data, offset, data.length - offset, AudioTrack.WRITE_BLOCKING);
                    if (written <= 0) break;
                    offset += written;
                }
            }
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        } catch (IllegalArgumentException | IllegalStateException ignored) {
            // Devices with unavailable output can still operate as a silent controller.
        } finally {
            if (track != null) { track.release(); }
        }
    }
    @Override public void close() { closed = true; queue.clear(); thread.interrupt(); }
}
