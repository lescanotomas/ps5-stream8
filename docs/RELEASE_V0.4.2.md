# Stream8 v0.4.2

Released on 2026-10-17 for PS5 firmware 13.60.

## Highlights

- 1920×1080 at 60 FPS through the PS5 hardware H.264 encoder.
- Runtime bitrate target of 30 Mbps, with live measured Mbps in the viewer.
- Synchronized AAC-LC 48 kHz stereo game audio.
- Windows setup app and one-command launcher.
- Dynamic `SceGameLiveStreaming` PID discovery.
- Selectable 60/80/120/200/400 ms playback profiles.
- A 300 ms grace window for short decoder/network bursts.
- Immediate recovery for frames that are genuinely late.
- Source FPS, displayed FPS, buffer, queue, jitter, burst and drop telemetry.

## Tested result

The 60 ms profile remained fluid during the final hardware test, with queue
drops and late drops at zero. Game switching was tested between Red Dead
Redemption 2 and Lego Batman by waiting for the replacement title to be fully
running and then using **Reconectar video**.

## Payload

```text
payloads/stream8-v0.3.0-audio-1080p60-30mbps-fw1360.elf
SHA-256 3B059DDA9333B7F615D7AC9138D74A2563EA617E1F2872A19A6A984AEBC458ED
```

The payload itself keeps its v0.3.0 media-format filename because v0.4.x
changes are in the PC setup application and browser playout layer.

## Upgrade note

Reboot the PS5 before replacing a resident payload. The desktop app can reuse
an already healthy matching payload, but it intentionally refuses to inject a
second instance over an unhealthy one.
