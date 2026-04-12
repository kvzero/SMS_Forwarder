#include "sms_inbox.h"

bool SmsInbox::IsInNumberBlackList(const AppConfig& config, const char* sender) const {
  if (config.numberBlackList.length() == 0) {
    return false;
  }

  const String original_sender = String(sender);
  const bool has86 = original_sender.startsWith("+86");
  const String stripped_sender = has86 ? original_sender.substring(3) : "";

  const int list_len = static_cast<int>(config.numberBlackList.length());
  int start = 0;
  while (start <= list_len) {
    int end = config.numberBlackList.indexOf('\n', start);
    if (end == -1) {
      end = list_len;
    }

    String line = config.numberBlackList.substring(start, end);
    line.trim();

    if (line.length() > 0 &&
        (line.equals(original_sender) || (has86 && line.equals(stripped_sender)))) {
      return true;
    }

    start = end + 1;
  }

  return false;
}

bool SmsInbox::IsAdmin(const AppConfig& config, const char* sender) const {
  if (config.adminPhone.length() == 0) {
    return false;
  }

  String sender_str = String(sender);
  String admin_str = config.adminPhone;

  if (sender_str.startsWith("+86")) {
    sender_str = sender_str.substring(3);
  }
  if (admin_str.startsWith("+86")) {
    admin_str = admin_str.substring(3);
  }

  return sender_str.equals(admin_str);
}

InboxAction SmsInbox::Process(const AppConfig& config, const SmsMessage& message) const {
  InboxAction action;
  action.message = message;

  // This stage only decides what should happen next. Hardware side effects are
  // executed later by the top-level app flow.
  Serial.println("=== Processing SMS content ===");
  Serial.println("Sender: " + message.sender);
  Serial.println("Timestamp: " + message.timestamp);
  Serial.println("Message: " + message.text);
  Serial.println("==============================");

  if (IsInNumberBlackList(config, message.sender.c_str())) {
    Serial.println("Sender is in the phone number blacklist; ignoring this SMS");
    return action;
  }

  if (IsAdmin(config, message.sender.c_str())) {
    Serial.println("Received an admin SMS; checking command...");
    String sms_text = message.text;
    sms_text.trim();

    if (sms_text.startsWith("SMS:")) {
      const int first_colon = sms_text.indexOf(':');
      const int second_colon = sms_text.indexOf(':', first_colon + 1);
      if (second_colon > first_colon + 1) {
        action.type = InboxActionType::SendSmsCommand;
        action.commandPhone = sms_text.substring(first_colon + 1, second_colon);
        action.commandText = sms_text.substring(second_colon + 1);
        action.commandPhone.trim();
        action.commandText.trim();
        Serial.println("Target phone: " + action.commandPhone);
        Serial.println("SMS content: " + action.commandText);
        return action;
      }

      Serial.println("Invalid SMS command format");
      action.type = InboxActionType::SendEmailOnly;
      action.emailSubject = "命令执行失败";
      action.emailBody = "SMS命令格式错误，正确格式: SMS:号码:内容";
      return action;
    }

    if (sms_text.equals("RESET")) {
      Serial.println("Executing RESET command");
      action.type = InboxActionType::ResetDevice;
      action.emailSubject = "重启命令已执行";
      action.emailBody = "收到RESET命令，即将重启模组和ESP32...";
      return action;
    }
  }

  action.type = InboxActionType::Notify;
  return action;
}
