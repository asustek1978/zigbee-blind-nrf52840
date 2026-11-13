/* Experimental v42.0-39: isolate software RX error sources while preserving
 * v42.0-37 fast Zigbee polling and low-power hybrid TIMER3/UARTE0 RX +
 * independent UARTE1 motor TX only for explicitly requested Zigbee cover
 * open/close/stop/position. NO startup motor traffic or calibration.
 */
import * as fz from 'zigbee-herdsman-converters/converters/fromZigbee';
import * as tz from 'zigbee-herdsman-converters/converters/toZigbee';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';
import * as reporting from 'zigbee-herdsman-converters/lib/reporting';

const e = exposes.presets;
const ea = exposes.access;
const diagnosticAttributes = {
    tx_frames: 0xF113,
    uart_tx_duration_ms: 0xF115,
    zigbee_motor_commands: 0xF117,
    uart_rx_ready: 0xF10A,
    motor_report_count: 0xF104,
    motor_dp_position: 0xF108,
    motor_dp_battery: 0xF109,
    tx_failed_bytes: 0xF103,
    uart_queue_to_tx_ms: 0xF114,
    uart_rx_after_tx_ms: 0xF116,
    rx_header_count: 0xF137,
    rx_error_count: 0xF138,
    rx_first4: 0xF139,
    rx_next4: 0xF13A,
    rx_last_error: 0xF13B,
    rx_framing_errors: 0xF13C,
    rx_ring_overflows: 0xF13D,
    rx_software_starts: 0xF13E,
    rx_software_nonheader: 0xF13F,
    uart_last_errorsrc: 0xF140,
    tuya_frames_ok: 0xF133,
    tuya_frames_rejected: 0xF134,
    stress_rx_bytes: 0xF12C,
    uart_wake_count: 0xF135,
    uart_dma_bytes: 0xF136,
    uart_handoff_errors: 0xF12A,
    uart_hardware_errors: 0xF12F,
    stress_elapsed_s: 0xF132,
    uart_test_phase: 0xF121,
    diag_last_command: 0xF105,
    diag_last_format: 0xF106,
};
const diagnosticDescriptions = {
    rx_framing_errors: 'GPIO/TIMER3 software UART invalid STOP bit; may include noise/false starts',
    rx_ring_overflows: 'Bytes dropped because the RX ring buffer was full (software or EasyDMA)',
    rx_software_starts: 'First-bit sampling attempts by TIMER3; not necessarily valid Tuya frames',
    rx_software_nonheader: 'Software-decoded bytes other than 0x55; possible partial or noisy traffic',
    uart_last_errorsrc: 'Last raw nRF UARTE0 ERRORSRC register (0 if no hardware error)',
};
const diagnosticFromZigbee = {
    cluster: 'genBasic',
    type: ['readResponse', 'attributeReport'],
    convert: (model, msg) => {
        const result = {};
        for (const [name, id] of Object.entries(diagnosticAttributes)) {
            if (msg.data[id] !== undefined) result[name] = msg.data[id];
        }
        return result;
    },
};
const diagnosticToZigbee = {
    key: Object.keys(diagnosticAttributes),
    convertGet: async (entity, key) => {
        await entity.read('genBasic', [diagnosticAttributes[key]]);
    },
};
export default {
    zigbeeModel: ['P3-NRF52840'],
    model: 'P3-NRF52840',
    vendor: 'LocalDIY',
    description: 'P3 nRF52840 v42.0-39 1s Zigbee parent poll and hybrid RX error breakdown (bench)',
    fromZigbee: [fz.cover_position_tilt, fz.battery, diagnosticFromZigbee],
    toZigbee: [tz.cover_state, tz.cover_position_tilt, diagnosticToZigbee],
    exposes: [
        e.cover_position(), e.battery(),
        ...Object.keys(diagnosticAttributes).map((name) =>
            e.numeric(name, ea.STATE_GET).withDescription(
                diagnosticDescriptions[name] ||
                    'Read-only diagnostic from Zigbee Basic cluster; first4/next4 are little-endian packed bytes'
            )),
    ],
    endpoint: () => ({default: 10}),
    configure: async (device, coordinatorEndpoint) => {
        const endpoint = device.getEndpoint(10);
        if (!endpoint) throw new Error('P3-NRF52840: endpoint 10 missing');
        await reporting.bind(endpoint, coordinatorEndpoint, ['genPowerCfg', 'closuresWindowCovering']);
        await reporting.batteryPercentageRemaining(endpoint);
        await reporting.currentPositionLiftPercentage(endpoint);
    },
};
