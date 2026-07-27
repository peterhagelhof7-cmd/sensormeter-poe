#include "RebootManager.h"

#include <time.h>
#include "TimeUtils.h"

RebootManager::RebootManager(DataManager& dataManager, ConfigManager& configManager)
    : _data(dataManager), _config(configManager) {}

void RebootManager::loop() {
  const DeviceConfig& cfg = _config.getConfig();
  if (!cfg.rebootScheduleEnabled) return;
  if (!isTimeSynced()) return;

  time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  if (local.tm_hour != cfg.rebootHour || local.tm_min != cfg.rebootMinute) return;

  Serial.println("[REBOOT] Geplanter automatischer Neustart erreicht");
  _data.pushLogEntry("Geplanter automatischer Neustart ausgeloest");
  delay(500);
  ESP.restart();
}
