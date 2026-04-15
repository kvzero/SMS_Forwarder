/**
 * @file notifier.cpp
 * @brief Email and push notification delivery implementation.
 */

#define ENABLE_SMTP
#define ENABLE_DEBUG
#include <ReadyMail.h>
#include <WiFiClientSecure.h>

#include "notifier.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <base64.h>
#include <mbedtls/md.h>
#include <sys/time.h>

// Lifecycle.
Notifier::Notifier()
    : ssl_client_(new WiFiClientSecure()),
      smtp_(new ReadyMailSMTP::SMTPClient(*ssl_client_)) {}

Notifier::~Notifier() {
  delete smtp_;
  delete ssl_client_;
}

void Notifier::Begin() {
  ssl_client_->setInsecure();
}

// User-visible notification entry points.
void Notifier::SendEmail(const AppConfig& config, const char* subject, const char* body) {
  if (config.smtpServer.length() == 0 || config.smtpUser.length() == 0 ||
      config.smtpPass.length() == 0 || config.smtpSendTo.length() == 0) {
    Serial.println("Email config is incomplete; skipping send");
    return;
  }

  auto status_callback = [](SMTPStatus status) {
    Serial.println(status.text);
  };

  smtp_->connect(config.smtpServer.c_str(), config.smtpPort, status_callback);
  if (!smtp_->isConnected()) {
    Serial.println("Failed to connect to the SMTP server");
    return;
  }

  if (!smtp_->authenticate(config.smtpUser.c_str(), config.smtpPass.c_str(),
                           readymail_auth_password)) {
    Serial.println("Failed to authenticate with the SMTP server");
    return;
  }

  SMTPMessage msg;
  String from = "sms notify <";
  from += config.smtpUser;
  from += ">";
  msg.headers.add(rfc822_from, from.c_str());

  String to = "your_email <";
  to += config.smtpSendTo;
  to += ">";
  msg.headers.add(rfc822_to, to.c_str());
  msg.headers.add(rfc822_subject, subject);
  msg.text.body(body);
  msg.timestamp = time(nullptr);
  if (!smtp_->send(msg)) {
    Serial.println("Failed to send email");
    return;
  }
  Serial.println("Email sent");
}

void Notifier::NotifySms(const AppConfig& config, const SmsMessage& message) {
  SendToAllChannels(config, message);

  String subject;
  subject += "短信";
  subject += message.sender;
  subject += ",";
  subject += message.text;

  String body;
  body += "来自：";
  body += message.sender;
  body += "，时间：";
  body += message.timestamp;
  body += "，内容：";
  body += message.text;

  SendEmail(config, subject.c_str(), body.c_str());
}

// Push fan-out.
void Notifier::SendToAllChannels(const AppConfig& config, const SmsMessage& message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi is not connected; skipping push delivery");
    return;
  }

  bool has_enabled_channel = false;
  for (int i = 0; i < kMaxPushChannels; ++i) {
    if (IsPushChannelValid(config.pushChannels[i])) {
      has_enabled_channel = true;
      break;
    }
  }

  if (!has_enabled_channel) {
    Serial.println("No push channel is enabled");
    return;
  }

  Serial.println("\n=== Starting multi-channel push delivery ===");
  for (int i = 0; i < kMaxPushChannels; ++i) {
    if (IsPushChannelValid(config.pushChannels[i])) {
      SendToChannel(config.pushChannels[i], message);
      delay(100);
    }
  }
  Serial.println("=== Multi-channel push delivery completed ===\n");
}

void Notifier::SendToChannel(const PushChannel& channel, const SmsMessage& message) {
  if (!channel.enabled) {
    return;
  }

  const bool need_url = channel.type == PUSH_TYPE_POST_JSON ||
                        channel.type == PUSH_TYPE_BARK ||
                        channel.type == PUSH_TYPE_GET ||
                        channel.type == PUSH_TYPE_DINGTALK ||
                        channel.type == PUSH_TYPE_CUSTOM;
  if (need_url && channel.url.length() == 0) {
    return;
  }

  HTTPClient http;
  const String channel_name =
      channel.name.length() > 0 ? channel.name : ("Channel " + String(channel.type));
  Serial.println("Sending to push channel: " + channel_name);

  int http_code = 0;
  const String sender_escaped = JsonEscape(message.sender);
  const String message_escaped = JsonEscape(message.text);
  const String timestamp_escaped = JsonEscape(message.timestamp);

  switch (channel.type) {
    case PUSH_TYPE_POST_JSON: {
      http.begin(channel.url);
      http.addHeader("Content-Type", "application/json");
      String json_data = "{";
      json_data += "\"sender\":\"" + sender_escaped + "\",";
      json_data += "\"message\":\"" + message_escaped + "\",";
      json_data += "\"timestamp\":\"" + timestamp_escaped + "\"";
      json_data += "}";
      Serial.println("POST JSON: " + json_data);
      http_code = http.POST(json_data);
      break;
    }

    case PUSH_TYPE_BARK: {
      http.begin(channel.url);
      http.addHeader("Content-Type", "application/json");
      String json_data = "{";
      json_data += "\"title\":\"" + sender_escaped + "\",";
      json_data += "\"body\":\"" + message_escaped + "\"";
      json_data += "}";
      Serial.println("Bark JSON: " + json_data);
      http_code = http.POST(json_data);
      break;
    }

    case PUSH_TYPE_GET: {
      String get_url = channel.url;
      if (get_url.indexOf('?') == -1) {
        get_url += "?";
      } else {
        get_url += "&";
      }
      get_url += "sender=" + UrlEncode(message.sender);
      get_url += "&message=" + UrlEncode(message.text);
      get_url += "&timestamp=" + UrlEncode(message.timestamp);
      Serial.println("GET URL: " + get_url);
      http.begin(get_url);
      http_code = http.GET();
      break;
    }

    case PUSH_TYPE_DINGTALK: {
      // DingTalk signs the UTC millisecond timestamp with the shared secret.
      String webhook_url = channel.url;
      if (channel.key1.length() > 0) {
        const int64_t timestamp = GetUtcMillis();
        const String sign = DingtalkSign(channel.key1, timestamp);
        if (webhook_url.indexOf('?') == -1) {
          webhook_url += "?";
        } else {
          webhook_url += "&";
        }
        char ts_buf[21];
        snprintf(ts_buf, sizeof(ts_buf), "%lld", timestamp);
        webhook_url += "timestamp=" + String(ts_buf) + "&sign=" + sign;
      }

      http.begin(webhook_url);
      http.addHeader("Content-Type", "application/json");
      String json_data = "{\"msgtype\":\"text\",\"text\":{\"content\":\"";
      json_data += "📱短信通知\\n发送者: " + sender_escaped +
                   "\\n内容: " + message_escaped + "\\n时间: " +
                   timestamp_escaped;
      json_data += "\"}}";
      Serial.println("DingTalk: " + json_data);
      http_code = http.POST(json_data);
      break;
    }

    case PUSH_TYPE_PUSHPLUS: {
      const String push_url =
          channel.url.length() > 0 ? channel.url : "http://www.pushplus.plus/send";
      http.begin(push_url);
      http.addHeader("Content-Type", "application/json");

      String pushplus_channel = "wechat";
      if (channel.key2.length() > 0) {
        if (channel.key2 == "wechat" || channel.key2 == "extension" ||
            channel.key2 == "app") {
          pushplus_channel = channel.key2;
        } else {
          Serial.println("Invalid PushPlus channel '" + channel.key2 +
                         "'. Using default 'wechat'.");
        }
      }

      String json_data = "{";
      json_data += "\"token\":\"" + channel.key1 + "\",";
      json_data += "\"title\":\"短信来自: " + sender_escaped + "\",";
      json_data += "\"content\":\"<b>发送者:</b> " + sender_escaped +
                   "<br><b>时间:</b> " + timestamp_escaped +
                   "<br><b>内容:</b><br>" + message_escaped + "\",";
      json_data += "\"channel\":\"" + pushplus_channel + "\"";
      json_data += "}";
      Serial.println("PushPlus: " + json_data);
      http_code = http.POST(json_data);
      break;
    }

    case PUSH_TYPE_SERVERCHAN: {
      const String serverchan_url = channel.url.length() > 0
                                        ? channel.url
                                        : ("https://sctapi.ftqq.com/" + channel.key1 +
                                           ".send");
      http.begin(serverchan_url);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      String post_data = "title=" + UrlEncode("短信来自: " + message.sender);
      post_data += "&desp=" + UrlEncode("**发送者:** " + message.sender +
                                        "\n\n**时间:** " + message.timestamp +
                                        "\n\n**内容:**\n\n" + message.text);
      Serial.println("ServerChan: " + post_data);
      http_code = http.POST(post_data);
      break;
    }

    case PUSH_TYPE_CUSTOM: {
      if (channel.customBody.length() == 0) {
        Serial.println("Custom template is empty; skipping channel");
        return;
      }
      http.begin(channel.url);
      http.addHeader("Content-Type", "application/json");
      String body = channel.customBody;
      body.replace("{sender}", sender_escaped);
      body.replace("{message}", message_escaped);
      body.replace("{timestamp}", timestamp_escaped);
      Serial.println("Custom body: " + body);
      http_code = http.POST(body);
      break;
    }

    case PUSH_TYPE_FEISHU: {
      const String webhook_url = channel.url;
      String json_data = "{";

      if (channel.key1.length() > 0) {
        // Feishu signs a second-based timestamp with the same secret value.
        const int64_t timestamp = time(nullptr);
        const String string_to_sign = String(timestamp) + "\n" + channel.key1;
        uint8_t hmac_result[32];
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
        mbedtls_md_hmac_starts(&ctx,
                               reinterpret_cast<const unsigned char*>(channel.key1.c_str()),
                               channel.key1.length());
        mbedtls_md_hmac_update(&ctx,
                               reinterpret_cast<const unsigned char*>(string_to_sign.c_str()),
                               string_to_sign.length());
        mbedtls_md_hmac_finish(&ctx, hmac_result);
        mbedtls_md_free(&ctx);
        const String sign = base64::encode(hmac_result, 32);

        json_data += "\"timestamp\":\"" + String(timestamp) + "\",";
        json_data += "\"sign\":\"" + sign + "\",";
      }

      json_data += "\"msg_type\":\"text\",";
      json_data += "\"content\":{\"text\":\"";
      json_data += "📱短信通知\\n发送者: " + sender_escaped +
                   "\\n内容: " + message_escaped + "\\n时间: " +
                   timestamp_escaped;
      json_data += "\"}}";

      http.begin(webhook_url);
      http.addHeader("Content-Type", "application/json");
      Serial.println("Feishu: " + json_data);
      http_code = http.POST(json_data);
      break;
    }

    case PUSH_TYPE_GOTIFY: {
      String gotify_url = channel.url;
      if (!gotify_url.endsWith("/")) {
        gotify_url += "/";
      }
      gotify_url += "message?token=" + channel.key1;

      http.begin(gotify_url);
      http.addHeader("Content-Type", "application/json");
      String json_data = "{";
      json_data += "\"title\":\"短信来自: " + sender_escaped + "\",";
      json_data += "\"message\":\"" + message_escaped + "\\n\\n时间: " +
                   timestamp_escaped + "\",";
      json_data += "\"priority\":5";
      json_data += "}";
      Serial.println("Gotify: " + json_data);
      http_code = http.POST(json_data);
      break;
    }

    case PUSH_TYPE_TELEGRAM: {
      String tg_base_url =
          channel.url.length() > 0 ? channel.url : "https://api.telegram.org";
      if (tg_base_url.endsWith("/")) {
        tg_base_url.remove(tg_base_url.length() - 1);
      }

      const String tg_url = tg_base_url + "/bot" + channel.key2 + "/sendMessage";
      http.begin(tg_url);
      http.addHeader("Content-Type", "application/json");

      String json_data = "{";
      json_data += "\"chat_id\":\"" + channel.key1 + "\",";
      String text = "📱短信通知\n发送者: " + sender_escaped +
                    "\n内容: " + message_escaped + "\n时间: " +
                    timestamp_escaped;
      json_data += "\"text\":\"" + text + "\"";
      json_data += "}";

      Serial.println("Telegram: " + json_data);
      http_code = http.POST(json_data);
      break;
    }

    default:
      Serial.println("Unknown push type");
      return;
  }

  if (http_code > 0) {
    Serial.printf("[%s] HTTP status: %d\n", channel_name.c_str(), http_code);
    if (http_code == HTTP_CODE_OK || http_code == HTTP_CODE_CREATED) {
      const String response = http.getString();
      Serial.println("Response: " + response);
    }
  } else {
    Serial.printf("[%s] HTTP request failed: %s\n", channel_name.c_str(),
                  http.errorToString(http_code).c_str());
  }
  http.end();
}

// Transport helpers.
String Notifier::UrlEncode(const String& value) const {
  String encoded;
  for (unsigned int i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == ' ') {
      encoded += '+';
    } else if (isalnum(c)) {
      encoded += c;
    } else {
      char code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      char code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

String Notifier::DingtalkSign(const String& secret, int64_t timestamp) const {
  const String string_to_sign = String(timestamp) + "\n" + secret;

  uint8_t hmac_result[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, reinterpret_cast<const unsigned char*>(secret.c_str()),
                         secret.length());
  mbedtls_md_hmac_update(
      &ctx, reinterpret_cast<const unsigned char*>(string_to_sign.c_str()),
      string_to_sign.length());
  mbedtls_md_hmac_finish(&ctx, hmac_result);
  mbedtls_md_free(&ctx);

  return UrlEncode(base64::encode(hmac_result, 32));
}

int64_t Notifier::GetUtcMillis() const {
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) == 0) {
    return static_cast<int64_t>(tv.tv_sec) * 1000LL + tv.tv_usec / 1000;
  }
  return static_cast<int64_t>(time(nullptr)) * 1000LL;
}

String Notifier::JsonEscape(const String& value) const {
  String result;
  for (unsigned int i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if (c == '"') {
      result += "\\\"";
    } else if (c == '\\') {
      result += "\\\\";
    } else if (c == '\n') {
      result += "\\n";
    } else if (c == '\r') {
      result += "\\r";
    } else if (c == '\t') {
      result += "\\t";
    } else {
      result += c;
    }
  }
  return result;
}
