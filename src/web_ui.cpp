/**
 * @file web_ui.cpp
 * @brief Implementation of the HTTP dashboard, WebSocket server, and browser OTA endpoint.
 */
#include "web_ui.h"

#include "config.h"
#include "mqtt_ha.h"
#include "relays.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <ctype.h>

static WebServer server(80);
static WebSocketsServer webSocket(81);

static PortalActionCallback portalActionCb = nullptr;
static bool pendingPortal = false;
static bool pendingReboot = false;
static unsigned long pendingActionAt = 0;

static unsigned long lastStatusBroadcast = 0;
static bool lastKnownMqtt = false;
static int lastKnownRssi = 0;

static bool checkAuth()
{
  if (strlen(appConfig.web_pass) == 0)
    return true;
  if (!server.authenticate("admin", appConfig.web_pass))
  {
    server.requestAuthentication();
    return false;
  }
  return true;
}

static String buildStateJson()
{
  JsonDocument doc;
  doc["hostname"] = appConfig.hostname;
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["mqtt"] = isMqttConnected();
  doc["powerOnMode"] = appConfig.power_on_mode;

  JsonArray relays = doc["relays"].to<JsonArray>();
  for (size_t i = 0; i < RELAY_COUNT; i++)
  {
    JsonObject item = relays.add<JsonObject>();
    item["pin"] = RELAY_PINS[i];
    item["state"] = getRelayState(i);
  }

  String out;
  serializeJson(doc, out);
  return out;
}

void broadcastStates()
{
  String json = buildStateJson();
  webSocket.broadcastTXT(json);
  lastKnownMqtt = isMqttConnected();
  lastKnownRssi = WiFi.RSSI();
}

static bool isPositiveInteger(const String &s)
{
  if (s.length() == 0)
    return false;
  for (unsigned int i = 0; i < s.length(); i++)
  {
    if (!isdigit(static_cast<unsigned char>(s[i])))
      return false;
  }
  return true;
}

static void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  if (type == WStype_CONNECTED)
  {
    String json = buildStateJson();
    webSocket.sendTXT(num, json);
  }
  else if (type == WStype_TEXT)
  {
    // Build bounded string using length (WebSockets payload is not guaranteed null-terminated)
    String msg;
    msg.reserve(length);
    for (size_t i = 0; i < length; i++)
      msg += static_cast<char>(payload[i]);

    if (msg.startsWith("TOGGLE:"))
    {
      String pinStr = msg.substring(7);
      if (isPositiveInteger(pinStr))
        toggleSingleRelayByPin(pinStr.toInt());
    }
    else if (msg == "ALL_ON")
    {
      setAllRelays(true);
    }
    else if (msg == "ALL_OFF")
    {
      setAllRelays(false);
    }
    else if (msg == "TOGGLE_ALL")
    {
      toggleAllRelays();
    }
    else if (msg == "SMART_TOGGLE")
    {
      smartToggleAllRelays();
    }
    else if (msg.startsWith("SET_POWER_MODE:"))
    {
      String modeStr = msg.substring(15);
      if (isPositiveInteger(modeStr))
      {
        int mode = modeStr.toInt();
        if (mode >= POWER_ON_ALL_OFF && mode <= POWER_ON_RESTORE)
        {
          savePowerOnMode(static_cast<uint8_t>(mode));
          if (appConfig.power_on_mode == POWER_ON_RESTORE)
            persistCurrentRelayMask();
          broadcastStates();
        }
      }
    }
  }
}

/// @cond INTERNAL
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-C3 Relays</title>
  <style>
    * { box-sizing: border-box; font-family: system-ui, -apple-system, sans-serif; }
    body { background: #0f172a; color: #f8fafc; display: flex; flex-direction: column; align-items: center; padding: 24px 16px; margin: 0; }
    h1 { font-size: 1.5rem; margin: 0 0 6px; }
    .meta { font-size: 0.82rem; color: #94a3b8; margin-bottom: 10px; text-align: center; }
    .badges { display: flex; flex-wrap: wrap; justify-content: center; gap: 8px; margin-bottom: 18px; }
    .badge { font-size: 0.78rem; padding: 4px 10px; border-radius: 999px; background: #1e293b; color: #cbd5e1; }
    .badge.ok { background: rgba(22, 163, 74, 0.2); color: #4ade80; }
    .badge.err { background: rgba(220, 38, 38, 0.2); color: #f87171; }
    .master-controls { display: flex; gap: 10px; width: 100%; max-width: 420px; margin-bottom: 16px; }
    .btn-master { flex: 1; padding: 11px 8px; font-size: 0.9rem; font-weight: 700; border: none; border-radius: 8px; cursor: pointer; color: #fff; }
    .btn-all-on { background: #16a34a; }
    .btn-all-off { background: #dc2626; }
    .btn-toggle-all { background: #2563eb; }
    .grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 14px; width: 100%; max-width: 420px; }
    .card { background: #1e293b; border-radius: 12px; padding: 14px; text-align: center; }
    .label { font-size: 0.88rem; color: #94a3b8; margin-bottom: 10px; }
    .btn-relay { width: 100%; padding: 14px; font-size: 1rem; font-weight: 700; border: none; border-radius: 8px; cursor: pointer; transition: all 0.15s ease; }
    .btn-off { background: #334155; color: #cbd5e1; }
    .btn-on { background: #16a34a; color: #ffffff; box-shadow: 0 0 12px rgba(22, 163, 74, 0.5); }
    .panel { width: 100%; max-width: 420px; margin-top: 18px; background: #1e293b; border-radius: 12px; padding: 14px; }
    .panel-row { display: flex; align-items: center; justify-content: space-between; gap: 10px; margin-bottom: 12px; font-size: 0.88rem; color: #cbd5e1; }
    select { background: #0f172a; color: #f8fafc; border: 1px solid #334155; border-radius: 6px; padding: 6px 10px; font-size: 0.85rem; }
    .actions { display: flex; gap: 8px; }
    .btn-sec { flex: 1; padding: 9px; font-size: 0.82rem; font-weight: 600; border: 1px solid #334155; border-radius: 8px; background: #0f172a; color: #cbd5e1; cursor: pointer; text-decoration: none; text-align: center; }
    .btn-sec:hover { background: #334155; }
  </style>
</head>
<body>
  <h1 id="device-title">Relay Controller</h1>
  <div class="meta" id="device-meta">Loading device info...</div>
  <div class="badges">
    <span class="badge err" id="ws-badge">WS: Connecting...</span>
    <span class="badge err" id="mqtt-badge">MQTT: Unknown</span>
  </div>

  <div class="master-controls">
    <button class="btn-master btn-all-on" onclick="sendAction('ALL_ON')">All ON</button>
    <button class="btn-master btn-all-off" onclick="sendAction('ALL_OFF')">All OFF</button>
    <button class="btn-master btn-toggle-all" onclick="sendAction('TOGGLE_ALL')">Toggle All</button>
  </div>

  <div class="grid" id="relay-grid"></div>

  <div class="panel">
    <div class="panel-row">
      <label for="power-mode">Power-On State</label>
      <select id="power-mode" onchange="sendAction('SET_POWER_MODE:' + this.value)">
        <option value="0">All OFF</option>
        <option value="1">All ON</option>
        <option value="2">Restore Last State</option>
      </select>
    </div>
    <div class="actions">
      <a class="btn-sec" href="/update">OTA Update</a>
      <button class="btn-sec" onclick="triggerEndpoint('/api/portal', 'Enter Wi-Fi/MQTT Config Portal? Device will restart into AP mode.')">Config Portal</button>
      <button class="btn-sec" onclick="triggerEndpoint('/api/reboot', 'Reboot the controller now?')">Reboot</button>
    </div>
  </div>

  <script>
    const grid = document.getElementById('relay-grid');
    const wsBadge = document.getElementById('ws-badge');
    const mqttBadge = document.getElementById('mqtt-badge');
    const titleEl = document.getElementById('device-title');
    const metaEl = document.getElementById('device-meta');
    const powerModeEl = document.getElementById('power-mode');

    let renderedPins = [];
    let socket;

    function renderGrid(relays) {
      const pins = relays.map(r => r.pin);
      if (pins.length === renderedPins.length && pins.every((p, idx) => p === renderedPins[idx])) {
        return;
      }
      renderedPins = pins;
      grid.innerHTML = '';
      relays.forEach((item, i) => {
        const card = document.createElement('div');
        card.className = 'card';
        card.innerHTML = `
          <div class="label">Relay ${i + 1} (IO${item.pin})</div>
          <button id="btn-${item.pin}" class="btn-relay btn-off" onclick="sendAction('TOGGLE:' + ${item.pin})">OFF</button>
        `;
        grid.appendChild(card);
      });
    }

    function updateButton(pin, isOn) {
      const btn = document.getElementById(`btn-${pin}`);
      if (!btn) return;
      btn.className = 'btn-relay ' + (isOn ? 'btn-on' : 'btn-off');
      btn.innerText = isOn ? 'ON' : 'OFF';
    }

    function applyState(data) {
      if (data.hostname) {
        titleEl.innerText = `${data.hostname} Controller`;
        document.title = `${data.hostname} Relays`;
      }
      if (data.ip !== undefined && data.rssi !== undefined) {
        metaEl.innerText = `IP: ${data.ip}  |  Wi-Fi: ${data.rssi} dBm`;
      }
      if (data.mqtt !== undefined) {
        mqttBadge.innerText = data.mqtt ? 'MQTT: Connected' : 'MQTT: Disconnected';
        mqttBadge.className = 'badge ' + (data.mqtt ? 'ok' : 'err');
      }
      if (data.powerOnMode !== undefined) {
        powerModeEl.value = String(data.powerOnMode);
      }
      if (Array.isArray(data.relays)) {
        renderGrid(data.relays);
        data.relays.forEach(item => updateButton(item.pin, item.state));
      }
    }

    function connectWS() {
      socket = new WebSocket(`ws://${window.location.hostname}:81/`);
      socket.onopen = () => {
        wsBadge.innerText = 'WS: Connected';
        wsBadge.className = 'badge ok';
      };
      socket.onmessage = (event) => {
        const data = JSON.parse(event.data);
        applyState(data);
      };
      socket.onclose = () => {
        wsBadge.innerText = 'WS: Reconnecting...';
        wsBadge.className = 'badge err';
        setTimeout(connectWS, 2000);
      };
    }

    function sendAction(cmd) {
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(cmd);
      }
    }

    function triggerEndpoint(path, confirmMsg) {
      if (!confirm(confirmMsg)) return;
      fetch(path, { method: 'POST' })
        .then(r => r.text())
        .then(msg => { metaEl.innerText = msg; })
        .catch(() => { metaEl.innerText = 'Request failed'; });
    }

    connectWS();
  </script>
</body>
</html>
)rawliteral";

static const char OTA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>OTA Firmware Update</title>
  <style>
    * { box-sizing: border-box; font-family: system-ui, -apple-system, sans-serif; }
    body { background: #0f172a; color: #f8fafc; display: flex; flex-direction: column; align-items: center; padding: 32px 16px; margin: 0; }
    .box { width: 100%; max-width: 400px; background: #1e293b; border-radius: 12px; padding: 20px; }
    h1 { font-size: 1.3rem; margin: 0 0 14px; }
    input[type=file] { width: 100%; margin-bottom: 14px; color: #cbd5e1; }
    button { width: 100%; padding: 12px; font-weight: 700; border: none; border-radius: 8px; background: #2563eb; color: #fff; cursor: pointer; }
    button:disabled { opacity: 0.6; cursor: not-allowed; }
    #status { margin-top: 12px; font-size: 0.9rem; color: #94a3b8; }
    a { display: inline-block; margin-top: 14px; color: #60a5fa; text-decoration: none; font-size: 0.88rem; }
  </style>
</head>
<body>
  <div class="box">
    <h1>Firmware OTA Update</h1>
    <form id="ota-form">
      <input type="file" id="firmware" name="update" accept=".bin" required>
      <button type="submit" id="upload-btn">Upload &amp; Flash</button>
    </form>
    <div id="status">Select a compiled firmware.bin file.</div>
    <a href="/">&larr; Back to Relays</a>
  </div>
  <script>
    const form = document.getElementById('ota-form');
    const status = document.getElementById('status');
    const btn = document.getElementById('upload-btn');
    form.addEventListener('submit', (e) => {
      e.preventDefault();
      const fileInput = document.getElementById('firmware');
      if (!fileInput.files.length) return;
      const data = new FormData();
      data.append('update', fileInput.files[0]);
      btn.disabled = true;
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/update', true);
      xhr.upload.onprogress = (ev) => {
        if (ev.lengthComputable) {
          status.innerText = `Uploading: ${Math.round((ev.loaded / ev.total) * 100)}%`;
        }
      };
      xhr.onload = () => {
        status.innerText = xhr.status === 200 ? 'Update complete! Rebooting...' : `Update failed: ${xhr.responseText}`;
        if (xhr.status === 200) setTimeout(() => { window.location.href = '/'; }, 8000);
        else btn.disabled = false;
      };
      xhr.onerror = () => {
        status.innerText = 'Upload error.';
        btn.disabled = false;
      };
      xhr.send(data);
    });
  </script>
</body>
</html>
)rawliteral";
/// @endcond

void initWebUi(PortalActionCallback onPortalAction)
{
  portalActionCb = onPortalAction;
  pendingPortal = false;
  pendingReboot = false;

  server.on("/", HTTP_GET, []() {
    if (!checkAuth())
      return;
    server.send(200, "text/html", INDEX_HTML);
  });

  server.on("/api/status", HTTP_GET, []() {
    if (!checkAuth())
      return;
    server.send(200, "application/json", buildStateJson());
  });

  server.on("/api/portal", HTTP_POST, []() {
    if (!checkAuth())
      return;
    server.send(200, "text/plain", "Opening configuration portal...");
    pendingPortal = true;
    pendingActionAt = millis() + 300;
  });

  server.on("/api/reboot", HTTP_POST, []() {
    if (!checkAuth())
      return;
    server.send(200, "text/plain", "Rebooting controller...");
    pendingReboot = true;
    pendingActionAt = millis() + 300;
  });

  server.on("/update", HTTP_GET, []() {
    if (!checkAuth())
      return;
    server.send(200, "text/html", OTA_HTML);
  });

  server.on(
      "/update",
      HTTP_POST,
      []() {
        if (!checkAuth())
          return;
        bool ok = !Update.hasError();
        server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
        if (ok)
        {
          pendingReboot = true;
          pendingActionAt = millis() + 500;
        }
      },
      []() {
        if (!checkAuth())
          return;
        HTTPUpload &upload = server.upload();
        if (upload.status == UPLOAD_FILE_START)
        {
          Serial.printf("[OTA] Update start: %s\n", upload.filename.c_str());
          publishMqttOffline();
          if (!Update.begin(UPDATE_SIZE_UNKNOWN))
          {
            Update.printError(Serial);
          }
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
          if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
          {
            Update.printError(Serial);
          }
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
          if (Update.end(true))
          {
            Serial.printf("[OTA] Update success: %u bytes\n", static_cast<unsigned>(upload.totalSize));
          }
          else
          {
            Update.printError(Serial);
          }
        }
      });

  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void handleWebUi()
{
  server.handleClient();
  webSocket.loop();

  unsigned long now = millis();
  if (pendingPortal && now >= pendingActionAt)
  {
    pendingPortal = false;
    if (portalActionCb)
      portalActionCb();
  }
  if (pendingReboot && now >= pendingActionAt)
  {
    pendingReboot = false;
    publishMqttOffline();
    delay(100);
    ESP.restart();
  }

  // Push periodic updates to WebSocket clients when MQTT or RSSI changes noticeably
  if (now - lastStatusBroadcast >= 5000)
  {
    lastStatusBroadcast = now;
    bool mqttNow = isMqttConnected();
    int rssiNow = WiFi.RSSI();
    if (mqttNow != lastKnownMqtt || abs(rssiNow - lastKnownRssi) >= 4)
    {
      broadcastStates();
    }
  }
}

void stopWebUi()
{
  server.stop();
  webSocket.close();
}
