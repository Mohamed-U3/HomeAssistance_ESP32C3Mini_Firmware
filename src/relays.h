/**
 * @file relays.h
 * @brief 8-channel relay GPIO control, NVS state persistence, and physical button handling.
 * @defgroup relays Relay & Button Control
 * @{
 */
#pragma once

#include <Arduino.h>
#include <stddef.h>

/** Hardware GPIO pin mapping for the 8 active-HIGH relays. */
static constexpr uint8_t RELAY_PINS[] = {0, 1, 4, 5, 19, 18, 10, 7};
/** Total number of managed relay channels. */
static constexpr size_t RELAY_COUNT = sizeof(RELAY_PINS) / sizeof(RELAY_PINS[0]);

#define RELAY_ACTIVE HIGH  ///< GPIO logic level that energizes a relay
#define RELAY_INACTIVE LOW ///< GPIO logic level that de-energizes a relay

static constexpr uint8_t MASTER_BTN_PIN = 2; ///< Physical master toggle button GPIO (INPUT_PULLUP)
static constexpr uint8_t CONFIG_BTN_PIN = 8; ///< Physical config portal button GPIO (3s hold, INPUT_PULLUP)

/** Callback invoked with relay index when a relay's MQTT state should be published. */
using RelayMqttCallback = void (*)(size_t index);
/** Callback invoked when WebSocket clients should receive updated relay states. */
using RelayBroadcastCallback = void (*)();
/** Callback invoked when the user holds `CONFIG_BTN_PIN` for 3 seconds. */
using PortalTriggerCallback = void (*)();

/**
 * @brief Register callbacks notified whenever relay states change.
 * @param onMqttState Function to publish a single relay's MQTT state.
 * @param onBroadcast Function to broadcast full JSON state over WebSockets.
 */
void setRelayCallbacks(RelayMqttCallback onMqttState, RelayBroadcastCallback onBroadcast);

/** @brief Configure relay and button GPIOs and apply the configured @ref PowerOnMode. */
void initRelays();

/**
 * @brief Read the cached state of a relay channel.
 * @param index Relay index (`0` to `RELAY_COUNT - 1`).
 * @return True if ON, false if OFF or out of range.
 */
bool getRelayState(size_t index);

/**
 * @brief Set a single relay's state and synchronize GPIO, NVS, MQTT, and WebSocket clients.
 * @param index Relay index (`0` to `RELAY_COUNT - 1`).
 * @param state Desired state (`true` = ON, `false` = OFF).
 */
void setRelay(size_t index, bool state);

/**
 * @brief Toggle a single relay identified by its ESP32-C3 GPIO pin number.
 * @param pin GPIO pin number matching an entry in `RELAY_PINS`.
 */
void toggleSingleRelayByPin(int pin);

/** @brief Invert the state of every relay channel in a single batch. */
void toggleAllRelays();

/** @brief Smart master toggle: turn all OFF if any relay is ON, otherwise turn all ON. */
void smartToggleAllRelays();

/**
 * @brief Set all relays to the same state in a single batch.
 * @param state Desired state (`true` = ON, `false` = OFF).
 */
void setAllRelays(bool state);

/** @brief Persist the current 8-relay state bitmask to NVS. */
void persistCurrentRelayMask();

/**
 * @brief Poll physical buttons with software debouncing.
 * @details Short-pressing `MASTER_BTN_PIN` triggers @ref smartToggleAllRelays.
 *          Holding `CONFIG_BTN_PIN` for 3 seconds invokes @p onPortalRequested.
 * @param onPortalRequested Callback invoked when the 3-second config hold completes (may be nullptr).
 */
void handleButtons(PortalTriggerCallback onPortalRequested);

/** @} */
