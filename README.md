# Stream8

Stream8 is an open-source game-streaming payload for a jailbroken PlayStation 5.
It captures the active game through the console's hardware H.264/AAC capture
path and sends it over the local network to Edge or Chrome. It does not start a
Remote Play session or carry controller input: the DualSense stays connected
directly to the PS5.

## v0.4.2 at a glance

- Tested on PS5 firmware **13.60**.
- **1920×1080 at 60 FPS** using the console's hardware H.264 encoder.
- Approximately **30 Mbps** during motion, with live measured bitrate.
- **AAC-LC 48 kHz stereo** game audio, synchronized in the browser.
- Selectable 60/80/120/200/400 ms playback profiles.
- Short-burst protection and late-frame recovery for smoother playback.
- Live source FPS, displayed FPS, Mbps, buffer, queue, jitter and drop metrics.
- Windows setup app for settings, injection, bridge startup and the viewer.
- Dynamic `SceGameLiveStreaming` discovery; no fixed PID.
- Tested game switching with Red Dead Redemption 2 and Lego Batman.

The v0.4.2 hardware test at the 60 ms profile remained fluid with queue drops
and late drops at zero. Results depend on the game, network and receiving PC.

> [!WARNING]
> This release contains firmware-specific offsets and is for **firmware 13.60
> only**. Do not inject it on another firmware.

## Architecture

```text
Active PS5 game
       │ composited game video + audio
       ▼
SceGameLiveStreaming / libSceAvcap2
       │ hardware H.264 + AAC
       ▼
Stream8 payload, TCP :9070
       │ trusted local network
       ▼
PC bridge, HTTP :9090
       │ framed media packets
       ▼
Edge/Chrome WebCodecs → canvas + Web Audio
```

Stream8 runs inside `SceGameLiveStreaming`, not inside the game. It captures
the active game's output and leaves gameplay input local to the console.

## Requirements

- Jailbroken PS5 on firmware 13.60 with an ELF loader (normally port 9021).
- A firmware-compatible [NineS](https://github.com/buzzer-re/NineS) injector.
  The tested dynamic variant listens on port 9041 and includes the patch in
  [`patches/nines-fw1360-dynamic.patch`](patches/nines-fw1360-dynamic.patch).
- Windows PC with Python 3.
- Microsoft Edge or Google Chrome with WebCodecs.
- Wired Ethernet is strongly recommended.

NineS is a separate project and its binary is intentionally not distributed.
Place your compatible binary at:

```text
payloads/stream8-injector-dynamic-fw1360.elf
```

## Quick start: desktop app

1. Reboot the PS5 before replacing an older resident Stream8 build.
2. Apply the jailbreak and start the PS5 ELF loader.
3. Open a game and wait until gameplay is visible.
4. Put the compatible NineS ELF in `payloads` as described above.
5. On the PC, run:

   ```powershell
   .\start-stream8-app.ps1
   ```

6. Enter the PS5 address, confirm the ports and click **Iniciar Stream8**.
7. In the viewer, click **Activar audio** when prompted by the browser.

The app remembers machine-specific settings in `.runtime`, which Git ignores.
It does not perform the jailbreak and refuses an unsafe duplicate reinjection
when a resident instance appears unhealthy.

See [`docs/DESKTOP_APP.md`](docs/DESKTOP_APP.md) for all controls.

## Command-line start

With the injector binary in `payloads`, replace the example address with the
PS5 address:

```powershell
.\start-stream8.ps1 -Ps5Ip 192.168.1.50
```

The launcher loads NineS when needed, deploys Stream8, verifies port 9070,
starts the localhost bridge and opens a normal Edge/Chrome app window. Keep the
terminal open while streaming; `Ctrl+C` stops the PC bridge, not the PS5
payload.

## Changing games

1. Leave the viewer open while closing the old game.
2. Start the replacement game and wait until it is fully running.
3. Click **Reconectar video** in the viewer.

Stream8 v0.4.2 captures games, not the PS5 system menu. Reconnecting during the
menu or transition can leave Sony's title-bound capture source waiting, so
reconnect only after the new game is ready.

## Building the payload

Install the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk), set
`PS5_PAYLOAD_SDK`, then run:

```sh
make
```

The validated build is written to:

```text
payloads/stream8-v0.3.0-audio-1080p60-30mbps-fw1360.elf
```

Release ELF SHA-256:

```text
3B059DDA9333B7F615D7AC9138D74A2563EA617E1F2872A19A6A984AEBC458ED
```

## Safety and limitations

- Firmware 13.60 only. A reboot is the supported recovery and update path.
- Do not kill `SceGameLiveStreaming`; doing so can cause PS5 system software
  error `CE-108262-9`.
- Do not inject a second Stream8 instance over an unhealthy resident instance.
- No PS5 system-menu capture.
- One viewer connection is recommended.
- No authentication or encryption. Use only on a trusted LAN and never expose
  ports 9041 or 9070 to the internet.
- The 30 Mbps setting is a target/ceiling; measured bitrate varies by scene.
- Homebrew software is experimental and cannot be guaranteed risk-free.

## Documentation

- [`docs/RELEASE_V0.4.2.md`](docs/RELEASE_V0.4.2.md) — release notes.
- [`docs/DESKTOP_APP.md`](docs/DESKTOP_APP.md) — setup app usage.
- [`docs/DYNAMIC_INJECTOR.md`](docs/DYNAMIC_INJECTOR.md) — dynamic injector.
- [`docs/STREAM_FORMAT.md`](docs/STREAM_FORMAT.md) — media and viewer behavior.

## License

MIT. NineS and the PS5 Payload SDK are separate projects with their own
licenses.
