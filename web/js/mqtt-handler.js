/**
 * MQTT handler — connects to broker via WebSocket and dispatches messages.
 * Status and data are delivered via callbacks so the UI layer owns DOM updates.
 */

const MqttHandler = {
    client: null,
    callbacks: [],
    statusCallbacks: [],
    connected: false,

    connect(brokerUrl, topic) {
        this.client = mqtt.connect(brokerUrl);

        this.client.on('connect', () => {
            this.connected = true;
            this.statusCallbacks.forEach(cb => cb(true));
            this.client.subscribe(topic);
            console.log(`Subscribed to ${topic}`);
        });

        this.client.on('close', () => {
            this.connected = false;
            this.statusCallbacks.forEach(cb => cb(false));
        });

        this.client.on('message', (topic, message) => {
            try {
                const data = JSON.parse(message.toString());
                this.callbacks.forEach(cb => cb(data));
            } catch (e) {
                console.warn('Invalid JSON:', message.toString());
            }
        });
    },

    onData(callback) {
        this.callbacks.push(callback);
    },

    onStatus(callback) {
        this.statusCallbacks.push(callback);
    },

    isConnected() {
        return this.connected;
    },

    publish(topic, payload) {
        if (!this.client || !this.connected) {
            console.warn('MQTT publish skipped — not connected:', topic);
            return false;
        }
        const body = (typeof payload === 'string') ? payload : JSON.stringify(payload);
        this.client.publish(topic, body);
        return true;
    },
};
