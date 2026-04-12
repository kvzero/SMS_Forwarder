/**
 * @file sms_inbox.h
 * @brief SMS decision layer for blacklist checks and admin commands.
 */

#pragma once

#include <Arduino.h>

#include "config_store.h"

/** @brief Final decoded SMS payload handed to the inbox workflow. */
struct SmsMessage {
  String sender;      ///< Sender number reported by the modem.
  String text;        ///< Decoded SMS body text.
  String timestamp;   ///< Modem-provided SMS timestamp.
};

/** @brief Next action requested by SmsInbox::Process(). */
enum class InboxActionType {
  Ignore,           ///< Drop the SMS without side effects.
  Notify,           ///< Forward the SMS through normal notification paths.
  SendSmsCommand,   ///< Execute an admin SMS send command.
  SendEmailOnly,    ///< Send an email-only administrative result message.
  ResetDevice,      ///< Reset the modem and restart the MCU.
};

/** @brief Action payload produced after interpreting a completed SMS. */
struct InboxAction {
  InboxActionType type = InboxActionType::Ignore;  ///< Selected next action.
  SmsMessage message;                              ///< Original SMS for Notify actions.
  String commandPhone;                             ///< Target number for SendSmsCommand.
  String commandText;                              ///< SMS body for SendSmsCommand.
  String emailSubject;                             ///< Subject for administrative email actions.
  String emailBody;                                ///< Body for administrative email actions.
};

/**
 * @brief Interprets a completed SMS and chooses the next application action.
 */
class SmsInbox {
 public:
  /**
   * @brief Applies blacklist and admin-command rules to one decoded SMS.
   * @param config Current runtime configuration.
   * @param message Completed SMS payload from the modem layer.
   * @return The next action the top-level app loop should execute.
   */
  InboxAction Process(const AppConfig& config, const SmsMessage& message) const;

 private:
  bool IsInNumberBlackList(const AppConfig& config, const char* sender) const;
  bool IsAdmin(const AppConfig& config, const char* sender) const;
};
