"""Deploy the Stream8 audiovisual experimental build through NineS."""

import argparse
import socket
from pathlib import Path

PROCESS_NAME = "SceGameLiveStreamin"


def injection_packet(process_name: str, elf_path: Path) -> bytes:
    encoded = process_name.encode("ascii")
    if len(encoded) >= 0x100:
        raise ValueError("process name is too long")
    return encoded.ljust(0x100, b"\0") + elf_path.read_bytes()


def send_injection(host: str, port: int, elf_path: Path,
                   process_name: str = PROCESS_NAME) -> None:
    with socket.create_connection((host, port), timeout=10) as connection:
        connection.sendall(injection_packet(process_name, elf_path))


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ps5-host", required=True)
    parser.add_argument("--injector-port", type=int, default=9041)
    parser.add_argument("--process", default=PROCESS_NAME,
                        help="target process name (default: GameLive)")
    parser.add_argument(
        "--payload",
        type=Path,
        default=root / "payloads/stream8-v0.3.0-audio-1080p60-30mbps-fw1360.elf",
    )
    args = parser.parse_args()

    if not args.payload.is_file():
        parser.error(f"missing ELF: {args.payload}")

    print("Sending Stream8 audiovisual experimental server...")
    send_injection(args.ps5_host, args.injector_port, args.payload,
                   args.process)
    print("Stream8 deployed. Start host/stream8_bridge.py and open http://localhost:9090")


if __name__ == "__main__":
    main()
