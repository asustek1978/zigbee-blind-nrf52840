/* Experimental v42.0-43: full UARTE0 RX during motor movement; watchdog cannot be renewed,
 * v42.0-37 fast Zigbee polling and low-power hybrid TIMER3/UARTE0 RX +
 * independent UARTE1 motor TX only for explicitly requested Zigbee cover
 * open/close/stop/position. NO startup motor traffic or calibration.
 * Battery percentage: bridge verified P3 MCU DP13 (0..100) to Zigbee2MQTT
 * Battery when ZCL PowerConfig remains unknown; no fabricated voltage.
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
    rx_bad_stop_gpio_last_ms: 0xF14E,
    rx_bad_stop_chained_last_ms: 0xF14F,
    rx_handoff_unprepared: 0xF150,
    rx_handoff_rxto_timeout: 0xF151,
    rx_handoff_amount_oversize: 0xF152,
    rx_handoff_no_rxstarted: 0xF153,
    rx_handoff_zero_amount: 0xF154,
    rx_handoff_last_ms: 0xF155,
    rx_handoff_last_reason: 0xF156,
    rx_hw_framing_events: 0xF157,
    rx_hw_overrun_events: 0xF158,
    rx_hw_break_events: 0xF159,
    rx_hw_parity_events: 0xF15A,
    rx_hw_last_ms: 0xF15B,
    rx_hw_last_amount: 0xF15C,
    hw_direct_active: 0xF15D,
    hw_direct_enters: 0xF15E,
    hw_direct_exits: 0xF15F,
    hw_direct_bytes: 0xF160,
    hw_direct_restarts: 0xF161,
    hw_direct_max_timeouts: 0xF162,
    hw_direct_prearm_timeouts: 0xF163,
    hw_direct_last_switch_ms: 0xF164,
    hw_direct_hw_errors: 0xF165,
    hw_direct_empty_dma: 0xF166,
    hw_direct_rxto_timeouts: 0xF167,
    hw_direct_session_start_ms: 0xF168,
    hw_direct_motion_lockout: 0xF169,
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
    rx_bad_stop_gpio_last_ms: 'Last software STOP-bit failure from GPIO start (ms uptime)',
    rx_bad_stop_chained_last_ms: 'Last software STOP-bit failure in chained byte (ms uptime)',
    rx_handoff_unprepared: 'DMA handoffs attempted while UARTE0 was not prepared (reason 1)',
    rx_handoff_rxto_timeout: 'UARTE0 STOPRX waits that timed out (reason 2)',
    rx_handoff_amount_oversize: 'DMA RXD.AMOUNT values exceeding buffer size (reason 3)',
    rx_handoff_no_rxstarted: 'DMA handoffs without RXSTARTED (reason 4; takes priority over zero amount)',
    rx_handoff_zero_amount: 'DMA handoffs with zero bytes but RXSTARTED set (reason 5)',
    rx_handoff_last_ms: 'Last DMA handoff error timestamp in ms since boot',
    rx_handoff_last_reason: 'Last DMA handoff reason: 1 not prepared, 2 RXTO timeout, 3 oversize, 4 no RXSTARTED, 5 zero amount',
    rx_hw_framing_events: 'UARTE ERRORSRC bit 0x4 FRAMING events',
    rx_hw_overrun_events: 'UARTE ERRORSRC bit 0x1 OVERRUN events',
    rx_hw_break_events: 'UARTE ERRORSRC bit 0x8 BREAK events',
    rx_hw_parity_events: 'UARTE ERRORSRC bit 0x2 PARITY events',
    rx_hw_last_ms: 'Last UARTE0 hardware error timestamp (ms uptime)',
    rx_hw_last_amount: 'RXD.AMOUNT snapshot at last UARTE0 error event',
    hw_direct_active: '1=full hardware UARTE0 receive active; 0=idle hybrid first-byte RX',
    hw_direct_enters: 'Number of transitions into full hardware RX',
    hw_direct_exits: 'Number of transitions back to low-power hybrid RX',
    hw_direct_bytes: 'Bytes received through full hardware UARTE0 mode',
    hw_direct_restarts: 'DMA burst re-arms while motor hardware RX remained active',
    hw_direct_max_timeouts: 'Hardware RX watchdog exits after 120 seconds without confirmed STOP',
    hw_direct_prearm_timeouts: 'Motor commands sent before hardware RX was ready; RX fallback may apply',
    hw_direct_last_switch_ms: 'Uptime ms of last entry or exit from full hardware RX',
    hw_direct_hw_errors: 'UARTE0 ERRORSRC events received while full hardware RX mode was active',
    hw_direct_empty_dma: 'Direct-RX bursts with RXDRDY but zero DMA bytes after STOPRX',
    hw_direct_rxto_timeouts: 'STOPRX->RXTO timeouts specifically in full hardware RX mode',
    hw_direct_session_start_ms: 'Uptime ms when the current/last full hardware RX session actually started; absolute 120 s watchdog anchor',
    hw_direct_motion_lockout: '1=after 120 s watchdog, ignore repeated MCU OPEN/CLOSE until fresh STOP or explicit Zigbee command',
};
const diagnosticFromZigbee = {
    cluster: 'genBasic',
    type: ['readResponse', 'attributeReport'],
    convert: (model, msg) => {
        const result = {};
        for (const [name, id] of Object.entries(diagnosticAttributes)) {
            if (msg.data[id] !== undefined) result[name] = msg.data[id];
        }
        // DP13 is the original P3 MCU's battery estimate in percent.
        // 0xFF means unknown; never display it as 255% or invent a reading.
        const battery = msg.data[diagnosticAttributes.motor_dp_battery];
        if (Number.isInteger(battery) && battery >= 0 && battery <= 100) {
            result.battery = battery;
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
// Allow the Battery refresh control to read the latest DP13 without firmware changes.
// A single Zigbee2MQTT GET refreshes all firmware diagnostic attributes.
// Split reads into small ZCL frames to avoid oversized responses; do not send
// motor commands or reset/modify any diagnostic counters.
const refreshAllDiagnosticsToZigbee = {
    key: ['refresh_all_diagnostics'],
    convertGet: async (entity) => {
        const attributes = Object.values(diagnosticAttributes);
        const batchSize = 5;
        for (let i = 0; i < attributes.length; i += batchSize) {
            await entity.read('genBasic', attributes.slice(i, i + batchSize));
        }
    },
};
const batteryToZigbee = {
    key: ['battery'],
    convertGet: async (entity) => {
        await entity.read('genBasic', [diagnosticAttributes.motor_dp_battery]);
    },
};
export default {
    zigbeeModel: ['P3-NRF52840'],
    model: 'P3-NRF52840',
    vendor: 'LocalDIY',
    description: 'P3 nRF52840 v42.0-43 fixed 120s UART RX watchdog, motion lockout, bulk refresh; bench UNVERIFIED',
    fromZigbee: [fz.cover_position_tilt, fz.battery, diagnosticFromZigbee],
    toZigbee: [tz.cover_state, tz.cover_position_tilt, diagnosticToZigbee, refreshAllDiagnosticsToZigbee, batteryToZigbee],
    exposes: [
        e.cover_position(), e.battery(),
        e.numeric('refresh_all_diagnostics', ea.STATE_GET).withDescription(
            'Click refresh to update all diagnostics in small Zigbee Basic read batches; no motor command is sent'
        ),
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
        // One-time initial snapshot; future refreshes use the Battery GET.
        await endpoint.read('genBasic', [diagnosticAttributes.motor_dp_battery]);
    },
};
