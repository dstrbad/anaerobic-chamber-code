/**
 * History (CSV) loader — parses LOGNNNN.CSV files dropped from the Mega's
 * SD card and turns them into the same shape the live MQTT path uses, so the
 * charts and live-values panel can render historical sessions without code
 * duplication.
 *
 * The Mega has no RTC, so the `ms` column is millis() since boot. We anchor
 * each session's wall-clock time to the file's lastModified timestamp (when
 * the SD card was last touched — close enough for chart axes).
 */

const History = (function () {
    let session = null; // { filename, rows, durationMs, sessionEnd }

    function parseCSV(text) {
        const lines = text.split(/\r?\n/).filter(l => l.trim().length > 0);
        if (lines.length < 2) throw new Error('CSV has no data rows');
        const header = lines[0].split(',').map(s => s.trim());
        const rows = [];
        for (let i = 1; i < lines.length; i++) {
            const parts = lines[i].split(',');
            if (parts.length !== header.length) continue;
            const row = {};
            for (let j = 0; j < header.length; j++) {
                row[header[j]] = parts[j];
            }
            if (!isFinite(parseInt(row.ms, 10))) continue;
            rows.push(row);
        }
        if (rows.length === 0) throw new Error('CSV had a header but no valid rows');
        return rows;
    }

    async function load(file) {
        const text = await file.text();
        const rows = parseCSV(text);
        const firstMs = parseInt(rows[0].ms, 10);
        const lastMs = parseInt(rows[rows.length - 1].ms, 10);
        const durationMs = lastMs - firstMs;

        // Anchor session end to file mtime; fall back to "now" if unavailable.
        const sessionEnd = file.lastModified || Date.now();
        const sessionStart = sessionEnd - durationMs;

        rows.forEach(r => {
            r._time = new Date(sessionStart + (parseInt(r.ms, 10) - firstMs));
        });

        session = {
            filename: file.name,
            rows: rows,
            durationMs: durationMs,
            sessionEnd: sessionEnd,
        };
        return session;
    }

    function clear() {
        session = null;
    }

    function isActive() {
        return session !== null;
    }

    function getSession() {
        return session;
    }

    function formatDuration(ms) {
        const s = Math.round(ms / 1000);
        const h = Math.floor(s / 3600);
        const m = Math.floor((s % 3600) / 60);
        const sec = s % 60;
        if (h > 0) return h + 'h ' + m + 'm';
        if (m > 0) return m + 'm ' + sec + 's';
        return sec + 's';
    }

    return {
        load: load,
        clear: clear,
        isActive: isActive,
        getSession: getSession,
        formatDuration: formatDuration,
    };
})();
