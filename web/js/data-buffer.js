/**
 * Authoritative time-series buffer for the dashboard. Both the live MQTT
 * stream and CSV history loads write here; the toolbar's CSV export reads
 * from here so the file always matches what the user sees on the charts.
 *
 * Capped at MAX_POINTS rows (FIFO). Each row carries a `_time` Date plus
 * the raw fields exactly as they arrive on MQTT / appear in the CSV.
 */

const DataBuffer = (function () {
    const MAX_POINTS = 7200; // 2 hours at 1 Hz, ~1.5 MB at ~200 B/row
    let buffer = [];

    function append(row) {
        buffer.push(row);
        if (buffer.length > MAX_POINTS) buffer.shift();
    }

    function replace(rows) {
        buffer = rows.slice();
    }

    function clear() {
        buffer = [];
    }

    function getAll() {
        return buffer;
    }

    function timeRange() {
        if (buffer.length === 0) return [null, null];
        return [buffer[0]._time, buffer[buffer.length - 1]._time];
    }

    return {
        append: append,
        replace: replace,
        clear: clear,
        getAll: getAll,
        timeRange: timeRange,
    };
})();
