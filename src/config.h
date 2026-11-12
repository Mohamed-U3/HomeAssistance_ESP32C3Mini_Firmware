/**
 * @file config.h
 * @brief NVS configuration persistence, hostname sanitization, and Wi-Fi failover management.
 * @defgroup config Configuration & Wi-Fi Management
 * @{
 */
#pragma once

#include <Arduino.h>

#define Module_Name module1             ///< Default compile-time module identifier
#define STR_HELPER(x) #x                ///< Macro helper for stringification
#define STR(x) STR_HELPER(x)            ///< Stringify macro argument
#define DEFAULT_HOSTNAME STR(Module_Name) ///< Default mDNS and MQTT device hostname

/** Timeout (ms) to search for the saved Wi-Fi SSID before opening the SoftAP portal. */
static constexpr unsigned long WIFI_SEARCH_TIMEOUT_MS = 120000UL;
/** Interval (ms) between STA reconnect attempts while searching for the saved SSID. */
static constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;
/** Timeout (seconds) for the SoftAP configuration portal before retrying the saved SSID. */
static constexpr unsigned long CONFIG_PORTAL_TIMEOUT_SEC = 180;

/**
 * @enum PowerOnMode
 * @brief Relay startup state policy applied at boot.
 */
enum PowerOnMode : uint8_t
{
  POWER_ON_ALL_OFF = 0, ///< Boot with all relays OFF
  POWER_ON_ALL_ON = 1,  ///< Boot with all relays ON
  POWER_ON_RESTORE = 2  ///< Restore last relay states from NVS bitmask
};

/**
 * @struct AppConfig
 * @brief Runtime and NVS-persisted device settings.
 */
struct AppConfig
{
  char hostname[33];     ///< Sanitized mDNS/MQTT device hostname
  char mqtt_server[40];  ///< MQTT broker hostname or IP address
  char mqtt_port[6];     ///< MQTT broker TCP port string
  char mqtt_user[32];    ///< Optional MQTT username
  char mqtt_pass[32];    ///< Optional MQTT password
  char web_pass[32];     ///< Optional HTTP Basic Auth & OTA password (user: admin)
  uint8_t power_on_mode; ///< Startup relay policy (@see PowerOnMode)
};

/** Global configuration instance loaded from NVS on startup. */
extern AppConfig appConfig;

/**
 * @brief Callback invoked during blocking Wi-Fi search or portal loops to service hardware buttons.
 * @param allowPortalTrigger True during STA search so long-pressing IO8 can open the portal early.
 * @return True if the callback requests aborting the search to open the portal immediately.
 */
using WifiTickCallback = bool (*)(bool allowPortalTrigger);

/** @brief Load persisted configuration from NVS into @ref appConfig. */
void loadConfig();

/** @brief Validate and persist @ref appConfig fields to NVS. */
void saveConfig();

/**
 * @brief Sanitize a hostname in-place to `[a-zA-Z0-9-]` with fallback to @ref DEFAULT_HOSTNAME.
 * @param buf Character buffer containing the hostname to sanitize.
 * @param bufSize Total size of @p buf in bytes.
 */
void sanitizeHostname(char *buf, size_t bufSize);

/**
 * @brief Build the runtime SoftAP SSID from the configured hostname (`ESP32-<hostname>-AP`).
 * @return SoftAP SSID string.
 */
String getApSsid();

/**
 * @brief Read the persisted 8-bit relay state bitmask from NVS.
 * @return Bitmask where bit `i` represents relay `i` state.
 */
uint8_t loadRelayMask();

/**
 * @brief Persist the 8-bit relay state bitmask to NVS only when it has changed.
 * @param mask Bitmask where bit `i` represents relay `i` state.
 */
void saveRelayMask(uint8_t mask);

/**
 * @brief Persist the relay power-on mode (`0`=OFF, `1`=ON, `2`=Restore) to NVS.
 * @param mode Desired @ref PowerOnMode value.
 */
void savePowerOnMode(uint8_t mode);

/**
 * @brief Check whether STA Wi-Fi credentials are stored in ESP32 NVS.
 * @return True if a non-empty SSID is stored.
 */
bool hasSavedWifi();

/**
 * @brief Read the stored STA SSID from ESP32 Wi-Fi NVS configuration.
 * @return Saved SSID string, or empty string if none.
 */
String getSavedWifiSsid();

/**
 * @brief Search and retry connecting to the saved Wi-Fi SSID for up to @p timeoutMs.
 * @param timeoutMs Maximum search duration in milliseconds.
 * @param onTick Optional callback invoked periodically to keep buttons responsive.
 * @return True if connected to Wi-Fi, false on timeout or early portal trigger.
 */
bool connectSavedWifi(unsigned long timeoutMs, WifiTickCallback onTick);

/**
 * @brief Start the SoftAP configuration portal for up to @p timeoutSec seconds.
 * @param timeoutSec Portal inactivity timeout in seconds.
 * @param onTick Optional callback invoked periodically to keep relay buttons responsive.
 * @return True if the user configured and connected to a new Wi-Fi network.
 */
bool runConfigPortal(unsigned long timeoutSec, WifiTickCallback onTick);

/**
 * @brief Ensure Wi-Fi connectivity at boot using a resilient search-and-portal cycle.
 * @details Searches for the saved SSID for 2 minutes; if unreachable (or none saved), opens
 *          the SoftAP portal for 3 minutes, then loops back to retry the saved SSID if timed out.
 * @param onTick Optional callback invoked during wait/portal loops.
 */
void ensureWifiConnected(WifiTickCallback onTick);

/** @} */
