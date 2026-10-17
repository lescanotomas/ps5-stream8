# Stream and viewer behavior

Stream8 v0.4.2 transports framed H.264 video and AAC audio from the PS5 over a
TCP connection on port 9070. The local Python bridge exposes the stream to a
browser on `localhost:9090`; it does not transcode the media.

## Video

- Codec: H.264/AVC, Annex B from the PS5 capture service.
- Validated mode: 1920×1080, 60 FPS.
- Rate control: 30 Mbps runtime target with matching native buffer update.
- Decode/display: WebCodecs `VideoDecoder` and a canvas.

The displayed bitrate is measured from received video bytes over a rolling
window. It is expected to vary with scene complexity rather than remain fixed
at exactly 30 Mbps.

## Audio

- Codec: AAC-LC.
- Sample rate: 48 kHz.
- Channels: stereo.
- Playback: browser `AudioContext` after the user clicks **Activar audio**.

The viewer derives a shared media timeline and schedules audio against the
video playout target. A reconnect resets both sides of that timeline.

## Latency profiles

The viewer offers 60, 80, 120, 200 and 400 ms targets. Lower settings reduce
delay but leave less room for network and decoder jitter. v0.4.2 allows a short
burst to settle for up to 300 ms before enforcing the decoded-frame queue cap,
while frames that are already too late are discarded immediately.

The on-screen telemetry reports source and displayed FPS, measured Mbps,
buffer duration, queue depth, jitter, current burst duration and drop counts.
