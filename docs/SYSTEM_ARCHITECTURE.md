# System Architecture & Presentation Diagrams

This document provides end-to-end visual diagrams of the **ESP32-C3 8-Channel Smart Relay Controller** for architecture reviews and presentations.

---

## 1. High-Level Hardware & Software Block Diagram

```mermaid
flowchart LR
    subgraph Hardware["ESP32-C3 Hardware Layer"]
        BTN_M["IO2: Master Button\n(INPUT_PULLUP, Active-LOW)"]
        BTN_C["IO8: Config Button\n(INPUT_PULLUP, 3s Hold)"]
        RELAYS_HW["8x Active-HIGH Relays\nIO0, IO1, IO4, IO5, IO19, IO18, IO10, IO7"]
        NVS[("NVS Flash\nNamespace: settings\nWi-Fi Credentials")]
    end

    subgraph Firmware["Firmware Modules (src/)"]
        MAIN["main.cpp\nLifecycle, ArduinoOTA,\nRuntime Wi-Fi Watchdog"]
        REL["relays.cpp\nState Matrix, Debounce,\nSmart Toggle"]
        CFG["config.cpp\nNVS Persistence, Sanitization,\nWiFiManager Failover"]
        MQTT["mqtt_ha.cpp\nPubSubClient, HA Discovery,\nFreeRTOS Probe Task"]
        WEB["web_ui.cpp\nHTTP :80, WS :81,\n/update OTA"]
    end

    subgraph External["Network & Users"]
        USER_WEB["Browser Client\nWeb UI & OTA"]
        HA["Home Assistant\nMQTT Broker"]
        AP_USER["Mobile / PC\nCaptive Portal (192.168.4.1)"]
    end

    BTN_M -->|"Short Press"| REL
    BTN_C -->|"3s Hold"| REL
    REL -->|"openConfigPortal()"| MAIN
    REL -->|"GPIO HIGH/LOW"| RELAYS_HW
    REL -->|"8-bit relay_mask"| CFG
    REL -->|"publishMqttState()"| MQTT
    REL -->|"broadcastStates()"| WEB

    MAIN --> CFG
    MAIN --> REL
    MAIN --> MQTT
    MAIN --> WEB

    CFG <--> NVS
    CFG <-->|"AP: ESP32-hostname-AP"| AP_USER
    MQTT <-->|"TCP :1883"| HA
    WEB <-->|"HTTP :80 / WS :81"| USER_WEB
```

---

## 2. Module Include & Dependency Graph

```mermaid
flowchart TB
    MAIN["main.cpp"] --> CFG_H["config.h"]
    MAIN --> REL_H["relays.h"]
    MAIN --> MQTT_H["mqtt_ha.h"]
    MAIN --> WEB_H["web_ui.h"]

    REL_C["relays.cpp"] --> REL_H
    REL_C --> CFG_H

    MQTT_C["mqtt_ha.cpp"] --> MQTT_H
    MQTT_C --> CFG_H
    MQTT_C --> REL_H

    WEB_C["web_ui.cpp"] --> WEB_H
    WEB_C --> CFG_H
    WEB_C --> MQTT_H
    WEB_C --> REL_H

    CFG_C["config.cpp"] --> CFG_H
```

---

## 3. Wi-Fi Connection, 2-Minute Failover & Portal State Machine

```mermaid
stateDiagram-v2
    [*] --> InitRelays: Power On / Reset
    InitRelays --> CheckSavedWifi: Load NVS Config & Apply Power-On Mode

    CheckSavedWifi --> SearchSavedWifi: Saved SSID exists
    CheckSavedWifi --> CaptivePortal: No saved SSID

    SearchSavedWifi --> Connected: Connected within 120s
    SearchSavedWifi --> CaptivePortal: 120s Timeout OR IO8 held 3s

    CaptivePortal --> Connected: New SSID saved & connected
    CaptivePortal --> CheckSavedWifi: 180s Portal Timeout (no new SSID)

    Connected --> RuntimeWatchdog: Start mDNS, OTA, WebUI, MQTT

    state RuntimeWatchdog {
        [*] --> Online
        Online --> Reconnecting: WiFi.status != WL_CONNECTED
        Reconnecting --> Online: Reconnected within 120s
        Reconnecting --> TriggerAP: Disconnected >= 120s
    }

    RuntimeWatchdog --> OpenPortalRestart: IO8 3s Hold / Web Button / 120s Loss
    OpenPortalRestart --> CaptivePortalRestart: Stop WebUI & Publish MQTT offline
    CaptivePortalRestart --> [*]: ESP.restart() after save or 180s timeout
```

---

## 4. Multi-Source Relay Control & Real-Time Synchronization

All control paths converge on `setRelay()` (or `setAllRelays()` for batch updates) so hardware, NVS, Home Assistant, and every open browser stay synchronized.

```mermaid
sequenceDiagram
    autonumber
    participant User as Browser Web UI
    participant HA as Home Assistant (MQTT)
    participant Btn as Physical Button (IO2)
    participant Relays as relays.cpp
    participant NVS as Config / NVS
    participant GPIO as 8x Relay Pins

    alt Browser toggles a relay
        User->>Relays: WS "TOGGLE:<pin>" -> toggleSingleRelayByPin()
    else Home Assistant sends command
        HA->>Relays: MQTT ".../relayN/set" ("ON"/"OFF") -> setRelay()
    else User presses IO2 button
        Btn->>Relays: Falling edge (50ms debounce) -> smartToggleAllRelays()
    end

    Relays->>GPIO: digitalWrite(pin, HIGH/LOW)
    opt Power-On Mode == Restore Last State
        Relays->>NVS: saveRelayMask(8-bit bitmask)
    end
    Relays->>HA: Publish ".../relayN/state" (retained)
    Relays->>User: WebSocket broadcastJSON (all clients)
```

---

## 5. Non-Blocking FreeRTOS MQTT Probe & LWT Flow

Because DNS resolution (`homeassistant.local`) or a down broker can block TCP sockets for several seconds, probing runs on a low-priority FreeRTOS task guarded by `probeMutex`.

```mermaid
flowchart TD
    subgraph ProbeTask["FreeRTOS Task: MQTTProbeTask (Priority 1, 5s Period)"]
        P1{"WiFi connected &\nMQTT disconnected &\n!mqttPaused?"}
        P1 -- No --> Delay5s["vTaskDelay(5000 ms)"]
        P1 -- Yes --> P2["WiFi.hostByName(mqtt_server)"]
        P2 -- Resolved --> P3["TCP connect probe (1500 ms timeout)"]
        P3 -- Success --> P4["Lock probeMutex\nSet mqttResolvedIP & mqttBrokerReachable=true\nUnlock probeMutex"]
        P2 -- Fail --> Delay5s
        P3 -- Fail --> Delay5s
        P4 --> Delay5s
        Delay5s --> P1
    end

    subgraph MainLoop["Arduino loop() -> handleMqtt()"]
        M1{"mqttClient.connected()?"}
        M1 -- Yes --> M2["mqttClient.loop()"]
        M1 -- No --> M3["Lock probeMutex\nRead reachable & copy targetIP\nReset flag, Unlock"]
        M3 -- Reachable --> M4["mqttClient.connect() with LWT\nTopic: .../status, Payload: offline"]
        M4 -- Connected --> M5["Publish Birth 'online' (retained)\nSubscribe .../relay0..7/set\nPublish 8x HA Discovery JSON"]
    end
```

---

## 6. OTA Firmware Update Workflows

```mermaid
flowchart LR
    subgraph WebOTA["Browser OTA (/update)"]
        B1["User uploads firmware.bin"] --> B2["HTTP Basic Auth Check\n(if web_pass set)"]
        B2 --> B3["publishMqttOffline()"]
        B3 --> B4["Update.begin() -> write() -> end()"]
        B4 --> B5["HTTP 200 + ESP.restart()"]
    end

    subgraph IDEOTA["PlatformIO / ArduinoOTA (:3232)"]
        I1["pio run -t upload --upload-port hostname.local"] --> I2["Optional OTA Password Check"]
        I2 --> I3["onStart -> publishMqttOffline()"]
        I3 --> I4["Flash & Auto Reboot"]
    end
```
