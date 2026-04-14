/**
 * @file web_admin.h
 * @brief Embedded HTTP admin interface for configuration and modem tools.
 */

#pragma once

#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "config_store.h"
#include "task_protocol.h"
#include "wifi_runtime.h"

/** @brief Embedded HTTP admin interface for configuration and modem tools. */
class WebAdmin {
 public:
  /**
   * @brief Creates the admin web controller around the long-lived task owners.
   * @param config_store Configuration persistence owner.
   * @param shared_state Shared runtime configuration guarded by a mutex.
   * @param wifi_runtime Wi-Fi STA/AP runtime owner.
   * @param modem_request_queue Queue used to submit modem work.
   * @param web_response_queue Queue used to receive modem results for web calls.
   * @param app_event_queue Queue used to notify the app task about config updates.
   */
  WebAdmin(ConfigStore& config_store, SharedConfigState& shared_state,
           WifiRuntime& wifi_runtime,
           QueueHandle_t modem_request_queue, QueueHandle_t web_response_queue,
           QueueHandle_t app_event_queue);

  /** @brief Registers routes and starts the HTTP server. */
  void Begin();
  /** @brief Pumps one iteration of WebServer client handling. */
  void HandleClient();

 private:
  struct PendingWebRequest {
    bool inUse = false;
    bool completed = false;
    uint32_t requestId = 0;
    bool success = false;
    unsigned long createdAtMs = 0;
    String message;
  };

  bool LoadConfigSnapshot(AppConfig& config, bool* config_valid = nullptr) const;
  bool CheckAuth();
  bool CheckProvisionAccess();
  bool SubmitModemRequest(const ModemRequest& request,
                          TickType_t timeout_ticks = pdMS_TO_TICKS(100));
  bool WaitForModemResponse(uint32_t request_id, ModemResponse& response,
                            TickType_t timeout_ticks);
  bool RequestAtCommand(const String& cmd, unsigned long timeout_ms, String& response);
  bool RequestSendSms(const String& phone, const String& content);
  uint32_t StartAsyncAtCommand(const String& cmd, unsigned long timeout_ms);
  uint32_t AllocateRequestId();
  void DrainModemResponses();
  void StorePendingResponse(const ModemResponse& response);
  PendingWebRequest* FindPendingRequest(uint32_t request_id);
  void CleanupPendingRequests();
  String GetDeviceUrl() const;
  String EscapeJson(const String& value) const;

  void HandleRoot();
  void HandleAdminPage();
  void HandleToolsPage();
  void HandleFlightMode();
  void HandleATCommand();
  void HandleQuery();
  void HandleSendSms();
  void HandleSave();
  void HandleModemResult();
  void HandleProvisionPage();
  void HandleProvisionStatus();
  void HandleProvisionNetworks();
  void HandleProvisionCredentials();
  void HandleProvisionConnect();
  void HandleProvisionDelete();
  void HandleProvisionClear();

  ConfigStore& config_store_;
  SharedConfigState& shared_state_;
  WifiRuntime& wifi_runtime_;
  QueueHandle_t modem_request_queue_;
  QueueHandle_t web_response_queue_;
  QueueHandle_t app_event_queue_;
  uint32_t next_request_id_;
  PendingWebRequest pending_requests_[8];
  WebServer server_;
};
