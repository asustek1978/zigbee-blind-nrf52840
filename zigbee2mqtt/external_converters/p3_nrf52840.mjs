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
    saadc_vddh_mv: 0xF16A,
    saadc_vdd_mv: 0xF16B,
    saadc_valid_mask: 0xF16C,
    saadc_samples: 0xF16D,
    saadc_last_ms: 0xF16E,
    saadc_last_error: 0xF16F,
    rx_idle_fault_episodes: 0xF170,
    rx_bad_stop_post_exit_30s: 0xF171,
    rx_bad_stop_late_or_no_exit: 0xF172,
    rx_last_bad_stop_gpio_age_ms: 0xF173,
    hw_direct_last_exit_ms: 0xF174,
    position_wake_count: 0xF175,
    position_wake_suppressed: 0xF176,
    position_wake_last_ms: 0xF177,
    position_wake_latched: 0xF178,
    cmd_queued: 0xF179,
    cmd_tx_ok: 0xF17A,
    cmd_tx_failed: 0xF17B,
    cmd_mcu_report: 0xF17C,
    cmd_position_reported: 0xF17D,
    cmd_stop_reported: 0xF17E,
    cmd_timeout_no_report: 0xF17F,
    cmd_timeout_no_target: 0xF180,
    cmd_superseded: 0xF181,
    cmd_pending: 0xF182,
    cmd_last_kind: 0xF183,
    cmd_last_target: 0xF184,
    cmd_last_result: 0xF185,
    cmd_last_report_delay_ms: 0xF186,
    ppi_enabled: 0xF187,
    ppi_starts: 0xF188,
    ppi_fallbacks: 0xF189,
    ppi_late_irqs: 0xF18A,
    session_last_id: 0xF18B,
    session_last_bytes: 0xF18C,
    session_last_valid_frames: 0xF18D,
    session_last_stop: 0xF18E,
    session_last_goal: 0xF18F,
    session_last_target: 0xF190,
    session_last_dp3: 0xF191,
    session_last_silence_ms: 0xF192,
    session_last_reason: 0xF193,
    session_active_id: 0xF194,
    session_total: 0xF195,
    session_stop_guard_exits: 0xF196,
    session_watchdog_exits: 0xF197,
    session_watchdog_goal_no_stop: 0xF198,
    session_goal_silence_exits: 0xF199,
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
/* v42.0-49 compact bench UI: 30 read-only power/UART/PPI/session fields needed to compare
 * low-power RX, PPI reliability, and the DP3-without-STOP watchdog issue.
 * The complete firmware attribute map stays available for manual GET and
 * archived diagnostics; do not remove it or the working v41.x mapping.
 */
const visibleBenchDiagnostics = [
    'uart_rx_ready',
    'tuya_frames_ok',
    'tuya_frames_rejected',
    'rx_framing_errors',
    'rx_ring_overflows',
    'rx_bad_stop_post_exit_30s',
    'hw_direct_active',
    'hw_direct_enters',
    'hw_direct_max_timeouts',
    /* SAADC supply diagnostics; VDDH is NOT confirmed as battery voltage. */
    'saadc_vddh_mv',
    'saadc_vdd_mv',
    'saadc_valid_mask',
    'ppi_enabled',
    'ppi_starts',
    'ppi_fallbacks',
    'ppi_late_irqs',
    'session_last_id',
    'session_last_bytes',
    'session_last_valid_frames',
    'session_last_stop',
    'session_last_goal',
    'session_last_target',
    'session_last_dp3',
    'session_last_silence_ms',
    'session_last_reason',
    'session_total',
    'session_stop_guard_exits',
    'session_watchdog_exits',
    'session_watchdog_goal_no_stop',
    'session_goal_silence_exits',
];

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
    saadc_vddh_mv: 'Offset-calibrated/16x nRF52840 VDDH rail (mV) via internal SAADC VDDH/5; NOT verified as battery voltage on HW-954',
    saadc_vdd_mv: 'Measured nRF52840 VDD supply rail in millivolts (not necessarily battery voltage)',
    saadc_valid_mask: 'SAADC successful readings: bit 0=VDDH; bit 1=VDD; 0=no valid reading',
    saadc_samples: 'Number of SAADC diagnostic sample attempts (every 10 min after initial 30 s)',
    saadc_last_ms: 'Uptime ms of last ADC rail sample attempt',
    saadc_last_error: 'Last ADC error code as unsigned two-complement U32; 0=success',
    rx_idle_fault_episodes: 'Groups of software-UART bad STOP bits separated by >250 ms; useful to distinguish noise bursts from isolated errors',
    rx_bad_stop_post_exit_30s: 'Software bad STOP bits within first 30 s after a full UARTE0 RX exit',
    rx_bad_stop_late_or_no_exit: 'Software bad STOP bits outside 30 s after full UARTE0 RX exit',
    rx_last_bad_stop_gpio_age_ms: 'Milliseconds from last accepted GPIO falling edge to most recent software bad STOP bit',
    hw_direct_last_exit_ms: 'Uptime ms when full hardware UART RX last returned to hybrid idle',
    position_wake_count: 'Hardware RX re-arms after watchdog only when a changed valid DP3 follows >=10 s without position changes; at most once until a fresh STOP or command',
    position_wake_suppressed: 'Changed valid DP3 reports after watchdog that were not eligible for hardware RX re-arm',
    position_wake_last_ms: 'Uptime ms of most recent accepted different DP3 position',
    position_wake_latched: '1=position-triggered RX re-arm already used; additional re-arm disabled until fresh STOP or motor command',
    cmd_queued: 'Commands accepted by firmware UART queue (may later be purged by STOP)',
    cmd_tx_ok: 'Commands successfully serialized on motor UART; NOT proof of execution',
    cmd_tx_failed: 'Commands that failed physical UART TX (no automatic retry)',
    cmd_mcu_report: 'Successful TX commands followed by at least one fresh valid MCU DP1/DP3 report within 180 s; NOT protocol ACK',
    cmd_position_reported: 'Successful OPEN/CLOSE/position TX where a subsequent valid MCU DP3 reports target within ±1% before 180 s; NOT physically independently verified',
    cmd_stop_reported: 'STOP TX followed by a fresh valid MCU DP1=STOP report within 180 s',
    cmd_timeout_no_report: 'Successful TX commands with no valid MCU DP1/DP3 report within 180 s (one-shot passive timeout)',
    cmd_timeout_no_target: 'Successful TX commands with MCU DP1/DP3 response but no matching DP3 target or STOP confirmation within 180 s',
    cmd_superseded: 'Pending command tracking superseded by a newer successfully transmitted command; not a failure verdict',
    cmd_pending: '1=tracking a successfully transmitted command still awaiting outcome; 0=none',
    cmd_last_kind: 'Last successfully transmitted kind: 1=OPEN 2=STOP 3=CLOSE 4=POSITION',
    cmd_last_target: 'Last successfully transmitted requested position 0..100; 255=STOP/unspecified',
    cmd_last_result: '0=none/pending; 1=target reported; 2=STOP reported; 3=no MCU report timeout; 4=no target timeout; 5=superseded; 6=UART TX failed',
    cmd_last_report_delay_ms: 'Delay from completed UART TX to first valid MCU DP1/DP3 report for the latest TX; UINT32_MAX means not yet reported',
    ppi_enabled: '1=GPIOTE IN event and free PPI channel found; 0=original GPIO IRQ timing fallback',
    ppi_starts: 'Number of first-byte TIMER3 starts synchronized by GPIOTE/PPI',
    ppi_fallbacks: 'Original GPIO IRQ start used because PPI was unavailable or no timer progress was visible',
    ppi_late_irqs: 'GPIOTE GPIO callback reached after first 156us comparison deadline; timing risk',
    session_last_id: 'Last completed full-UARTE0 RX session ID, increments from 1 since boot',
    session_last_bytes: 'Bytes observed by UARTE0 EasyDMA during the last completed session; includes bytes subsequently dropped',
    session_last_valid_frames: 'Tuya parser-valid frames attributed to the last completed full-UARTE0 RX session',
    session_last_stop: '1=fresh accepted MCU DP1=STOP received in last session; 0=no such report',
    session_last_goal: '1=fresh accepted MCU DP3 exactly equaled last successfully transmitted target during session',
    session_last_target: 'Last successfully transmitted movement target 0..100 during session; 255=no target',
    session_last_dp3: 'Last valid MCU DP3 0..100 observed during session; 255=none',
    session_last_silence_ms: 'Elapsed ms since last RXDRDY observed at session exit; not guaranteed continuous wire silence',
    session_last_reason: 'Last session exit cause: 1=STOP guard, 2=watchdog, 3=other, 4=shutdown, 5=fresh DP3 target + 3s silence',
    session_active_id: 'Current full-UARTE0 session ID; 0 when no session active',
    session_total: 'Total completed sessions published since boot',
    session_stop_guard_exits: 'Cumulative full-UARTE0 exits after confirmed fresh DP1=STOP guard',
    session_watchdog_exits: 'Cumulative full-UARTE0 exits after 120-second watchdog',
    session_watchdog_goal_no_stop: 'Cumulative watchdog exits with DP3 matching transmitted target but no accepted DP1=STOP',
    session_goal_silence_exits: 'Cumulative hardware RX exits after a fresh validated target DP3 and 3 seconds without UART bytes',
};
/* Zigbee2MQTT loads ONE converter for all P3-NRF52840 devices.
 * Older kitchen motors have SWBuildID=41.3 and do NOT implement v42 Basic
 * attributes. Detect per-device firmware, not model name or IEEE address.
 * If SWBuildID is unknown, default to the legacy/minimal UI.
 */
const benchSupported = (device) => {
    const match = /^42\.0-(\d+)$/.exec(device?.softwareBuildID || '');
    return match !== null && Number(match[1]) >= 48;
};
const goalCounterSupported = (device) => {
    const match = /^42\.0-(\d+)$/.exec(device?.softwareBuildID || '');
    return match !== null && Number(match[1]) >= 49;
};
const benchDiagnosticsFor = (device) =>
    visibleBenchDiagnostics.filter((name) =>
        name !== 'session_goal_silence_exits' || goalCounterSupported(device));
const assertBenchSupported = (entity, meta) => {
    const device = meta?.device ||
        (typeof entity.getDevice === 'function' ? entity.getDevice() : undefined);
    if (!benchSupported(device)) {
        throw new Error('P3 diagnostic requires SWBuildID >=42.0-48; v41.3 kitchen motors do not support v42 attributes');
    }
    return device;
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
    convertGet: async (entity, key, meta) => {
        /* Legacy 41.3 has DP13 battery support but not v42 diagnostics. */
        if (key !== 'motor_dp_battery') {
            const device = assertBenchSupported(entity, meta);
            if (key === 'session_goal_silence_exits' && !goalCounterSupported(device)) {
                throw new Error('P3 goal-silence counter requires SWBuildID 42.0-49');
            }
        }
        await entity.read('genBasic', [diagnosticAttributes[key]]);
    },
};
// Allow the Battery refresh control to read the latest DP13 without firmware changes.
// A single Zigbee2MQTT GET refreshes all firmware diagnostic attributes.
// Split reads into small ZCL frames to avoid oversized responses; do not send
// motor commands or reset/modify any diagnostic counters.
/* Compact manual refresh of 29 (v48) or 30 (v49) bench fields.
 * This uses 6 ZCL Basic read batches, not a full diagnostics sweep.
 * Not called automatically when an older kitchen v41.3 device joins.
 */
const refreshBenchDiagnosticsToZigbee = {
    key: ['refresh_bench_diagnostics'],
    convertGet: async (entity, key, meta) => {
        const device = assertBenchSupported(entity, meta);
        const attributes = benchDiagnosticsFor(device).map((name) => diagnosticAttributes[name]);
        const batchSize = 5;
        for (let i = 0; i < attributes.length; i += batchSize) {
            await entity.read('genBasic', attributes.slice(i, i + batchSize));
        }
    },
};
const refreshAllDiagnosticsToZigbee = {
    key: ['refresh_all_diagnostics'],
    convertGet: async (entity, key, meta) => {
        const device = assertBenchSupported(entity, meta);
        const attributes = Object.entries(diagnosticAttributes)
            .filter(([name]) => name !== 'session_goal_silence_exits' || goalCounterSupported(device))
            .map(([, id]) => id);
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
    description: 'P3 nRF52840 v41.3-compatible / v42.0-49 goal-silence exit bench; battery path unverified',
    fromZigbee: [fz.cover_position_tilt, fz.battery, diagnosticFromZigbee],
    toZigbee: [tz.cover_state, tz.cover_position_tilt, diagnosticToZigbee, refreshBenchDiagnosticsToZigbee, refreshAllDiagnosticsToZigbee, batteryToZigbee],
    /* Device-scoped exposures: 41.3 gets cover+battery only; v42.0-48+
     * gets the bench controls. Never offer unknown ZCL IDs on 41.3.
     */
    exposes: (device) => [
        e.cover_position(), e.battery(),
        ...(benchSupported(device) ? [
            e.numeric('refresh_bench_diagnostics', ea.STATE_GET).withDescription(
                goalCounterSupported(device)
                    ? 'Manually read 30 power/UART/PPI/session counters in 6 Zigbee batches; no motor command'
                    : 'Manually read 29 power/UART/PPI/session counters in 6 Zigbee batches; no motor command'
            ),
            ...benchDiagnosticsFor(device).map((name) => {
                const expose = e.numeric(name, ea.STATE_GET).withDescription(
                    diagnosticDescriptions[name] ||
                        'Read-only diagnostic from Zigbee Basic cluster'
                );
                return (name === 'saadc_vddh_mv' || name === 'saadc_vdd_mv')
                    ? expose.withUnit('mV') : expose;
            }),
        ] : []),
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
