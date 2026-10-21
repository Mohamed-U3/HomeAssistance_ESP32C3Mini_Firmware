# ESP32-C3 8-Channel Relay Controller (Wi-Fi + MQTT + Home Assistant)

An 8-relay controller for the ESP32-C3-MINI. It has a local web UI with live WebSocket sync, Home Assistant MQTT auto-discovery, a captive configuration portal, and two physical buttons.

## Features

- 8 relay outputs, active-HIGH, all OFF at boot.
- Web UI on port 80, with real-time state sync over a WebSocket on port 81.
- Home Assistant MQTT auto-discovery: 8 `switch` entities grouped under one device.
- Non-blocking MQTT: a background FreeRTOS task probes the broker, so the web UI never freezes.
- Captive portal (WiFiManager) for Wi-Fi, hostname and MQTT settings, stored in NVS.
- mDNS access at `http://<hostname>.local`.
- Master button toggles all relays. Config button opens the portal.

## Hardware

| Item                 | Value                                                    |
| -------------------- | -------------------------------------------------------- |
| MCU                  | ESP32-C3-MINI (PlatformIO board: `esp32-c3-devkitm-1`) |
| Serial / programming | USB-to-UART (CH340) on UART0, 115200 baud                |
| Relays (active-HIGH) | GPIO 0, 1, 4, 5, 19, 18, 10, 7 (Relay 1 to 8)            |
| Master toggle button | GPIO 2 (to GND, internal pull-up)                        |
| Config portal button | GPIO 8 (to GND, internal pull-up)                        |

GPIO18 and GPIO19 are the native USB pins of the C3. They are used as plain GPIO here because programming goes through the CH340.

## Hardware Design

The PCB, schematic and design files for this controller are in a separate repository:

👉 [Smart_home_Board](https://github.com/Mohamed-U3/Smart_home_Board)

This firmware is written for that board (ESP32-C3-MINI, 8 relay outputs, 2 buttons, CH340 USB-to-UART).

## Software Requirements

- [PlatformIO](https://platformio.org/)
- Framework: Arduino (espressif32)
- Libraries (installed automatically):
  - `tzapu/WiFiManager @ ^2.0.17`
  - `links2004/WebSockets @ ^2.4.1`
  - `knolleary/PubSubClient @ ^2.8`

## Build and Flash

1. Clone the repository and open it in VS Code with PlatformIO.
2. (Optional) Set the module name in `main.cpp`:

   ```cpp
   #define Module_Name module3
   ```

   This sets the default hostname (`module3`), the AP name (`ESP32-module3-AP`) and the MQTT topic prefix.
3. Build and upload:

   ```bash
   pio run -t upload
   pio device monitor -b 115200
   ```

## First-Time Setup

1. On first boot (no saved Wi-Fi), the board starts an access point named `ESP32-<Module_Name>-AP`.
2. Connect to it. The captive portal opens, or browse to `192.168.4.1`.
3. Choose your Wi-Fi network and fill in:
   - Custom hostname
   - MQTT broker (IP or hostname)
   - MQTT port
   - MQTT username and password (optional)
4. Save. The board connects and prints its IP address and `.local` URL on the serial monitor.

### Re-opening the portal

Press the Config button (GPIO 8). The services stop, the AP starts with a 180-second timeout, new settings are saved, and the board restarts.

## Default Settings

| Setting       | Default                 |
| ------------- | ----------------------- |
| Hostname      | `module3`             |
| MQTT server   | `homeassistant.local` |
| MQTT port     | `1883`                |
| MQTT user     | `esp32_user`          |
| MQTT password | change it in the portal |

## Usage

### Web UI

Open `http://<hostname>.local` or the board's IP address. You get one toggle per relay, plus **Turn All OFF** and **Toggle All**. All open browsers stay in sync.

### WebSocket commands (port 81)

| Message           | Action                        |
| ----------------- | ----------------------------- |
| `TOGGLE:<gpio>` | Toggle the relay on that GPIO |
| `TOGGLE_ALL`    | Invert every relay            |
| `ALL_OFF`       | Turn every relay off          |

The server sends a JSON array on connect and after every change:

```json
[{"pin":0,"state":true},{"pin":1,"state":false}]
```

### MQTT / Home Assistant

Discovery messages are published automatically after the MQTT connection is made. Topics, with `i` from 0 to 7:

| Purpose          | Topic                                               |
| ---------------- | --------------------------------------------------- |
| Discovery        | `homeassistant/switch/<hostname>/relay<i>/config` |
| State (retained) | `homeassistant/switch/<hostname>/relay<i>/state`  |
| Command          | `homeassistant/switch/<hostname>/relay<i>/set`    |

Payloads are `ON` and `OFF`. Home Assistant needs the MQTT integration enabled and a broker that accepts the credentials you configured.

### Physical buttons

- **Master (GPIO 2):** toggles all relays (50 ms debounce).
- **Config (GPIO 8):** opens the configuration portal.
- **Flash:** For the boot mode of uploading a Firmware
- **Reset:** resets the MCU.

## How It Works

- `setRelay()` is the single entry point. It updates the GPIO, publishes the MQTT state and broadcasts to WebSocket clients.
- `mqttProbeTask` resolves the broker and tests its TCP port every 5 seconds while disconnected. `reconnectMQTT()` in `loop()` connects only after the probe succeeds.
- Settings are stored in NVS under the `settings` namespace.

## Known Limitations and Ideas

- Relay states are not saved. All relays are OFF after a reboot.
- No authentication on the web UI or WebSocket. Use only on a trusted LAN.
- No MQTT availability topic (LWT) yet.
- The config button reboots the board immediately. A long-press would be safer.
- The MQTT buffer is 512 bytes, so long hostnames may exceed it for discovery messages.

## License

Add your license here (for example MIT).
