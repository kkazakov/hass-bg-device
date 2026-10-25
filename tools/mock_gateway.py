#!/usr/bin/env python3
"""Minimal fake hass-bg-gateway for firmware development.

It implements just enough of the /v1/voice contract:

    POST /v1/voice      multipart field "audio" (WAV) -> JSON
    GET  /v1/audio/test.wav -> generated PCM16 mono WAV
    GET  /health        -> {"ok": true}

No dependencies outside the Python standard library.
"""

from __future__ import annotations

import argparse
import io
import json
import math
import struct
import sys
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Optional


def make_wav(sample_rate: int, duration: float, frequency: float) -> bytes:
    frames = max(1, int(sample_rate * duration))
    amplitude = 0.25 * 32767
    fade = max(1, int(sample_rate * 0.02))
    samples = bytearray()
    for i in range(frames):
        envelope = min(1.0, i / fade, (frames - i) / fade)
        value = int(amplitude * envelope * math.sin(2 * math.pi * frequency * i / sample_rate))
        samples += struct.pack("<h", value)

    out = io.BytesIO()
    with wave.open(out, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(bytes(samples))
    return out.getvalue()


def parse_multipart(body: bytes, content_type: str) -> tuple[Optional[bytes], dict[str, str]]:
    """Return (audio_bytes, form_fields) from a multipart/form-data body."""
    marker = "boundary="
    if marker not in content_type:
        return None, {}
    boundary = content_type.split(marker, 1)[1].strip().strip('"')
    delimiter = b"--" + boundary.encode("ascii")

    audio: Optional[bytes] = None
    fields: dict[str, str] = {}
    for part in body.split(delimiter):
        if not part or part in (b"--\r\n", b"--", b"\r\n"):
            continue
        part = part.lstrip(b"\r\n")
        if b"\r\n\r\n" not in part:
            continue
        raw_headers, data = part.split(b"\r\n\r\n", 1)
        headers = raw_headers.decode("utf-8", "replace")
        if data.endswith(b"\r\n"):
            data = data[:-2]

        name = None
        for token in headers.split(";"):
            token = token.strip()
            if token.startswith("name="):
                name = token.split("=", 1)[1].strip().strip('"')
        if name == "audio":
            audio = data
        elif name:
            fields[name] = data.decode("utf-8", "replace")

    return audio, fields


class GatewayHandler(BaseHTTPRequestHandler):
    server_version = "bg-mock-gateway/0.1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt: str, *args) -> None:  # noqa: A003
        sys.stderr.write("[mock] " + (fmt % args) + "\n")

    def _send_json(self, status: int, payload: dict) -> None:
        data = json.dumps(payload, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def _send_bytes(self, status: int, data: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def _check_key(self) -> bool:
        expected = self.server.api_key  # type: ignore[attr-defined]
        if not expected:
            return True
        provided = self.headers.get("X-API-Key", "")
        return provided == expected

    def do_GET(self) -> None:  # noqa: N802
        if not self._check_key():
            self._send_json(401, {"error": "invalid or missing X-API-Key"})
            return

        if self.path == "/health":
            self._send_json(200, {"ok": True, "time": int(time.time())})
            return

        if self.path == "/v1/audio/test.wav":
            wav = make_wav(self.server.sample_rate, self.server.duration, self.server.frequency)  # type: ignore[attr-defined]
            self._send_bytes(200, wav, "audio/wav")
            return

        self._send_json(404, {"error": "not found", "path": self.path})

    def do_POST(self) -> None:  # noqa: N802
        if not self._check_key():
            self._send_json(401, {"error": "invalid or missing X-API-Key"})
            return

        if self.path != "/v1/voice":
            self._send_json(404, {"error": "not found", "path": self.path})
            return

        fail_status = self.server.fail_status  # type: ignore[attr-defined]
        if fail_status:
            self._send_json(fail_status, {"error": f"simulated HTTP {fail_status}"})
            return

        length_header = self.headers.get("Content-Length")
        if length_header is None:
            self._send_json(411, {"error": "Content-Length required"})
            return
        try:
            length = int(length_header)
        except ValueError:
            self._send_json(400, {"error": "invalid Content-Length"})
            return

        body = self.rfile.read(length)
        content_type = self.headers.get("Content-Type", "")
        audio, fields = parse_multipart(body, content_type)

        if audio is None:
            self._send_json(400, {"error": "missing multipart field 'audio'"})
            return

        save_path = Path(self.server.save_path)  # type: ignore[attr-defined]
        save_path.parent.mkdir(parents=True, exist_ok=True)
        save_path.write_bytes(audio)

        self.log_message(
            "voice: %d bytes, audio=%d bytes, fields=%s, key=%s, saved=%s",
            len(body),
            len(audio),
            sorted(fields),
            "yes" if self.headers.get("X-API-Key") else "no",
            save_path,
        )

        if self.server.no_speak:  # type: ignore[attr-defined]
            payload = {
                "kind": "action",
                "speak": False,
                "text": "Готово.",
                "transcript": "окей дом тест",
                "audio_url": None,
                "matched": {"intent": "test_action", "score": 0.99},
                "timings": {"total_ms": 42},
                "reason": None,
            }
        else:
            payload = {
                "kind": "query",
                "speak": True,
                "text": "Това е тестов отговор от mock gateway.",
                "transcript": "окей дом тест",
                "audio_url": "/v1/audio/test.wav",
                "matched": {"intent": "test_query", "score": 0.99},
                "timings": {"total_ms": 42},
                "reason": None,
            }

        self._send_json(200, payload)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=18125)
    parser.add_argument("--api-key", default="", help="require this X-API-Key (empty = keyless)")
    parser.add_argument("--save", default="/tmp/bg-voice-last-upload.wav")
    parser.add_argument("--sample-rate", type=int, default=16000)
    parser.add_argument("--duration", type=float, default=1.0)
    parser.add_argument("--frequency", type=float, default=440.0)
    parser.add_argument("--fail", type=int, default=0, metavar="STATUS", help="always return this HTTP status for /v1/voice")
    parser.add_argument("--no-speak", action="store_true", help="return speak=false")
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), GatewayHandler)
    server.daemon_threads = True
    server.api_key = args.api_key  # type: ignore[attr-defined]
    server.save_path = args.save  # type: ignore[attr-defined]
    server.sample_rate = args.sample_rate  # type: ignore[attr-defined]
    server.duration = args.duration  # type: ignore[attr-defined]
    server.frequency = args.frequency  # type: ignore[attr-defined]
    server.fail_status = args.fail  # type: ignore[attr-defined]
    server.no_speak = args.no_speak  # type: ignore[attr-defined]

    print(f"mock gateway listening on http://{args.host}:{args.port}")
    print(f"  POST /v1/voice -> saves to {args.save}")
    print(f"  GET  /v1/audio/test.wav -> {args.sample_rate} Hz PCM16 mono")
    print(f"  auth: {'X-API-Key required' if args.api_key else 'keyless'}")
    if args.fail:
        print(f"  /v1/voice always returns HTTP {args.fail}")
    print("Ctrl-C to stop")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
