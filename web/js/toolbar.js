/**
 * Toolbar — metric show/hide pills, date range, CSV export.
 *
 * Metric visibility is persisted in localStorage so user preferences
 * survive reloads. Range and export read from DataBuffer (which mirrors
 * whatever's currently on the charts — live or history).
 */

const Toolbar = (function () {
    const STORAGE_KEY = 'anaerobic.metrics_visible';
    const visibility = {};

    function loadVisibility() {
        let saved = {};
        try {
            saved = JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}');
        } catch (e) { /* ignore corrupted storage */ }
        Charts.metrics.forEach(m => {
            visibility[m.key] = saved[m.key] !== false; // default visible
        });
    }

    function saveVisibility() {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(visibility));
    }

    function applyVisibility() {
        Charts.metrics.forEach(m => {
            Charts.setVisibility(m.chart, m.idx, visibility[m.key]);
        });
    }

    function buildPills() {
        const container = document.getElementById('metric-toggles');
        container.innerHTML = '';
        Charts.metrics.forEach(m => {
            const pill = document.createElement('button');
            pill.className = 'metric-pill';
            pill.style.borderColor = m.color;
            pill.dataset.key = m.key;
            pill.type = 'button';

            const dot = document.createElement('span');
            dot.className = 'dot';
            dot.style.background = m.color;
            pill.appendChild(dot);

            const label = document.createElement('span');
            label.dataset.i18n = m.i18n;
            label.textContent = I18n.t(m.i18n);
            pill.appendChild(label);

            updatePillState(pill, visibility[m.key]);
            pill.addEventListener('click', () => {
                visibility[m.key] = !visibility[m.key];
                saveVisibility();
                Charts.setVisibility(m.chart, m.idx, visibility[m.key]);
                updatePillState(pill, visibility[m.key]);
            });
            container.appendChild(pill);
        });
    }

    function updatePillState(pill, isVisible) {
        pill.classList.toggle('off', !isVisible);
    }

    function getRangeInputs() {
        const fromVal = document.getElementById('range-from').value;
        const toVal = document.getElementById('range-to').value;
        const from = fromVal ? new Date(fromVal) : null;
        const to = toVal ? new Date(toVal) : null;
        return { from: from, to: to };
    }

    function applyRange() {
        const { from, to } = getRangeInputs();
        if (!from || !to) {
            alert(I18n.t('toolbar.range_required'));
            return;
        }
        if (from >= to) {
            alert(I18n.t('toolbar.range_invalid'));
            return;
        }
        Charts.setRange(from, to);
    }

    function resetRange() {
        document.getElementById('range-from').value = '';
        document.getElementById('range-to').value = '';
        Charts.setRange(null, null);
    }

    // Format Date as "YYYY-MM-DDTHH:mm:ss" in local time (no Z suffix) — keeps
    // exported timestamps human-readable in spreadsheets.
    function formatLocalIso(d) {
        const pad = n => String(n).padStart(2, '0');
        return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate())
             + 'T' + pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
    }

    function csvEscape(v) {
        if (v === undefined || v === null) return '';
        const s = String(v);
        if (s.indexOf(',') >= 0 || s.indexOf('"') >= 0 || s.indexOf('\n') >= 0) {
            return '"' + s.replace(/"/g, '""') + '"';
        }
        return s;
    }

    const EXPORT_COLUMNS = [
        'time',
        'o2', 'h2', 'h2s', 'o3', 'co2',
        'pb', 'ps',
        'tc', 'th', 'ta',
        'sp_cat', 'sp_chm',
        'st', 'ts', 'f',
        'pump', 'sol', 'cpwm', 'hpwm',
        'wu', 'up', 'pc', 'pt',
    ];

    function exportCSV() {
        const buffer = DataBuffer.getAll();
        if (buffer.length === 0) {
            alert(I18n.t('toolbar.no_data'));
            return;
        }
        const { from, to } = getRangeInputs();
        const filtered = buffer.filter(r => {
            if (from && r._time < from) return false;
            if (to && r._time > to) return false;
            return true;
        });
        if (filtered.length === 0) {
            alert(I18n.t('toolbar.no_data_range'));
            return;
        }

        const lines = [EXPORT_COLUMNS.join(',')];
        filtered.forEach(r => {
            const cells = EXPORT_COLUMNS.map(col => {
                if (col === 'time') return formatLocalIso(r._time);
                return csvEscape(r[col]);
            });
            lines.push(cells.join(','));
        });

        const blob = new Blob([lines.join('\n') + '\n'], { type: 'text/csv;charset=utf-8' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        const stamp = formatLocalIso(new Date()).replace(/[:T]/g, '-');
        a.href = url;
        a.download = 'chamber_' + stamp + '.csv';
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }

    function init() {
        loadVisibility();
        buildPills();
        applyVisibility();

        document.getElementById('range-apply').addEventListener('click', applyRange);
        document.getElementById('range-reset').addEventListener('click', resetRange);
        document.getElementById('export-csv').addEventListener('click', exportCSV);

        // Pill labels follow the language toggle.
        window.addEventListener('i18n:change', () => {
            document.querySelectorAll('#metric-toggles [data-i18n]').forEach(el => {
                el.textContent = I18n.t(el.dataset.i18n);
            });
        });
    }

    return { init: init };
})();
