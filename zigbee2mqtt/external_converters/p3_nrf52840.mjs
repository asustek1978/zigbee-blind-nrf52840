/* Experimental v42.0-40: classify software start/STOP failures without changing
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
    rx_gpio_start_events: 0xF141,
    rx_last_gpio_start_ms: 0xF142,
    rx_bad_stop_gpio: 0xF143,
    rx_bad_stop_chained: 0xF144,
    rx_bad_stop_reported_idle: 0xF145,
    rx_bad_stop_reported_moving: 0xF146,
    rx_bad_stop_motion_unknown: 0xF147,
    rx_last_bad_stop_ms: 0xF148,
    rx_last_bad_stop_byte: 0xF149,
    rx_reported_motion: 0xF14A,
    rx_gpio_starts_reported_idle: 0xF14B,
    rx_gpio_starts_reported_moving: 0xF14C,
    rx_gpio_starts_motion_unknown: 0xF14D,
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
    rx_gpio_start_events: 'Number of accepted GPIO P0.17 falling edges while software UART idle',
    rx_last_gpio_start_ms: 'Uptime in ms of last GPIO start edge; coarse time, not bit timing',
    rx_bad_stop_gpio: 'Bad software UART STOP bit on first byte triggered by GPIO edge',
    rx_bad_stop_chained: 'Bad software UART STOP bit on subsequent chained software byte',
    rx_bad_stop_reported_idle: 'Bad STOP bits while last MCU-reported DP1=1 (STOP); may be stale',
    rx_bad_stop_reported_moving: 'Bad STOP bits while last MCU-reported DP1=0/2 (OPEN/CLOSE); may be stale',
    rx_bad_stop_motion_unknown: 'Bad STOP bits before first valid MCU DP1 report',
    rx_last_bad_stop_ms: 'Uptime in ms at last bad STOP bit (zero means none yet)',
    rx_last_bad_stop_byte: 'Software-sampled 8 data bits from most recent bad STOP event (not a valid byte)',
    rx_reported_motion: 'Last validated MCU DP1: 0=open, 1=stop, 2=close, 3=unknown; may lag actual movement',
    rx_gpio_starts_reported_idle: 'Accepted RX GPIO falling edges while last reported MCU DP1=1 STOP',
    rx_gpio_starts_reported_moving: 'Accepted RX GPIO falling edges while last reported MCU DP1=0/2 OPEN/CLOSE',
    rx_gpio_starts_motion_unknown: 'Accepted RX GPIO falling edges before first validated MCU DP1',
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
    description: 'P3 nRF52840 v42.0-40 start/stop-bit/motion diagnostics; 1s Zigbee poll, hybrid low-power RX (bench)',
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
