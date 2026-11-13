/* Experimental v42.0-29 passive UART diagnostics.
 * Never sends UART motor commands. Diagnostic reads use Zigbee Basic cluster.
 */
import * as fz from 'zigbee-herdsman-converters/converters/fromZigbee';
import * as tz from 'zigbee-herdsman-converters/converters/toZigbee';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';
import * as reporting from 'zigbee-herdsman-converters/lib/reporting';

const e = exposes.presets;
const ea = exposes.access;
const diagnosticAttributes = {
    rx_header_count: 0xF137,
    rx_error_count: 0xF138,
    rx_first4: 0xF139,
    rx_next4: 0xF13A,
    rx_last_error: 0xF13B,
    tuya_frames_ok: 0xF133,
    tuya_frames_rejected: 0xF134,
    stress_rx_bytes: 0xF12C,
    stress_elapsed_s: 0xF132,
    uart_test_phase: 0xF121,
    diag_last_command: 0xF10B,
    diag_last_format: 0xF10C,
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
    description: 'P3 nRF52840 v42.0-29 passive UART RX diagnostics',
    fromZigbee: [fz.cover_position_tilt, fz.battery, diagnosticFromZigbee],
    toZigbee: [tz.cover_state, tz.cover_position_tilt, diagnosticToZigbee],
    exposes: [
        e.cover_position(), e.battery(),
        ...Object.keys(diagnosticAttributes).map((name) =>
            e.numeric(name, ea.STATE_GET).withDescription(
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
