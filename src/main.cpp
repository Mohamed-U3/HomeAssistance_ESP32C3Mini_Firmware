#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>

Preferences prefs;
char hostname[33] = "module1";

// Servers
WebServer server(80);
WebSocketsServer webSocket(81);

// Relay Pin assignments
const uint8_t relayPins[] = {0, 1, 4, 5};
const size_t relayCount = sizeof(relayPins) / sizeof(relayPins[0]);
bool relayStates[] = {false, false, false, false};

// Active-HIGH relays (HIGH = ON, LOW = OFF)
#define RELAY_ACTIVE HIGH
#define RELAY_INACTIVE LOW

// Master Toggle Button (IO2)
const uint8_t MASTER_BTN_PIN = 2;
int lastMasterBtnState = HIGH;
unsigned long lastMasterDebounce = 0;

// Config Portal Button (IO8)
const uint8_t CONFIG_BTN_PIN = 8;
int lastConfigBtnState = HIGH;
unsigned long lastConfigDebounce = 0;
const unsigned long debounceDelay = 50;

// Build JSON payload of all relay states
String getStatesJson()
{
  String json = "[";
  for (size_t i = 0; i < relayCount; i++)
  {
    json += "{\"pin\":" + String(relayPins[i]) + ",\"state\":" + (relayStates[i] ? "true" : "false") + "}";
    if (i < relayCount - 1)
      json += ",";
  }
  json += "]";
  return json;
}

void broadcastStates()
{
  String json = getStatesJson();
  webSocket.broadcastTXT(json);
}

void toggleSingleRelay(int pin)
{
  for (size_t i = 0; i < relayCount; i++)
  {
    if (relayPins[i] == pin)
    {
      relayStates[i] = !relayStates[i];
      digitalWrite(pin, relayStates[i] ? RELAY_ACTIVE : RELAY_INACTIVE);
      broadcastStates();
      return;
    }
  }
}

void toggleAllRelays()
{
  for (size_t i = 0; i < relayCount; i++)
  {
    relayStates[i] = !relayStates[i];
    digitalWrite(relayPins[i], relayStates[i] ? RELAY_ACTIVE : RELAY_INACTIVE);
  }
  broadcastStates();
}

// Open captive portal on demand
void openConfigPortal()
{
  Serial.println("\n[Portal] Stopping background services...");

  // 1. Stop active servers so ports 80 and 81 are fully released
  server.stop();
  webSocket.close();
  delay(100);

  // 2. Disconnect from router WITHOUT clearing saved credentials from NVS
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.mode(WIFI_AP);

  WiFiManager wm;

  // 3. Enable captive portal detection
  wm.setCaptivePortalEnable(true);

  // 4. Force AP IP configuration
  wm.setAPStaticIPConfig(
      IPAddress(192, 168, 4, 1),
      IPAddress(192, 168, 4, 1),
      IPAddress(255, 255, 255, 0));

  // 5. Add custom hostname parameter
  WiFiManagerParameter custom_hostname("hostname_id", "Custom Hostname (.local)", hostname, 32);
  wm.addParameter(&custom_hostname);

  // 6. Set timeout (180 seconds)
  wm.setConfigPortalTimeout(180);

  // Start the portal
  if (!wm.startConfigPortal("ESP32-Config-AP"))
  {
    Serial.println("[Portal] Timed out or exited without changes.");
  }
  else
  {
    Serial.println("[Portal] Configuration saved!");
    String newHostname = custom_hostname.getValue();
    newHostname.trim();
    if (newHostname.length() > 0 && newHostname != String(hostname))
    {
      prefs.begin("settings", false);
      prefs.putString("hostname", newHostname);
      prefs.end();
      Serial.printf("[Portal] Hostname updated to: %s\n", newHostname.c_str());
    }
  }

  Serial.println("[Portal] Restarting ESP32...");
  delay(1000);
  ESP.restart();
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  if (type == WStype_CONNECTED)
  {
    String json = getStatesJson();
    webSocket.sendTXT(num, json);
  }
  else if (type == WStype_TEXT)
  {
    String msg = String((char *)payload);
    if (msg.startsWith("TOGGLE:"))
    {
      int pin = msg.substring(7).toInt();
      toggleSingleRelay(pin);
    }
  }
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-C3 Relays</title>
  <style>
    * { box-sizing: border-box; font-family: system-ui, -apple-system, sans-serif; }
    body { background: #0f172a; color: #f8fafc; display: flex; flex-direction: column; align-items: center; padding: 24px 16px; margin: 0; }
    h1 { font-size: 1.5rem; margin-bottom: 8px; }
    #status { font-size: 0.85rem; color: #94a3b8; margin-bottom: 24px; }
    .grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 16px; width: 100%; max-width: 400px; }
    .card { background: #1e293b; border-radius: 12px; padding: 16px; text-align: center; }
    .label { font-size: 0.9rem; color: #94a3b8; margin-bottom: 12px; }
    button { width: 100%; padding: 14px; font-size: 1rem; font-weight: 700; border: none; border-radius: 8px; cursor: pointer; transition: all 0.15s ease; }
    .btn-off { background: #334155; color: #cbd5e1; }
    .btn-on { background: #16a34a; color: #ffffff; box-shadow: 0 0 12px rgba(22, 163, 74, 0.5); }
  </style>
</head>
<body>
  <h1>Relay Controller</h1>
  <div id="status">Connecting WebSocket...</div>
  <div class="grid" id="relay-grid"></div>

  <script>
    const pins = [0, 1, 4, 5];
    const grid = document.getElementById('relay-grid');
    const statusDiv = document.getElementById('status');

    pins.forEach((pin, i) => {
      const card = document.createElement('div');
      card.className = 'card';
      card.innerHTML = `
        <div class="label">Relay ${i + 1} (IO${pin})</div>
        <button id="btn-${pin}" class="btn-off" onclick="sendToggle(${pin})">OFF</button>
      `;
      grid.appendChild(card);
    });

    function updateButton(pin, isOn) {
      const btn = document.getElementById(`btn-${pin}`);
      if (!btn) return;
      btn.className = isOn ? 'btn-on' : 'btn-off';
      btn.innerText = isOn ? 'ON' : 'OFF';
    }

    let socket;
    function connectWS() {
      socket = new WebSocket(`ws://${window.location.hostname}:81/`);
      socket.onopen = () => {
        statusDiv.innerText = "Live Sync: Connected";
        statusDiv.style.color = "#4ade80";
      };
      socket.onmessage = (event) => {
        const data = JSON.parse(event.data);
        if (Array.isArray(data)) {
          data.forEach(item => updateButton(item.pin, item.state));
        }
      };
      socket.onclose = () => {
        statusDiv.innerText = "Reconnecting...";
        statusDiv.style.color = "#f87171";
        setTimeout(connectWS, 2000);
      };
    }

    function sendToggle(pin) {
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(`TOGGLE:${pin}`);
      }
    }

    connectWS();
  </script>
</body>
</html>
)rawliteral";

void setup()
{
  Serial.begin(115200);

  // Configure Relays
  for (size_t i = 0; i < relayCount; i++)
  {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], RELAY_INACTIVE);
  }

  // Configure Buttons
  pinMode(MASTER_BTN_PIN, INPUT_PULLUP);
  pinMode(CONFIG_BTN_PIN, INPUT_PULLUP);

  // Read saved hostname
  prefs.begin("settings", false);
  String savedHostname = prefs.getString("hostname", "module1");
  savedHostname.toCharArray(hostname, sizeof(hostname));
  prefs.end();

  // Connect to Wi-Fi
  WiFiManager wm;
  WiFiManagerParameter custom_hostname("hostname_id", "Custom Hostname (.local)", hostname, 32);
  wm.addParameter(&custom_hostname);
  wm.setHostname(hostname);

  if (!wm.autoConnect("ESP32-Config-AP"))
  {
    Serial.println("Failed to connect, restarting...");
    ESP.restart();
  }

  // Save hostname if modified during initial boot portal
  String newHostname = custom_hostname.getValue();
  newHostname.trim();
  if (newHostname.length() > 0 && newHostname != savedHostname)
  {
    newHostname.toCharArray(hostname, sizeof(hostname));
    prefs.begin("settings", false);
    prefs.putString("hostname", hostname);
    prefs.end();
  }

  // Initialize mDNS
  if (MDNS.begin(hostname))
  {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
  }

  Serial.println("\n--- WiFi Connected ---");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("Host URL:   http://%s.local\n", hostname);
  Serial.println("----------------------\n");

  server.on("/", HTTP_GET, []()
            { server.send(200, "text/html", INDEX_HTML); });
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop()
{
  server.handleClient();
  webSocket.loop();

  unsigned long currentMillis = millis();

  // --- IO2 Handler (Toggle All Relays) ---
  int masterReading = digitalRead(MASTER_BTN_PIN);
  if (masterReading != lastMasterBtnState)
  {
    lastMasterDebounce = currentMillis;
  }
  if ((currentMillis - lastMasterDebounce) > debounceDelay)
  {
    static int masterState = HIGH;
    if (masterReading != masterState)
    {
      masterState = masterReading;
      if (masterState == LOW)
      {
        Serial.println("IO2 Pressed: Toggling all relays");
        toggleAllRelays();
      }
    }
  }
  lastMasterBtnState = masterReading;

  // --- IO8 Handler (Open Config Portal) ---
  int configReading = digitalRead(CONFIG_BTN_PIN);
  if (configReading != lastConfigBtnState)
  {
    lastConfigDebounce = currentMillis;
  }
  if ((currentMillis - lastConfigDebounce) > debounceDelay)
  {
    static int configState = HIGH;
    if (configReading != configState)
    {
      configState = configReading;
      if (configState == LOW)
      {
        openConfigPortal();
      }
    }
  }
  lastConfigBtnState = configReading;
}