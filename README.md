# ATAS-120 Antenna Controller

An Arduino-based controller that allows the Yaesu ATAS-120 HF mobile antenna to be used with **any transceiver**, while providing improved mechanical protection compared to the original Yaesu implementation.

**Author:** Adrian Florescu YO3HJV — March 2026

---

## Overview

The ATAS-120 antenna tunes across the 7–50 MHz range by varying the motor position:
- **7–8.5 V** applied via the coax cable → motor runs **DOWN** (increases inductance, lowers resonant frequency)
- **>10.5 V** → motor runs **UP** (decreases inductance, raises resonant frequency)

This controller intercepts that voltage signal, monitors motor current and supply voltage via an **INA219** sensor, and drives the motor through a **high-side transistor**. All protection logic runs on an AVR microcontroller (Arduino).

---

## Hardware

| Component | Role |
|-----------|------|
| AVR microcontroller (Arduino) | Main control logic |
| INA219 (I²C, 0x40) | Current and voltage monitoring |
| High-side transistor on `ctrlPin` (pin 11) | Motor power switch |
| Blue LED (pin 10, active LOW) | Power / secondary status |
| Red LED (pin 12, active LOW) | Primary status indicator |

LEDs are connected to +5 V through 1 kΩ resistors (LOW = ON).

---

## Features

- **Interactive POST** (Power-On Self Test with user participation) — validates motor connectivity before allowing any movement
- **UP / DOWN motor control** with configurable voltage thresholds and hysteresis
- **End-of-travel detection** — both directions, without POKE
- **POKE (unstick) sequence** — gentle current pulses to free a jammed antenna
- **Directional blocking** — a blocked direction stays locked until the user commands the opposite direction
- **Fault detection:**
  - Short circuit / overcurrent (latched, requires reset)
  - No antenna / motor not detected (latched, requires reset)
  - Battery / supply out of range (recoverable automatically)

---

## LED Status Indicator

### Legend
| Symbol | Meaning |
|--------|---------|
| Blue ON | Blue LED steady on |
| Blue FAST | Blue blinks fast (`fastBlueLedFlashOn` / `fastBlueLedFlashOff`) |
| Red ON | Red LED steady on |
| Red FAST | Red blinks fast (`fastRedLedFlashOn` / `fastRedLedFlashOff`) |
| Red SLOW | Red blinks slow (`slowRedLedFlashOn` / `slowRedLedFlashOff`) |
| Red pulse | Short ~30 ms red pulse at the start of each POKE ON step |

### Normal States
| State | Blue | Red | Meaning |
|-------|------|-----|---------|
| POWER ON | OFF | ON steady | Controller powered, before POST |
| POST | ON steady | ON/OFF guided | Guides user through self-test |
| STANDBY | ON steady | OFF | Idle, ready for commands |
| RUN UP | FAST blink | OFF | Motor running UP |
| RUN DOWN | FAST blink | OFF | Motor running DOWN |
| BLOCKED | ON steady | FAST blink | End-of-travel reached; red blinks while user holds the blocked direction |
| POKE | FAST blink | Short pulses | Unstick sequence running |

### Fault States
| Fault | Blue | Red | Meaning |
|-------|------|-----|---------|
| SHORT | OFF | SLOW blink | Short circuit detected — **requires reset** |
| NO ANTENNA | Rare flash (ON short / OFF long) | OFF | Motor/antenna not detected — **requires reset** |
| CHECK BATTERY | 3 fast flashes + long pause | ON during long pause | Supply voltage out of range — **recoverable** |

---

## POST Procedure (Power-On Self Test)

POST starts automatically at power-on and must be completed before normal operation.

1. **WAIT DOWN** — Blue ON, Red ON. Apply a DOWN command (voltage in the `vDown` window). The controller activates `ctrlPin` briefly.
2. **TEST DOWN** (max `postHoldTimeoutMs`) — Hold the command. Current must exceed `postMinMotorCurrent` for at least `postQualifyMs`. Failure → fault latch.
3. **WAIT UP** — Release to neutral (0 … `liliThreshV`), then apply an UP command (voltage ≥ `vUp`). Supply voltage is also validated here.
4. **TEST UP** (max `postHoldTimeoutMs`) — Same as TEST DOWN. Supply voltage checked.
5. **PAUSE** (`postPauseAfterMs`) — Red ON, no movement → transitions to **STANDBY**.

---

## Runtime Logic

### antenaUp (`runUpTick`)
- `ctrlPin` active while input voltage ≥ `vUp`
- Releasing the command (voltage < `vUpOff()`) stops immediately → STANDBY
- Settle window: `postSettleMs` after start before current evaluation

| Condition | Duration | Grace period | Action |
|-----------|----------|--------------|--------|
| current ≥ `shortAntenna` | `shortDetectMs` | — | FAULT: SHORT (latched) |
| current ≤ `noAntennaCurrent` × 3 | consecutive | `runNoAntennaGraceMs` | FAULT: NO ANTENNA (latched) |
| current ≥ `stopCurr` | `stopCurrDetectMs` | `runStopGraceMs` | STOP, `blockUp` = true |
| current ≥ `stuckCurr` | `stuckDetectMs` | `runStuckGraceMs` | Enter POKE sequence |
| voltage < `vccMin` or > `vccMax` | — | — | FAULT: CHECK BATTERY (recoverable) |

### antenaDown (`runDownTick`)
- `ctrlPin` active while input voltage in (`downMinCmdV` … `vDownOff()`)
- Releasing command stops immediately → STANDBY
- Battery voltage is **not** checked during DOWN

| Condition | Duration | Grace / window | Action |
|-----------|----------|----------------|--------|
| current ≥ `shortAntenna` | `shortDetectMs` | — | FAULT: SHORT (latched) |
| current ≤ `noAntennaCurrent` × 3 | consecutive | `runNoAntennaGraceMs` | FAULT: NO ANTENNA (latched) |
| current ≥ `endDownCurrent` | `endDownAlreadyQualMs` | first `endDownAlreadyThereMs` | Already at bottom — STOP, `blockDown` (no POKE) |
| current ≥ `stuckCurr` | `stuckDetectMs` | `runStuckGraceMs` | Enter POKE sequence |
| current ≥ `endDownCurrent` | `endDownDetectMs` | `runStopGraceMs` | End of travel — STOP, `blockDown` |
| current ≥ `stopCurr` | `stopCurrDetectMs` | `runStopGraceMs` | STOP, `blockDown` |

---

## Fine Tuning Parameters

All parameters are `const` values defined at the top of `ATAS_Release_1.ino`.

### Current Thresholds (mA)
| Variable | Default | Description |
|----------|---------|-------------|
| `noAntennaCurrent` | 25 | Below this → antenna absent |
| `shortAntenna` | 750 | Above this → short circuit |
| `stopCurr` | 500 | Stop threshold at end of travel (both directions) |
| `stuckCurr` | 280 | Threshold to enter POKE |
| `endDownCurrent` | 400 | Bottom end-of-travel threshold |
| `postMinMotorCurrent` | 100 | Minimum current accepted during POST |

### Qualification Timings (ms)
| Variable | Default | Description |
|----------|---------|-------------|
| `shortDetectMs` | 50 | Minimum hold time to validate SHORT |
| `stuckDetectMs` | 300 | Minimum hold time to validate STUCK |
| `endDownAlreadyThereMs` | 150 | Initial window for "already at bottom" detection |
| `endDownAlreadyQualMs` | 60 | Qualification time within initial window |
| `endDownDetectMs` | 450 | Normal bottom end-of-travel qualification |
| `stopCurrDetectMs` | 80 | `stopCurr` qualification time |

### Runtime Grace Periods (ms)
| Variable | Default | Description |
|----------|---------|-------------|
| `runNoAntennaGraceMs` | 150 | Grace before NO_ANTENNA detection |
| `runStopGraceMs` | 150 | Grace before `stopCurr` / `endDownCurrent` evaluation |
| `runStuckGraceMs` | 400 | Grace before entering POKE |

### Voltage Thresholds (V)
| Variable | Default | Description |
|----------|---------|-------------|
| `vccMin` | 10.20 | Minimum allowed supply voltage |
| `vccMax` | 14.95 | Maximum allowed supply voltage |
| `vDown` | 8.20 | Upper bound of DOWN command window |
| `vUp` | 10.20 | Lower bound of UP command |
| `vHystPercent` | 0.10 | 10% hysteresis on both command thresholds |
| `downMinCmdV` | 5.00 | Hard minimum voltage to recognise a DOWN command |

### POKE Parameters
| Variable | Default | Description |
|----------|---------|-------------|
| `unstuckPokeNr` | 7 | Number of pulse cycles |
| `pokeDuration` | 100 ms | ON time per cycle |
| `pokePause` | 75 ms | Pause between pulses |

### POST Timings (ms)
| Variable | Default | Description |
|----------|---------|-------------|
| `postHoldTimeoutMs` | 250 | Max time to hold command during POST |
| `postQualifyMs` | 80 | Current qualification time in POST |
| `postSettleMs` | 20 | Settle time after `ctrlPin` activation |
| `postPauseAfterMs` | 200 | Final pause before entering STANDBY |

### LED Timings (ms)
| Variable | Default | Description |
|----------|---------|-------------|
| `fastBlueLedFlashOn/Off` | 75 / 300 | Fast Blue blink |
| `slowBlueLedFlashOn/Off` | 500 / 300 | Slow Blue blink |
| `fastRedLedFlashOn/Off` | 75 / 300 | Fast Red blink |
| `slowRedLedFlashOn/Off` | 500 / 300 | Slow Red blink |
| `flashSequencePause` | 400 | Pause between flash sequences (e.g. CHECK BATTERY) |

---

## License

Personal project — © Adrian Florescu YO3HJV, March 2026.
