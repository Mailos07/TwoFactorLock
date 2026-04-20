# CSC375/530 Final Assignment — Secure Unlock System

## Group Members
- [Lance Sigersmith] — [Contribution]
- [Horacio Valdes] — [Contribution]
- [Blessings Mailos] — [Contribution]

---

## System Overview

A two-stage embedded authentication system on the **XIAO ESP32-S3 Sense** + **Seeed Expansion Board**.

### Stage 1 — IR Code Entry
- User presses digits on the Grove IR mini-controller.
- Entered digits are shown on the OLED (code is not revealed).
- Press the `*` / DEL key to delete the last digit; `#` / OK to confirm.
- On a 4-digit match against `lock_config.txt`, advance to Stage 2.
- On mismatch → `ACCESS DENIED`, return to LOCKED.

### Stage 2 — Spoken Word Recognition
- A random number 1–7 is generated and displayed on the OLED.
- The user speaks the corresponding color word from the vocabulary list:
  `1=Blue, 2=Cyan, 3=Green, 4=Magenta, 5=Red, 6=White, 7=Yellow`
- Three recordings are taken; **2 out of 3 must match** (majority vote).
- Confidence threshold: **70%** — results below this are treated as non-matches.
- On majority pass → `UNLOCKED`.
- On majority fail → return to `LOCKED`.

---

## Hardware

| Component | Connection |
|-----------|------------|
| XIAO ESP32-S3 Sense | Main MCU |
| Seeed Expansion Board | OLED (I2C), SD card, RTC |
| Grove IR Receiver v1.2 | D2 |
| Grove Buzzer | D3 |
| Onboard PDM Microphone | GPIO 41 (DATA), 42 (CLK) — internal |

---

## SD Card Setup

Create a file called `lock_config.txt` in the root of your SD card:

```
CODE:1234
```

Replace `1234` with your desired 4-digit code.

---

## Edge Impulse ML Model

1. Collect or download audio samples for all 7 color words from:  
   https://figshare.com/s/50807c77f1acc943e4c3?file=50448711
2. Create a project in [Edge Impulse](https://studio.edgeimpulse.com).
3. Upload samples, add an MFCC DSP block + Neural Network classifier.
4. Train and test the model targeting the XIAO ESP32-S3 (Cortex-M33 / ESP32-S3).
5. Export as **Arduino library** (ZIP).
6. Unzip the library into `lib/` inside this PlatformIO project.
7. In `main.cpp`, replace the include:
   ```cpp
   #include <your-edge-impulse-project_inferencing.h>
   ```
   with the actual header name from your exported library.

### Confidence Threshold
`CONFIDENCE_THRESHOLD` is set to `0.70` in `main.cpp`. Tune this after testing — too low causes false accepts; too high causes false rejects.

---

## IR Remote Key Mapping

The default mapping uses NEC codes for a standard Grove IR mini-controller.
**Run the `IRremote` library's `ReceiveDump` example** to capture your remote's actual hex codes and update the `#define IR_0` … `IR_9`, `IR_DEL`, `IR_OK` values in `main.cpp`.

---

## Building & Flashing

```bash
# Install PlatformIO CLI or use the VS Code extension

# Build
pio run

# Flash
pio run --target upload

# Monitor serial
pio device monitor
```

---

## Log File Format (`unlock_log.txt` on SD card)

Cleared and recreated at every startup.

```

```

---

## Authentication Flow

```
LOCKED
  │  (any IR keypress)
  ▼
CODE ENTRY  ──── wrong code ──────► ACCESS DENIED ──► LOCKED
  │  (correct 4-digit code)
  ▼
VOICE ENTRY  ─── <2/3 correct ────► ACCESS DENIED ──► LOCKED
  │  (≥2/3 attempts match, conf ≥70%)
  ▼
UNLOCKED
  │  (any IR keypress)
  ▼
LOCKED
```

---

## Design Notes & Assumptions

- **Security**: The OLED shows the vocabulary index (number), not the word itself, requiring the user to know the mapping independently.
- **Voice majority vote**: 3 recordings, 2-of-3 required to unlock. This reduces false rejects from background noise without significantly increasing false accept rate.
- **Failed voice → LOCKED**: After a failed voice attempt the system returns to fully LOCKED (not CODE ENTRY), forcing re-entry of the code. This prevents an attacker from replaying a correct code indefinitely while brute-forcing the voice stage.
- **RTC**: The Seeed expansion board's PCF8563 RTC is used for log timestamps. Set it once with `rtc.setDateTime(...)` and it persists on battery.
- **No long blocking delays**: All `delay()` calls are short (≤2.5 s for user feedback). The audio recording uses I2S DMA, which does not block the MCU beyond the recording window.
- **.pio is gitignored** — see `.gitignore`.

---

## .gitignore

```
.pio/
.vscode/
__pycache__/
*.pyc
```
