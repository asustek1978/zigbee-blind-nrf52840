/* P3-NRF52840 stable Zigbee2MQTT converter.
 * User-facing controls: cover position/state and battery only.
 * Diagnostics and calibration commands intentionally not exposed.
 * Physical calibration remains implemented in device firmware.
 */
import * as fz from 'zigbee-herdsman-converters/converters/fromZigbee';
import * as tz from 'zigbee-herdsman-converters/converters/toZigbee';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';
import * as reporting from 'zigbee-herdsman-converters/lib/reporting';

const e = exposes.presets;

export default {
    zigbeeModel: ['P3-NRF52840'],
    model: 'P3-NRF52840',
    vendor: 'LocalDIY',
    description: 'P3 roller blind with nRF52840 Zigbee End Device; motor control, position and battery',
    fromZigbee: [fz.cover_position_tilt, fz.battery],
    toZigbee: [tz.cover_state, tz.cover_position_tilt],
    exposes: [e.cover_position(), e.battery()],
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
