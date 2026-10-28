#!/usr/bin/env python3
"""
Kickray audio relay – stream the simulator's sound to the browser (headless mode).

    simulator --JACK--> ffmpeg (raw PCM) --> this relay --WebSocket--> browser (index.html)

The simulator plays into a JACK server without hardware (jackd -d dummy). This script
  * runs ffmpeg as a JACK client that captures 2 channels as 48 kHz s16le PCM,
  * keeps the simulator's outputs (Kickray:out_1/2) connected to ffmpeg's inputs,
    also after the simulator was restarted,
  * serves the PCM as binary WebSocket messages on --port to every connected browser.

Only the Python standard library is used. Slow clients lose old audio instead of
building up latency.

SPDX-License-Identifier: AGPL-3.0-or-later
"""
import argparse
import base64
import collections
import hashlib
import json
import os
import signal
import socket
import socketserver
import subprocess
import sys
import threading
import time

RATE = 48000
CHANNELS = 2
CHUNK = RATE // 50 * CHANNELS * 2          # 20 ms of s16le stereo
MAX_QUEUED = 10                            # per client: at most 200 ms waiting to be sent
CAPTURE_CLIENT = "kickray_stream"
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

clients = set()
clients_lock = threading.Lock()


def log(*args):
    print("[Kickray audio]", *args, file=sys.stderr, flush=True)


# --- capture ---------------------------------------------------------------------
def run_ffmpeg():
    """Capture from JACK forever; restart ffmpeg if it stops (e.g. jackd restarted)."""
    while True:
        cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-fflags", "nobuffer",
               "-f", "jack", "-channels", str(CHANNELS), "-i", CAPTURE_CLIENT,
               "-ac", str(CHANNELS), "-ar", str(RATE), "-f", "s16le", "-flush_packets", "1", "pipe:1"]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stdin=subprocess.DEVNULL)
        run_ffmpeg.proc = proc
        log("capturing from JACK as", CAPTURE_CLIENT)
        while True:
            data = proc.stdout.read(CHUNK)
            if not data:
                break
            with clients_lock:
                for q, ready in clients:
                    if len(q) >= MAX_QUEUED:
                        q.popleft()            # drop the oldest chunk: keep latency low
                    q.append(data)
                    ready.set()
        proc.wait()
        log("ffmpeg stopped (exit %s), restarting in 2 s" % proc.returncode)
        time.sleep(2)


run_ffmpeg.proc = None


def keep_connected(source):
    """Connect <source>:out_1/2 to the capture client whenever both exist."""
    while True:
        try:
            ports = subprocess.run(["jack_lsp"], capture_output=True, text=True, timeout=5).stdout.split("\n")
            for ch in (1, 2):
                src, dst = "%s:out_%d" % (source, ch), "%s:input_%d" % (CAPTURE_CLIENT, ch)
                if src in ports and dst in ports:
                    connected = subprocess.run(["jack_lsp", "-c", src], capture_output=True, text=True, timeout=5).stdout
                    if dst not in connected:
                        subprocess.run(["jack_connect", src, dst], capture_output=True, timeout=5)
                        log("connected", src, "->", dst)
        except (OSError, subprocess.SubprocessError) as e:
            log("jack tools:", e)
        time.sleep(2)


# --- WebSocket server ----------------------------------------------------------------
def ws_frame(payload, opcode):
    n = len(payload)
    if n < 126:
        header = bytes([0x80 | opcode, n])
    elif n < 65536:
        header = bytes([0x80 | opcode, 126]) + n.to_bytes(2, "big")
    else:
        header = bytes([0x80 | opcode, 127]) + n.to_bytes(8, "big")
    return header + payload


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        sock = self.request
        sock.settimeout(5)
        request = b""
        while b"\r\n\r\n" not in request and len(request) < 8192:
            part = sock.recv(1024)
            if not part:
                return
            request += part
        key = None
        for line in request.decode("latin-1").split("\r\n"):
            if line.lower().startswith("sec-websocket-key:"):
                key = line.split(":", 1)[1].strip()
        if not key:
            sock.sendall(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
            return
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        sock.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      "Sec-WebSocket-Accept: %s\r\n\r\n" % accept).encode())
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.sendall(ws_frame(json.dumps({"rate": RATE, "channels": CHANNELS, "format": "s16le"}).encode(), 0x1))

        q, ready = collections.deque(), threading.Event()
        entry = (q, ready)
        with clients_lock:
            clients.add(entry)
        log("browser connected:", self.client_address[0])
        try:
            while True:
                ready.wait(1.0)
                ready.clear()
                while q:
                    sock.sendall(ws_frame(q.popleft(), 0x2))
        except OSError:
            pass
        finally:
            with clients_lock:
                clients.discard(entry)
            log("browser disconnected:", self.client_address[0])


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    # Never let ffmpeg or the jack tools start a JACK server on real hardware by themselves.
    os.environ["JACK_NO_START_SERVER"] = "1"
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=6081, help="WebSocket port (default 6081)")
    ap.add_argument("--source", default="Kickray", help="JACK client name of the simulator (default Kickray)")
    args = ap.parse_args()

    def stop(*_):
        if run_ffmpeg.proc and run_ffmpeg.proc.poll() is None:
            run_ffmpeg.proc.terminate()
        sys.exit(0)
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    threading.Thread(target=run_ffmpeg, daemon=True).start()
    threading.Thread(target=keep_connected, args=(args.source,), daemon=True).start()
    log("WebSocket on port", args.port)
    Server(("", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
