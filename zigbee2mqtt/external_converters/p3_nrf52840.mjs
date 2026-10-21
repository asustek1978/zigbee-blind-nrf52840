/* Zigbee2MQTT external converter for the custom Zigbee End Device.
 * The firmware advertises model P3-NRF52840 and endpoint 10.
 * Movement UART TX stays disabled. Build 31 optionally emits exactly
 * four non-movement service queries once at startup (separate allowlist).
 */
import * as fz from 'zigbee-herdsman-converters/converters/fromZigbee';
import * as tz from 'zigbee-herdsman-converters/converters/toZigbee';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';
import * as reporting from 'zigbee-herdsman-converters/lib/reporting';

const e = exposes.presets;
const ea = exposes.access;

/* IDs must match the read-only ZCL Basic attributes in src/main.c.
 * DP values are RAW motor values, not calibrated lift or battery readings.
 */
const diagnosticAttrs = {
    p3_uart_rx_bytes: 0xf100,
    p3_uart_valid_frames: 0xf101,
    p3_uart_rejected_frames: 0xf102,
    p3_uart_dropped_bytes: 0xf103,
    p3_uart_accepted_reports: 0xf104,
    p3_uart_last_command: 0xf105,
    p3_uart_last_format: 0xf106,
    p3_dp1_raw: 0xf107,
    p3_dp3_raw: 0xf108,
    p3_dp13_raw: 0xf109,
    p3_uart_rx_ready: 0xf10a,
    p3_uart_startup_requests_sent: 0xf10b,
    p3_uart_startup_finished: 0xf10c,
};

const p3DiagnosticsFromZigbee = {
    cluster: 'genBasic',
    type: ['readResponse', 'attributeReport'],
    convert: (model, msg) => {
        const result = {};
        for (const [key, id] of Object.entries(diagnosticAttrs)) {
            const raw = msg.data[id] ?? msg.data[String(id)] ?? msg.data[`0x${id.toString(16)}`];
            if (typeof raw !== 'number') continue;
            // 0xFF explicitly represents "not observed" for these fields.
            result[key] = (['p3_dp1_raw', 'p3_dp3_raw', 'p3_dp13_raw',
                            'p3_uart_last_command', 'p3_uart_last_format'].includes(key)
                            && raw === 255) ? null : raw;
        }
        return result;
    },
};

const p3DiagnosticsRefresh = {
    key: ['p3_diagnostics_refresh'],
    convertSet: async (entity, key, value) => {
        if (value !== 'refresh') {
            throw new Error('P3-NRF52840: only p3_diagnostics_refresh=refresh is supported');
        }
        // ZBOSS cannot fit 13 custom Basic attributes in one Read Response:
        // it returned INSUFFICIENT_SPACE for 0xF10C after the first 12.
        // Read eight and five attributes separately, preserving the same
        // p3_diagnostics_refresh MQTT API. Neither request sends motor UART.
        const ids = Object.values(diagnosticAttrs);
        await entity.read('genBasic', ids.slice(0, 8));
        await entity.read('genBasic', ids.slice(8));
    },
};

export default {
    zigbeeModel: ['P3-NRF52840'],
    model: 'P3-NRF52840',
    vendor: 'LocalDIY',
    description: 'Battery-powered Tuya P3 roller blind on nice!nano nRF52840 (prototype)',
    fromZigbee: [fz.cover_position_tilt, fz.battery, p3DiagnosticsFromZigbee],
    toZigbee: [tz.cover_state, tz.cover_position_tilt, p3DiagnosticsRefresh],
    exposes: [
        e.cover_position(), e.battery(),
        ...Object.keys(diagnosticAttrs).map((key) =>
            e.numeric(key, ea.STATE).withCategory('diagnostic')
        ),
        e.enum('p3_diagnostics_refresh', ea.SET, ['refresh'])
            .withDescription('Read P3 UART diagnostics (including one-time non-movement startup queries).'),
    ],
    endpoint: () => ({default: 10}),
    configure: async (device, coordinatorEndpoint) => {
        const endpoint = device.getEndpoint(10);
        if (!endpoint) {
            throw new Error('P3-NRF52840: required Zigbee endpoint 10 missing');
        }
        await reporting.bind(endpoint, coordinatorEndpoint, [
            'genPowerCfg', 'closuresWindowCovering',
        ]);
        await reporting.batteryPercentageRemaining(endpoint);
        await reporting.currentPositionLiftPercentage(endpoint);
    },
};
