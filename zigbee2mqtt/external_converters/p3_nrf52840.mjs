/* Zigbee2MQTT external converter for the custom Zigbee End Device.
 * The firmware advertises model P3-NRF52840 and endpoint 10.
 * UART transmission remains disabled by default in firmware.
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
    description: 'Battery-powered Tuya P3 roller blind on nice!nano nRF52840 (prototype)',
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
