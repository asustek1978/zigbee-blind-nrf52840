/* The converter module uses external ZHC imports; exercise its pure
 * SWBuildID gate without importing the Zigbee runtime on a CI host.
 */
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';

const js = fs.readFileSync(new URL('../zigbee2mqtt/external_converters/p3_nrf52840.js', import.meta.url), 'utf8');
const mjs = fs.readFileSync(new URL('../zigbee2mqtt/external_converters/p3_nrf52840.mjs', import.meta.url), 'utf8');
assert.equal(js, mjs, 'both deployed converter variants must match');

const source = js.match(/const benchSupported = \(device\) => \{[\s\S]*?\n\};/);
assert.ok(source, 'version check must exist');
const supported = vm.runInNewContext(source[0] + '\nbenchSupported;', {});
assert.equal(supported({softwareBuildID: '41.3'}), false);
assert.equal(supported({softwareBuildID: '42.0-47'}), false);
assert.equal(supported({softwareBuildID: '42.0-48'}), true);
assert.equal(supported({softwareBuildID: '42.0-49'}), true);
assert.equal(supported({softwareBuildID: '42.0-50'}), true);
assert.equal(supported({}), false);
assert.equal(supported(undefined), false);

const goalSource = js.match(/const goalCounterSupported = \(device\) => \{[\s\S]*?\n\};/);
assert.ok(goalSource, 'goal counter version check must exist');
const goalSupported = vm.runInNewContext(goalSource[0] + '\ngoalCounterSupported;', {});
assert.equal(goalSupported({softwareBuildID: '41.3'}), false);
assert.equal(goalSupported({softwareBuildID: '42.0-48'}), false);
assert.equal(goalSupported({softwareBuildID: '42.0-49'}), true);
assert.equal(goalSupported({softwareBuildID: '42.0-50'}), true);

const calibrationSource = js.match(/const calibrationSupported = \(device\) => \{[\s\S]*?\n\};/);
assert.ok(calibrationSource, 'v50 calibration version gate must exist');
const calibrationSupported = vm.runInNewContext(calibrationSource[0] + '\ncalibrationSupported;', {});
assert.equal(calibrationSupported({softwareBuildID: '41.3'}), false);
assert.equal(calibrationSupported({softwareBuildID: '42.0-49'}), false);
assert.equal(calibrationSupported({softwareBuildID: '42.0-50'}), true);

const fieldsSource = js.match(/const visibleBenchDiagnostics = \[[\s\S]*?\n\];/);
const selectionSource = js.match(/const benchDiagnosticsFor = \(device\) =>[\s\S]*?;\n/);
assert.ok(fieldsSource && selectionSource);
const fieldSelection = vm.runInNewContext(
    [fieldsSource[0], goalSource[0], calibrationSource[0], selectionSource[0], 'benchDiagnosticsFor;'].join('\n'), {});
assert.equal(fieldSelection({softwareBuildID: '42.0-48'}).length, 45);
assert.equal(fieldSelection({softwareBuildID: '42.0-49'}).length, 46);
assert.equal(fieldSelection({softwareBuildID: '42.0-50'}).length, 47);
assert.ok(!fieldSelection({softwareBuildID: '42.0-48'}).includes('session_goal_silence_exits'));
assert.ok(fieldSelection({softwareBuildID: '42.0-49'}).includes('session_goal_silence_exits'));
assert.ok(!fieldSelection({softwareBuildID: '42.0-49'}).includes('saadc_vddh_calibrated_mv'));
assert.ok(fieldSelection({softwareBuildID: '42.0-50'}).includes('saadc_vddh_calibrated_mv'));
for (const version of ['42.0-48', '42.0-49']) {
    const fields = fieldSelection({softwareBuildID: version});
    for (const field of [
        'saadc_vddh_mv', 'saadc_vdd_mv', 'saadc_valid_mask', 'saadc_samples', 'saadc_last_ms', 'saadc_last_error',
        'session_stop_guard_exits', 'session_active_id', 'hw_direct_exits', 'hw_direct_prearm_timeouts',
        'rx_gpio_starts_reported_idle', 'uart_handoff_errors', 'uart_hardware_errors',
        'cmd_queued', 'cmd_tx_ok', 'cmd_tx_failed', 'cmd_position_reported', 'cmd_stop_reported',
        'cmd_timeout_no_report', 'cmd_timeout_no_target'
    ]) {
        assert.ok(fields.includes(field), `${version} should expose ${field}`);
    }
}
assert.match(js, /expose\.withUnit\('mV'\)/);
assert.match(js, /NOT verified as battery voltage on HW-954/);

assert.match(js, /exposes: \(device\) =>/);
assert.match(js, /benchSupported\(device\) \? \[/);
assert.match(js, /assertBenchSupported\(entity, meta\);/);
assert.match(js, /key: \['refresh_bench_diagnostics'\]/);
assert.match(js, /session_goal_silence_exits: 0xF199/);
assert.match(js, /session_stop_guard_exits: 0xF196/);
assert.match(js, /saadc_vddh_calibrated_mv: 0xF19A/);
assert.match(js, /Manually read 47 power\/UART\/PPI\/session\/command fields in 10 Zigbee batches/);
assert.match(js, /calibrationSupported\(device\)/);
assert.match(js, /Manually read 46 power\/UART\/PPI\/session\/command counters in 10 Zigbee batches/);
assert.match(js, /Manually read 45 power\/UART\/PPI\/session\/command counters in 9 Zigbee batches/);
console.log('Z2M SWBuildID gate and converter mirrors: PASS');
