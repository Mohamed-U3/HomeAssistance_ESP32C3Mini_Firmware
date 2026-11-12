/**
 * @file main.cpp
 * @brief Firmware entry point, ArduinoOTA setup, Wi-Fi watchdog, and main loop orchestration.
 */
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"
#include "mqtt_ha.h"
#include "relays.h"
#include "web_ui.h"

static bool portalTriggerRequested = false;   ///< Set when IO8 is held during startup Wi-Fi search
static unsigned long wifiDisconnectedSince = 0; ///< Timestamp (ms) when runtime Wi-Fi loss began
static unsigned long lastWifiRetry = 0;       ///< Timestamp (ms) of the last runtime reconnect attempt

/** @brief Callback invoked if IO8 is held for 3 seconds during startup Wi-Fi search. */
static void onPortalButtonDuringSearch()
{
  portalTriggerRequested = true;
}

/**
 * @brief Tick handler passed into Wi-Fi search and portal loops to keep physical buttons active.
 * @param allowPortalTrigger True during STA search to allow early jump into Config Portal.
 * @return True if @ref onPortalButtonDuringSearch triggered.
 */
static bool wifiWaitTick(bool allowPortalTrigger)
{
  portalTriggerRequested = false;
  handleButtons(allowPortalTrigger ? onPortalButtonDuringSearch : nullptr);
  return portalTriggerRequested;
}

/**
 * @brief Gracefully stop network services, open the SoftAP Config Portal, and restart.
 */
void openConfigPortal()
{
  Serial.println("\n[Portal] Stopping background services...");

  publishMqttOffline();
  stopWebUi();
  delay(100);

  runConfigPortal(CONFIG_PORTAL_TIMEOUT_SEC, wifiWaitTick);

  delay(500);
  ESP.restart();
}

/** @brief Configure and start network OTA updates via ArduinoOTA. */
static void setupArduinoOta()
{
  ArduinoOTA.setHostname(appConfig.hostname);
  if (strlen(appConfig.web_pass) > 0)
  {
    ArduinoOTA.setPassword(appConfig.web_pass);
  }

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Start updating...");
    publishMqttOffline();
  });
  ArduinoOTA.onEnd([]() { Serial.println("\n[OTA] Update complete."); });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]\n", static_cast<unsigned>(error));
  });
  ArduinoOTA.begin();
}

/** @brief Initialize NVS configuration, relays, Wi-Fi failover, mDNS, OTA, Web UI, and MQTT. */
void setup()
{
  Serial.begin(115200);

  loadConfig();
  setRelayCallbacks(publishMqttState, broadcastStates);
  initRelays();

  ensureWifiConnected(wifiWaitTick);

  if (MDNS.begin(appConfig.hostname))
  {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
  }

  setupArduinoOta();

  Serial.println("\n--- WiFi Connected ---");
  Serial.print("AP Name:    ");
  Serial.println(getApSsid());
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("Host URL:   http://%s.local\n", appConfig.hostname);
  Serial.println("----------------------\n");

  initWebUi(openConfigPortal);
  initMqtt();
}

/** @brief Service Web UI, OTA, MQTT, physical buttons, and the 2-minute Wi-Fi loss watchdog. */
void loop()
{
  handleWebUi();
  ArduinoOTA.handle();
  handleMqtt();
  handleButtons(openConfigPortal);

  // Monitor runtime Wi-Fi connectivity: retry for 2 minutes, then open Config Portal AP
  if (WiFi.status() == WL_CONNECTED)
  {
    wifiDisconnectedSince = 0;
  }
  else
  {
    unsigned long now = millis();
    if (wifiDisconnectedSince == 0)
    {
      wifiDisconnectedSince = now;
      lastWifiRetry = now;
      Serial.println("[WiFi] Connection lost. Searching for saved SSID for 2 minutes...");
    }
    else if (now - lastWifiRetry >= WIFI_RETRY_INTERVAL_MS)
    {
      lastWifiRetry = now;
      WiFi.reconnect();
    }

    if (now - wifiDisconnectedSince >= WIFI_SEARCH_TIMEOUT_MS)
    {
      Serial.println("[WiFi] Saved SSID unreachable for 2 minutes. Opening Config Portal AP...");
      openConfigPortal();
    }
  }
}
