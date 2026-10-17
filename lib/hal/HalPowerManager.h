#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <freertos/semphr.h>

#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;

  mutable int _batteryCachedPercent = 0;         // Last read battery percentage (0-100)
  mutable unsigned long _batteryLastPollMs = 0;  // Timestamp of last battery read in milliseconds

  uint16_t adcBatteryPercent() const;

  enum LockMode { None, NormalSpeed };
  LockMode currentLockMode = None;
  SemaphoreHandle_t modeMutex = nullptr;  // Protect access to currentLockMode

 public:
#if BOARD_HAS_PSRAM
  static constexpr int LOW_POWER_FREQ = 80;  // MHz
#else
  static constexpr int LOW_POWER_FREQ = 10;  // MHz
#endif
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;  // ms
  static constexpr unsigned long BATTERY_POLL_MS = 1500;       // ms

  void begin();

  // Control CPU frequency for power saving
  void setPowerSaving(bool enabled);

  // Setup wake up GPIO and enter deep sleep
  // Should be called inside main loop() to handle the currentLockMode
  // timerWakeMs > 0 also wakes after that long. keepPower (implied by a timer)
  // keeps the Xteink battery latch on so the system clock survives the sleep;
  // otherwise sleep is the stock battery power-off.
  void startDeepSleep(HalGPIO& gpio, uint32_t timerWakeMs = 0, bool keepPower = false) const;

  // Light-sleep the chip for up to `ms`, waking early when the power button is
  // pressed. RAM, the system clock and the panel image are kept; the buttons on
  // the ADC ladder cannot wake it. Returns true when the power button woke it.
  bool lightSleep(uint32_t ms) const;

  // Get battery percentage (range 0-100)
  uint16_t getBatteryPercentage() const;
  // Last averaged and filtered ADC battery voltage (X4), for the CMD:BATTERY dev command.
  void batteryDebug(uint16_t& rawMv, uint16_t& filteredMv) const;

  // RAII helper class to manage power saving locks
  // Usage: create an instance of Lock in a scope to disable power saving, for example when running a task that needs
  // full performance. When the Lock instance is destroyed (goes out of scope), power saving will be re-enabled.
  class Lock {
    friend class HalPowerManager;
    bool valid = false;

   public:
    explicit Lock();
    ~Lock();

    // Non-copyable and non-movable
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };
};
