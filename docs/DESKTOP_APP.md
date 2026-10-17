# Stream8 desktop setup app

`stream8_setup.pyw` is a Windows control panel for the complete Stream8 startup
flow. It uses only Python's standard library.

## Start

Run from PowerShell:

```powershell
.\start-stream8-app.ps1
```

You can also double-click `stream8_setup.pyw` when `.pyw` is associated with
Python.

## Configuration

The panel accepts the PS5 IPv4 address, payload-loader port, NineS port,
Stream8 port, local viewer port, NineS ELF path and Stream8 ELF path. Values
are stored in `.runtime/desktop-config.json`. This directory is ignored by Git,
so local addresses and machine-specific paths are not published.

Default ports are:

| Service | Port |
| --- | ---: |
| PS5 ELF loader | 9021 |
| Dynamic NineS injector | 9041 |
| Stream8 media server | 9070 |
| Local browser viewer | 9090 |

## Controls

- **Comprobar PS5** performs read-only connection checks.
- **Iniciar Stream8** loads NineS only when needed, injects Stream8 only when a
  healthy instance is not already present, starts the local bridge and opens
  the viewer.
- **Abrir visor** opens the already-running viewer without injecting anything.
- **Detener bridge** stops only a PC bridge started by the app.

## Safety behavior

The app does not jailbreak the PS5. Apply the jailbreak and start the payload
loader first. It refuses to reinject if port 9070 is open but the Stream8 HTTP
probe fails, because a second resident instance could destabilize the system
process.

The app never kills `SceGameLiveStreaming`, never calls a global Avcap
terminate routine and never attempts to unload a PS5 payload. Reboot the console
before replacing an unhealthy or different resident ELF.
