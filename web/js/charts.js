/**
 * Plotly chart management — creates and updates time-series charts.
 * Re-renders titles and legends on i18n:change.
 */

const Charts = {
    maxPoints: 3600, // 1 hour at 1 Hz

    // Trace metadata used by the toolbar to build the show/hide toggles.
    // Order in the array doesn't matter; (chart, idx) is the unique key.
    metrics: [
        { chart: 'chart-gas',      idx: 0, key: 'o2',  i18n: 'label.o2',         color: '#4ecca3' },
        { chart: 'chart-gas',      idx: 1, key: 'h2',  i18n: 'label.h2',         color: '#e74c3c' },
        { chart: 'chart-gas',      idx: 2, key: 'h2s', i18n: 'label.h2s',        color: '#f39c12' },
        { chart: 'chart-gas',      idx: 3, key: 'o3',  i18n: 'label.o3',         color: '#9b59b6' },
        { chart: 'chart-pressure', idx: 0, key: 'pb',  i18n: 'chart.p_big',      color: '#3498db' },
        { chart: 'chart-pressure', idx: 1, key: 'ps',  i18n: 'chart.p_small',    color: '#e67e22' },
        { chart: 'chart-temp',     idx: 0, key: 'tc',  i18n: 'chart.t_catalyst', color: '#e74c3c' },
        { chart: 'chart-temp',     idx: 1, key: 'th',  i18n: 'chart.t_heater',   color: '#f39c12' },
        { chart: 'chart-temp',     idx: 2, key: 'ta',  i18n: 'chart.t_chamber',  color: '#2ecc71' },
    ],

    chartIds: ['chart-gas', 'chart-pressure', 'chart-temp'],

    setVisibility(chartId, traceIdx, visible) {
        Plotly.restyle(chartId, { visible: visible ? true : 'legendonly' }, [traceIdx]);
    },

    setRange(start, end) {
        const update = (start && end)
            ? { 'xaxis.range': [start, end] }
            : { 'xaxis.autorange': true };
        this.chartIds.forEach(id => Plotly.relayout(id, update));
    },

    _baseLayout() {
        return {
            paper_bgcolor: '#1a1a2e',
            plot_bgcolor: '#16213e',
            font: { color: '#e0e0e0', size: 11 },
            margin: { l: 50, r: 20, t: 30, b: 30 },
            xaxis: { type: 'date', gridcolor: '#0f3460' },
            yaxis: { gridcolor: '#0f3460' },
            legend: { orientation: 'h', y: 1.12 },
        };
    },

    init() {
        const t = I18n.t.bind(I18n);
        const base = this._baseLayout();

        Plotly.newPlot('chart-gas', [
            { name: 'O₂ (%)', x: [], y: [], mode: 'lines', line: { color: '#4ecca3' } },
            { name: 'H₂ (ppm)', x: [], y: [], mode: 'lines', yaxis: 'y2', line: { color: '#e74c3c' } },
            { name: 'H₂S (ppm)', x: [], y: [], mode: 'lines', yaxis: 'y2', line: { color: '#f39c12' } },
            { name: 'O₃ (ppm)', x: [], y: [], mode: 'lines', yaxis: 'y2', line: { color: '#9b59b6' } },
        ], {
            ...base,
            title: t('chart.gas'),
            yaxis: { ...base.yaxis, title: 'O₂ (%)' },
            yaxis2: { title: 'ppm', overlaying: 'y', side: 'right', gridcolor: '#0f3460' },
        }, { responsive: true });

        Plotly.newPlot('chart-pressure', [
            { name: t('chart.p_big'), x: [], y: [], mode: 'lines', line: { color: '#3498db' } },
            { name: t('chart.p_small'), x: [], y: [], mode: 'lines', line: { color: '#e67e22' } },
        ], {
            ...base,
            title: t('chart.pressure'),
            yaxis: { ...base.yaxis, title: 'hPa' },
        }, { responsive: true });

        Plotly.newPlot('chart-temp', [
            { name: t('chart.t_catalyst'), x: [], y: [], mode: 'lines', line: { color: '#e74c3c' } },
            { name: t('chart.t_heater'), x: [], y: [], mode: 'lines', line: { color: '#f39c12' } },
            { name: t('chart.t_chamber'), x: [], y: [], mode: 'lines', line: { color: '#2ecc71' } },
        ], {
            ...base,
            title: t('chart.temperature'),
            yaxis: { ...base.yaxis, title: '°C' },
        }, { responsive: true });

        window.addEventListener('i18n:change', () => this.relabel());
    },

    relabel() {
        const t = I18n.t.bind(I18n);
        Plotly.relayout('chart-gas', { title: t('chart.gas') });
        Plotly.relayout('chart-pressure', { title: t('chart.pressure') });
        Plotly.relayout('chart-temp', { title: t('chart.temperature') });
        Plotly.restyle('chart-pressure', { name: [t('chart.p_big'), t('chart.p_small')] }, [0, 1]);
        Plotly.restyle('chart-temp', { name: [t('chart.t_catalyst'), t('chart.t_heater'), t('chart.t_chamber')] }, [0, 1, 2]);
    },

    setHistory(rows) {
        const x = rows.map(r => r._time);
        const num = (v) => {
            const n = parseFloat(v);
            return isFinite(n) ? n : null;
        };

        Plotly.restyle('chart-gas', {
            x: [x, x, x, x],
            y: [
                rows.map(r => num(r.o2)),
                rows.map(r => num(r.h2)),
                rows.map(r => num(r.h2s)),
                rows.map(r => num(r.o3)),
            ],
        }, [0, 1, 2, 3]);

        Plotly.restyle('chart-pressure', {
            x: [x, x],
            y: [
                rows.map(r => num(r.pb)),
                rows.map(r => num(r.ps)),
            ],
        }, [0, 1]);

        Plotly.restyle('chart-temp', {
            x: [x, x, x],
            y: [
                rows.map(r => num(r.tc)),
                rows.map(r => num(r.th)),
                rows.map(r => num(r.ta)),
            ],
        }, [0, 1, 2]);
    },

    clear() {
        const reset = (id, n) => {
            const empties = Array.from({ length: n }, () => []);
            Plotly.restyle(id, { x: empties, y: empties }, Array.from({ length: n }, (_, i) => i));
        };
        reset('chart-gas', 4);
        reset('chart-pressure', 2);
        reset('chart-temp', 3);
    },

    update(data) {
        const now = data._time || new Date();

        Plotly.extendTraces('chart-gas', {
            x: [[now], [now], [now], [now]],
            y: [
                [parseFloat(data.o2)],
                [parseFloat(data.h2)],
                [parseFloat(data.h2s)],
                [parseFloat(data.o3)],
            ],
        }, [0, 1, 2, 3], this.maxPoints);

        Plotly.extendTraces('chart-pressure', {
            x: [[now], [now]],
            y: [[parseFloat(data.pb)], [parseFloat(data.ps)]],
        }, [0, 1], this.maxPoints);

        Plotly.extendTraces('chart-temp', {
            x: [[now], [now], [now]],
            y: [
                [parseFloat(data.tc)],
                [parseFloat(data.th)],
                [parseFloat(data.ta)],
            ],
        }, [0, 1, 2], this.maxPoints);
    }
};
