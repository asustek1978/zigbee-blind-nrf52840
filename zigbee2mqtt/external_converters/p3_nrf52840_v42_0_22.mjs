/* P3-NRF52840 v42.0-22 UART 20-minute stress Zigbee2MQTT converter.
 * User-facing controls: cover position/state and battery only.
 * Diagnostics and calibration commands intentionally not exposed.
 * Physical calibration remains implemented in device firmware.
 */
import * as fz from 'zigbee-herdsman-converters/converters/fromZigbee';
import * as tz from 'zigbee-herdsman-converters/converters/toZigbee';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';
import * as reporting from 'zigbee-herdsman-converters/lib/reporting';

const e = exposes.presets;

// v42.0-22: private Basic-cluster read-only UART stress-test attributes.
const ea = exposes.access;
const uartDiagnostics = {
    uart_init_rc: 0xF11F,
    uart_deinit_rc: 0xF120,
    uart_test_phase: 0xF121,
    uart_resume_rc: 0xF122,
    uart_suspend2_rc: 0xF123,
    uart_rx_rc: 0xF124,
    uart_rx_byte: 0xF125,
    uart_rx_attempts: 0xF126,
    uart_rx_wait_ms: 0xF127,
    stress_cycles: 0xF128,
    stress_ok: 0xF129,
    stress_failed: 0xF12A,
    stress_tx_bytes: 0xF12B,
    stress_rx_bytes: 0xF12C,
    stress_mismatch: 0xF12D,
    stress_rx_timeout: 0xF12E,
    stress_resume_err: 0xF12F,
    stress_suspend_err: 0xF130,
    stress_max_rx_ms: 0xF131,
    stress_elapsed_s: 0xF132,
};
const fromUartDiagnostics = {
    cluster: 'genBasic',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        const result = {};
        for (const [name, id] of Object.entries(uartDiagnostics)) {
            const value = msg.data[id] ?? msg.data[String(id)] ?? msg.data[`0x${id.toString(16)}`];
            if (typeof value === 'number') {
                result[name] = name.endsWith('_rc') && value > 0x7FFFFFFF ? value - 0x100000000 : value;
            }
        }
        return result;
    },
};
const getUartDiagnostics = {
    key: Object.keys(uartDiagnostics),
    convertGet: async (entity, key) => {
        await entity.read('genBasic', [uartDiagnostics[key]]);
    },
};


export default {
    zigbeeModel: ['P3-NRF52840'],
    model: 'P3-NRF52840',
    vendor: 'LocalDIY',
    description: 'P3 roller blind with nRF52840 Zigbee End Device; motor control, position and battery',
    fromZigbee: [fz.cover_position_tilt, fz.battery, fromUartDiagnostics],
    toZigbee: [tz.cover_state, tz.cover_position_tilt, getUartDiagnostics],
    exposes: [
        e.cover_position(), e.battery(),
        e.numeric('uart_init_rc', ea.STATE_GET),
        e.numeric('uart_deinit_rc', ea.STATE_GET),
        e.numeric('uart_test_phase', ea.STATE_GET),
        e.numeric('uart_resume_rc', ea.STATE_GET),
        e.numeric('uart_suspend2_rc', ea.STATE_GET),
        e.numeric('uart_rx_rc', ea.STATE_GET),
        e.numeric('uart_rx_byte', ea.STATE_GET),
        e.numeric('uart_rx_attempts', ea.STATE_GET),
        e.numeric('uart_rx_wait_ms', ea.STATE_GET),
        e.numeric('stress_cycles', ea.STATE_GET),
        e.numeric('stress_ok', ea.STATE_GET),
        e.numeric('stress_failed', ea.STATE_GET),
        e.numeric('stress_tx_bytes', ea.STATE_GET),
        e.numeric('stress_rx_bytes', ea.STATE_GET),
        e.numeric('stress_mismatch', ea.STATE_GET),
        e.numeric('stress_rx_timeout', ea.STATE_GET),
        e.numeric('stress_resume_err', ea.STATE_GET),
        e.numeric('stress_suspend_err', ea.STATE_GET),
        e.numeric('stress_max_rx_ms', ea.STATE_GET),
        e.numeric('stress_elapsed_s', ea.STATE_GET),
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
