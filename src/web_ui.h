/**
 * @file web_ui.h
 * @brief Embedded HTTP dashboard, WebSocket live synchronization, and browser OTA updates.
 * @defgroup webui Web UI & OTA
 * @{
 */
#pragma once

#include <Arduino.h>

/** Callback invoked when the user triggers the Config Portal from the Web UI. */
using PortalActionCallback = void (*)();

/**
 * @brief Start the HTTP server (port 80) and WebSocket server (port 81).
 * @param onPortalAction Callback invoked after `/api/portal` responds to launch the captive portal.
 */
void initWebUi(PortalActionCallback onPortalAction);

/** @brief Service pending HTTP requests, WebSocket traffic, delayed actions, and status pushes. */
void handleWebUi();

/** @brief Broadcast full device and relay JSON state to all connected WebSocket clients. */
void broadcastStates();

/** @brief Stop HTTP and WebSocket servers cleanly before entering AP portal mode. */
void stopWebUi();

/** @} */
