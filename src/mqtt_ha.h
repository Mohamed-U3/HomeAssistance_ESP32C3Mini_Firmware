/**
 * @file mqtt_ha.h
 * @brief Home Assistant MQTT auto-discovery, LWT availability, and non-blocking broker probing.
 * @defgroup mqtt Home Assistant MQTT Integration
 * @{
 */
#pragma once

#include <Arduino.h>
#include <stddef.h>

/** @brief Initialize MQTT client settings and spawn the FreeRTOS background probe task. */
void initMqtt();

/** @brief Maintain MQTT broker connection and process incoming command messages. */
void handleMqtt();

/**
 * @brief Publish a single relay's retained state (`"ON"` / `"OFF"`) to MQTT.
 * @param index Relay index (`0` to `RELAY_COUNT - 1`).
 */
void publishMqttState(size_t index);

/** @brief Publish retained LWT `"offline"` status, pause probing, and disconnect cleanly. */
void publishMqttOffline();

/**
 * @brief Check whether the MQTT client is currently connected to the broker.
 * @return True if connected.
 */
bool isMqttConnected();

/** @} */
