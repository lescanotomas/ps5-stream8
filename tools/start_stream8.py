"""One-command Stream8 startup for a jailbroken PS5 on firmware 13.60."""

from __future__ import annotations

import argparse
import socket
import subprocess
import sys
import time
import os
import shutil
import urllib.request
import webbrowser
from pathlib import Path

from deploy import send_injection


LOADER_PORT = 9021
INJECTOR_PORT = 9041
STREAM_PORT = 9070
WEB_PORT = 9090


def port_open(host: str, port: int, timeout: float = 1.0) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def wait_for_port(host: str, port: int, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if port_open(host, port, timeout=0.75):
            return True
        time.sleep(0.25)
    return False


def upload_payload(host: str, port: int, payload: Path) -> None:
    data = payload.read_bytes()
    with socket.create_connection((host, port), timeout=10) as connection:
        connection.sendall(data)
        connection.shutdown(socket.SHUT_WR)


def probe_stream(host: str, port: int, timeout: float = 3.0) -> bool:
    request = f"GET / HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n"
    try:
        with socket.create_connection((host, port), timeout=timeout) as connection:
            connection.settimeout(timeout)
            connection.sendall(request.encode("ascii"))
            return connection.recv(16).startswith(b"HTTP/")
    except OSError:
        return False


def local_web_ready(port: int) -> bool:
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=2) as response:
            return response.status == 200
    except Exception:
        return False


def deploy_stream8(payload: Path, host: str, injector_port: int) -> None:
    if not payload.is_file():
        raise RuntimeError(f"Falta el archivo requerido: {payload}")

    print("[4/6] Omitiendo el preparador legado (Stream8 administra su proxy).")
    print("[5/6] Inyectando Stream8 audiovisual experimental...")
    send_injection(host, injector_port, payload)


def find_app_browser() -> str | None:
    """Locate Edge or Chrome so the viewer can open as a standalone window."""
    candidates = []
    for variable in ("ProgramFiles(x86)", "ProgramFiles", "LocalAppData"):
        base = os.environ.get(variable)
        if not base:
            continue
        candidates += [
            Path(base) / "Microsoft/Edge/Application/msedge.exe",
            Path(base) / "Google/Chrome/Application/chrome.exe",
        ]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    return shutil.which("msedge") or shutil.which("chrome")


def open_viewer(url: str, as_window: bool) -> None:
    browser = find_app_browser() if as_window else None
    if browser:
        # Use app mode for a separate tab-less window, but keep the user's
        # normal Edge/Chrome profile (no custom user-data directory).
        subprocess.Popen(
            [browser, "--new-window", f"--app={url}",
             "--window-size=1280,760", "--no-first-run"]
        )
    else:
        webbrowser.open(url)


def start_bridge(
    root: Path,
    host: str,
    stream_port: int,
    web_port: int,
    open_browser: bool,
    as_window: bool = True,
) -> int:
    url = f"http://localhost:{web_port}"
    if local_web_ready(web_port):
        print(f"[6/6] El bridge ya está activo en {url}")
        if open_browser:
            open_viewer(url, as_window)
        return 0

    bridge = root / "host" / "stream8_bridge.py"
    print(f"[6/6] Iniciando el bridge en {url}")
    process = subprocess.Popen(
        [
            sys.executable,
            str(bridge),
            "--ps5-host",
            host,
            "--ps5-port",
            str(stream_port),
            "--listen-port",
            str(web_port),
        ]
    )
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"El bridge terminó con código {process.returncode}")
        if local_web_ready(web_port):
            print(f"Stream8 listo: {url}")
            print("Dejá esta ventana abierta. Ctrl+C detiene solamente el bridge de la PC.")
            if open_browser:
                open_viewer(url, as_window)
            try:
                return process.wait()
            except KeyboardInterrupt:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                return 0
        time.sleep(0.25)

    process.terminate()
    raise RuntimeError("El bridge no respondió en localhost")


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(line_buffering=True)

    root = Path(__file__).resolve().parent.parent
    default_injector = root / "payloads" / "stream8-injector-dynamic-fw1360.elf"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ps5-host", required=True, help="Dirección IPv4 de la PS5")
    parser.add_argument("--injector", type=Path, default=default_injector)
    parser.add_argument("--loader-port", type=int, default=LOADER_PORT)
    parser.add_argument("--injector-port", type=int, default=INJECTOR_PORT)
    parser.add_argument(
        "--payload",
        type=Path,
        default=root / "payloads" / "stream8-v0.3.0-audio-1080p60-30mbps-fw1360.elf",
    )
    parser.add_argument("--stream-port", type=int, default=STREAM_PORT)
    parser.add_argument("--web-port", type=int, default=WEB_PORT)
    parser.add_argument("--no-browser", action="store_true")
    parser.add_argument("--tab", action="store_true", help="abrir en una pestaña normal en vez de una ventana")
    args = parser.parse_args()

    try:
        print(f"[1/6] Comprobando la PS5 {args.ps5_host}...")

        if port_open(args.ps5_host, args.stream_port):
            if not probe_stream(args.ps5_host, args.stream_port):
                raise RuntimeError(
                    "Stream8 está residente pero no responde. No se reinjectó para evitar "
                    "una instancia duplicada; reiniciá la PS5 y volvé a ejecutar este comando."
                )
            print("[2/6] Stream8 ya está activo; se omite la inyección.")
        else:
            if not port_open(args.ps5_host, args.injector_port):
                print(f"[2/6] Cargando el inyector dinámico por el puerto {args.loader_port}...")
                if not args.injector.is_file():
                    raise RuntimeError(f"No se encontró el inyector dinámico: {args.injector}")
                if not port_open(args.ps5_host, args.loader_port):
                    raise RuntimeError(
                        f"El loader {args.loader_port} no está abierto. Reabrí el payload loader "
                        "del jailbreak y ejecutá nuevamente este comando."
                    )
                upload_payload(args.ps5_host, args.loader_port, args.injector)
                if not wait_for_port(args.ps5_host, args.injector_port, 10):
                    raise RuntimeError(
                        f"El inyector no abrió el puerto {args.injector_port}. "
                        "No se envió Stream8."
                    )
            else:
                print(f"[2/6] El inyector ya está activo en el puerto {args.injector_port}.")

            print("[3/6] Buscando SceGameLiveStreaming dinámicamente...")
            deploy_stream8(args.payload, args.ps5_host, args.injector_port)
            if not wait_for_port(args.ps5_host, args.stream_port, 10):
                raise RuntimeError(f"Stream8 no abrió el puerto {args.stream_port}")
            if not probe_stream(args.ps5_host, args.stream_port):
                raise RuntimeError("Stream8 abrió el puerto de video pero no responde correctamente")

        return start_bridge(
            root,
            args.ps5_host,
            args.stream_port,
            args.web_port,
            not args.no_browser,
            not args.tab,
        )
    except (OSError, RuntimeError, TimeoutError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
