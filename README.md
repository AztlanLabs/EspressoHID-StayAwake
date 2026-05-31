# EspressoHID: StayAwake

EspressoHID: StayAwake is a Hardware HID Keyboard a free and open-source ESP32-S3 USB HID activity simulator that presents itself as a regular keyboard, performs subtle keep-awake actions, and exposes a phone-friendly web dashboard for provisioning, control, and OTA updates.

It is designed for authorized lab, kiosk, demo, and test environments where you want device-side activity simulation without installing software on the host machine.

Use it only on systems you own or are explicitly authorized to manage.

## Highlights

- ESP32-S3 native USB HID firmware built with Arduino + PlatformIO
- Randomized keyboard and consumer-control activity to avoid fixed patterns
- Two runtime profiles: `ACTIVE` and `MEETING`
- First-boot captive portal for Wi-Fi provisioning
- Live browser UI for status, configuration, history, logs, and manual actions
- OTA firmware update from the web interface
- Random USB vendor/product identity selection on each boot
- Persistent runtime configuration stored in ESP32 NVS
- Single-button hardware control for start/stop, profile switching, and factory reset

## What It Does

Temporal Keyboard behaves like a physical USB keyboard and periodically performs low-impact HID actions such as arrow-key movement, modifier taps, media nudges, and optional text typing. The firmware also manages breaks, exposes device state over HTTP, and lets you tune intervals and action weights without recompiling.

## Hardware

### Required

- ESP32-S3 development board with native USB
- USB cable for power, flashing, and HID connection

### Used by the firmware

| Component | Default pin | Purpose |
| --- | --- | --- |
| BOOT button | GPIO 0 | Start/stop, profile switch, factory reset |
| WS2812 / RGB LED | `RGB_BUILTIN` or GPIO 38 | Status feedback |
| Native USB | ESP32-S3 USB | Keyboard + consumer control HID device |
| Wi-Fi | ESP32-S3 built-in radio | Captive portal, dashboard, API, OTA |

Notes:

- The firmware defaults `RGB_BUILTIN` to `38` if your board definition does not provide one.
- Some ESP32-S3 boards expose the onboard RGB LED on GPIO 48 instead. Adjust `RGB_BUILTIN` in [include/config.h](include/config.h) if needed.

## Quick Start

### 1. Install PlatformIO

You can use either:

- the VS Code PlatformIO extension, or
- the CLI:

```bash
pip install platformio
```

### 2. Build the firmware

```bash
pio run -e esp32-s3-devkitm-1
```

### 3. Flash the board

```bash
pio run -e esp32-s3-devkitm-1 -t upload
```

### 4. First boot

- Power the board.
- If no Wi-Fi credentials are stored, the device starts a captive portal AP.
- Connect to `KBSetup-XXXX` where `XXXX` is derived from the ESP32 MAC address.
- Open `http://192.168.4.1/` if the captive portal does not open automatically.
- Save Wi-Fi credentials and runtime settings.
- The device restarts automatically.

### 5. Open the dashboard

Once the board joins your network, open one of these:

- `http://fakekeyboard.local/`
- `http://<device-ip>/`

## Runtime Behavior

### Button controls

| Action | Result |
| --- | --- |
| Short press | Toggle active state |
| Hold `1.5s` | Switch profile |
| Hold `10s` | Factory reset and reboot |

### Profiles

| Profile | Goal | Default interval range |
| --- | --- | --- |
| `ACTIVE` | More frequent activity for active workstation-style use | `10s` to `60s` |
| `MEETING` | Lower-visibility behavior with gentler actions | `45s` to `180s` |

The first action after activation is intentionally quicker than the steady-state interval.

### Breaks and sleep

The firmware can simulate pauses in activity:

- short naps chosen randomly after actions
- longer breaks after sustained active sessions
- wake-up via timer or manual button interaction

### LED states

The RGB LED communicates the main state of the device:

- blue: idle
- amber to green: charging toward next action
- bright green pulse: ready
- blinking green: action just ran
- yellow breathing: sleeping/break
- red: stopped or Wi-Fi warning state

## Action Catalog

The current firmware includes these built-in actions:

- `ArrowScroll`
- `AltTab`
- `Volume`
- `Brightness`
- `CapsToggle`
- `NumLockToggle`
- `ShiftTap`
- `WinArrow`
- `TypeText`
- `CtrlTap`
- `WinSearch`
- `EmojiPeek`

Important details:

- actions are chosen by weighted random selection
- actions can be enabled or disabled individually at runtime
- profile-specific weights are supported
- `TypeText` only runs when custom text has been configured
- several actions are implemented as net-zero or reversible interactions to reduce host-side side effects

## Networking and Provisioning

Temporal Keyboard has two network modes.

### First boot or no saved credentials

- Starts a SoftAP captive portal
- SSID pattern: `KBSetup-XXXX`
- Setup page served from `http://192.168.4.1/`

### After Wi-Fi is configured

- Tries to connect as a station client only
- Exposes the dashboard on the STA IP and via mDNS at `fakekeyboard.local`
- If the connection fails, it retries every 60 seconds up to 10 times
- If all retries fail, Wi-Fi is disabled for the rest of that boot
- It does not automatically fall back to captive portal mode after provisioning

You can also disable Wi-Fi features entirely from the provisioning screen. In that state, the web UI stays off until a factory reset.

## Web Dashboard

The browser UI provides:

- live device status
- profile switching
- sleep and wake controls
- interval editing for each profile
- per-action enable flags and weight overrides
- custom text for the `TypeText` action
- manual action triggering
- recent action history
- in-memory event logs
- OTA firmware upload

## REST API

### Status and configuration

| Endpoint | Method | Purpose |
| --- | --- | --- |
| `/api/status` | `GET` | Current activity, sleep state, profile, firmware, Wi-Fi mode, and timing info |
| `/api/config` | `GET` | Current runtime config, profile intervals, action mask, action list, custom text |
| `/api/logs` | `GET` | Event log entries |
| `/api/history` | `GET` | Recent action history |

### Control endpoints

| Endpoint | Method | Fields |
| --- | --- | --- |
| `/api/config` | `POST` | `p0MinMs`, `p0MaxMs`, `p1MinMs`, `p1MaxMs`, or second-based variants like `p0MinS`; `actionMask`; `wA0...`; `wM0...`; `customText` |
| `/api/trigger` | `POST` | `id` |
| `/api/control` | `POST` | `active`, `profile`, `sleepS` or `sleepMs`, `wake`, `clearHistory`, `factoryReset`, `reboot` |
| `/api/ota` | `POST` | multipart field `firmware` |

Notes:

- action history is stored in RAM and resets on reboot
- event logs are also in RAM and are not meant to be persistent audit logs

## Project Structure

```text
.
├── platformio.ini
├── src/
│   ├── main.cpp
│   └── web_portal.cpp
├── include/
│   ├── config.h
│   ├── state.h
│   ├── runtime_config.h
│   ├── actions.h
│   ├── profiles.h
│   ├── control.h
│   ├── sleep_manager.h
│   ├── led_controller.h
│   ├── usb_identity.h
│   ├── button_handler.h
│   ├── event_log.h
│   ├── device_status.h
│   └── web_portal.h
├── lib/
│   └── fake_keyboard_core/
│       ├── include/
│       │   └── fake_keyboard_core.h
│       └── src/
│           ├── actions.cpp
│           ├── button_handler.cpp
│           ├── control.cpp
│           ├── device_status.cpp
│           ├── event_log.cpp
│           ├── human_input.cpp
│           ├── led_controller.cpp
│           ├── profiles.cpp
│           ├── runtime_config.cpp
│           ├── sleep_manager.cpp
│           ├── state.cpp
│           └── usb_identity.cpp
└── test/
```

### Module overview

- [src/main.cpp](src/main.cpp): firmware entry point and main loop
- [src/web_portal.cpp](src/web_portal.cpp): captive portal, dashboard UI, JSON API, OTA handlers
- [lib/fake_keyboard_core/src/actions.cpp](lib/fake_keyboard_core/src/actions.cpp): action catalog and weighted selection
- [lib/fake_keyboard_core/src/runtime_config.cpp](lib/fake_keyboard_core/src/runtime_config.cpp): persistent NVS-backed configuration
- [lib/fake_keyboard_core/src/control.cpp](lib/fake_keyboard_core/src/control.cpp): state transitions and runtime controls
- [lib/fake_keyboard_core/src/led_controller.cpp](lib/fake_keyboard_core/src/led_controller.cpp): RGB state machine

## Development Notes

### Release mode vs debug mode

The included PlatformIO configuration is optimized for stealth by default:

- USB CDC serial is disabled on boot
- the device enumerates as HID only

If you want serial debug logging during development:

1. uncomment `-D DEBUG_MODE` in [platformio.ini](platformio.ini)
2. change `ARDUINO_USB_CDC_ON_BOOT` to `1`
3. rebuild and flash

Then you can monitor logs with:

```bash
pio device monitor -e esp32-s3-devkitm-1
```

### Runtime tuning

Compile-time defaults live in [include/config.h](include/config.h), but most day-to-day tuning can be done from the web UI and persisted in NVS.

## Current Limitations

- The project currently has no automated test suite; [test/README](test/README) is still the default PlatformIO placeholder.
- The web interface uses HTTP only. There is no authentication, HTTPS, or rate limiting.
- Most higher-level desktop actions are Windows-oriented.
- Action history and logs are in-memory only and are lost on reboot.
- There is no automatic AP fallback after a previously provisioned STA connection fails.

## Contributing

Contributions are welcome.

Good starting points:

- add automated tests for action selection and configuration persistence
- improve cross-platform action behavior for Linux and macOS
- harden the web API with authentication and transport security
- expand documentation and hardware compatibility notes

If you open a pull request, keep changes focused and update this README when behavior, configuration, or endpoints change.

## License

This repository does not currently include a `LICENSE` file.

If you plan to publish Temporal Keyboard as a true open-source project, add a license before release. `MIT` or `Apache-2.0` are both good lightweight options if you want broad reuse.
