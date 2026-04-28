# Alarm Panel — Improvement Ideas

## Reliability & Robustness
- [x] Watchdog timer — Enable ESP32 hardware watchdog to auto-recover from hangs
- [x] MQTT last will — Set LWT message ("offline" on `home/keypad/status`) so HA knows when panel drops
- [x] NVS-stored settings — Persist display timeout and brightness to flash using `Preferences.h`

## UI / UX Improvements
- [x] Animations — LVGL transition animations on modal open/close (fade + slide) and opacity pulse on state changes
- [x] Triggered alarm screen — Full-screen red flashing alert when `triggered` state is received, tap to dismiss
- [x] Toast/snackbar feedback — Brief auto-dismissing popup ("Command sent", "Not connected", "Enter PIN first")
- [x] Auto-sleep timer — Dim/sleep display after N seconds of inactivity, configurable via MQTT (`home/keypad/display/timeout`)
- [x] Screensaver — Minimal clock-only view (Montserrat 48) on dimmed black overlay when idle

## Code Architecture
- [x] Split `main.cpp` into modules — Break into `ui.cpp`, `mqtt.cpp`, `touch.cpp`, `network.cpp`
- [ ] State machine — Replace scattered if/else state checks with enum-driven state machine for alarm lifecycle

## Features
- [ ] More MQTT entities — Subscribe to additional HA entities (lights, locks, temperature sensors) and add panels
- [ ] Multi-page navigation — LVGL screen switching for home dashboard, alarm detail, and settings pages
- [ ] Buzzer/speaker — Piezo for audible feedback on button press, arming countdown beeps, or triggered alarm siren
- [ ] Ambient light sensor — Auto-adjust backlight brightness based on room lighting
- [ ] Settings page — On-device config for display timeout, brightness, timezone without reflashing

## Security
- [ ] PIN lockout — Rate-limit PIN attempts (e.g., 3 failures = 30s cooldown)
- [ ] TLS for MQTT — Use `WiFiClientSecure` instead of `WiFiClient` for encrypted MQTT
- [ ] PIN hash — Store a hash rather than plaintext if validating locally
