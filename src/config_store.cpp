/**
 * @file config_store.cpp
 * @brief Validation helpers and NVS persistence for AppConfig.
 */

#include "config_store.h"

#include <Preferences.h>

namespace {

Preferences preferences;

struct WifiCredentialPoolSnapshot {
  WifiCredential items[kMaxWifiCredentials];
  uint8_t count = 0;
};

void CaptureWifiCredentialPool(const AppConfig& config, WifiCredentialPoolSnapshot& snapshot) {
  snapshot.count = config.wifiCredentialCount;
  for (int i = 0; i < kMaxWifiCredentials; ++i) {
    snapshot.items[i] = config.wifiCredentials[i];
  }
}

bool WifiCredentialPoolMatches(const AppConfig& config,
                               const WifiCredentialPoolSnapshot& snapshot) {
  if (config.wifiCredentialCount != snapshot.count) {
    return false;
  }

  for (int i = 0; i < kMaxWifiCredentials; ++i) {
    if (config.wifiCredentials[i].ssid != snapshot.items[i].ssid ||
        config.wifiCredentials[i].password != snapshot.items[i].password) {
      return false;
    }
  }

  return true;
}

void NormalizeWifiCredentialPool(AppConfig& config) {
  WifiCredential normalized[kMaxWifiCredentials];
  uint8_t normalized_count = 0;

  for (uint8_t i = 0; i < config.wifiCredentialCount && i < kMaxWifiCredentials; ++i) {
    const String ssid = config.wifiCredentials[i].ssid;
    if (ssid.length() == 0) {
      continue;
    }

    bool duplicate = false;
    for (uint8_t j = 0; j < normalized_count; ++j) {
      if (normalized[j].ssid == ssid) {
        duplicate = true;
        break;
      }
    }

    if (duplicate) {
      continue;
    }

    normalized[normalized_count] = config.wifiCredentials[i];
    normalized_count++;
  }

  for (int i = 0; i < kMaxWifiCredentials; ++i) {
    if (i < normalized_count) {
      config.wifiCredentials[i] = normalized[i];
    } else {
      config.wifiCredentials[i].ssid = "";
      config.wifiCredentials[i].password = "";
    }
  }

  config.wifiCredentialCount = normalized_count;
}

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
bool ConfigStore::Save(const AppConfig& config) {
  // Preserve existing NVS key names so saved settings survive refactors.
  if (!preferences.begin("sms_config", false)) return false;
  bool ok = true;
  ok &= preferences.putString("smtpServer", config.smtpServer) == config.smtpServer.length();
  ok &= preferences.putInt("smtpPort", config.smtpPort) == 4;
  ok &= preferences.putString("smtpUser", config.smtpUser) == config.smtpUser.length();
  ok &= preferences.putString("smtpPass", config.smtpPass) == config.smtpPass.length();
  ok &= preferences.putString("smtpSendTo", config.smtpSendTo) == config.smtpSendTo.length();
  ok &= preferences.putString("adminPhone", config.adminPhone) == config.adminPhone.length();
  ok &= preferences.putString("webUser", config.webUser) == config.webUser.length();
  ok &= preferences.putString("webPass", config.webPass) == config.webPass.length();
  ok &= preferences.putString("numBlkList", config.numberBlackList) == config.numberBlackList.length();
  ok &= preferences.putUChar("wifiCount", config.wifiCredentialCount) == 1;

  for (int i = 0; i < kMaxPushChannels; ++i) {
    const String prefix = "push" + String(i);
    ok &= preferences.putBool((prefix + "en").c_str(), config.pushChannels[i].enabled) == 1;
    ok &= preferences.putUChar((prefix + "type").c_str(),
                               static_cast<uint8_t>(config.pushChannels[i].type)) == 1;
    ok &= preferences.putString((prefix + "url").c_str(), config.pushChannels[i].url) == config.pushChannels[i].url.length();
    ok &= preferences.putString((prefix + "name").c_str(), config.pushChannels[i].name) == config.pushChannels[i].name.length();
    ok &= preferences.putString((prefix + "k1").c_str(), config.pushChannels[i].key1) == config.pushChannels[i].key1.length();
    ok &= preferences.putString((prefix + "k2").c_str(), config.pushChannels[i].key2) == config.pushChannels[i].key2.length();
    ok &= preferences.putString((prefix + "body").c_str(), config.pushChannels[i].customBody) == config.pushChannels[i].customBody.length();
  }

  for (int i = 0; i < kMaxWifiCredentials; ++i) {
    const String prefix = "wifi" + String(i);
    if (i < config.wifiCredentialCount) {
      ok &= preferences.putString((prefix + "Ssid").c_str(), config.wifiCredentials[i].ssid) == config.wifiCredentials[i].ssid.length();
      ok &= preferences.putString((prefix + "Pass").c_str(), config.wifiCredentials[i].password) == config.wifiCredentials[i].password.length();
    } else {
      ok &= preferences.putString((prefix + "Ssid").c_str(), "") == 0;
      ok &= preferences.putString((prefix + "Pass").c_str(), "") == 0;
    }
  }

  preferences.end();
  if (ok) Serial.println("Config saved"); return ok;
}

void ConfigStore::Load(AppConfig& config) {
  if (!preferences.begin("sms_config", false)) {
    Serial.println("Failed to open NVS config namespace; using defaults");
    config = AppConfig{};
    config.smtpPort = 465;
    config.webUser = DEFAULT_WEB_USER;
    config.webPass = DEFAULT_WEB_PASS;
    return;
  }
  config.smtpServer = preferences.getString("smtpServer", "");
  config.smtpPort = preferences.getInt("smtpPort", 465);
  config.smtpUser = preferences.getString("smtpUser", "");
  config.smtpPass = preferences.getString("smtpPass", "");
  config.smtpSendTo = preferences.getString("smtpSendTo", "");
  config.adminPhone = preferences.getString("adminPhone", "");
  config.wifiCredentialCount = preferences.getUChar("wifiCount", 0);
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

  for (int i = 0; i < kMaxWifiCredentials; ++i) {
    const String prefix = "wifi" + String(i);
    config.wifiCredentials[i].ssid = preferences.getString((prefix + "Ssid").c_str(), "");
    config.wifiCredentials[i].password = preferences.getString((prefix + "Pass").c_str(), "");
  }
  NormalizeWifiCredentialPool(config);

  preferences.end();
  Serial.println("Config loaded");
}

bool ConfigStore::UpsertWifiCredential(AppConfig& config, const String& ssid,
                                       const String& password) {
  if (ssid.length() == 0) {
    return false;
  }

  WifiCredentialPoolSnapshot previous;
  CaptureWifiCredentialPool(config, previous);

  WifiCredential updated[kMaxWifiCredentials];
  uint8_t updated_count = 0;

  updated[updated_count].ssid = ssid;
  updated[updated_count].password = password;
  updated_count++;

  for (uint8_t i = 0; i < config.wifiCredentialCount && updated_count < kMaxWifiCredentials;
       ++i) {
    if (config.wifiCredentials[i].ssid == ssid) {
      continue;
    }

    updated[updated_count] = config.wifiCredentials[i];
    updated_count++;
  }

  config.wifiCredentialCount = updated_count;
  for (int i = 0; i < kMaxWifiCredentials; ++i) {
    if (i < updated_count) {
      config.wifiCredentials[i] = updated[i];
    } else {
      config.wifiCredentials[i].ssid = "";
      config.wifiCredentials[i].password = "";
    }
  }

  return !WifiCredentialPoolMatches(config, previous);
}

bool ConfigStore::RemoveWifiCredential(AppConfig& config, const String& ssid) {
  if (ssid.length() == 0 || config.wifiCredentialCount == 0) {
    return false;
  }

  WifiCredentialPoolSnapshot previous;
  CaptureWifiCredentialPool(config, previous);

  WifiCredential updated[kMaxWifiCredentials];
  uint8_t updated_count = 0;

  for (uint8_t i = 0; i < config.wifiCredentialCount && updated_count < kMaxWifiCredentials;
       ++i) {
    if (config.wifiCredentials[i].ssid == ssid) {
      continue;
    }

    updated[updated_count] = config.wifiCredentials[i];
    updated_count++;
  }

  config.wifiCredentialCount = updated_count;
  for (int i = 0; i < kMaxWifiCredentials; ++i) {
    if (i < updated_count) {
      config.wifiCredentials[i] = updated[i];
    } else {
      config.wifiCredentials[i].ssid = "";
      config.wifiCredentials[i].password = "";
    }
  }

  return !WifiCredentialPoolMatches(config, previous);
}

bool ConfigStore::ClearWifiCredentials(AppConfig& config) {
  WifiCredentialPoolSnapshot previous;
  CaptureWifiCredentialPool(config, previous);

  config.wifiCredentialCount = 0;
  for (int i = 0; i < kMaxWifiCredentials; ++i) {
    config.wifiCredentials[i].ssid = "";
    config.wifiCredentials[i].password = "";
  }

  return !WifiCredentialPoolMatches(config, previous);
}
