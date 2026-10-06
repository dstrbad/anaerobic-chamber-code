/**
 * Mock data generator — simulates realistic anaerobic chamber readings.
 * Feeds data into MqttHandler callbacks at 1 Hz without needing a broker.
 */

const MockData = {
    t: 0,
    running: false,

    start() {
        this.running = true;

        // Pre-fill 5 minutes of historical data for nice-looking charts
        const now = Date.now();
        for (let i = 300; i > 0; i--) {
            const frame = this._generate(this.t);
            frame._time = new Date(now - i * 1000);
            MqttHandler.callbacks.forEach(cb => cb(frame));
            this.t++;
        }

        setInterval(() => {
            const frame = this._generate(this.t);
            frame._time = new Date();
            MqttHandler.callbacks.forEach(cb => cb(frame));
            this.t++;
        }, 1000);
    },

    _generate(t) {
        const noise = (amp) => (Math.random() - 0.5) * amp;
        const sin = (period, amp, offset) => offset + amp * Math.sin(2 * Math.PI * t / period);

        // O2 dropping over time (purge simulation from ~1% to 0.02%)
        const o2Base = Math.max(0.02, 1.2 * Math.exp(-t / 200));
        const o2 = Math.max(0, o2Base + noise(0.01));

        // H2 rising as it gets injected
        const h2Base = Math.min(800, 50 + 600 * (1 - Math.exp(-t / 150)));
        const h2 = Math.max(0, h2Base + noise(15));

        // H2S should stay very low (good catalyst)
        const h2s = Math.max(0, 0.05 + noise(0.02));

        // O3 low, tiny spikes (UVC lamp occasional)
        const o3 = Math.max(0, 0.02 + noise(0.01) + (t % 120 < 5 ? 0.08 : 0));

        // CO2 slowly rising from bacterial activity
        const co2Base = 400 + Math.min(2500, t * 0.8);
        const co2 = co2Base + noise(20);

        // Pressure — stable around 1013 hPa with slight variations
        const pb = 1013.2 + sin(300, 2, 0) + noise(0.5);
        const ps = 1012.8 + sin(250, 1.5, 0) + noise(0.5);

        // Temperature — catalyst heating up to 50C, chamber to 37C
        const tc = 50.0 + sin(60, 0.8, 0) + noise(0.2);
        const th = 37.0 + sin(45, 0.5, 0) + noise(0.15);
        const ta = 36.2 + sin(80, 0.3, 0) + noise(0.1);

        // Purge: cycle through 7 cycles in first 3 minutes
        let pc = 0, pt = 7;
        if (t < 180) {
            pc = Math.min(7, Math.floor(t / 25));
        } else {
            pc = 7;
        }

        return {
            o2: o2.toFixed(2),
            h2: Math.round(h2).toString(),
            h2s: h2s.toFixed(2),
            o3: o3.toFixed(2),
            co2: Math.round(co2).toString(),
            pb: pb.toFixed(1),
            ps: ps.toFixed(1),
            tc: tc.toFixed(1),
            th: th.toFixed(1),
            ta: ta.toFixed(1),
            sp_cat: '50',
            sp_chm: '37',
            st: t < 180 ? 'PURGE_S' : 'IDLE',
            ts: 'BOTH',
            f: 'NONE',
            pc: pc,
            pt: pt,
            pump: t < 180 ? 1 : 0,
            sol: 0,
            cpwm: 180,
            hpwm: 120,
            wu: Math.max(0, 600 - t),
            up: t,
        };
    }
};
