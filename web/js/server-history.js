/**
 * Server history — fills the charts from the collector's recorded history
 * (GET http://<host>:8000/history, SQLite-backed) instead of starting empty.
 *
 * The server downsamples any window to `points` buckets (never finer than
 * 1 s): mean per bucket plus <key>_min / <key>_max for O2, pressures and
 * temperatures (drawn as bands). Rows use the same keys as live MQTT frames,
 * plus `_time` (Date).
 *
 * The default window comes from the toolbar selector (WINDOWS, saved in
 * localStorage). Each window has its own point budget: 2 h is every recorded
 * second, 8 h has 3x the points of 1 d, longer windows ~3000 points.
 * While showing that default window it is re-fetched every REFRESH_MS, so live
 * points appended in between are folded back into server buckets and memory
 * stays bounded no matter how long the tab is open. A custom toolbar range
 * is not auto-refreshed.
 */

const ServerHistory = (function () {
    const API = 'http://' + (window.location.hostname || 'localhost') + ':8000/history';
    const STORAGE_KEY = 'anaerobic.history_window';
    const LEGACY_DAYS_KEY = 'anaerobic.history_days';  // before hour windows existed
    const POINTS = 3000;  // default budget (1 d and longer, custom ranges)
    const WINDOWS = [
        { id: '2h',  label: '2 h',  hours: 2,      points: 7200 },   // 1 point per second
        { id: '8h',  label: '8 h',  hours: 8,      points: 9000 },   // 3x the points of 1 d
        { id: '1d',  label: '1 d',  hours: 24,     points: POINTS },
        { id: '2d',  label: '2 d',  hours: 48,     points: POINTS },
        { id: '3d',  label: '3 d',  hours: 72,     points: POINTS },
        { id: '7d',  label: '7 d',  hours: 168,    points: POINTS },
        { id: '14d', label: '14 d', hours: 336,    points: POINTS },
        { id: '30d', label: '30 d', hours: 720,    points: POINTS },
    ];
    const DEFAULT_WINDOW = '1d';
    const REFRESH_MS = 10 * 60 * 1000;

    let customRange = false;
    let requestSeq = 0;
    let statusCb = function () {};

    function findWindow(id) {
        return WINDOWS.find(w => w.id === id);
    }

    function getWindow() {
        let id = null;
        try {
            id = localStorage.getItem(STORAGE_KEY);
            if (!id) {
                // Carry over a choice saved as a number of days
                const legacy = localStorage.getItem(LEGACY_DAYS_KEY);
                if (legacy) id = legacy + 'd';
            }
        } catch (e) { /* storage unavailable */ }
        return findWindow(id) || findWindow(DEFAULT_WINDOW);
    }

    function setWindow(id) {
        if (!findWindow(id)) return;
        try {
            localStorage.setItem(STORAGE_KEY, id);
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
        // Only the newest request may draw (e.g. window changed mid-fetch).
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
        // Window is sent as (fractional) days so the server's clock defines "now"
        const w = getWindow();
        return show({ days: w.hours / 24, points: w.points });
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
        WINDOWS: WINDOWS,
        getWindow: getWindow,
        setWindow: setWindow,
        loadDefault: loadDefault,
        loadRange: loadRange,
        start: start,
    };
})();
