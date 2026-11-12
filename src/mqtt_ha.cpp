/**
 * @file mqtt_ha.cpp
 * @brief Implementation of Home Assistant MQTT discovery, LWT, and FreeRTOS broker probing.
 */
#include "mqtt_ha.h"

#include "config.h"
#include "relays.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static WiFiClient espClient;
static PubSubClient mqttClient(espClient);

// Thread-safe probe state shared between mqttProbeTask and main loop
static SemaphoreHandle_t probeMutex = nullptr;
static bool mqttBrokerReachable = false;
static IPAddress mqttResolvedIP;
static volatile bool mqttPaused = false;

static unsigned long lastMqttReconnectAttempt = 0;

static String getAvailabilityTopic()
{
  return String("homeassistant/switch/") + appConfig.hostname + "/status";
}

static String getRelayTopic(size_t index, const char *suffix)
{
  return String("homeassistant/switch/") + appConfig.hostname + "/relay" + String(index) + "/" + suffix;
}

void publishMqttState(size_t index)
{
  if (!mqttClient.connected() || index >= RELAY_COUNT)
    return;

  String topic = getRelayTopic(index, "state");
  mqttClient.publish(topic.c_str(), getRelayState(index) ? "ON" : "OFF", true);
}

static void sendHADiscovery()
{
  String availTopic = getAvailabilityTopic();
  String deviceName = String(appConfig.hostname) + " Controller";

  for (size_t i = 0; i < RELAY_COUNT; i++)
  {
    String discoveryTopic = getRelayTopic(i, "config");
    String stateTopic = getRelayTopic(i, "state");
    String cmdTopic = getRelayTopic(i, "set");

    JsonDocument doc;
    doc["name"] = String("Relay ") + String(i + 1);
    doc["unique_id"] = String(appConfig.hostname) + "_relay_" + String(i);
    doc["cmd_t"] = cmdTopic;
    doc["stat_t"] = stateTopic;
    doc["avty_t"] = availTopic;
    doc["pl_on"] = "ON";
    doc["pl_off"] = "OFF";
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    JsonObject dev = doc["dev"].to<JsonObject>();
    JsonArray ids = dev["ids"].to<JsonArray>();
    ids.add(appConfig.hostname);
    dev["name"] = deviceName;
    dev["mf"] = "DIY";
    dev["mdl"] = "ESP32-C3 Relay Board";

    String payload;
    serializeJson(doc, payload);

    if (!mqttClient.publish(discoveryTopic.c_str(), payload.c_str(), true))
    {
      Serial.printf("[MQTT] Failed to publish discovery for relay %u (len=%u)\n",
                    static_cast<unsigned>(i), static_cast<unsigned>(payload.length()));
    }

    publishMqttState(i);
    mqttClient.loop(); // Flush buffer between retained discovery publishes
  }
}

static void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String prefix = String("homeassistant/switch/") + appConfig.hostname + "/relay";
  String strTopic(topic);
  if (!strTopic.startsWith(prefix))
    return;

  String rest = strTopic.substring(prefix.length());
  int slashIdx = rest.indexOf('/');
  if (slashIdx <= 0 || rest.substring(slashIdx) != "/set")
    return;

  String idxStr = rest.substring(0, slashIdx);
  for (unsigned int i = 0; i < idxStr.length(); i++)
  {
    if (!isdigit(static_cast<unsigned char>(idxStr[i])))
      return;
  }

  int index = idxStr.toInt();
  if (index < 0 || static_cast<size_t>(index) >= RELAY_COUNT)
    return;

  if (length == 2 && memcmp(payload, "ON", 2) == 0)
  {
    setRelay(static_cast<size_t>(index), true);
  }
  else if (length == 3 && memcmp(payload, "OFF", 3) == 0)
  {
    setRelay(static_cast<size_t>(index), false);
  }
}

static void mqttProbeTask(void *pvParameters)
{
  (void)pvParameters;

  for (;;)
  {
    bool shouldProbe = false;
    if (!mqttPaused && WiFi.status() == WL_CONNECTED && !mqttClient.connected() && strlen(appConfig.mqtt_server) > 0)
    {
      if (probeMutex && xSemaphoreTake(probeMutex, pdMS_TO_TICKS(50)) == pdTRUE)
      {
        shouldProbe = !mqttBrokerReachable;
        xSemaphoreGive(probeMutex);
      }
    }

    if (shouldProbe)
    {
      IPAddress resolved;
      int port = atoi(appConfig.mqtt_port);
      if (port <= 0 || port > 65535)
        port = 1883;

      if (WiFi.hostByName(appConfig.mqtt_server, resolved))
      {
        WiFiClient probeClient;
        if (probeClient.connect(resolved, port, 1500))
        {
          probeClient.stop();
          if (probeMutex && xSemaphoreTake(probeMutex, pdMS_TO_TICKS(100)) == pdTRUE)
          {
            mqttResolvedIP = resolved;
            mqttBrokerReachable = true;
            xSemaphoreGive(probeMutex);
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

static void reconnectMQTT()
{
  if (mqttPaused || mqttClient.connected())
    return;

  unsigned long now = millis();
  if (now - lastMqttReconnectAttempt <= 5000)
    return;

  bool reachable = false;
  IPAddress targetIP;

  if (probeMutex && xSemaphoreTake(probeMutex, pdMS_TO_TICKS(20)) == pdTRUE)
  {
    if (mqttBrokerReachable)
    {
      reachable = true;
      targetIP = mqttResolvedIP;
      mqttBrokerReachable = false; // Require a fresh probe if this attempt fails
    }
    xSemaphoreGive(probeMutex);
  }

  if (!reachable)
    return;

  lastMqttReconnectAttempt = now;

  int port = atoi(appConfig.mqtt_port);
  if (port <= 0 || port > 65535)
    port = 1883;

  mqttClient.setServer(targetIP, port);

  Serial.printf("Connecting to MQTT (%s / %s:%d)...",
                appConfig.mqtt_server, targetIP.toString().c_str(), port);

  String clientId = String(appConfig.hostname) + "-" + String(random(0xffff), HEX);
  String availTopic = getAvailabilityTopic();
  bool connected = false;

  if (strlen(appConfig.mqtt_user) > 0)
  {
    connected = mqttClient.connect(
        clientId.c_str(),
        appConfig.mqtt_user,
        appConfig.mqtt_pass,
        availTopic.c_str(),
        1,
        true,
        "offline");
  }
  else
  {
    connected = mqttClient.connect(
        clientId.c_str(),
        availTopic.c_str(),
        1,
        true,
        "offline");
  }

  if (connected)
  {
    Serial.println(" Connected!");
    mqttClient.publish(availTopic.c_str(), "online", true);

    for (size_t i = 0; i < RELAY_COUNT; i++)
    {
      String cmdTopic = getRelayTopic(i, "set");
      mqttClient.subscribe(cmdTopic.c_str());
    }

    sendHADiscovery();
  }
  else
  {
    Serial.printf(" Failed, rc=%d\n", mqttClient.state());
  }
}

void initMqtt()
{
  mqttPaused = false;
  if (!probeMutex)
    probeMutex = xSemaphoreCreateMutex();

  espClient.setTimeout(1);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024); // Fits 32-char hostnames in HA discovery payloads
  mqttClient.setKeepAlive(15);

  xTaskCreate(
      mqttProbeTask,
      "MQTTProbeTask",
      4096,
      nullptr,
      1,
      nullptr);
}

void handleMqtt()
{
  if (mqttPaused || WiFi.status() != WL_CONNECTED)
    return;

  if (!mqttClient.connected())
    reconnectMQTT();
  else
    mqttClient.loop();
}

void publishMqttOffline()
{
  mqttPaused = true;
  if (mqttClient.connected())
  {
    String availTopic = getAvailabilityTopic();
    mqttClient.publish(availTopic.c_str(), "offline", true);
    mqttClient.loop();
    mqttClient.disconnect();
  }
}

bool isMqttConnected()
{
  return mqttClient.connected();
}
