/**
 * Settings modal — remote-control panel mirroring the OLED menu.
 * Publishes JSON commands on anaerobic/chamber1/command/<verb>.
 *
 * Templates are persisted server-side via the collector's HTTP API
 * (default http://<host>:8000/templates) so they're shared across browsers
 * and survive across re-installs of the dashboard. Templates contain only
 * conditions (chamber SP, heater toggles, logging) — purges remain
 * explicit operator actions and are not part of the saved presets.
 *
 * Confirmation is implicit: the next 1 Hz state broadcast is the ACK.
 */

const Settings = (function () {
    const COMMAND_TOPIC_PREFIX = 'anaerobic/chamber1/command/';
    const TEMPLATES_API = 'http://' + (window.location.hostname || 'localhost') + ':8000/templates';

    let modal, lastState = null;

    function publish(verb, payload) {
        const ok = MqttHandler.publish(COMMAND_TOPIC_PREFIX + verb, payload);
        if (!ok) {
            alert(I18n.t('settings.no_mqtt'));
        }
        return ok;
    }

    // --- Modal open/close -----------------------------------------------------

    function open() {
        // Pre-fill controls from the most recent state we've seen
        if (lastState) {
            const spChm = parseInt(lastState.sp_chm, 10);
            if (isFinite(spChm)) {
                document.getElementById('settings-sp').value = spChm;
                document.getElementById('settings-sp-display').textContent = spChm + ' °C';
            }
            const spCat = parseInt(lastState.sp_cat, 10);
            if (isFinite(spCat)) {
                document.getElementById('settings-sp-cat').value = spCat;
                document.getElementById('settings-sp-cat-display').textContent = spCat + ' °C';
            }
            const t = lastState.ts;
            document.getElementById('settings-cat').checked = (t === 'CAT' || t === 'BOTH');
            document.getElementById('settings-chm').checked = (t === 'CHM' || t === 'BOTH');
        }
        // Disable big purge if O2 is too high
        refreshPurgeAvailability();
        modal.classList.remove('hidden');
        loadTemplates();
    }

    function close() {
        modal.classList.add('hidden');
    }

    function refreshPurgeAvailability() {
        const o2 = lastState ? parseFloat(lastState.o2) : NaN;
        const big = document.getElementById('settings-purge-big');
        if (!big) return;
        const blocked = isFinite(o2) && o2 > 3.0;
        big.disabled = blocked;
        big.title = blocked ? I18n.t('settings.big_blocked') : '';
    }

    // --- Commands -------------------------------------------------------------

    function applyAll() {
        const spChm = parseInt(document.getElementById('settings-sp').value, 10);
        const spCat = parseInt(document.getElementById('settings-sp-cat').value, 10);
        const cat = document.getElementById('settings-cat').checked;
        const chm = document.getElementById('settings-chm').checked;
        const log = document.getElementById('settings-log').checked;
        publish('apply', {
            cmd: 'apply',
            fields: {
                chm_sp: spChm,
                cat_sp: spCat,
                heater_cat: cat,
                heater_chm: chm,
                log: log,
            },
        });
    }

    function startSmallPurge() {
        if (!confirm(I18n.t('settings.confirm_small'))) return;
        publish('purge', { cmd: 'purge', v: 'small' });
    }

    function startBigPurge() {
        if (!confirm(I18n.t('settings.confirm_big'))) return;
        publish('purge', { cmd: 'purge', v: 'big' });
    }

    function abort() {
        if (!confirm(I18n.t('settings.confirm_abort'))) return;
        publish('abort', { cmd: 'abort' });
    }

    // --- Templates ------------------------------------------------------------

    async function loadTemplates() {
        const list = document.getElementById('settings-template-list');
        list.innerHTML = '<li class="settings-template-empty" data-i18n="settings.loading">' +
            I18n.t('settings.loading') + '</li>';
        try {
            const res = await fetch(TEMPLATES_API);
            if (!res.ok) throw new Error('HTTP ' + res.status);
            const data = await res.json();
            const names = Object.keys(data).sort();
            list.innerHTML = '';
            if (names.length === 0) {
                const li = document.createElement('li');
                li.className = 'settings-template-empty';
                li.textContent = I18n.t('settings.no_templates');
                list.appendChild(li);
                return;
            }
            names.forEach(name => {
                const li = document.createElement('li');
                li.className = 'settings-template-item';

                const label = document.createElement('span');
                label.className = 'settings-template-name';
                label.textContent = name;
                label.title = JSON.stringify(data[name]);
                li.appendChild(label);

                const apply = document.createElement('button');
                apply.className = 'header-btn';
                apply.type = 'button';
                apply.textContent = I18n.t('settings.apply_template');
                apply.addEventListener('click', () => applyTemplate(name, data[name]));
                li.appendChild(apply);

                const del = document.createElement('button');
                del.className = 'header-btn';
                del.type = 'button';
                del.textContent = I18n.t('settings.delete');
                del.addEventListener('click', () => deleteTemplate(name));
                li.appendChild(del);

                list.appendChild(li);
            });
        } catch (e) {
            list.innerHTML = '';
            const li = document.createElement('li');
            li.className = 'settings-template-empty';
            li.textContent = I18n.t('settings.templates_error') + ': ' + e.message;
            list.appendChild(li);
        }
    }

    function captureCurrentFields() {
        return {
            chm_sp: parseInt(document.getElementById('settings-sp').value, 10),
            cat_sp: parseInt(document.getElementById('settings-sp-cat').value, 10),
            heater_cat: document.getElementById('settings-cat').checked,
            heater_chm: document.getElementById('settings-chm').checked,
            log: document.getElementById('settings-log').checked,
        };
    }

    async function saveTemplate() {
        const nameInput = document.getElementById('settings-template-name');
        const name = nameInput.value.trim();
        if (!name) {
            alert(I18n.t('settings.template_name_required'));
            return;
        }
        const fields = captureCurrentFields();
        try {
            const res = await fetch(TEMPLATES_API + '/' + encodeURIComponent(name), {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(fields),
            });
            if (!res.ok) throw new Error('HTTP ' + res.status);
            nameInput.value = '';
            loadTemplates();
        } catch (e) {
            alert(I18n.t('settings.save_error') + ': ' + e.message);
        }
    }

    async function deleteTemplate(name) {
        if (!confirm(I18n.t('settings.confirm_delete') + ' "' + name + '"?')) return;
        try {
            const res = await fetch(TEMPLATES_API + '/' + encodeURIComponent(name), { method: 'DELETE' });
            if (!res.ok) throw new Error('HTTP ' + res.status);
            loadTemplates();
        } catch (e) {
            alert(I18n.t('settings.delete_error') + ': ' + e.message);
        }
    }

    function applyTemplate(name, fields) {
        // Reflect into the form so the user sees what's about to happen,
        // then publish a single bulk command.
        if (typeof fields.chm_sp === 'number') {
            document.getElementById('settings-sp').value = fields.chm_sp;
            document.getElementById('settings-sp-display').textContent = fields.chm_sp + ' °C';
        }
        if (typeof fields.cat_sp === 'number') {
            document.getElementById('settings-sp-cat').value = fields.cat_sp;
            document.getElementById('settings-sp-cat-display').textContent = fields.cat_sp + ' °C';
        }
        if (typeof fields.heater_cat === 'boolean') {
            document.getElementById('settings-cat').checked = fields.heater_cat;
        }
        if (typeof fields.heater_chm === 'boolean') {
            document.getElementById('settings-chm').checked = fields.heater_chm;
        }
        if (typeof fields.log === 'boolean') {
            document.getElementById('settings-log').checked = fields.log;
        }
        publish('apply', { cmd: 'apply', fields: fields });
    }

    // --- State syncing --------------------------------------------------------

    function onState(data) {
        lastState = data;
        // If modal is open, keep the purge availability fresh.
        if (modal && !modal.classList.contains('hidden')) {
            refreshPurgeAvailability();
        }
    }

    // --- Init -----------------------------------------------------------------

    function init() {
        modal = document.getElementById('settings-modal');

        document.getElementById('settings-open').addEventListener('click', open);
        document.getElementById('settings-close').addEventListener('click', close);
        modal.addEventListener('click', (e) => {
            if (e.target === modal) close();
        });

        const spInput = document.getElementById('settings-sp');
        const spDisplay = document.getElementById('settings-sp-display');
        spInput.addEventListener('input', () => {
            spDisplay.textContent = spInput.value + ' °C';
        });

        const spCatInput = document.getElementById('settings-sp-cat');
        const spCatDisplay = document.getElementById('settings-sp-cat-display');
        spCatInput.addEventListener('input', () => {
            spCatDisplay.textContent = spCatInput.value + ' °C';
        });

        document.getElementById('settings-apply').addEventListener('click', applyAll);
        document.getElementById('settings-purge-small').addEventListener('click', startSmallPurge);
        document.getElementById('settings-purge-big').addEventListener('click', startBigPurge);
        document.getElementById('settings-abort').addEventListener('click', abort);
        document.getElementById('settings-template-save').addEventListener('click', saveTemplate);
    }

    return { init: init, onState: onState };
})();
