#include <Arduino.h>
#include <WiFi.h>

#include "board_pins.h"
#include "config_store.h"
#include "modem.h"
#include "notifier.h"
#include "sms_inbox.h"
#include "web_admin.h"
#include "wifi_config.h"

/**
 * @file main.cpp
 * @brief Top-level application composition and blocking loop orchestration.
 */

namespace {

// Top-level composition root for the long-lived application modules.
ConfigStore config_store;
AppConfig config;
Modem modem(Serial1);
SmsInbox sms_inbox;
Notifier notifier;
bool config_valid = false;
bool time_synced = false;
unsigned long last_print_time = 0;
WebAdmin web_admin(config_store, config, modem, notifier, config_valid);

void BlinkShort(unsigned long gap_time = 500) {
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(gap_time);
}

String GetDeviceUrl() {
  return "http://" + WiFi.localIP().toString() + "/";
}

void HandleInboxAction(const InboxAction& action) {
  // SmsInbox decides which action is needed; main.cpp performs the side effects.
  switch (action.type) {
    case InboxActionType::Ignore:
      return;

    case InboxActionType::Notify:
      notifier.NotifySms(config, action.message);
      return;

    case InboxActionType::SendEmailOnly:
      notifier.SendEmail(config, action.emailSubject.c_str(), action.emailBody.c_str());
      return;

    case InboxActionType::SendSmsCommand: {
      const bool success =
          modem.SendSms(action.commandPhone.c_str(), action.commandText.c_str());

      String subject = success ? "短信发送成功" : "短信发送失败";
      String body = "管理员命令执行结果:\n";
      body += "命令: SMS:";
      body += action.commandPhone;
      body += ":";
      body += action.commandText;
      body += "\n";
      body += "目标号码: ";
      body += action.commandPhone;
      body += "\n";
      body += "短信内容: ";
      body += action.commandText;
      body += "\n";
      body += "执行结果: ";
      body += success ? "成功" : "失败";
      notifier.SendEmail(config, subject.c_str(), body.c_str());
      return;
    }

    case InboxActionType::ResetDevice:
      notifier.SendEmail(config, action.emailSubject.c_str(), action.emailBody.c_str());
      modem.Reset();
      Serial.println("Restarting ESP32...");
      delay(1000);
      ESP.restart();
      return;
  }
}

}  // namespace

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(1500);

  // Load persisted configuration before any network-facing services start.
  config_store.Load(config);
  config_valid = IsConfigValid(config);

  // Initialize the modem first so the cellular side reaches a stable state
  // before Wi-Fi and the admin UI come online.
  modem.Begin();

  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 0, nullptr, true);
  Serial.println("Connecting to Wi-Fi");
  Serial.println(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    BlinkShort();
  }
  Serial.println("Wi-Fi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Syncing NTP time...");
  configTime(0, 0, "ntp.ntsc.ac.cn", "ntp.aliyun.com", "pool.ntp.org");
  int ntp_retry = 0;
  while (time(nullptr) < 100000 && ntp_retry < 100) {
    delay(100);
    ntp_retry++;
  }
  if (time(nullptr) >= 100000) {
    time_synced = true;
    Serial.println("NTP sync succeeded");
    Serial.print("Current UTC timestamp: ");
    Serial.println(time(nullptr));
  } else {
    Serial.println("NTP sync failed; falling back to device time");
  }

  notifier.Begin();
  web_admin.Begin();
  digitalWrite(LED_BUILTIN, LOW);

  if (config_valid) {
    Serial.println("Config is valid; sending startup notice...");
    String subject = "短信转发器已启动";
    String body = "设备已启动\n设备地址: " + GetDeviceUrl();
    notifier.SendEmail(config, subject.c_str(), body.c_str());
  }
}

void loop() {
  // Keep the admin UI responsive on every iteration of the blocking main loop.
  web_admin.HandleClient();

  // Print the local setup URL once per second until delivery settings exist.
  if (!config_valid && millis() - last_print_time >= 1000) {
    last_print_time = millis();
    Serial.println("Please visit " + GetDeviceUrl() + " to configure the system");
  }

  SmsMessage message;
  if (modem.Poll(message)) {
    const InboxAction action = sms_inbox.Process(config, message);
    HandleInboxAction(action);
  }

  if (Serial.available()) {
    modem.WritePassthroughByte(Serial.read());
  }
}
