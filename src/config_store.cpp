/**
 * @file config_store.cpp
 * @brief Validation helpers and NVS persistence for AppConfig.
 */

#include "config_store.h"

#include <Preferences.h>

namespace {

Preferences preferences;

}  // namespace

// Validation helpers.
bool IsPushChannelValid(const PushChannel& channel) {
  if (!channel.enabled) {
    return false;
  }

  switch (channel.type) {
    case PUSH_TYPE_POST_JSON:
    case PUSH_TYPE_BARK:
    case PUSH_TYPE_GET:
    case PUSH_TYPE_DINGTALK:
    case PUSH_TYPE_FEISHU:
    case PUSH_TYPE_CUSTOM:
      return channel.url.length() > 0;
    case PUSH_TYPE_PUSHPLUS:
    case PUSH_TYPE_SERVERCHAN:
      return channel.key1.length() > 0;
    case PUSH_TYPE_GOTIFY:
      return channel.url.length() > 0 && channel.key1.length() > 0;
    case PUSH_TYPE_TELEGRAM:
      return channel.key1.length() > 0 && channel.key2.length() > 0;
    default:
      return false;
  }
}

bool IsConfigValid(const AppConfig& config) {
  const bool email_valid = config.smtpServer.length() > 0 &&
                           config.smtpUser.length() > 0 &&
                           config.smtpPass.length() > 0 &&
                           config.smtpSendTo.length() > 0;

  bool push_valid = false;
  for (int i = 0; i < kMaxPushChannels; ++i) {
    if (IsPushChannelValid(config.pushChannels[i])) {
      push_valid = true;
      break;
    }
  }

  return email_valid || push_valid;
}

// Persistence.
void ConfigStore::Save(const AppConfig& config) {
  // Preserve existing NVS key names so saved settings survive refactors.
  preferences.begin("sms_config", false);
  preferences.putString("smtpServer", config.smtpServer);
  preferences.putInt("smtpPort", config.smtpPort);
  preferences.putString("smtpUser", config.smtpUser);
  preferences.putString("smtpPass", config.smtpPass);
  preferences.putString("smtpSendTo", config.smtpSendTo);
  preferences.putString("adminPhone", config.adminPhone);
  preferences.putString("webUser", config.webUser);
  preferences.putString("webPass", config.webPass);
  preferences.putString("numBlkList", config.numberBlackList);

  for (int i = 0; i < kMaxPushChannels; ++i) {
    const String prefix = "push" + String(i);
    preferences.putBool((prefix + "en").c_str(), config.pushChannels[i].enabled);
    preferences.putUChar((prefix + "type").c_str(),
                         static_cast<uint8_t>(config.pushChannels[i].type));
    preferences.putString((prefix + "url").c_str(), config.pushChannels[i].url);
    preferences.putString((prefix + "name").c_str(), config.pushChannels[i].name);
    preferences.putString((prefix + "k1").c_str(), config.pushChannels[i].key1);
    preferences.putString((prefix + "k2").c_str(), config.pushChannels[i].key2);
    preferences.putString((prefix + "body").c_str(), config.pushChannels[i].customBody);
  }

  preferences.end();
  Serial.println("Config saved");
}

void ConfigStore::Load(AppConfig& config) {
  preferences.begin("sms_config", true);
  config.smtpServer = preferences.getString("smtpServer", "");
  config.smtpPort = preferences.getInt("smtpPort", 465);
  config.smtpUser = preferences.getString("smtpUser", "");
  config.smtpPass = preferences.getString("smtpPass", "");
  config.smtpSendTo = preferences.getString("smtpSendTo", "");
  config.adminPhone = preferences.getString("adminPhone", "");
  config.webUser = preferences.getString("webUser", DEFAULT_WEB_USER);
  config.webPass = preferences.getString("webPass", DEFAULT_WEB_PASS);
  config.numberBlackList = preferences.getString("numBlkList", "");

  for (int i = 0; i < kMaxPushChannels; ++i) {
    const String prefix = "push" + String(i);
    config.pushChannels[i].enabled = preferences.getBool((prefix + "en").c_str(), false);
    config.pushChannels[i].type = static_cast<PushType>(
        preferences.getUChar((prefix + "type").c_str(), PUSH_TYPE_POST_JSON));
    config.pushChannels[i].url = preferences.getString((prefix + "url").c_str(), "");
    config.pushChannels[i].name =
        preferences.getString((prefix + "name").c_str(), "通道" + String(i + 1));
    config.pushChannels[i].key1 = preferences.getString((prefix + "k1").c_str(), "");
    config.pushChannels[i].key2 = preferences.getString((prefix + "k2").c_str(), "");
    config.pushChannels[i].customBody = preferences.getString((prefix + "body").c_str(), "");
  }

  // Migrate the legacy single HTTP target into push channel 1 on first load.
  const String old_http_url = preferences.getString("httpUrl", "");
  if (old_http_url.length() > 0 && !config.pushChannels[0].enabled) {
    config.pushChannels[0].enabled = true;
    config.pushChannels[0].url = old_http_url;
    config.pushChannels[0].type = preferences.getUChar("barkMode", 0) != 0
                                      ? PUSH_TYPE_BARK
                                      : PUSH_TYPE_POST_JSON;
    config.pushChannels[0].name = "迁移通道";
    Serial.println("Migrated legacy HTTP config to push channel 1");
  }

  preferences.end();
  Serial.println("Config loaded");
}
