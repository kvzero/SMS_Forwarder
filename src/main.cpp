#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "board_pins.h"
#include "config_store.h"
#include "modem.h"
#include "notifier.h"
#include "sms_inbox.h"
#include "status_led.h"
#include "task_protocol.h"
#include "web_admin.h"
#include "wifi_runtime.h"

/**
 * @file main.cpp
 * @brief Top-level task wiring for the Modem, App, and Web owners.
 */

namespace {

constexpr uint32_t kStartupDelayMs = 1500;
constexpr uint32_t kLoopSleepMs = 100;
constexpr uint32_t kTaskIdleDelayMs = 10;
constexpr uint32_t kSendSmsWaitMs = 40000;
constexpr uint32_t kProvisionButtonHoldMs = 3000;
constexpr uint32_t kLedRefreshMs = 100;
constexpr uint32_t kModemTaskStackBytes = 8192;
constexpr uint32_t kAppTaskStackBytes = 12288;
constexpr uint32_t kWebTaskStackBytes = 12288;
constexpr UBaseType_t kModemQueueDepth = 8;
constexpr UBaseType_t kWebResponseQueueDepth = 8;
constexpr UBaseType_t kAppResponseQueueDepth = 4;
constexpr UBaseType_t kAppEventQueueDepth = 16;

ConfigStore config_store;
SharedConfigState shared_state;
QueueHandle_t modem_request_queue = nullptr;
QueueHandle_t web_modem_response_queue = nullptr;
QueueHandle_t app_modem_response_queue = nullptr;
QueueHandle_t app_event_queue = nullptr;
StatusLed status_led;

struct ProvisionButtonState {
  bool tracking = false;
  bool triggered = false;
  unsigned long pressed_since_ms = 0;
};

String GetDeviceUrl() {
  if (WiFi.status() == WL_CONNECTED) {
    return "http://" + WiFi.localIP().toString() + "/";
  }

  if ((WiFi.getMode() & WIFI_AP) != 0) {
    return "http://" + WiFi.softAPIP().toString() + "/";
  }

  return "http://0.0.0.0/";
}

bool LoadConfigSnapshot(AppConfig& config, bool* config_valid = nullptr) {
  if (shared_state.mutex == nullptr) {
    return false;
  }

  if (xSemaphoreTake(shared_state.mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  config = shared_state.config;
  if (config_valid != nullptr) {
    *config_valid = shared_state.configValid;
  }
  xSemaphoreGive(shared_state.mutex);
  return true;
}

bool SubmitModemRequest(const ModemRequest& request,
                        TickType_t timeout_ticks = pdMS_TO_TICKS(100)) {
  if (modem_request_queue == nullptr) {
    return false;
  }

  return xQueueSend(modem_request_queue, &request, timeout_ticks) == pdTRUE;
}

bool WaitForAppModemResponse(uint32_t request_id, ModemResponse& response,
                             TickType_t timeout_ticks) {
  const TickType_t start = xTaskGetTickCount();
  while (true) {
    const TickType_t now = xTaskGetTickCount();
    const TickType_t elapsed = now - start;
    if (elapsed >= timeout_ticks) {
      break;
    }

    const TickType_t remaining = timeout_ticks - elapsed;
    const TickType_t wait_time = remaining > pdMS_TO_TICKS(50)
                                     ? pdMS_TO_TICKS(50)
                                     : remaining;

    if (xQueueReceive(app_modem_response_queue, &response, wait_time) != pdTRUE) {
      continue;
    }

    if (response.requestId == request_id) {
      return true;
    }
  }

  return false;
}

void CopySmsEnvelope(SmsEnvelope& envelope, const SmsMessage& message) {
  if (CopyString(envelope.sender, message.sender)) {
    Serial.printf("Incoming SMS sender was truncated (%u -> %u bytes)\n",
                  static_cast<unsigned int>(message.sender.length()),
                  static_cast<unsigned int>(sizeof(envelope.sender) - 1));
  }
  if (CopyString(envelope.text, message.text)) {
    Serial.printf("Incoming SMS text was truncated (%u -> %u bytes)\n",
                  static_cast<unsigned int>(message.text.length()),
                  static_cast<unsigned int>(sizeof(envelope.text) - 1));
  }
  if (CopyString(envelope.timestamp, message.timestamp)) {
    Serial.printf("Incoming SMS timestamp was truncated (%u -> %u bytes)\n",
                  static_cast<unsigned int>(message.timestamp.length()),
                  static_cast<unsigned int>(sizeof(envelope.timestamp) - 1));
  }
}

SmsMessage ToSmsMessage(const SmsEnvelope& envelope) {
  SmsMessage message;
  message.sender = envelope.sender;
  message.text = envelope.text;
  message.timestamp = envelope.timestamp;
  return message;
}

bool QueueAppEvent(const AppEvent& event,
                   TickType_t timeout_ticks = pdMS_TO_TICKS(100)) {
  if (app_event_queue == nullptr) {
    return false;
  }

  return xQueueSend(app_event_queue, &event, timeout_ticks) == pdTRUE;
}

StatusLedMode ResolveStatusLedMode(const WifiStatusSnapshot& snapshot) {
  if (snapshot.connectInProgress ||
      snapshot.mode == WifiRuntimeMode::TryingSavedNetworks) {
    return StatusLedMode::Connecting;
  }

  if (snapshot.portalActive ||
      snapshot.mode == WifiRuntimeMode::ProvisioningPortal ||
      snapshot.mode == WifiRuntimeMode::PortalHandoff) {
    return StatusLedMode::Provisioning;
  }

  return StatusLedMode::Off;
}

void PollProvisionButton(WifiRuntime& wifi_runtime, ProvisionButtonState& state) {
  const bool pressed = digitalRead(kBootButtonPin) == LOW;
  const unsigned long now = millis();

  if (!pressed) {
    state = ProvisionButtonState{};
    return;
  }

  if (!state.tracking) {
    state.tracking = true;
    state.pressed_since_ms = now;
    return;
  }

  if (state.triggered || now - state.pressed_since_ms < kProvisionButtonHoldMs) {
    return;
  }

  state.triggered = true;
  wifi_runtime.ForceProvisioningPortal();
}

void SendStartupNotice(Notifier& notifier) {
  AppConfig config;
  bool config_valid = false;
  if (!LoadConfigSnapshot(config, &config_valid) || !config_valid) {
    return;
  }

  const String subject = "SMS Forwarder started";
  const String body = "Device is online.\nURL: " + GetDeviceUrl();
  notifier.SendEmail(config, subject.c_str(), body.c_str());
}

void SendConfigUpdatedNotice(Notifier& notifier) {
  AppConfig config;
  bool config_valid = false;
  if (!LoadConfigSnapshot(config, &config_valid) || !config_valid) {
    return;
  }

  const String subject = "SMS Forwarder configuration updated";
  const String body = "Configuration has been saved.\nURL: " + GetDeviceUrl();
  notifier.SendEmail(config, subject.c_str(), body.c_str());
}

void HandleInboxAction(const InboxAction& action, Notifier& notifier,
                       uint32_t& next_request_id) {
  AppConfig config;
  if (!LoadConfigSnapshot(config, nullptr)) {
    Serial.println("Failed to load the shared configuration snapshot");
    return;
  }

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
      ModemRequest request;
      request.requestId = next_request_id++;
      request.requester = ModemRequester::App;
      request.type = ModemRequestType::SendSms;
      CopyString(request.phone, action.commandPhone);
      CopyString(request.text, action.commandText);

      bool success = false;
      if (SubmitModemRequest(request, pdMS_TO_TICKS(1000))) {
        ModemResponse response;
        success = WaitForAppModemResponse(request.requestId, response,
                                          pdMS_TO_TICKS(kSendSmsWaitMs)) &&
                  response.success;
      }

      String subject = success ? "Admin SMS command succeeded"
                               : "Admin SMS command failed";
      String body = "Target: ";
      body += action.commandPhone;
      body += "\nMessage: ";
      body += action.commandText;
      body += "\nResult: ";
      body += success ? "Success" : "Failed";
      notifier.SendEmail(config, subject.c_str(), body.c_str());
      return;
    }

    case InboxActionType::ResetDevice: {
      notifier.SendEmail(config, action.emailSubject.c_str(), action.emailBody.c_str());

      ModemRequest request;
      request.requestId = next_request_id++;
      request.requester = ModemRequester::App;
      request.type = ModemRequestType::Reset;
      SubmitModemRequest(request, pdMS_TO_TICKS(1000));

      ModemResponse response;
      WaitForAppModemResponse(request.requestId, response, pdMS_TO_TICKS(10000));

      Serial.println("Restarting ESP32...");
      vTaskDelay(pdMS_TO_TICKS(1000));
      ESP.restart();
      return;
    }
  }
}

void SendModemResponse(const ModemResponse& response) {
  QueueHandle_t target_queue = nullptr;
  if (response.requester == ModemRequester::Web) {
    target_queue = web_modem_response_queue;
  } else {
    target_queue = app_modem_response_queue;
  }

  if (target_queue == nullptr) {
    Serial.println("No modem response queue is available for the requester");
    return;
  }

  if (xQueueSend(target_queue, &response, pdMS_TO_TICKS(100)) != pdTRUE) {
    Serial.println("Failed to deliver the modem response to the requester queue");
  }
}

void ProcessModemRequest(Modem& modem, const ModemRequest& request) {
  ModemResponse response;
  response.requestId = request.requestId;
  response.requester = request.requester;
  response.type = request.type;

  switch (request.type) {
    case ModemRequestType::SendAtCommand: {
      const String raw_response = modem.SendAtCommand(
          request.command,
          request.timeoutMs > 0 ? request.timeoutMs : 5000);
      response.success = raw_response.length() > 0;
      CopyString(response.message, raw_response);
      break;
    }

    case ModemRequestType::SendSms: {
      response.success = modem.SendSms(request.phone, request.text);
      CopyCString(response.message,
                  response.success ? "SMS send completed." : "SMS send failed.");
      break;
    }

    case ModemRequestType::Reset:
      modem.Reset();
      response.success = true;
      CopyCString(response.message, "Modem reset completed.");
      break;
  }

  SendModemResponse(response);
}

void ModemTaskMain(void*) {
  static Modem modem(Serial1);

  modem.Begin();

  for (;;) {
    ModemRequest request;
    if (xQueueReceive(modem_request_queue, &request, 0) == pdTRUE) {
      ProcessModemRequest(modem, request);
    }

    SmsMessage message;
    if (modem.Poll(message)) {
      AppEvent event;
      event.type = AppEventType::IncomingSms;
      CopySmsEnvelope(event.sms, message);
      if (!QueueAppEvent(event, pdMS_TO_TICKS(100))) {
        Serial.println("Failed to enqueue the incoming SMS for AppTask");
      }
    }

    if (Serial.available()) {
      modem.WritePassthroughByte(Serial.read());
    }

    vTaskDelay(pdMS_TO_TICKS(kTaskIdleDelayMs));
  }
}

void AppTaskMain(void*) {
  static SmsInbox sms_inbox;
  static Notifier notifier;

  notifier.Begin();
  uint32_t next_request_id = 1;

  for (;;) {
    AppEvent event;
    if (xQueueReceive(app_event_queue, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    switch (event.type) {
      case AppEventType::StartupNotice:
        SendStartupNotice(notifier);
        break;

      case AppEventType::ConfigUpdated:
        SendConfigUpdatedNotice(notifier);
        break;

      case AppEventType::IncomingSms: {
        AppConfig config;
        if (!LoadConfigSnapshot(config, nullptr)) {
          Serial.println("Failed to load config before processing an SMS");
          break;
        }

        const SmsMessage message = ToSmsMessage(event.sms);
        const InboxAction action = sms_inbox.Process(config, message);
        HandleInboxAction(action, notifier, next_request_id);
        break;
      }
    }
  }
}

void WebTaskMain(void*) {
  static WifiRuntime wifi_runtime(config_store, shared_state, app_event_queue);
  static WebAdmin web_admin(config_store, shared_state, wifi_runtime, modem_request_queue,
                            web_modem_response_queue, app_event_queue);
  ProvisionButtonState provision_button_state;
  unsigned long last_led_refresh_ms = 0;

  wifi_runtime.Begin();
  web_admin.Begin();
  for (;;) {
    PollProvisionButton(wifi_runtime, provision_button_state);
    wifi_runtime.Poll();

    const unsigned long now = millis();
    if (now - last_led_refresh_ms >= kLedRefreshMs) {
      WifiStatusSnapshot snapshot;
      wifi_runtime.GetStatusSnapshot(snapshot);
      status_led.SetMode(ResolveStatusLedMode(snapshot));
      last_led_refresh_ms = now;
    }

    status_led.Poll(now);
    web_admin.HandleClient();
    vTaskDelay(pdMS_TO_TICKS(kTaskIdleDelayMs));
  }
}

void HaltSetup(const char* message) {
  Serial.println(message);
  status_led.RunBlockingFaultPattern();
}

}  // namespace

void setup() {
  status_led.Begin();
  status_led.SetMode(StatusLedMode::Booting);
  pinMode(kBootButtonPin, INPUT_PULLUP);

  Serial.begin(115200);
  delay(kStartupDelayMs);

  shared_state.mutex = xSemaphoreCreateMutex();
  if (shared_state.mutex == nullptr) {
    HaltSetup("Failed to create the shared config mutex");
  }

  modem_request_queue = xQueueCreate(kModemQueueDepth, sizeof(ModemRequest));
  web_modem_response_queue = xQueueCreate(kWebResponseQueueDepth, sizeof(ModemResponse));
  app_modem_response_queue = xQueueCreate(kAppResponseQueueDepth, sizeof(ModemResponse));
  app_event_queue = xQueueCreate(kAppEventQueueDepth, sizeof(AppEvent));
  if (modem_request_queue == nullptr || web_modem_response_queue == nullptr ||
      app_modem_response_queue == nullptr || app_event_queue == nullptr) {
    HaltSetup("Failed to create one or more task queues");
  }

  config_store.Load(shared_state.config);
  shared_state.configValid = IsConfigValid(shared_state.config);

  if (xTaskCreate(ModemTaskMain, "ModemTask", kModemTaskStackBytes, nullptr, 4,
                  nullptr) != pdPASS) {
    HaltSetup("Failed to create ModemTask");
  }
  if (xTaskCreate(AppTaskMain, "AppTask", kAppTaskStackBytes, nullptr, 3,
                  nullptr) != pdPASS) {
    HaltSetup("Failed to create AppTask");
  }
  if (xTaskCreate(WebTaskMain, "WebTask", kWebTaskStackBytes, nullptr, 2,
                  nullptr) != pdPASS) {
    HaltSetup("Failed to create WebTask");
  }
}

void loop() {
  static unsigned long last_print_time = 0;
  bool config_valid = false;

  if (shared_state.mutex != nullptr &&
      xSemaphoreTake(shared_state.mutex, portMAX_DELAY) == pdTRUE) {
    config_valid = shared_state.configValid;
    xSemaphoreGive(shared_state.mutex);
  }

  if (!config_valid && millis() - last_print_time >= 1000) {
    last_print_time = millis();
    Serial.println("Please visit " + GetDeviceUrl() + " to configure the system");
  }

  vTaskDelay(pdMS_TO_TICKS(kLoopSleepMs));
}
