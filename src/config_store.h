/**
 * @file config_store.h
 * @brief Persistent configuration model and NVS storage helpers.
 */

#pragma once

#include <Arduino.h>

/** @brief Supported outbound push transports. */
enum PushType {
  PUSH_TYPE_NONE = 0,        ///< Channel disabled.
  PUSH_TYPE_POST_JSON = 1,   ///< Generic HTTP POST JSON payload.
  PUSH_TYPE_BARK = 2,        ///< Bark push payload for iOS clients.
  PUSH_TYPE_GET = 3,         ///< HTTP GET with parameters embedded in the URL.
  PUSH_TYPE_DINGTALK = 4,    ///< DingTalk robot webhook.
  PUSH_TYPE_PUSHPLUS = 5,    ///< PushPlus message delivery.
  PUSH_TYPE_SERVERCHAN = 6,  ///< ServerChan message delivery.
  PUSH_TYPE_CUSTOM = 7,      ///< Caller-provided JSON body template.
  PUSH_TYPE_FEISHU = 8,      ///< Feishu robot webhook.
  PUSH_TYPE_GOTIFY = 9,      ///< Gotify application push.
  PUSH_TYPE_TELEGRAM = 10    ///< Telegram Bot sendMessage API.
};

constexpr int kMaxPushChannels = 5;  ///< Number of configurable push channels.

/** Default web credentials used until the user saves custom values. */
#define DEFAULT_WEB_USER "admin"
#define DEFAULT_WEB_PASS "admin123"

/** @brief One configured outbound push channel. */
struct PushChannel {
  bool enabled;        ///< Whether this channel participates in notifications.
  PushType type;       ///< Transport type selected in the web UI.
  String name;         ///< Display name shown in the configuration page.
  String url;          ///< Webhook or base URL when the transport needs one.
  String key1;         ///< Primary credential or token field.
  String key2;         ///< Secondary credential or routing field.
  String customBody;   ///< JSON body template used by PUSH_TYPE_CUSTOM.
};

/** @brief Runtime configuration persisted in NVS. */
struct AppConfig {
  String smtpServer;                             ///< SMTP host name.
  int smtpPort;                                 ///< SMTP port number.
  String smtpUser;                              ///< SMTP login user or sender address.
  String smtpPass;                              ///< SMTP password or app authorization code.
  String smtpSendTo;                            ///< Destination email address for alerts.
  String adminPhone;                            ///< Phone number allowed to issue SMS commands.
  PushChannel pushChannels[kMaxPushChannels];   ///< Configured push notification channels.
  String webUser;                               ///< HTTP Basic Auth user name.
  String webPass;                               ///< HTTP Basic Auth password.
  String numberBlackList;                       ///< Newline-delimited ignored sender list.
};

/**
 * @brief Checks whether a push channel has enough data to be used.
 * @param channel Candidate channel configuration.
 * @return True when the channel is enabled and all required fields are present.
 */
bool IsPushChannelValid(const PushChannel& channel);

/**
 * @brief Checks whether the overall configuration can deliver notifications.
 * @param config Full runtime configuration.
 * @return True when SMTP is complete or at least one push channel is valid.
 */
bool IsConfigValid(const AppConfig& config);

/**
 * @brief Loads and saves the application configuration.
 *
 * The NVS key names are kept stable so existing devices retain their saved
 * settings across refactors.
 */
class ConfigStore {
 public:
  /**
   * @brief Loads configuration from NVS into the provided object.
   * @param config Destination configuration instance.
   */
  void Load(AppConfig& config);

  /**
   * @brief Saves configuration to NVS using the stable legacy key set.
   * @param config Source configuration instance.
   */
  void Save(const AppConfig& config);
};
