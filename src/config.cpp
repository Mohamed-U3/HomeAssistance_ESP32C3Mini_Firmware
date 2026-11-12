/**
 * @file config.cpp
 * @brief Implementation of NVS settings, sanitization, and 2-minute Wi-Fi failover logic.
 */
#include "config.h"

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ctype.h>
#include <esp_wifi.h>
#include <stdlib.h>
#include <string.h>

static const char *NVS_NAMESPACE = "settings";

AppConfig appConfig = {
    DEFAULT_HOSTNAME,
    "homeassistant.local",
    "1883",
    "",
    "",
    "",
    POWER_ON_ALL_OFF};

void sanitizeHostname(char *buf, size_t bufSize)
{
  if (!buf || bufSize == 0)
    return;

  char clean[33];
  size_t out = 0;
  size_t maxCopy = (bufSize - 1 < sizeof(clean) - 1) ? (bufSize - 1) : (sizeof(clean) - 1);

  for (size_t i = 0; buf[i] != '\0' && out < maxCopy; i++)
  {
    unsigned char c = static_cast<unsigned char>(buf[i]);
    if (isalnum(c) || c == '-')
    {
      clean[out++] = static_cast<char>(c);
    }
    else if (c == '_' || c == ' ')
    {
      clean[out++] = '-';
    }
  }
  clean[out] = '\0';

  // Trim leading/trailing hyphens for valid mDNS hostnames
  size_t start = 0;
  while (clean[start] == '-')
    start++;

  size_t end = strlen(clean);
  while (end > start && clean[end - 1] == '-')
    end--;

  if (end <= start)
  {
    strlcpy(buf, DEFAULT_HOSTNAME, bufSize);
    return;
  }

  size_t len = end - start;
  if (len >= bufSize)
    len = bufSize - 1;
  memmove(buf, clean + start, len);
  buf[len] = '\0';
}

static uint8_t clampPowerOnMode(int mode)
{
  if (mode < POWER_ON_ALL_OFF || mode > POWER_ON_RESTORE)
    return POWER_ON_ALL_OFF;
  return static_cast<uint8_t>(mode);
}

static void sanitizePort(char *buf, size_t bufSize)
{
  int port = atoi(buf);
  if (port <= 0 || port > 65535)
  {
    strlcpy(buf, "1883", bufSize);
  }
  else
  {
    snprintf(buf, bufSize, "%d", port);
  }
}

void loadConfig()
{
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);

  String host = prefs.getString("hostname", DEFAULT_HOSTNAME);
  strlcpy(appConfig.hostname, host.c_str(), sizeof(appConfig.hostname));

  // Fix legacy literal "Module_Name" if previously saved in NVS
  if (strcmp(appConfig.hostname, "Module_Name") == 0)
  {
    strlcpy(appConfig.hostname, DEFAULT_HOSTNAME, sizeof(appConfig.hostname));
  }
  sanitizeHostname(appConfig.hostname, sizeof(appConfig.hostname));
  if (host != appConfig.hostname)
  {
    prefs.putString("hostname", appConfig.hostname);
  }

  strlcpy(appConfig.mqtt_server,
          prefs.getString("mqtt_server", "homeassistant.local").c_str(),
          sizeof(appConfig.mqtt_server));
  strlcpy(appConfig.mqtt_port,
          prefs.getString("mqtt_port", "1883").c_str(),
          sizeof(appConfig.mqtt_port));
  sanitizePort(appConfig.mqtt_port, sizeof(appConfig.mqtt_port));

  strlcpy(appConfig.mqtt_user,
          prefs.getString("mqtt_user", "").c_str(),
          sizeof(appConfig.mqtt_user));
  strlcpy(appConfig.mqtt_pass,
          prefs.getString("mqtt_pass", "").c_str(),
          sizeof(appConfig.mqtt_pass));
  strlcpy(appConfig.web_pass,
          prefs.getString("web_pass", "").c_str(),
          sizeof(appConfig.web_pass));

  appConfig.power_on_mode = clampPowerOnMode(prefs.getUChar("pwr_mode", POWER_ON_ALL_OFF));
  prefs.end();
}

void saveConfig()
{
  sanitizeHostname(appConfig.hostname, sizeof(appConfig.hostname));
  sanitizePort(appConfig.mqtt_port, sizeof(appConfig.mqtt_port));
  appConfig.power_on_mode = clampPowerOnMode(appConfig.power_on_mode);

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("hostname", appConfig.hostname);
  prefs.putString("mqtt_server", appConfig.mqtt_server);
  prefs.putString("mqtt_port", appConfig.mqtt_port);
  prefs.putString("mqtt_user", appConfig.mqtt_user);
  prefs.putString("mqtt_pass", appConfig.mqtt_pass);
  prefs.putString("web_pass", appConfig.web_pass);
  prefs.putUChar("pwr_mode", appConfig.power_on_mode);
  prefs.end();
}

String getApSsid()
{
  return String("ESP32-") + appConfig.hostname + "-AP";
}

uint8_t loadRelayMask()
{
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  uint8_t mask = prefs.getUChar("relay_mask", 0);
  prefs.end();
  return mask;
}

void saveRelayMask(uint8_t mask)
{
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  if (prefs.getUChar("relay_mask", 0) != mask)
  {
    prefs.putUChar("relay_mask", mask);
  }
  prefs.end();
}

void savePowerOnMode(uint8_t mode)
{
  appConfig.power_on_mode = clampPowerOnMode(mode);
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putUChar("pwr_mode", appConfig.power_on_mode);
  prefs.end();
}

String getSavedWifiSsid()
{
  // Wi-Fi must be initialized in STA mode before esp_wifi_get_config reads NVS credentials
  if ((WiFi.getMode() & WIFI_STA) == 0)
  {
    WiFi.mode(WIFI_STA);
    delay(50);
  }

  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK)
    return String();

  char ssid[33];
  memcpy(ssid, conf.sta.ssid, 32);
  ssid[32] = '\0';
  return String(ssid);
}

bool hasSavedWifi()
{
  return getSavedWifiSsid().length() > 0;
}

bool connectSavedWifi(unsigned long timeoutMs, WifiTickCallback onTick)
{
  if (!hasSavedWifi())
    return false;

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(appConfig.hostname);
  WiFi.setAutoReconnect(true);
  WiFi.begin();

  unsigned long start = millis();
  unsigned long lastRetry = start;

  while (millis() - start < timeoutMs)
  {
    if (WiFi.status() == WL_CONNECTED)
      return true;

    unsigned long now = millis();
    if (now - lastRetry >= WIFI_RETRY_INTERVAL_MS)
    {
      lastRetry = now;
      WiFi.disconnect(false, false);
      delay(50);
      WiFi.begin();
    }

    if (onTick && onTick(true))
      return false;

    delay(10);
  }

  return WiFi.status() == WL_CONNECTED;
}

static volatile bool wmShouldSave = false;
static volatile bool wmPortalTimedOut = false;

bool runConfigPortal(unsigned long timeoutSec, WifiTickCallback onTick)
{
  wmShouldSave = false;
  wmPortalTimedOut = false;

  WiFi.disconnect(false, false);
  delay(100);
  WiFi.mode(WIFI_AP);

  WiFiManager wm;
  wm.setHostname(appConfig.hostname);
  wm.setCaptivePortalEnable(true);
  wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  wm.setConfigPortalTimeout(timeoutSec);
  wm.setConfigPortalBlocking(false);
  wm.setSaveParamsCallback([]() { wmShouldSave = true; });
  wm.setSaveConfigCallback([]() { wmShouldSave = true; });
  wm.setConfigPortalTimeoutCallback([]() { wmPortalTimedOut = true; });

  char powerModeBuf[4];
  snprintf(powerModeBuf, sizeof(powerModeBuf), "%u", appConfig.power_on_mode);

  WiFiManagerParameter custom_hostname("hostname_id", "Custom Hostname (.local)", appConfig.hostname, 32);
  WiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT Broker (IP or Hostname)", appConfig.mqtt_server, 39);
  WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", appConfig.mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_user("mqtt_user", "MQTT Username (optional)", appConfig.mqtt_user, 31);
  WiFiManagerParameter custom_mqtt_pass("mqtt_pass", "MQTT Password (optional)", appConfig.mqtt_pass, 31);
  WiFiManagerParameter custom_web_pass("web_pass", "Web UI / OTA Password (optional, user: admin)", appConfig.web_pass, 31);
  WiFiManagerParameter custom_pwr_mode("pwr_mode", "Power-On Mode (0=OFF, 1=ON, 2=Restore)", powerModeBuf, 2);

  wm.addParameter(&custom_hostname);
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_web_pass);
  wm.addParameter(&custom_pwr_mode);

  String apSsid = getApSsid();
  Serial.printf("[Portal] Starting AP '%s' at 192.168.4.1 (timeout %lu s)\n",
                apSsid.c_str(), timeoutSec);
  wm.startConfigPortal(apSsid.c_str());

  bool connected = false;
  while (!wmPortalTimedOut)
  {
    if (wm.process())
    {
      connected = true;
      break;
    }
    if (!wm.getConfigPortalActive())
      break;

    if (onTick)
      onTick(false);

    delay(5);
  }

  if (wmShouldSave)
  {
    strlcpy(appConfig.hostname, custom_hostname.getValue(), sizeof(appConfig.hostname));
    strlcpy(appConfig.mqtt_server, custom_mqtt_server.getValue(), sizeof(appConfig.mqtt_server));
    strlcpy(appConfig.mqtt_port, custom_mqtt_port.getValue(), sizeof(appConfig.mqtt_port));
    strlcpy(appConfig.mqtt_user, custom_mqtt_user.getValue(), sizeof(appConfig.mqtt_user));
    strlcpy(appConfig.mqtt_pass, custom_mqtt_pass.getValue(), sizeof(appConfig.mqtt_pass));
    strlcpy(appConfig.web_pass, custom_web_pass.getValue(), sizeof(appConfig.web_pass));
    appConfig.power_on_mode = clampPowerOnMode(atoi(custom_pwr_mode.getValue()));
    saveConfig();
  }

  return connected && (WiFi.status() == WL_CONNECTED);
}

void ensureWifiConnected(WifiTickCallback onTick)
{
  for (;;)
  {
    if (hasSavedWifi())
    {
      String savedSsid = getSavedWifiSsid();
      Serial.printf("[WiFi] Searching for saved SSID '%s' for up to %lu s...\n",
                    savedSsid.c_str(), WIFI_SEARCH_TIMEOUT_MS / 1000);

      if (connectSavedWifi(WIFI_SEARCH_TIMEOUT_MS, onTick))
        return;

      Serial.println("[WiFi] Saved SSID not found within 2 minutes. Opening Config Portal AP...");
    }
    else
    {
      Serial.println("[WiFi] No saved Wi-Fi credentials. Opening Config Portal AP...");
    }

    if (runConfigPortal(CONFIG_PORTAL_TIMEOUT_SEC, onTick))
      return;

    Serial.println("[WiFi] Config Portal timed out without new Wi-Fi. Retrying saved SSID...");
  }
}
