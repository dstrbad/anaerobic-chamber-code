"""HTTP API for chamber configuration templates.

Templates are server-side so they're shared across browsers and survive
a clean re-install. Storage is a single JSON file on the volume that the
docker-compose mount maps to ./collector/data — same place CSVs land.

Runs on a daemon thread alongside the MQTT subscriber. Suppresses access
logs to keep the collector's stdout focused on data ingestion.

API:
    GET    /templates              -> {name: {fields}, ...}
    PUT    /templates/<name>       body: {fields}            -> 204
    DELETE /templates/<name>                                  -> 204

CORS is wide open since the dashboard runs on a different port (8080)
than this API (8000). Acceptable for a lab-internal deployment.
"""

import json
import os
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import unquote

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
        if self.path != "/templates":
            self._respond(404)
            return
        with _lock:
            data = _load()
        self._respond(200, json.dumps(data).encode("utf-8"))

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
        server = HTTPServer(("0.0.0.0", TEMPLATES_PORT), _Handler)
        print(f"Templates HTTP API listening on :{TEMPLATES_PORT}")
        server.serve_forever()

    t = threading.Thread(target=_run, daemon=True, name="templates-http")
    t.start()
