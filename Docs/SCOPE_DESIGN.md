# Oscilloscope acquisition and triggering

## Timing and input

PA0 is ADC1 rank 1; PA5 is the resistor-ladder keypad on rank 2.
TIM6 update TRGO starts each complete two-rank scan at 100,000 scans/s.
The ADC runs in externally triggered, noncontinuous mode. TIM6 uses the
actual APB1 timer clock, including TIMPRE. A clock which cannot divide
exactly to 100 kHz is rejected rather than silently changing measurement time.

On STM32H723, PLL2P is 96 MHz. ASYNC_DIV4 plus the device's fixed divide by
two gives a 12 MHz ADC clock. At 32.5 sampling cycles and at most 16.5
conversion cycles per rank, a two-rank sequence takes about 8.17 us.
This fits the 10 us trigger period. HAL selects ADC boost automatically.
Offset and linearity calibration run before the initial DMA start.

ADC prescaler/trigger, DMA priority and sampling-task stack size are also
recorded in the CubeMX .ioc. TIM6 is reserved and configured by the
acquisition module; ADC IRQ setup/routing lives in generated files' USER
CODE sections. Preserve those sections and the custom scatter file when
regenerating the project.

The two ranks are sequential, not simultaneous. The scope channel has a
uniform sample interval independent of FreeRTOS scheduling; the keypad
remains available during slow timebases and HOLD.

Voltages assume VDDA = 3300 mV. There is no new calibrated analog front end,
input attenuator, amplifier, negative-voltage support, or analog anti-alias
filter. Input source impedance and actual VDDA affect accuracy.

## DMA ownership

The 4096-byte circular buffer is in DMA-accessible SRAM1 at 0x30000000,
separate from DTCM. Each 2048-byte half contains 512 scan pairs and occupies
whole 32-byte cache lines. Its ownership period is 5.12 ms.

DMA HT/TC callbacks record the completed half and sequence, then notify the
sample task. They do not draw, copy samples, or run the trigger algorithm.
The task snapshots the completed sequence, verifies the half is inactive,
invalidates its cache lines, and copies PA0 into a private 512-sample block.
It rechecks DMA ownership and generation after copying. Data from a half
reused during the copy is discarded; the next accepted block carries a gap.
Sticky DMA boundary flags detect a complete ring rotation while the
completion interrupt is delayed; a 5 ms copy bound provides another check.
Cache clean/invalidate only happens before DMA starts; running DMA uses
invalidate only, preventing dirty CPU cache lines from overwriting samples.

Gaps clear partial bins, trigger history and frequency-cycle history.
No frame combines samples from both sides of a lost block. A completed
SINGLE remains stopped after a gap. Display frame replacement is counted
separately from acquisition loss, because the LCD deliberately consumes
only the latest frame.

ADC overrun and DMA errors stop TIM6 in the callback. Stop/restart and
bounded retry happen in the sampling task. A 100 ms missing-completion
watchdog also requests recovery. The next successful stream starts with
a gap. Error counts remain visible after recovery.

## Capture and measurements

The pure C engine owns 272 circular bins and does not allocate memory.
Each bin stores min/max, raw sum, sum of squares, and complete-cycle
statistics. Decimation factors are 1, 2, 5, 10, 20, 50, 100 and 200.
The original sample stream always remains 100 kSa/s.

A Schmitt crossing at the configured level with +/-25 mV hysteresis
starts post-trigger collection after at least 68 complete history bins.
The edge-containing bin is displayed at index 68. Trigger alignment
uncertainty is less than one displayed bin; raw edge timing is quantized
to 10 us. This is software triggering on a continuous DMA stream.

AUTO publishes after about two record durations without a trigger.
NORMAL waits indefinitely. SINGLE accepts one triggered record and stops;
Configure rearms it even if values are unchanged. HOLD retains the last
completed record while the hardware continues supplying keypad samples.
Changing timing or trigger settings resets incomplete history.

PP, mean and total RMS use every raw sample in the displayed record.
Frequency and high-time duty use complete rising-to-rising cycles contained
within that record, even when falling-edge triggering is selected.
Cycles spanning the left record boundary are excluded.

A frequency result requires at least two complete cycles, at least ten
samples per cycle, at least two in each level, and maximum period no more
than 125% of minimum period. Otherwise no numerical frequency is claimed.
These are data-quality checks, not proof of absence of aliasing. An analog
signal above Nyquist can still produce a plausible lower-frequency result.
Non-square-wave duty depends on the configured level and hysteresis.

## Display and concurrency

Only the sampling task accesses the capture context. Keys publish a small
configuration plus revision. The sampling task applies that revision,
then publishes a complete frame tagged with it. LCD copies the frame in a
bounded critical section and draws outside it. Settings which invalidate
the frame suppress it until a matching capture is ready; HOLD retains
the original frame's time scale. Vertical gain/offset only affect rendering.

The renderer combines mean trace segments with each bin's vertical min/max
envelope. Bitmask symmetric differences update only changed pixels and
restore underlying grid colors. Static axes and unchanged text are cached.
The trigger marker lies above the plot. All 272 bins map into 232 columns.

## Diagnostics

USB SCOPE reports the latest complete record's statistics and the requested
settings, with a separate frame-configuration-valid flag. Sequence zero
means no capture has completed. An ADC failure is independently indicated
by SYS readiness and ADCSTAT; a retained record is historical data.

ADCSTAT last_error uses HAL ADC error bits, with high bits for backend
failures: 0x80000000 timer clock, 0x40000000 startup/calibration,
0x20000000 missing DMA completion, and 0x10000000 stop failure.
Errors/restarts/loss are cumulative; last_error remains for diagnosis.

## Validation

Host tests exercise phase alignment on all timebases and both edges,
hysteresis, AUTO timeout, NORMAL waiting, SINGLE rearm, HOLD, gap reset,
tick wrap, narrow-pulse envelopes, raw RMS/mean, incomplete-cycle exclusion,
and rejection of undersampled or unstable periodic signals.
The acquisition suite models DMA half ownership, cache operations,
completion changes, delayed service, errors and recovery.
App tests preserve independent pixel-reference checks plus key, frame,
CAN, PWM and USB regressions. Keil ARMCC5 builds the actual firmware.

Bench acceptance is still required:

1. Connect PE13 to PA0 with common ground. At 1 kHz / 50%, confirm stable
   rising and falling triggers and approximately 3.3 V peak-to-peak.
2. Compare 10 Hz, 100 Hz, 1 kHz and 5 kHz against a reference instrument;
   select enough record length for at least two full cycles.
3. Verify SINGLE stops after one edge, ARM rearms, and HOLD permits vertical
   inspection while keys continue responding.
4. At slow timebase, inject pulses several raw samples wide and verify their
   extrema remain visible. Pulses below 10 us are not guaranteed captured.
5. Exercise all CAN ports and USB during acquisition. Check ADCSTAT for loss
   or errors; deliberate missed blocks must never create a joined waveform.
6. Verify behavior at flat DC, disconnected input, and trigger levels outside
   the signal range. NORMAL/SINGLE wait; AUTO refreshes without a trigger.
7. Use the RTOS stack high-water marks and external timing measurement to
   confirm remaining stack and the processing budget under bus/LCD load.
