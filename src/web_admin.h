/**
 * @file web_admin.h
 * @brief Embedded HTTP admin interface for configuration and modem tools.
 */

#pragma once

#include <WebServer.h>

#include "config_store.h"
#include "modem.h"
#include "notifier.h"

/** @brief Embedded HTTP admin interface for configuration and modem tools. */
class WebAdmin {
 public:
  /**
   * @brief Creates the admin web controller around the long-lived app owners.
   * @param config_store Configuration persistence owner.
   * @param config Mutable runtime configuration.
   * @param modem Modem owner used by tools and queries.
   * @param notifier Notification owner used for update emails.
   * @param config_valid Shared flag indicating whether delivery settings exist.
   */
  WebAdmin(ConfigStore& config_store, AppConfig& config, Modem& modem,
           Notifier& notifier, bool& config_valid);

  /** @brief Registers routes and starts the HTTP server. */
  void Begin();
  /** @brief Pumps one iteration of WebServer client handling. */
  void HandleClient();

 private:
  bool CheckAuth();
  String GetDeviceUrl() const;
  String EscapeJson(const String& value) const;

  void HandleRoot();
  void HandleToolsPage();
  void HandleFlightMode();
  void HandleATCommand();
  void HandleQuery();
  void HandleSendSms();
  void HandlePing();
  void HandleSave();

  ConfigStore& config_store_;
  AppConfig& config_;
  Modem& modem_;
  Notifier& notifier_;
  bool& config_valid_;
  WebServer server_;
};
