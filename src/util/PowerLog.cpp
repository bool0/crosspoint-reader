// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "PowerLog.h"

#include <BoardConfig.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <Logging.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <cstdio>
#include <ctime>

namespace powerlog {
namespace {
constexpr char LOG_PATH[] = "/.crosspoint/power.log";
constexpr size_t MAX_LOG_BYTES = 16 * 1024;  // started afresh beyond this
constexpr uint64_t WATCH_PERIOD_US = 250 * 1000;
constexpr uint8_t FORCE_RESTART_TICKS = 40;  // 10 s

void watchTick(void*) {
  static uint8_t heldTicks = 0;
  const auto pin = static_cast<gpio_num_t>(BoardConfig::ACTIVE.input.power);
  const bool pressed = gpio_get_level(pin) == (BoardConfig::ACTIVE.input.powerActiveHigh ? 1 : 0);
  heldTicks = pressed ? heldTicks + 1 : 0;
  if (heldTicks >= FORCE_RESTART_TICKS) esp_restart();
}
}  // namespace

void note(const char* event) {
  const uint16_t percent = powerManager.getBatteryPercentage();
  uint16_t rawMv = 0;
  uint16_t filteredMv = 0;
  powerManager.batteryDebug(rawMv, filteredMv);

  char when[20] = "-";
  const time_t now = time(nullptr);
  if (now > 1700000000) {
    tm local{};
    localtime_r(&now, &local);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &local);
  }
  char line[128];
  const int len = snprintf(line, sizeof(line), "%s %s reset=%s battery=%umV/%u%% usb=%d\n", when, event,
                           HalSystem::resetReasonName(), rawMv, percent, gpio.isUsbConnected() ? 1 : 0);
  if (len <= 0) return;

  if (Storage.exists(LOG_PATH)) {
    HalFile existing = Storage.open(LOG_PATH);
    const bool full = existing && existing.fileSize() > MAX_LOG_BYTES;
    existing.close();
    if (full) Storage.remove(LOG_PATH);
  }
  HalFile file = Storage.open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("PWRLOG", "Cannot open %s", LOG_PATH);
    return;
  }
  file.write(line, static_cast<size_t>(len));
  LOG_INF("PWRLOG", "%s", line);
}

void startForceRestartWatch() {
  if (BoardConfig::ACTIVE.input.power < 0) return;
  const esp_timer_create_args_t args = {.callback = &watchTick, .name = "force_restart"};
  esp_timer_handle_t timer = nullptr;
  if (esp_timer_create(&args, &timer) != ESP_OK || esp_timer_start_periodic(timer, WATCH_PERIOD_US) != ESP_OK) {
    LOG_ERR("PWRLOG", "Force-restart watch not started");
  }
}

}  // namespace powerlog
