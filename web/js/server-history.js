/**
 * Server history — fills the charts from the collector's recorded history
 * (GET http://<host>:8000/history, SQLite-backed) instead of starting empty.
 *
 * The server downsamples any window to ~POINTS buckets: mean per bucket plus
 * <key>_min / <key>_max for O2, pressures and temperatures (drawn as bands).
 * Rows use the same keys as live MQTT frames, plus `_time` (Date).
 *
 * Default window is the last N days (toolbar selector, saved in localStorage).
 * While showing that default window it is re-fetched every REFRESH_MS, so live
 * points appended in between are folded back into server buckets and memory
 * stays bounded no matter how long the tab is open. A custom toolbar range
 * is not auto-refreshed.
 */

const ServerHistory = (function () {
    const API = 'http://' + (window.location.hostname || 'localhost') + ':8000/history';
    const STORAGE_KEY = 'anaerobic.history_days';
    const DEFAULT_DAYS = 1;
    const DAY_OPTIONS = [1, 2, 3, 7, 14, 30];
    const POINTS = 3000;
    const REFRESH_MS = 10 * 60 * 1000;

    let customRange = false;
    let requestSeq = 0;
    let statusCb = function () {};

    function getDays() {
        let n = NaN;
        try {
            n = parseInt(localStorage.getItem(STORAGE_KEY), 10);
        } catch (e) { /* storage unavailable */ }
        return DAY_OPTIONS.includes(n) ? n : DEFAULT_DAYS;
    }

    function setDays(n) {
        if (!DAY_OPTIONS.includes(n)) return;
        try {
            localStorage.setItem(STORAGE_KEY, String(n));
        } catch (e) { /* storage unavailable */ }
    }

    function toRows(json) {
        const fields = json.fields;
        return json.rows.map(arr => {
            const row = {};
            for (let i = 0; i < fields.length; i++) row[fields[i]] = arr[i];
            row._time = new Date(row.t);
            return row;
        });
    }

    async function show(params) {
        // Only the newest request may draw (e.g. days changed mid-fetch).
        const seq = ++requestSeq;
        statusCb('loading');
        try {
            const res = await fetch(API + '?' + new URLSearchParams(params).toString());
            if (!res.ok) throw new Error('HTTP ' + res.status);
            const rows = toRows(await res.json());
            if (seq !== requestSeq) return;
            // User opened an SD-card CSV meanwhile — don't overwrite it.
            if (History.isActive()) return;
            Charts.setHistory(rows);
            DataBuffer.replace(rows);
            statusCb('ok');
        } catch (e) {
            if (seq !== requestSeq) return;
            console.warn('Server history unavailable:', e);
            statusCb('error');
        }
    }

    function loadDefault() {
        customRange = false;
        return show({ days: getDays(), points: POINTS });
    }

    function loadRange(from, to) {
        customRange = true;
        return show({ from: from.getTime(), to: to.getTime(), points: POINTS });
    }

    function start(onStatus) {
        if (onStatus) statusCb = onStatus;
        loadDefault();
        setInterval(() => {
            if (!customRange && !History.isActive()) loadDefault();
        }, REFRESH_MS);
    }

    return {
        DAY_OPTIONS: DAY_OPTIONS,
        getDays: getDays,
        setDays: setDays,
        loadDefault: loadDefault,
        loadRange: loadRange,
        start: start,
    };
})();
