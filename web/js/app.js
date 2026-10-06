/**
 * Main application — wires i18n, MQTT, charts, live values, and CSV history.
 * Add ?demo to URL to use mock data without an MQTT broker.
 */

(function () {
    const MQTT_BROKER_WS = 'ws://' + (window.location.hostname || 'localhost') + ':9001';
    const MQTT_TOPIC = 'anaerobic/chamber1/bulk';

    I18n.applyToDom();

    const importBtn = document.getElementById('import-btn');
    const backLiveBtn = document.getElementById('back-live-btn');
    const csvInput = document.getElementById('csv-input');
    const banner = document.getElementById('history-banner');
    const langToggle = document.getElementById('lang-toggle');
    const statusEl = document.getElementById('connection-status');

    let lastLiveData = null;
    let isDemo = false;

    function refreshStatusLabel(connected) {
        if (History.isActive()) {
            // Connection pill is replaced by the history banner — keep it dim.
            statusEl.textContent = I18n.t('header.history');
            statusEl.className = 'status-disconnected';
            return;
        }
        if (isDemo) {
            statusEl.textContent = I18n.t('header.demo');
            statusEl.className = 'status-connected';
            return;
        }
        statusEl.textContent = connected ? I18n.t('header.connected') : I18n.t('header.disconnected');
        statusEl.className = connected ? 'status-connected' : 'status-disconnected';
    }

    function render(data) {
        document.getElementById('v-o2').textContent = data.o2 || '--';
        document.getElementById('v-h2').textContent = data.h2 || '--';
        document.getElementById('v-h2s').textContent = data.h2s || '--';
        document.getElementById('v-o3').textContent = data.o3 || '--';
        document.getElementById('v-co2').textContent = data.co2 || '--';
        document.getElementById('v-pb').textContent = data.pb || '--';
        document.getElementById('v-ps').textContent = data.ps || '--';
        document.getElementById('v-tc').textContent = data.tc || '--';
        document.getElementById('v-th').textContent = data.th || '--';
        document.getElementById('v-ta').textContent = data.ta || '--';
        document.getElementById('v-sp-chm').textContent = data.sp_chm || '--';
        document.getElementById('v-sp-cat').textContent = data.sp_cat || '--';

        document.getElementById('v-state').textContent = data.st || '--';
        document.getElementById('v-thermal').textContent = data.ts || '--';

        const fault = data.f;
        const faultEl = document.getElementById('v-fault');
        const isFault = fault && fault !== 'NONE';
        faultEl.textContent = isFault ? fault : I18n.t('value.none');
        faultEl.style.color = isFault ? '#e74c3c' : '#e0e0e0';

        document.getElementById('v-purge').textContent =
            (data.pc !== undefined && data.pt) ? data.pc + '/' + data.pt : '--';
        document.getElementById('v-warmup').textContent =
            (data.wu > 0) ? data.wu + 's' : I18n.t('value.ready');
    }

    function showHistoryBanner(session) {
        document.getElementById('history-filename').textContent = session.filename;
        document.getElementById('history-rows').textContent = session.rows.length;
        document.getElementById('history-duration').textContent = History.formatDuration(session.durationMs);
        banner.classList.remove('hidden');
        backLiveBtn.classList.remove('hidden');
    }

    function hideHistoryBanner() {
        banner.classList.add('hidden');
        backLiveBtn.classList.add('hidden');
    }

    async function loadHistoryFile(file) {
        try {
            const session = await History.load(file);
            Charts.setHistory(session.rows);
            DataBuffer.replace(session.rows);
            render(session.rows[session.rows.length - 1]);
            showHistoryBanner(session);
            refreshStatusLabel(MqttHandler.isConnected());
        } catch (e) {
            console.error(e);
            alert(I18n.t('history.error') + ': ' + e.message);
        }
    }

    function exitHistory() {
        History.clear();
        Charts.clear();
        DataBuffer.clear();
        hideHistoryBanner();
        lastLiveData = null;
        refreshStatusLabel(MqttHandler.isConnected());
    }

    importBtn.addEventListener('click', () => csvInput.click());
    backLiveBtn.addEventListener('click', exitHistory);
    csvInput.addEventListener('change', (e) => {
        const file = e.target.files && e.target.files[0];
        if (file) loadHistoryFile(file);
        // Reset so the same file can be re-selected later.
        e.target.value = '';
    });
    langToggle.addEventListener('click', () => I18n.toggle());

    Charts.init();
    Toolbar.init();
    Settings.init();

    MqttHandler.onStatus(refreshStatusLabel);
    MqttHandler.onData(function (data) {
        // Live data ignored while viewing history — user explicitly opted out.
        if (History.isActive()) return;
        lastLiveData = data;
        // Stamp wall-clock time and feed both the live charts and the export buffer.
        const stamped = Object.assign({}, data, { _time: data._time || new Date() });
        DataBuffer.append(stamped);
        render(stamped);
        Charts.update(stamped);
        Settings.onState(stamped);
    });

    window.addEventListener('i18n:change', function () {
        refreshStatusLabel(MqttHandler.isConnected());
        if (History.isActive()) {
            const s = History.getSession();
            document.getElementById('history-duration').textContent = History.formatDuration(s.durationMs);
            render(s.rows[s.rows.length - 1]);
        } else if (lastLiveData) {
            render(lastLiveData);
        }
    });

    if (window.location.search.indexOf('demo') !== -1) {
        isDemo = true;
        refreshStatusLabel(true);
        MockData.start();
    } else {
        refreshStatusLabel(false);
        MqttHandler.connect(MQTT_BROKER_WS, MQTT_TOPIC);
    }
})();
