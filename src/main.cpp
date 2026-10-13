#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <PubSubClient.h>

#define Module_Name module7

Preferences prefs;
char hostname[33] = "Module_Name";
char mqtt_server[40] = "homeassistant.local";
char mqtt_port[6] = "1883";
char mqtt_user[32] = "esp32_user";
char mqtt_pass[32] = "mqttpass123";

// Servers
WebServer server(80);
WebSocketsServer webSocket(81);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

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

unsigned long lastMqttReconnectAttempt = 0;

// JSON payload of relay states for WebSocket
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

// Publish state to MQTT for Home Assistant
void publishMqttState(size_t index)
{
  if (mqttClient.connected())
  {
    String topic = String("homeassistant/switch/") + hostname + "/relay" + String(index) + "/state";
    mqttClient.publish(topic.c_str(), relayStates[index] ? "ON" : "OFF", true);
  }
}

// Centralized state switcher: synchronizes Hardware, Web UI, and Home Assistant
void setRelay(size_t index, bool state)
{
  if (index >= relayCount)
    return;
  relayStates[index] = state;
  digitalWrite(relayPins[index], state ? RELAY_ACTIVE : RELAY_INACTIVE);
  publishMqttState(index);
  broadcastStates();
}

void toggleSingleRelay(int pin)
{
  for (size_t i = 0; i < relayCount; i++)
  {
    if (relayPins[i] == pin)
    {
      setRelay(i, !relayStates[i]);
      return;
    }
  }
}

void toggleAllRelays()
{
  for (size_t i = 0; i < relayCount; i++)
  {
    setRelay(i, !relayStates[i]);
  }
}

void turnOffAllRelays()
{
  for (size_t i = 0; i < relayCount; i++)
  {
    setRelay(i, false);
  }
}

// Publish Home Assistant Auto-Discovery Configuration
void sendHADiscovery()
{
  for (size_t i = 0; i < relayCount; i++)
  {
    String discoveryTopic = String("homeassistant/switch/") + hostname + "/relay" + String(i) + "/config";
    String stateTopic = String("homeassistant/switch/") + hostname + "/relay" + String(i) + "/state";
    String cmdTopic = String("homeassistant/switch/") + hostname + "/relay" + String(i) + "/set";

    String payload = "{"
                     "\"name\":\"Relay " +
                     String(i + 1) + "\","
                                     "\"unique_id\":\"" +
                     String(hostname) + "_relay_" + String(i) + "\","
                                                                "\"cmd_t\":\"" +
                     cmdTopic + "\","
                                "\"stat_t\":\"" +
                     stateTopic + "\","
                                  "\"pl_on\":\"ON\","
                                  "\"pl_off\":\"OFF\","
                                  "\"dev\":{"
                                  "\"ids\":[\"" +
                     String(hostname) + "\"],"
                                        "\"name\":\"" +
                     String(hostname) + " Controller\","
                                        "\"mf\":\"DIY\","
                                        "\"mdl\":\"ESP32-C3 Relay Board\""
                                        "}"
                                        "}";

    mqttClient.publish(discoveryTopic.c_str(), payload.c_str(), true);
    publishMqttState(i);
  }
}

// Incoming MQTT messages from Home Assistant
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String message;
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  String strTopic = String(topic);
  for (size_t i = 0; i < relayCount; i++)
  {
    String cmdTopic = String("homeassistant/switch/") + hostname + "/relay" + String(i) + "/set";
    if (strTopic == cmdTopic)
    {
      if (message == "ON")
      {
        setRelay(i, true);
      }
      else if (message == "OFF")
      {
        setRelay(i, false);
      }
    }
  }
}

// Non-blocking MQTT reconnection loop
void reconnectMQTT()
{
  // Inside reconnectMQTT(), right before mqttClient.connect(...)
  Serial.printf("Connecting as User: '%s' | Pass: '%s'\n", mqtt_user, mqtt_pass);
  if (strlen(mqtt_server) == 0)
    return;

  if (!mqttClient.connected())
  {
    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt > 5000)
    {
      lastMqttReconnectAttempt = now;
      Serial.print("Connecting to MQTT Broker (");
      Serial.print(mqtt_server);
      Serial.print(")...");

      String clientId = String(hostname) + "-" + String(random(0xffff), HEX);
      bool connected = false;

      if (strlen(mqtt_user) > 0)
      {
        connected = mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass);
      }
      else
      {
        connected = mqttClient.connect(clientId.c_str());
      }

      if (connected)
      {
        Serial.println(" Connected!");
        // Subscribe to commands for all relays
        for (size_t i = 0; i < relayCount; i++)
        {
          String cmdTopic = String("homeassistant/switch/") + hostname + "/relay" + String(i) + "/set";
          mqttClient.subscribe(cmdTopic.c_str());
        }
        sendHADiscovery();
      }
      else
      {
        Serial.print(" Failed, rc=");
        Serial.println(mqttClient.state());
      }
    }
  }
}

// Captive portal launcher
void openConfigPortal()
{
  Serial.println("\n[Portal] Stopping background services...");
  server.stop();
  webSocket.close();
  delay(100);

  WiFi.disconnect(false, false);
  delay(100);
  WiFi.mode(WIFI_AP);

  WiFiManager wm;
  wm.setCaptivePortalEnable(true);
  wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

  WiFiManagerParameter custom_hostname("hostname_id", "Custom Hostname (.local)", hostname, 32);
  WiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT Broker (IP or Hostname)", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("mqtt_user", "MQTT Username (optional)", mqtt_user, 32);
  WiFiManagerParameter custom_mqtt_pass("mqtt_pass", "MQTT Password (optional)", mqtt_pass, 32);

  wm.addParameter(&custom_hostname);
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.setConfigPortalTimeout(180);

  if (wm.startConfigPortal("ESP32-Module_Name-AP"))
  {
    prefs.begin("settings", false);
    prefs.putString("hostname", custom_hostname.getValue());
    prefs.putString("mqtt_server", custom_mqtt_server.getValue());
    prefs.putString("mqtt_port", custom_mqtt_port.getValue());
    prefs.putString("mqtt_user", custom_mqtt_user.getValue());
    prefs.putString("mqtt_pass", custom_mqtt_pass.getValue());
    prefs.end();
  }

  delay(500);
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
    else if (msg == "TOGGLE_ALL")
    {
      toggleAllRelays();
    }
    else if (msg == "ALL_OFF")
    {
      turnOffAllRelays();
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
    h1 { font-size: 1.5rem; margin-bottom: 6px; }
    #status { font-size: 0.85rem; color: #94a3b8; margin-bottom: 20px; }
    .master-controls { display: flex; gap: 12px; width: 100%; max-width: 400px; margin-bottom: 18px; }
    .btn-master { flex: 1; padding: 12px; font-size: 0.95rem; font-weight: 700; border: none; border-radius: 8px; cursor: pointer; }
    .btn-all-off { background: #dc2626; color: #ffffff; }
    .btn-toggle-all { background: #2563eb; color: #ffffff; }
    .grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 16px; width: 100%; max-width: 400px; }
    .card { background: #1e293b; border-radius: 12px; padding: 16px; text-align: center; }
    .label { font-size: 0.9rem; color: #94a3b8; margin-bottom: 12px; }
    .btn-relay { width: 100%; padding: 14px; font-size: 1rem; font-weight: 700; border: none; border-radius: 8px; cursor: pointer; transition: all 0.15s ease; }
    .btn-off { background: #334155; color: #cbd5e1; }
    .btn-on { background: #16a34a; color: #ffffff; box-shadow: 0 0 12px rgba(22, 163, 74, 0.5); }
  </style>
</head>
<body>
  <h1>Relay Controller</h1>
  <div id="status">Connecting WebSocket...</div>
  <div class="master-controls">
    <button class="btn-master btn-all-off" onclick="sendAction('ALL_OFF')">Turn All OFF</button>
    <button class="btn-master btn-toggle-all" onclick="sendAction('TOGGLE_ALL')">Toggle All</button>
  </div>
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
        <button id="btn-${pin}" class="btn-relay btn-off" onclick="sendAction('TOGGLE:' + ${pin})">OFF</button>
      `;
      grid.appendChild(card);
    });

    function updateButton(pin, isOn) {
      const btn = document.getElementById(`btn-${pin}`);
      if (!btn) return;
      btn.className = 'btn-relay ' + (isOn ? 'btn-on' : 'btn-off');
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

    function sendAction(cmd) {
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(cmd);
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

  // Buttons
  pinMode(MASTER_BTN_PIN, INPUT_PULLUP);
  pinMode(CONFIG_BTN_PIN, INPUT_PULLUP);

  // Load preferences
  prefs.begin("settings", false);
  prefs.getString("hostname", "Module_Name").toCharArray(hostname, sizeof(hostname));
  prefs.getString("mqtt_server", "homeassistant.local").toCharArray(mqtt_server, sizeof(mqtt_server));
  prefs.getString("mqtt_port", "1883").toCharArray(mqtt_port, sizeof(mqtt_port));
  prefs.getString("mqtt_user", "").toCharArray(mqtt_user, sizeof(mqtt_user));
  prefs.getString("mqtt_pass", "").toCharArray(mqtt_pass, sizeof(mqtt_pass));
  prefs.end();

  // WiFiManager
  WiFiManager wm;
  WiFiManagerParameter custom_hostname("hostname_id", "Custom Hostname (.local)", hostname, 32);
  WiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT Broker (IP or Hostname)", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("mqtt_user", "MQTT Username (optional)", mqtt_user, 32);
  WiFiManagerParameter custom_mqtt_pass("mqtt_pass", "MQTT Password (optional)", mqtt_pass, 32);

  wm.addParameter(&custom_hostname);
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.setHostname(hostname);

  if (!wm.autoConnect("ESP32-Module_Name-AP"))
  {
    ESP.restart();
  }

  // mDNS
  if (MDNS.begin(hostname))
  {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
  }

  // Configure MQTT Client with 512-byte buffer for HA Discovery JSON
  mqttClient.setServer(mqtt_server, atoi(mqtt_port));
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);

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

  // Handle MQTT connection in background
  if (WiFi.status() == WL_CONNECTED)
  {
    if (!mqttClient.connected())
    {
      reconnectMQTT();
    }
    else
    {
      mqttClient.loop();
    }
  }

  unsigned long currentMillis = millis();

  // IO2 (Master Toggle All)
  int masterReading = digitalRead(MASTER_BTN_PIN);
  if (masterReading != lastMasterBtnState)
    lastMasterDebounce = currentMillis;
  if ((currentMillis - lastMasterDebounce) > debounceDelay)
  {
    static int masterState = HIGH;
    if (masterReading != masterState)
    {
      masterState = masterReading;
      if (masterState == LOW)
        toggleAllRelays();
    }
  }
  lastMasterBtnState = masterReading;

  // IO8 (Open Portal)
  int configReading = digitalRead(CONFIG_BTN_PIN);
  if (configReading != lastConfigBtnState)
    lastConfigDebounce = currentMillis;
  if ((currentMillis - lastConfigDebounce) > debounceDelay)
  {
    static int configState = HIGH;
    if (configReading != configState)
    {
      configState = configReading;
      if (configState == LOW)
        openConfigPortal();
    }
  }
  lastConfigBtnState = configReading;
}