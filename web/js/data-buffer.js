/**
 * Authoritative time-series buffer for the dashboard. Server history, the
 * live MQTT stream and CSV imports write here; the toolbar's CSV export reads
 * from here so the file always matches what the user sees on the charts
 * (server history rows are bucket means, not raw 1 Hz samples).
 *
 * Capped at MAX_POINTS rows (FIFO). Normally it holds ~3000 history rows
 * plus <10 min of live rows, because ServerHistory replaces it on refresh.
 * Each row carries a `_time` Date plus the fields as they arrive.
 */

const DataBuffer = (function () {
    const MAX_POINTS = 50000; // matches Charts.maxPoints
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
