/**
 * Lightweight i18n — Croatian + English. No build step, no framework.
 * Elements opt in with data-i18n="key" or data-i18n-title="key".
 * Code reads strings via I18n.t(key) and listens for 'i18n:change' to re-render.
 */

const I18n = (function () {
    const dict = {
        hr: {
            'app.title': 'Nadzor anaerobne komore',
            'header.connected': 'Spojeno',
            'header.disconnected': 'Odspojeno',
            'header.demo': 'Demo način',
            'header.history': 'Povijest',
            'header.import': 'Uvezi CSV',
            'header.back_live': 'Natrag na uživo',
            'header.settings': 'Postavke',
            'header.lang': 'EN',
            'settings.title': 'Postavke komore',
            'settings.conditions': 'Uvjeti',
            'settings.actions': 'Akcije',
            'settings.templates': 'Predlošci',
            'settings.sp_cat': 'Setpoint katalizatora',
            'settings.sp_chm': 'Setpoint komore',
            'settings.heater_cat': 'Grijač katalizatora',
            'settings.heater_chm': 'Grijač komore',
            'settings.logging': 'Aktivno logiranje',
            'settings.apply': 'Primijeni',
            'settings.purge_small': 'Pokreni mali purge',
            'settings.purge_big': 'Pokreni veliki purge',
            'settings.abort': 'Prekini sve',
            'settings.actions_hint': 'Akcije se ne spremaju u predloške — uvijek su eksplicitne.',
            'settings.loading': 'Učitavanje…',
            'settings.no_templates': 'Nema spremljenih predložaka',
            'settings.templates_error': 'Greška pri dohvaćanju predložaka',
            'settings.save_error': 'Greška pri spremanju',
            'settings.delete_error': 'Greška pri brisanju',
            'settings.template_name_placeholder': 'Naziv predloška',
            'settings.template_name_required': 'Upiši naziv predloška',
            'settings.save_current': 'Spremi trenutno',
            'settings.apply_template': 'Primijeni',
            'settings.delete': 'Obriši',
            'settings.confirm_delete': 'Obriši predložak',
            'settings.confirm_small': 'Pokrenuti mali purge?',
            'settings.confirm_big': 'Pokrenuti veliki purge?',
            'settings.confirm_abort': 'Prekinuti sve operacije?',
            'settings.big_blocked': 'Blokirano: O₂ iznad 3 %',
            'settings.no_mqtt': 'MQTT nije spojen — komanda nije poslana',
            'history.banner': 'Povijesni pregled',
            'history.rows': 'redaka',
            'history.duration': 'trajanje',
            'history.error': 'Greška pri učitavanju datoteke',
            'toolbar.metrics': 'Metrike',
            'toolbar.range': 'Raspon',
            'toolbar.apply': 'Primijeni',
            'toolbar.reset': 'Resetiraj',
            'toolbar.export': 'Izvoz CSV',
            'toolbar.range_required': 'Postavi početak i kraj raspona',
            'toolbar.range_invalid': 'Početak mora biti prije kraja',
            'toolbar.no_data': 'Nema podataka za izvoz',
            'toolbar.no_data_range': 'Nema podataka u odabranom rasponu',

            'section.live': 'Trenutne vrijednosti',
            'section.gas': 'Senzori plinova',
            'section.pressure': 'Tlak',
            'section.temperature': 'Temperatura',
            'section.status': 'Status',

            'label.o2': 'O₂',
            'label.h2': 'H₂',
            'label.h2s': 'H₂S',
            'label.o3': 'O₃',
            'label.co2': 'CO₂',
            'label.pb': 'P velika',
            'label.ps': 'P mala',
            'label.tc': 'T kat.',
            'label.th': 'T grijač',
            'label.ta': 'T komora',
            'label.sp_chm': 'SP komora',
            'label.sp_cat': 'SP kat.',
            'label.state': 'Stanje',
            'label.thermal': 'Toplinski',
            'label.fault': 'Greška',
            'label.purge': 'Ispiranje',
            'label.warmup': 'Zagrijavanje',

            'value.ready': 'Spreman',
            'value.none': 'NEMA',

            'chart.gas': 'Senzori plinova',
            'chart.pressure': 'Tlak (hPa)',
            'chart.temperature': 'Temperatura (°C)',
            'chart.t_catalyst': 'T katalizator',
            'chart.t_heater': 'T grijač',
            'chart.t_chamber': 'T komora',
            'chart.p_big': 'P velika',
            'chart.p_small': 'P mala',
        },
        en: {
            'app.title': 'Anaerobic Chamber Monitor',
            'header.connected': 'Connected',
            'header.disconnected': 'Disconnected',
            'header.demo': 'Demo Mode',
            'header.history': 'History',
            'header.import': 'Import CSV',
            'header.back_live': 'Back to live',
            'header.settings': 'Settings',
            'header.lang': 'HR',
            'settings.title': 'Chamber settings',
            'settings.conditions': 'Conditions',
            'settings.actions': 'Actions',
            'settings.templates': 'Templates',
            'settings.sp_cat': 'Catalyst setpoint',
            'settings.sp_chm': 'Chamber setpoint',
            'settings.heater_cat': 'Catalyst heater',
            'settings.heater_chm': 'Chamber heater',
            'settings.logging': 'Active logging',
            'settings.apply': 'Apply',
            'settings.purge_small': 'Start small purge',
            'settings.purge_big': 'Start big purge',
            'settings.abort': 'Abort all',
            'settings.actions_hint': 'Actions are not saved into templates — they remain explicit.',
            'settings.loading': 'Loading…',
            'settings.no_templates': 'No saved templates',
            'settings.templates_error': 'Failed to fetch templates',
            'settings.save_error': 'Failed to save',
            'settings.delete_error': 'Failed to delete',
            'settings.template_name_placeholder': 'Template name',
            'settings.template_name_required': 'Enter a template name',
            'settings.save_current': 'Save current',
            'settings.apply_template': 'Apply',
            'settings.delete': 'Delete',
            'settings.confirm_delete': 'Delete template',
            'settings.confirm_small': 'Start small purge?',
            'settings.confirm_big': 'Start big purge?',
            'settings.confirm_abort': 'Abort all operations?',
            'settings.big_blocked': 'Blocked: O₂ above 3 %',
            'settings.no_mqtt': 'MQTT not connected — command not sent',
            'history.banner': 'Viewing history',
            'history.rows': 'rows',
            'history.duration': 'duration',
            'history.error': 'Failed to load file',
            'toolbar.metrics': 'Metrics',
            'toolbar.range': 'Range',
            'toolbar.apply': 'Apply',
            'toolbar.reset': 'Reset',
            'toolbar.export': 'Export CSV',
            'toolbar.range_required': 'Set both start and end',
            'toolbar.range_invalid': 'Start must be before end',
            'toolbar.no_data': 'No data to export',
            'toolbar.no_data_range': 'No data in the selected range',

            'section.live': 'Live Values',
            'section.gas': 'Gas Sensors',
            'section.pressure': 'Pressure',
            'section.temperature': 'Temperature',
            'section.status': 'Status',

            'label.o2': 'O₂',
            'label.h2': 'H₂',
            'label.h2s': 'H₂S',
            'label.o3': 'O₃',
            'label.co2': 'CO₂',
            'label.pb': 'P big',
            'label.ps': 'P small',
            'label.tc': 'T cat.',
            'label.th': 'T heater',
            'label.ta': 'T chamber',
            'label.sp_chm': 'SP chamber',
            'label.sp_cat': 'SP cat.',
            'label.state': 'State',
            'label.thermal': 'Thermal',
            'label.fault': 'Fault',
            'label.purge': 'Purge',
            'label.warmup': 'Warmup',

            'value.ready': 'Ready',
            'value.none': 'NONE',

            'chart.gas': 'Gas Sensors',
            'chart.pressure': 'Pressure (hPa)',
            'chart.temperature': 'Temperature (°C)',
            'chart.t_catalyst': 'T catalyst',
            'chart.t_heater': 'T heater',
            'chart.t_chamber': 'T chamber',
            'chart.p_big': 'P big',
            'chart.p_small': 'P small',
        },
    };

    const STORAGE_KEY = 'anaerobic.lang';
    const SUPPORTED = ['hr', 'en'];
    let current = (function () {
        const saved = localStorage.getItem(STORAGE_KEY);
        if (saved && SUPPORTED.includes(saved)) return saved;
        const nav = (navigator.language || 'hr').slice(0, 2);
        return SUPPORTED.includes(nav) ? nav : 'hr';
    })();

    function t(key) {
        return (dict[current] && dict[current][key]) || dict.en[key] || key;
    }

    function applyToDom() {
        document.documentElement.lang = current;
        document.title = t('app.title');
        document.querySelectorAll('[data-i18n]').forEach(function (el) {
            el.textContent = t(el.dataset.i18n);
        });
        document.querySelectorAll('[data-i18n-title]').forEach(function (el) {
            el.title = t(el.dataset.i18nTitle);
        });
        document.querySelectorAll('[data-i18n-placeholder]').forEach(function (el) {
            el.placeholder = t(el.dataset.i18nPlaceholder);
        });
    }

    function setLang(lang) {
        if (!SUPPORTED.includes(lang) || lang === current) return;
        current = lang;
        localStorage.setItem(STORAGE_KEY, current);
        applyToDom();
        window.dispatchEvent(new CustomEvent('i18n:change', { detail: { lang: current } }));
    }

    function toggle() {
        setLang(current === 'hr' ? 'en' : 'hr');
    }

    function lang() {
        return current;
    }

    return { t: t, setLang: setLang, toggle: toggle, lang: lang, applyToDom: applyToDom };
})();
