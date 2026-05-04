/**
 * @file task_protocol.h
 * @brief Queue-safe contracts shared by the Modem, App, and Web tasks.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>

#include "board_config.h"
#include "config_store.h"

constexpr size_t kTaskPhoneCapacity = 32;
constexpr size_t kTaskTimestampCapacity = 48;
constexpr size_t kTaskCommandCapacity = 128;
constexpr size_t kTaskOutgoingTextCapacity = kMaxOutboundSmsUtf8Bytes + 1;
constexpr size_t kTaskIncomingTextCapacity = kMaxInboundSmsUtf8Bytes + 1;
constexpr size_t kTaskResultCapacity = 1024;

/**
 * @brief Identifies which task requested a modem-side operation.
 */
enum class ModemRequester : uint8_t {
  Web,
  App,
};

/**
 * @brief Supported operations that must be executed by the modem owner task.
 */
enum class ModemRequestType : uint8_t {
  SendAtCommand,
  SendSms,
  Reset,
};

/**
 * @brief Queue-safe SMS payload exchanged between tasks.
 */
struct SmsEnvelope {
  char sender[kTaskPhoneCapacity] = {0};
  char text[kTaskIncomingTextCapacity] = {0};
  char timestamp[kTaskTimestampCapacity] = {0};
};

/**
 * @brief Request queued to the modem task.
 *
 * The payload uses fixed-size character arrays so the struct can be copied
 * through a FreeRTOS queue without sharing heap-owned String state.
 */
struct ModemRequest {
  uint32_t requestId = 0;
  ModemRequester requester = ModemRequester::Web;
  ModemRequestType type = ModemRequestType::SendAtCommand;
  uint32_t timeoutMs = 0;
  char command[kTaskCommandCapacity] = {0};
  char phone[kTaskPhoneCapacity] = {0};
  char text[kTaskOutgoingTextCapacity] = {0};
};

/**
 * @brief Result returned by the modem task.
 */
struct ModemResponse {
  uint32_t requestId = 0;
  ModemRequester requester = ModemRequester::Web;
  ModemRequestType type = ModemRequestType::SendAtCommand;
  bool success = false;
  char message[kTaskResultCapacity] = {0};
};

/**
 * @brief Events queued to the application task.
 */
enum class AppEventType : uint8_t {
  StartupNotice,
  ConfigUpdated,
  IncomingSms,
};

/**
 * @brief Queue-safe event consumed by the application task.
 */
struct AppEvent {
  AppEventType type = AppEventType::IncomingSms;
  SmsEnvelope sms;
};

/**
 * @brief Shared runtime configuration guarded by a mutex.
 *
 * The web task updates the persisted configuration, while the app task reads
 * snapshots when it needs to process SMS actions or emit notifications.
 */
struct SharedConfigState {
  SemaphoreHandle_t mutex = nullptr;
  AppConfig config;
  bool configValid = false;
};

/**
 * @brief Copies a C string into a queue-safe fixed-size buffer.
 * @tparam N Destination capacity including the trailing null terminator.
 * @param dest Fixed-size destination buffer.
 * @param src Source string. Null is treated as an empty string.
 * @return True when the source was truncated to fit in @p dest.
 */
template <size_t N>
inline bool CopyCString(char (&dest)[N], const char* src) {
  static_assert(N > 0, "Destination buffer must not be empty");
  if (src == nullptr) {
    dest[0] = '\0';
    return false;
  }

  const size_t src_len = std::strlen(src);
  std::strncpy(dest, src, N - 1);
  dest[N - 1] = '\0';
  return src_len >= N;
}

/**
 * @brief Copies an Arduino String into a queue-safe fixed-size buffer.
 * @tparam N Destination capacity including the trailing null terminator.
 * @param dest Fixed-size destination buffer.
 * @param src Source string.
 * @return True when the source was truncated to fit in @p dest.
 */
template <size_t N>
inline bool CopyString(char (&dest)[N], const String& src) {
  return CopyCString(dest, src.c_str());
}
