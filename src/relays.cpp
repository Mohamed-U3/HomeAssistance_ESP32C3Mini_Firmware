/**
 * @file relays.cpp
 * @brief Implementation of 8-channel relay switching, NVS mask persistence, and button debouncing.
 */
#include "relays.h"
#include "config.h"

static bool relayStates[RELAY_COUNT] = {false};

static RelayMqttCallback mqttStateCb = nullptr;
static RelayBroadcastCallback broadcastCb = nullptr;

// Button state tracking
static constexpr unsigned long DEBOUNCE_DELAY_MS = 50;
static constexpr unsigned long CONFIG_HOLD_MS = 3000;

static int lastMasterReading = HIGH;
static int masterStableState = HIGH;
static unsigned long lastMasterDebounce = 0;

static int lastConfigReading = HIGH;
static int configStableState = HIGH;
static unsigned long lastConfigDebounce = 0;
static unsigned long configPressStart = 0;
static bool configTriggered = false;

static uint8_t buildRelayMask()
{
  uint8_t mask = 0;
  for (size_t i = 0; i < RELAY_COUNT; i++)
  {
    if (relayStates[i])
      mask |= (1U << i);
  }
  return mask;
}

static void applyRelayPin(size_t index, bool state)
{
  relayStates[index] = state;
  digitalWrite(RELAY_PINS[index], state ? RELAY_ACTIVE : RELAY_INACTIVE);
}

static void syncOutputs(bool notifyAll)
{
  if (appConfig.power_on_mode == POWER_ON_RESTORE)
  {
    saveRelayMask(buildRelayMask());
  }
  if (notifyAll && mqttStateCb)
  {
    for (size_t i = 0; i < RELAY_COUNT; i++)
      mqttStateCb(i);
  }
  if (broadcastCb)
    broadcastCb();
}

void setRelayCallbacks(RelayMqttCallback onMqttState, RelayBroadcastCallback onBroadcast)
{
  mqttStateCb = onMqttState;
  broadcastCb = onBroadcast;
}

void initRelays()
{
  uint8_t savedMask = (appConfig.power_on_mode == POWER_ON_RESTORE) ? loadRelayMask() : 0;

  for (size_t i = 0; i < RELAY_COUNT; i++)
  {
    bool initialState = false;
    if (appConfig.power_on_mode == POWER_ON_ALL_ON)
      initialState = true;
    else if (appConfig.power_on_mode == POWER_ON_RESTORE)
      initialState = (savedMask & (1U << i)) != 0;

    pinMode(RELAY_PINS[i], OUTPUT);
    applyRelayPin(i, initialState);
  }

  pinMode(MASTER_BTN_PIN, INPUT_PULLUP);
  pinMode(CONFIG_BTN_PIN, INPUT_PULLUP);
}

bool getRelayState(size_t index)
{
  if (index >= RELAY_COUNT)
    return false;
  return relayStates[index];
}

void setRelay(size_t index, bool state)
{
  if (index >= RELAY_COUNT)
    return;

  applyRelayPin(index, state);

  if (appConfig.power_on_mode == POWER_ON_RESTORE)
    saveRelayMask(buildRelayMask());

  if (mqttStateCb)
    mqttStateCb(index);
  if (broadcastCb)
    broadcastCb();
}

void toggleSingleRelayByPin(int pin)
{
  for (size_t i = 0; i < RELAY_COUNT; i++)
  {
    if (RELAY_PINS[i] == pin)
    {
      setRelay(i, !relayStates[i]);
      return;
    }
  }
}

void toggleAllRelays()
{
  for (size_t i = 0; i < RELAY_COUNT; i++)
    applyRelayPin(i, !relayStates[i]);

  syncOutputs(true);
}

void smartToggleAllRelays()
{
  bool anyOn = false;
  for (size_t i = 0; i < RELAY_COUNT; i++)
  {
    if (relayStates[i])
    {
      anyOn = true;
      break;
    }
  }
  setAllRelays(!anyOn);
}

void setAllRelays(bool state)
{
  for (size_t i = 0; i < RELAY_COUNT; i++)
    applyRelayPin(i, state);

  syncOutputs(true);
}

void persistCurrentRelayMask()
{
  saveRelayMask(buildRelayMask());
}

void handleButtons(PortalTriggerCallback onPortalRequested)
{
  unsigned long now = millis();

  // Master toggle button (short press -> smart toggle all)
  int masterReading = digitalRead(MASTER_BTN_PIN);
  if (masterReading != lastMasterReading)
    lastMasterDebounce = now;

  if ((now - lastMasterDebounce) > DEBOUNCE_DELAY_MS && masterReading != masterStableState)
  {
    masterStableState = masterReading;
    if (masterStableState == LOW)
      smartToggleAllRelays();
  }
  lastMasterReading = masterReading;

  // Config portal button (3-second hold to prevent accidental triggers)
  int configReading = digitalRead(CONFIG_BTN_PIN);
  if (configReading != lastConfigReading)
    lastConfigDebounce = now;

  if ((now - lastConfigDebounce) > DEBOUNCE_DELAY_MS && configReading != configStableState)
  {
    configStableState = configReading;
    if (configStableState == LOW)
    {
      configPressStart = now;
      configTriggered = false;
    }
    else
    {
      configTriggered = false;
    }
  }
  lastConfigReading = configReading;

  if (configStableState == LOW && !configTriggered && (now - configPressStart) >= CONFIG_HOLD_MS)
  {
    configTriggered = true;
    if (onPortalRequested)
      onPortalRequested();
  }
}
