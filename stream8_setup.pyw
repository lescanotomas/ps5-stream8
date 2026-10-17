"""Stream8 desktop setup and launcher for Windows."""

from __future__ import annotations

import ipaddress
import json
import queue
import subprocess
import sys
import threading
import time
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk


ROOT = Path(__file__).resolve().parent
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
APP_VERSION = "0.4.2"

from start_stream8 import (  # noqa: E402
    local_web_ready,
    open_viewer,
    port_open,
    probe_stream,
    send_injection,
    upload_payload,
    wait_for_port,
)


CONFIG_PATH = ROOT / ".runtime" / "desktop-config.json"
DEFAULTS = {
    "ps5_host": "192.168.1.50",
    "loader_port": "9021",
    "injector_port": "9041",
    "stream_port": "9070",
    "web_port": "9090",
    "injector": str(ROOT / "payloads" / "stream8-injector-dynamic-fw1360.elf"),
    "payload": str(
        ROOT / "payloads" / "stream8-v0.3.0-audio-1080p60-30mbps-fw1360.elf"
    ),
}


class Stream8Setup(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(f"Stream8 Setup v{APP_VERSION}")
        self.geometry("820x680")
        self.minsize(760, 620)
        self.protocol("WM_DELETE_WINDOW", self.close_app)

        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.bridge_process: subprocess.Popen[str] | None = None
        self.busy = False
        self.values = self.load_config()
        self.vars = {name: tk.StringVar(value=value) for name, value in self.values.items()}
        self.status_vars = {
            "loader": tk.StringVar(value="Sin comprobar"),
            "injector": tk.StringVar(value="Sin comprobar"),
            "stream": tk.StringVar(value="Sin comprobar"),
            "viewer": tk.StringVar(value="Sin comprobar"),
        }

        self.build_ui()
        self.after(100, self.process_events)

    def load_config(self) -> dict[str, str]:
        config = DEFAULTS.copy()
        try:
            saved = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
            config.update({key: str(value) for key, value in saved.items() if key in config})
            # Transparently migrate the pre-v0.5 locations after the active
            # ELFs were centralized under the repository-level payloads folder.
            for key in ("injector", "payload"):
                if not Path(config[key]).is_file() and Path(DEFAULTS[key]).is_file():
                    config[key] = DEFAULTS[key]
        except (OSError, ValueError, TypeError):
            pass
        return config

    def save_config(self, config: dict[str, object]) -> None:
        CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
        serializable = {key: str(value) for key, value in config.items()}
        CONFIG_PATH.write_text(
            json.dumps(serializable, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

    def build_ui(self) -> None:
        style = ttk.Style(self)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure("Title.TLabel", font=("Segoe UI", 20, "bold"))
        style.configure("Subtitle.TLabel", font=("Segoe UI", 10))
        style.configure("Status.TLabel", font=("Segoe UI", 9, "bold"))

        outer = ttk.Frame(self, padding=22)
        outer.pack(fill="both", expand=True)
        ttk.Label(outer, text=f"Stream8 Setup v{APP_VERSION}", style="Title.TLabel").pack(anchor="w")
        ttk.Label(
            outer,
            text="Configurá, verificá e iniciá la transmisión de tu PS5 desde un solo lugar.",
            style="Subtitle.TLabel",
        ).pack(anchor="w", pady=(0, 16))

        connection = ttk.LabelFrame(outer, text="Conexión", padding=14)
        connection.pack(fill="x")
        self.add_entry(connection, 0, "IP de la PS5", "ps5_host", width=24)
        ports = ttk.Frame(connection)
        ports.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(10, 0))
        for column, (label, key) in enumerate(
            (
                ("Loader", "loader_port"),
                ("NineS", "injector_port"),
                ("Stream8", "stream_port"),
                ("Visor local", "web_port"),
            )
        ):
            ttk.Label(ports, text=label).grid(row=0, column=column, padx=(0, 8), sticky="w")
            ttk.Entry(ports, textvariable=self.vars[key], width=11).grid(
                row=1, column=column, padx=(0, 18), sticky="w"
            )

        files = ttk.LabelFrame(outer, text="Payloads", padding=14)
        files.pack(fill="x", pady=(12, 0))
        self.add_file_picker(files, 0, "Inyector NineS", "injector")
        self.add_file_picker(files, 1, "Payload Stream8", "payload")

        services = ttk.LabelFrame(outer, text="Estado", padding=14)
        services.pack(fill="x", pady=(12, 0))
        for column, (label, key) in enumerate(
            (
                ("Loader", "loader"),
                ("NineS", "injector"),
                ("Stream8", "stream"),
                ("Visor", "viewer"),
            )
        ):
            box = ttk.Frame(services)
            box.grid(row=0, column=column, sticky="w", padx=(0, 35))
            ttk.Label(box, text=label).pack(anchor="w")
            ttk.Label(box, textvariable=self.status_vars[key], style="Status.TLabel").pack(
                anchor="w"
            )

        actions = ttk.Frame(outer)
        actions.pack(fill="x", pady=14)
        self.check_button = ttk.Button(actions, text="Comprobar PS5", command=self.check)
        self.check_button.pack(side="left")
        self.start_button = ttk.Button(actions, text="Iniciar Stream8", command=self.start)
        self.start_button.pack(side="left", padx=8)
        ttk.Button(actions, text="Abrir visor", command=self.open_viewer_now).pack(side="left")
        ttk.Button(actions, text="Detener bridge", command=self.stop_bridge).pack(
            side="left", padx=8
        )

        log_frame = ttk.LabelFrame(outer, text="Actividad", padding=8)
        log_frame.pack(fill="both", expand=True)
        self.log_text = tk.Text(
            log_frame,
            height=12,
            wrap="word",
            state="disabled",
            font=("Consolas", 9),
            background="#111827",
            foreground="#e5e7eb",
            insertbackground="#e5e7eb",
        )
        scrollbar = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scrollbar.set)
        self.log_text.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        ttk.Label(
            outer,
            text=(
                "Firmware 13.60 · La app no realiza el jailbreak · Reiniciá la PS5 antes "
                "de reemplazar un payload residente."
            ),
        ).pack(anchor="w", pady=(10, 0))

    def add_entry(self, parent: ttk.Widget, row: int, label: str, key: str, width: int) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w")
        ttk.Entry(parent, textvariable=self.vars[key], width=width).grid(
            row=row, column=1, sticky="w", padx=(12, 0)
        )

    def add_file_picker(self, parent: ttk.Widget, row: int, label: str, key: str) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=5)
        ttk.Entry(parent, textvariable=self.vars[key]).grid(
            row=row, column=1, sticky="ew", padx=12, pady=5
        )
        ttk.Button(parent, text="Elegir…", command=lambda: self.choose_file(key)).grid(
            row=row, column=2, pady=5
        )
        parent.columnconfigure(1, weight=1)

    def choose_file(self, key: str) -> None:
        selected = filedialog.askopenfilename(
            title="Elegir ELF", filetypes=(("Payload ELF", "*.elf"), ("Todos", "*.*"))
        )
        if selected:
            self.vars[key].set(selected)

    def get_config(self, require_files: bool = False) -> dict[str, object]:
        host = self.vars["ps5_host"].get().strip()
        try:
            address = ipaddress.ip_address(host)
            if address.version != 4:
                raise ValueError
        except ValueError as exc:
            raise ValueError("Ingresá una dirección IPv4 válida para la PS5.") from exc

        config: dict[str, object] = {"ps5_host": host}
        for key in ("loader_port", "injector_port", "stream_port", "web_port"):
            try:
                port = int(self.vars[key].get().strip())
            except ValueError as exc:
                raise ValueError(f"El valor de {key} no es un puerto válido.") from exc
            if not 1 <= port <= 65535:
                raise ValueError(f"El puerto {key} debe estar entre 1 y 65535.")
            config[key] = port

        for key in ("injector", "payload"):
            path = Path(self.vars[key].get().strip()).expanduser()
            config[key] = path
            if require_files and not path.is_file():
                raise ValueError(f"No se encontró el archivo de {key}: {path}")
        return config

    def set_busy(self, value: bool) -> None:
        self.busy = value
        state = "disabled" if value else "normal"
        self.check_button.configure(state=state)
        self.start_button.configure(state=state)

    def log(self, message: str) -> None:
        self.events.put(("log", message))

    def launch_task(self, target, require_files: bool = False) -> None:
        if self.busy:
            return
        try:
            config = self.get_config(require_files=require_files)
            self.save_config(config)
        except ValueError as error:
            messagebox.showerror("Configuración inválida", str(error), parent=self)
            return
        self.set_busy(True)
        threading.Thread(target=target, args=(config,), daemon=True).start()

    def check(self) -> None:
        self.launch_task(self.check_worker)

    def check_worker(self, config: dict[str, object]) -> None:
        try:
            host = str(config["ps5_host"])
            states = {
                "loader": port_open(host, int(config["loader_port"])),
                "injector": port_open(host, int(config["injector_port"])),
                "stream": port_open(host, int(config["stream_port"])),
                "viewer": local_web_ready(int(config["web_port"])),
            }
            self.events.put(("states", states))
            self.log(
                "Comprobación terminada: "
                + ", ".join(f"{name}={'activo' if active else 'cerrado'}" for name, active in states.items())
            )
        finally:
            self.events.put(("busy", False))

    def start(self) -> None:
        self.launch_task(self.start_worker, require_files=True)

    def start_worker(self, config: dict[str, object]) -> None:
        try:
            host = str(config["ps5_host"])
            loader_port = int(config["loader_port"])
            injector_port = int(config["injector_port"])
            stream_port = int(config["stream_port"])
            web_port = int(config["web_port"])
            injector = Path(config["injector"])
            payload = Path(config["payload"])

            self.log(f"Comprobando PS5 en {host}…")
            if port_open(host, stream_port):
                if not probe_stream(host, stream_port):
                    raise RuntimeError(
                        "Stream8 parece residente pero no responde correctamente. No se "
                        "reinyectó para evitar una instancia duplicada. Reiniciá la PS5."
                    )
                self.log("Stream8 ya está activo; no se vuelve a inyectar.")
            else:
                if not port_open(host, injector_port):
                    if not port_open(host, loader_port):
                        raise RuntimeError(
                            f"El loader del jailbreak no responde en el puerto {loader_port}."
                        )
                    self.log(f"Cargando NineS por el puerto {loader_port}…")
                    upload_payload(host, loader_port, injector)
                    if not wait_for_port(host, injector_port, 10):
                        raise RuntimeError(
                            f"NineS no abrió el puerto {injector_port}; Stream8 no fue enviado."
                        )
                else:
                    self.log("NineS ya está activo.")

                self.log("Inyectando Stream8 en SceGameLiveStreaming…")
                send_injection(host, injector_port, payload)
                if not wait_for_port(host, stream_port, 10):
                    raise RuntimeError(f"Stream8 no abrió el puerto {stream_port}.")
                if not probe_stream(host, stream_port):
                    raise RuntimeError("Stream8 abrió el puerto pero no responde correctamente.")
                self.log("Payload inyectado correctamente.")

            self.ensure_bridge(host, stream_port, web_port)
            self.log(f"Todo listo: http://localhost:{web_port}")
            self.events.put(("open_viewer", web_port))
            self.events.put(("refresh", config))
        except (OSError, RuntimeError, TimeoutError) as error:
            self.log(f"ERROR: {error}")
            self.events.put(("error", str(error)))
        finally:
            self.events.put(("busy", False))

    def ensure_bridge(self, host: str, stream_port: int, web_port: int) -> None:
        if local_web_ready(web_port):
            self.log("El bridge local ya está activo.")
            return
        bridge = ROOT / "host" / "stream8_bridge.py"
        self.log(f"Iniciando bridge local en el puerto {web_port}…")
        creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        self.bridge_process = subprocess.Popen(
            [
                sys.executable,
                str(bridge),
                "--ps5-host",
                host,
                "--ps5-port",
                str(stream_port),
                "--listen-port",
                str(web_port),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            creationflags=creation_flags,
        )
        threading.Thread(target=self.read_bridge_output, daemon=True).start()
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if self.bridge_process.poll() is not None:
                raise RuntimeError(
                    f"El bridge terminó con código {self.bridge_process.returncode}."
                )
            if local_web_ready(web_port):
                return
            time.sleep(0.25)
        self.stop_bridge_process()
        raise RuntimeError("El bridge no respondió en localhost.")

    def read_bridge_output(self) -> None:
        process = self.bridge_process
        if process is None or process.stdout is None:
            return
        for line in process.stdout:
            self.log(f"Bridge: {line.rstrip()}")

    def open_viewer_now(self) -> None:
        try:
            config = self.get_config()
        except ValueError as error:
            messagebox.showerror("Configuración inválida", str(error), parent=self)
            return
        web_port = int(config["web_port"])
        if not local_web_ready(web_port):
            messagebox.showwarning(
                "Visor no disponible",
                "El bridge local todavía no está activo. Usá Iniciar Stream8 primero.",
                parent=self,
            )
            return
        open_viewer(f"http://localhost:{web_port}", True)

    def stop_bridge_process(self) -> bool:
        process = self.bridge_process
        if process is None or process.poll() is not None:
            self.bridge_process = None
            return False
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
        self.bridge_process = None
        return True

    def stop_bridge(self) -> None:
        if self.stop_bridge_process():
            self.log("Bridge iniciado por esta app detenido. El payload de la PS5 sigue activo.")
            self.status_vars["viewer"].set("Cerrado")
        else:
            messagebox.showinfo(
                "Bridge",
                "Esta instancia de la app no tiene un bridge propio en ejecución.",
                parent=self,
            )

    def process_events(self) -> None:
        try:
            while True:
                kind, data = self.events.get_nowait()
                if kind == "log":
                    self.log_text.configure(state="normal")
                    self.log_text.insert("end", f"{data}\n")
                    self.log_text.see("end")
                    self.log_text.configure(state="disabled")
                elif kind == "busy":
                    self.set_busy(bool(data))
                elif kind == "states":
                    for key, active in dict(data).items():
                        self.status_vars[key].set("Activo" if active else "Cerrado")
                elif kind == "error":
                    messagebox.showerror("Stream8", str(data), parent=self)
                elif kind == "open_viewer":
                    open_viewer(f"http://localhost:{int(data)}", True)
                elif kind == "refresh":
                    threading.Thread(target=self.check_worker, args=(dict(data),), daemon=True).start()
        except queue.Empty:
            pass
        self.after(100, self.process_events)

    def close_app(self) -> None:
        if self.bridge_process is not None and self.bridge_process.poll() is None:
            stop = messagebox.askyesno(
                "Cerrar Stream8 Setup",
                "¿También querés detener el bridge de la PC? El payload de la PS5 no se detendrá.",
                parent=self,
            )
            if stop:
                self.stop_bridge_process()
        self.destroy()


if __name__ == "__main__":
    Stream8Setup().mainloop()
