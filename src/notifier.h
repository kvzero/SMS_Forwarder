/**
 * @file notifier.h
 * @brief Outbound email and push notification delivery.
 */

#pragma once

#include <Arduino.h>

#include "config_store.h"
#include "sms_inbox.h"

class WiFiClientSecure;
namespace ReadyMailSMTP {
class SMTPClient;
}

/**
 * @brief Sends outbound notifications through email and configured push channels.
 */
class Notifier {
 public:
  Notifier();
  ~Notifier();

  Notifier(const Notifier&) = delete;
  Notifier& operator=(const Notifier&) = delete;

  /** @brief Initializes shared transport state used by the notifier. */
  void Begin();
  /**
   * @brief Sends a direct email notification when SMTP is configured.
   * @param config Current runtime configuration.
   * @param subject RFC822 subject line.
   * @param body Plain-text message body.
   */
  void SendEmail(const AppConfig& config, const char* subject, const char* body);
  /**
   * @brief Delivers one SMS through all enabled notification paths.
   * @param config Current runtime configuration.
   * @param message SMS payload to forward.
   */
  void NotifySms(const AppConfig& config, const SmsMessage& message);

 private:
  void SendToAllChannels(const AppConfig& config, const SmsMessage& message);
  void SendToChannel(const PushChannel& channel, const SmsMessage& message);

  String UrlEncode(const String& value) const;
  String JsonEscape(const String& value) const;
  String DingtalkSign(const String& secret, int64_t timestamp) const;
  int64_t GetUtcMillis() const;

  WiFiClientSecure* ssl_client_;
  ReadyMailSMTP::SMTPClient* smtp_;
};
