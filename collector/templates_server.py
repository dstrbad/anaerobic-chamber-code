"""HTTP API for chamber configuration templates and recorded history.

Templates are server-side so they're shared across browsers and survive
a clean re-install. Storage is a single JSON file on the volume that the
docker-compose mount maps to ./collector/data — same place CSVs land.
History comes from the SQLite store the collector fills (storage.py).

Runs on a daemon thread alongside the MQTT subscriber. Suppresses access
logs to keep the collector's stdout focused on data ingestion.

API:
    GET    /templates              -> {name: {fields}, ...}
    PUT    /templates/<name>       body: {fields}            -> 204
    DELETE /templates/<name>                                  -> 204
    GET    /history?days=1[&points=3000]                      -> downsampled rows
    GET    /history?from=<ms>&to=<ms>[&points=3000]           -> downsampled rows

CORS is wide open since the dashboard runs on a different port (8080)
than this API (8000). Acceptable for a lab-internal deployment.
"""

import gzip
import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlparse

import storage

HISTORY_MAX_DAYS = 400
HISTORY_DEFAULT_POINTS = 3000
HISTORY_MAX_POINTS = 20000

TEMPLATES_PATH = os.environ.get("TEMPLATES_PATH", "/data/templates.json")
TEMPLATES_PORT = int(os.environ.get("TEMPLATES_PORT", "8000"))

_lock = threading.Lock()


def _load() -> dict:
    if not os.path.exists(TEMPLATES_PATH):
        return {}
    try:
        with open(TEMPLATES_PATH, "r") as f:
            data = json.load(f)
            return data if isinstance(data, dict) else {}
    except (OSError, json.JSONDecodeError):
        return {}


def _save(data: dict) -> None:
    os.makedirs(os.path.dirname(TEMPLATES_PATH) or ".", exist_ok=True)
    tmp = TEMPLATES_PATH + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=2, sort_keys=True)
    os.replace(tmp, TEMPLATES_PATH)


class _Handler(BaseHTTPRequestHandler):
    def _cors_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header(
            "Access-Control-Allow-Methods", "GET, PUT, DELETE, OPTIONS"
        )
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def _respond(self, code: int, body: bytes = b"", content_type: str = "application/json"):
        self.send_response(code)
        self._cors_headers()
        if body:
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _name_from_path(self) -> str | None:
        prefix = "/templates/"
        if not self.path.startswith(prefix):
            return None
        name = unquote(self.path[len(prefix):]).strip()
        if not name or "/" in name or len(name) > 64:
            return None
        return name

    def do_OPTIONS(self):
        self._respond(204)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/history":
            self._history()
            return
        if path != "/templates":
            self._respond(404)
            return
        with _lock:
            data = _load()
        self._respond(200, json.dumps(data).encode("utf-8"))

    def _history(self):
        q = parse_qs(urlparse(self.path).query)

        def arg(name, cast, default):
            try:
                return cast(q[name][0])
            except (KeyError, IndexError, ValueError):
                return default

        now_ms = int(time.time() * 1000)
        to_ms = arg("to", int, now_ms)
        if "from" in q:
            from_ms = arg("from", int, None)
        else:
            days = min(max(arg("days", float, 1.0), 1 / 24), HISTORY_MAX_DAYS)
            from_ms = to_ms - int(days * 86400000)
        points = min(max(arg("points", int, HISTORY_DEFAULT_POINTS), 100), HISTORY_MAX_POINTS)

        if from_ms is None or from_ms >= to_ms:
            self._respond(400)
            return

        try:
            data = storage.query(from_ms, to_ms, points)
        except Exception as e:
            print(f"History query failed: {e}")
            self._respond(500)
            return

        body = json.dumps(data, separators=(",", ":")).encode("utf-8")
        if "gzip" in (self.headers.get("Accept-Encoding") or ""):
            body = gzip.compress(body, compresslevel=5)
            self.send_response(200)
            self._cors_headers()
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self._respond(200, body)

    def do_PUT(self):
        name = self._name_from_path()
        if name is None:
            self._respond(404)
            return
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length <= 0 or length > 4096:
            self._respond(400)
            return
        try:
            body = self.rfile.read(length)
            fields = json.loads(body)
        except (json.JSONDecodeError, ValueError):
            self._respond(400)
            return
        if not isinstance(fields, dict):
            self._respond(400)
            return
        with _lock:
            data = _load()
            data[name] = fields
            _save(data)
        self._respond(204)

    def do_DELETE(self):
        name = self._name_from_path()
        if name is None:
            self._respond(404)
            return
        with _lock:
            data = _load()
            if name in data:
                del data[name]
                _save(data)
        self._respond(204)

    def log_message(self, format, *args):
        return  # silence per-request access logs


def start():
    def _run():
        server = ThreadingHTTPServer(("0.0.0.0", TEMPLATES_PORT), _Handler)
        print(f"Templates + history HTTP API listening on :{TEMPLATES_PORT}")
        server.serve_forever()

    t = threading.Thread(target=_run, daemon=True, name="templates-http")
    t.start()
