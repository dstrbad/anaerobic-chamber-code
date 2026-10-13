"""SQLite history store for the dashboard.

Every bulk frame is inserted here alongside the weekly CSV (the CSV stays the
human-readable export). The dashboard asks GET /history for a time window and
gets it downsampled into ~N buckets: mean per bucket, plus min/max for the
traces where a short spike matters (O2, pressures, temperatures).

Timestamps are epoch milliseconds (UTC), so the browser can show local time
regardless of the container's timezone.

Concurrency: the MQTT thread writes, the HTTP threads read. WAL mode lets
readers run while the writer commits; each thread gets its own connection.
"""

import csv
import glob
import os
import sqlite3
import threading
from datetime import datetime

import config

# Frame keys -> column type. "ts" (thermal state) is stored as "tstate" so it
# can't be confused with a timestamp in SQL.
REAL_FIELDS = ["o2", "h2", "h2s", "o3", "co2", "pb", "ps", "tc", "th", "ta",
               "sp_cat", "sp_chm"]
INT_FIELDS = ["pump", "sol", "cpwm", "hpwm", "wu", "up", "pc", "pt",
              "rst", "boots", "i2c"]
TEXT_FIELDS = {"st": "st", "ts": "tstate", "f": "f"}

# Traces that also get a min/max band in downsampled history
BAND_FIELDS = ["o2", "pb", "ps", "tc", "th", "ta"]
# Averaged per bucket (everything else is "last value in the bucket")
AVG_FIELDS = REAL_FIELDS + ["cpwm", "hpwm"]
# "Was it on at any point in the bucket"
ANY_FIELDS = ["pump", "sol"]
LAST_INT_FIELDS = ["wu", "up", "pc", "pt", "rst", "boots", "i2c"]

ALL_COLUMNS = REAL_FIELDS + INT_FIELDS + list(TEXT_FIELDS.values())

CSV_GLOB = "*_chamber_conditions.csv"

_local = threading.local()


def _conn() -> sqlite3.Connection:
    conn = getattr(_local, "conn", None)
    if conn is None:
        os.makedirs(os.path.dirname(config.HISTORY_DB) or ".", exist_ok=True)
        conn = sqlite3.connect(config.HISTORY_DB, timeout=30)
        conn.execute("PRAGMA journal_mode=WAL")
        conn.execute("PRAGMA synchronous=NORMAL")
        _local.conn = conn
    return conn


def init():
    conn = _conn()
    cols = ", ".join(
        [f"{c} REAL" for c in REAL_FIELDS]
        + [f"{c} INTEGER" for c in INT_FIELDS]
        + [f"{c} TEXT" for c in TEXT_FIELDS.values()]
    )
    conn.execute(f"CREATE TABLE IF NOT EXISTS samples (t_ms INTEGER NOT NULL, {cols})")
    conn.execute("CREATE INDEX IF NOT EXISTS samples_t ON samples(t_ms)")
    conn.execute("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)")
    conn.commit()


# --- Value conversion ---------------------------------------------------------

def _real(v):
    if v is None or v == "":
        return None
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def _int(v):
    f = _real(v)
    return None if f is None else int(f)


def _text(v):
    if v is None or v == "":
        return None
    return str(v)


def _values_from_frame(frame: dict) -> list:
    return ([_real(frame.get(k)) for k in REAL_FIELDS]
            + [_int(frame.get(k)) for k in INT_FIELDS]
            + [_text(frame.get(k)) for k in TEXT_FIELDS])


_INSERT_SQL = (f"INSERT INTO samples (t_ms, {', '.join(ALL_COLUMNS)}) "
               f"VALUES ({', '.join(['?'] * (len(ALL_COLUMNS) + 1))})")


# --- Writes -------------------------------------------------------------------

def insert(frame: dict, t_ms: int):
    conn = _conn()
    conn.execute(_INSERT_SQL, [t_ms] + _values_from_frame(frame))
    conn.commit()


def import_csv_once(csv_dir: str, before_ms: int):
    """One-time backfill from the weekly CSVs written before SQLite existed.

    Only rows older than `before_ms` (collector start) are imported, because
    newer rows are already being inserted live. Runs in a background thread;
    commits in batches so the live writer is never blocked for long.
    """
    conn = _conn()
    done = conn.execute("SELECT value FROM meta WHERE key = 'csv_imported'").fetchone()
    if done:
        return

    csv_to_key = {col: key for key, col in config.JSON_TO_CSV.items()}
    total = 0
    for path in sorted(glob.glob(os.path.join(csv_dir, CSV_GLOB))):
        batch = []
        try:
            with open(path, newline="") as f:
                for row in csv.DictReader(f):
                    try:
                        # Written by datetime.now().isoformat() in the same
                        # environment, so naive = this process's local time.
                        t_ms = int(datetime.fromisoformat(row["timestamp"]).timestamp() * 1000)
                    except (KeyError, TypeError, ValueError):
                        continue
                    if t_ms >= before_ms:
                        continue
                    frame = {csv_to_key[c]: v for c, v in row.items() if c in csv_to_key}
                    batch.append([t_ms] + _values_from_frame(frame))
                    if len(batch) >= 5000:
                        conn.executemany(_INSERT_SQL, batch)
                        conn.commit()
                        total += len(batch)
                        batch = []
        except OSError as e:
            print(f"History import: skipping {path}: {e}")
            continue
        if batch:
            conn.executemany(_INSERT_SQL, batch)
            conn.commit()
            total += len(batch)

    conn.execute("INSERT OR REPLACE INTO meta (key, value) VALUES ('csv_imported', '1')")
    conn.commit()
    print(f"History import: {total} rows from existing CSVs")


def start_import(csv_dir: str, before_ms: int):
    t = threading.Thread(target=import_csv_once, args=(csv_dir, before_ms),
                         daemon=True, name="history-import")
    t.start()


# --- Retention ----------------------------------------------------------------

def _week_start_ms(csv_path: str):
    name = os.path.basename(csv_path)
    try:
        return int(datetime.strptime(name[:8], "%Y%m%d").timestamp() * 1000)
    except ValueError:
        return None


def _db_bytes() -> int:
    size = 0
    for suffix in ("", "-wal", "-shm"):
        p = config.HISTORY_DB + suffix
        if os.path.exists(p):
            size += os.path.getsize(p)
    return size


def enforce_limit(csv_dir: str, limit_bytes: int):
    """Delete the oldest week while weekly CSVs + DB exceed the limit.

    Called when a new week's CSV is opened (and at startup). Normal growth
    means one week out per week in. The current week is never deleted.
    The DB file does not shrink after DELETE, but SQLite reuses the freed
    pages, so it stays roughly at its high-water mark.
    """
    files = sorted(glob.glob(os.path.join(csv_dir, CSV_GLOB)))
    deleted = []
    while len(files) > 1:
        total = sum(os.path.getsize(f) for f in files) + _db_bytes()
        if total <= limit_bytes:
            break
        oldest = files.pop(0)
        try:
            os.remove(oldest)
            deleted.append(os.path.basename(oldest))
        except OSError as e:
            print(f"Retention: could not delete {oldest}: {e}")
            break

    if deleted:
        keep_from = _week_start_ms(files[0])
        if keep_from is not None:
            conn = _conn()
            conn.execute("DELETE FROM samples WHERE t_ms < ?", (keep_from,))
            conn.commit()
        print(f"Retention: over {limit_bytes / 1e9:.0f} GB, deleted {', '.join(deleted)}")


# --- Reads --------------------------------------------------------------------

def query(from_ms: int, to_ms: int, points: int) -> dict:
    """Downsample [from_ms, to_ms] into at most `points` buckets (min 1 s)."""
    span = max(1, to_ms - from_ms)
    bucket = max(1000, -(-span // points))  # ceil, never finer than 1 s

    agg = (["MAX(t_ms) AS last_t"]
           + [f"AVG({c}) AS {c}" for c in AVG_FIELDS]
           + [f"MIN({c}) AS {c}_min" for c in BAND_FIELDS]
           + [f"MAX({c}) AS {c}_max" for c in BAND_FIELDS]
           + [f"MAX({c}) AS {c}" for c in ANY_FIELDS]
           + ["MAX(CASE WHEN f IS NOT NULL AND f <> 'NONE' THEN 1 ELSE 0 END) AS any_fault"])
    last = ["st", "tstate", "f"] + LAST_INT_FIELDS

    sql = (f"WITH agg AS (SELECT t_ms / :b AS k, {', '.join(agg)} "
           f"FROM samples WHERE t_ms >= :from AND t_ms <= :to GROUP BY k) "
           f"SELECT agg.*, {', '.join('s.' + c for c in last)} "
           f"FROM agg JOIN samples s ON s.t_ms = agg.last_t ORDER BY agg.k")

    cur = _conn().execute(sql, {"b": bucket, "from": from_ms, "to": to_ms})
    names = [d[0] for d in cur.description]

    # Output uses the frame's key names so the dashboard can treat history
    # rows like live frames ("tstate" -> "ts", time as "t").
    out_names = ["t"] + [("ts" if n == "tstate" else n) for n in names
                         if n not in ("k", "last_t")]
    rows = []
    seen = set()
    for r in cur:
        rec = dict(zip(names, r))
        k = rec["k"]
        if k in seen:  # two samples with the same t_ms -> duplicate join row
            continue
        seen.add(k)
        t = min(k * bucket + bucket // 2, rec["last_t"])
        row = [t]
        for n in names:
            if n in ("k", "last_t"):
                continue
            v = rec[n]
            row.append(round(v, 3) if isinstance(v, float) else v)
        rows.append(row)

    return {"from": from_ms, "to": to_ms, "bucket_ms": bucket,
            "fields": out_names, "rows": rows}
