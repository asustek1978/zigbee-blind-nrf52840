# v42.0-30 — UART RX power baseline and safe optimization plan

Status: measurement/design stage. **No firmware behavior has been changed yet.**

## Verified baseline (HW-954 nRF52840 only)

- Firmware v42.0-29; continuous passive UART RX; motor UART TX disabled.
- Current: **1.39–1.41 mA** (measured by user).
- 20-minute run: 3337 RX bytes, 235 detected headers, 233 valid Tuya frames, 1 rejected, 0 UART errors, phase 9.
- Between 680 and 1200 seconds RX byte count remained 3337; this is not evidence of UART failure without a stimulus.
- Baseline source uses `uart_poll_in()` and `k_sleep(K_MSEC(1))` when no data.
- v42.0-27 GPIO-edge wake was unsuccessful: 1589 bytes, 0 valid frames, 60 rejected.

## Safety constraints

1. Preserve stable v41.3 and verified diagnostic v42.0-29 unchanged.
2. Do not connect nRF UART TX to the P3 motor during passive tests.
3. Do not enable motor commands, calibration, or startup queries.
4. Do not introduce a longer polling interval without proving no loss at the actual P3 UART baud rate and frame spacing.
5. Measure **board-only** current in identical wiring, supply voltage, Zigbee association, and test conditions.

## Experiment sequence

A. Measure current during v42.0-29 startup, active P3 UART traffic, and idle after RX traffic stops. Repeat 3 times and note meter resolution and supply voltage.

B. Capture UART line rate, inter-byte spacing and maximum burst length with a logic analyzer. Establish safe RX buffering requirements.

C. In a **separate** firmware commit, prototype interrupt-driven or async buffered RX (depending on nRF52840 UART driver support) with a persistent Tuya parser. Avoid one-byte GPIO edge wake/suspend design used in v42.0-27.

D. Validate 20 minutes plus CLOSE→STOP and OPEN→STOP after a prolonged idle. Compare RX bytes, headers, valid/rejected frames, UART errors, and board-only current with the baseline. Zigbee rejoin and battery reporting must be assessed independently.

## Acceptance criteria

- No UART errors; no unexplained new frame rejection during equivalent controlled traffic.
- Motor reports remain receivable after prolonged idle.
- Reduced measured board-only current, reproducible across three trials.
- Preserve passive RX safety and do not regress Zigbee joining.
- A successful CI build is required before generating or flashing a UF2.

**Note:** 0.05–0.10 mA is an exploratory target, not a demonstrated capability.
