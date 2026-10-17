"""Localhost bridge for browsers that require a secure context for WebCodecs."""

import argparse
from http.client import HTTPConnection
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

PS5_HOST = ""
PS5_PORT = 9070
LOCAL_HOST = "127.0.0.1"
LOCAL_PORT = 9090


class Bridge(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.path == "/favicon.ico":
            self.send_response(204)
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            return
        if self.path in {"/", "/index.html"}:
            page = Path(__file__).with_name("viewer.html").read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(page)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(page)
            self.close_connection = True
            return
        if self.path not in {"/stream", "/stream-av"}:
            self.send_error(404)
            return
        upstream = HTTPConnection(PS5_HOST, PS5_PORT, timeout=10)
        headers_sent = False
        try:
            upstream.request("GET", self.path, headers={"Connection": "close"})
            response = upstream.getresponse()
            self.send_response(response.status)
            for name, value in response.getheaders():
                if name.lower() not in {"connection", "transfer-encoding"}:
                    self.send_header(name, value)
            self.send_header("Connection", "close")
            self.end_headers()
            headers_sent = True
            if upstream.sock is not None:
                # Game transitions can temporarily stop encoded frames while
                # Stream8 safely reopens its capture handle.
                upstream.sock.settimeout(None)
            while True:
                # read() waits until the whole requested block is full. At the
                # encoder's current bitrate that introduced ~2 seconds of
                # artificial latency. read1() forwards whatever is available.
                chunk = response.read1(64 * 1024)
                if not chunk:
                    break
                self.wfile.write(chunk)
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as exc:
            if not headers_sent and not self.wfile.closed:
                try:
                    self.send_error(502, f"PS5 bridge error: {exc}")
                except (BrokenPipeError, ConnectionResetError):
                    pass
        finally:
            upstream.close()
            self.close_connection = True

    def log_message(self, _format, *_args):
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Stream8 PS5 localhost bridge")
    parser.add_argument("--ps5-host", required=True, help="PS5 IPv4 address")
    parser.add_argument("--ps5-port", type=int, default=PS5_PORT)
    parser.add_argument("--listen-port", type=int, default=LOCAL_PORT)
    args = parser.parse_args()
    PS5_HOST = args.ps5_host
    PS5_PORT = args.ps5_port
    LOCAL_PORT = args.listen_port
    server = ThreadingHTTPServer((LOCAL_HOST, LOCAL_PORT), Bridge)
    print(f"Stream8 bridge ready at http://localhost:{LOCAL_PORT}", flush=True)
    server.serve_forever()
