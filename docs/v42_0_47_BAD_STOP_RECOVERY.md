# P3 nRF52840 v42.0-47 — UART STOP-bit recovery (experimental)

Branch: `experiment/v42-0-47-bad-stop-gpio-recovery`.
Base: verified CI head of v42.0-46 (`16808d61095ae13cb1092f5750464695403e2c02`).
This is **not** a stable release; v41.3 and v42.0-46 remain untouched.

## Observation (v42.0-46, before this change)

After 8,424 seconds uptime: 601 valid Tuya frames, 0 rejected; 32
software STOP failures split into 7 GPIO-first-byte and 25 chained-byte
failures, grouped into 5 episodes. Hardware UARTE framing/overrun errors
and DMA handoff errors were all 0. There is no evidence that every invalid
software STOP corresponded to a lost Tuya report.

## Single behavioural change

In `src/p3_soft_uart.c`, after an invalid software STOP bit:
- Record the existing fault breakdown, unchanged.
- Stop/disable TIMER3, clear its compare event, disable prepared UARTE0
  if it was warmed up, reset phase/bit index and release the software
  receiver ownership flag **last**.
- Wait for the *next* GPIO falling edge. If the line remains LOW, no
  immediate re-trigger occurs; sampling resumes only at a future edge.
- Do not enter the speculative `P3_RX_NEXT_START` chained-byte path
  on this **error**. After a **valid** non-0x55 byte, that existing path
  still applies.
- On valid `0x55` preamble, the original TIMER3->UARTE0 handoff is
  unchanged, including its 52-us remaining STOP-half-bit budget.

No new MCU command, UART retry, Zigbee poll, extra DMA session,
GPIO pin assignment or watchdog extension.

## Limitations / failure modes

This abort may **discard remainder of a damaged stream** until a new
falling edge. A falling edge *inside* a byte can still look like a start,
and may produce another software framing error. One UART STOP error is
not equivalent to one lost Tuya frame. There is no signal-quiet-time gate
in this first experiment; adding one can increase wake duty cycle and
risk missing a fresh Tuya frame. Verify on a physical motor before
drawing conclusions about latency or battery savings.

## Controlled bench test

1. Record firmware build ID and a full diagnostics snapshot on v42.0-46;
   note previous total counters and device uptime (counters reset on flash).
2. Flash experimental UF2 **on one supervised test motor only**;
   motor clear of obstructions, physical STOP/power cutoff accessible.
3. After boot, wait 20-30 minutes without movement, refresh the counters:
   framing errors, bad STOP GPIO/chained, idle fault episodes, Tuya frames,
   ring overflows, hardware errors, handoff errors, hardware RX entries/exits.
4. Test 3 distinct positions (e.g. 70->50->90->70) and STOP once in mid-travel,
   respecting motor safety; validate reported positions, last command result,
   every hardware RX exit and absence of DMA failures.
5. Confirm no lingering hardware RX in idle and compare **measured**
   battery current during identical idle periods. Counters cannot
   independently measure power draw.
6. Keep v42.0-46 UF2 available for rollback. Do not deploy unattended
   until tests and consumption checks pass.

Success criterion: improved chained STOP-failure rate **without**
increasing GPIO-first-byte faults, Tuya frame loss, RX session length,
or current draw. Absolute error counters across reflashes are not comparable:
measure events per unit time and report count.
