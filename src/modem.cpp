/**
 * @file modem.cpp
 * @brief Modem bring-up, AT command flow, and SMS receive pipeline.
 */

#include "modem.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr TickType_t kRetryYieldDelay = pdMS_TO_TICKS(500);
constexpr const char* kTruncatedSuffixPrefix = "[TRUNCATED: total=";

enum class MultipartSplitStatus : uint8_t {
  Ready = 0,
  TooManyParts,
  EncodeFailed,
};

void FillSmsMessage(SmsMessage& message, const String& sender, const String& text,
                    const String& timestamp) {
  message.sender = sender;
  message.text = text;
  message.timestamp = timestamp;
}

int ClampConcatTotalParts(int reported_total_parts) {
  if (reported_total_parts < 1) {
    return 1;
  }
  if (reported_total_parts > kMaxInboundSmsParts) {
    return kMaxInboundSmsParts;
  }
  return reported_total_parts;
}

String Utf8CharacterAt(PDU& encoder, const char* text) {
  const int utf8_length = encoder.utf8Length(text);
  if (utf8_length <= 0) {
    return String(text[0]);
  }

  String character;
  for (int index = 0; index < utf8_length && text[index] != '\0'; ++index) {
    character += text[index];
  }
  return character;
}

MultipartSplitStatus SplitMultipartSms(const char* phone_number, const String& message,
                                       String (&parts)[kMaxOutboundSmsParts],
                                       uint8_t& part_count) {
  part_count = 0;
  PDU probe(512);
  probe.setSCAnumber();

  const char* cursor = message.c_str();
  String current_part;
  while (*cursor != '\0') {
    const String next_character = Utf8CharacterAt(probe, cursor);
    if (next_character.length() == 0) {
      return MultipartSplitStatus::EncodeFailed;
    }

    const String candidate = current_part + next_character;
    const int encode_result = probe.encodePDU(phone_number, candidate.c_str(), 1, 2, 1);
    if (encode_result >= 0) {
      current_part = candidate;
      cursor += next_character.length();
      continue;
    }

    if (encode_result != PDU::GSM7_TOO_LONG && encode_result != PDU::UCS2_TOO_LONG) {
      return MultipartSplitStatus::EncodeFailed;
    }
    if (current_part.length() == 0 || part_count >= kMaxOutboundSmsParts) {
      return MultipartSplitStatus::TooManyParts;
    }

    parts[part_count++] = current_part;
    current_part = "";
  }

  if (current_part.length() > 0) {
    if (part_count >= kMaxOutboundSmsParts) {
      return MultipartSplitStatus::TooManyParts;
    }
    parts[part_count++] = current_part;
  }

  return part_count > 0 ? MultipartSplitStatus::Ready
                        : MultipartSplitStatus::EncodeFailed;
}

}  // namespace

OutboundSmsPolicyStatus AnalyzeOutboundSms(const char* phone_number, const String& message) {
  PDU encoder(512);
  encoder.setSCAnumber();
  const int pdu_len = encoder.encodePDU(phone_number, message.c_str());
  if (pdu_len >= 0) {
    return OutboundSmsPolicyStatus::CanSend;
  }

  if (pdu_len != PDU::GSM7_TOO_LONG && pdu_len != PDU::UCS2_TOO_LONG) {
    return OutboundSmsPolicyStatus::EncodeFailed;
  }

  String parts[kMaxOutboundSmsParts];
  uint8_t part_count = 0;
  switch (SplitMultipartSms(phone_number, message, parts, part_count)) {
    case MultipartSplitStatus::Ready:
      return OutboundSmsPolicyStatus::CanSend;
    case MultipartSplitStatus::TooManyParts:
      return OutboundSmsPolicyStatus::TooManyParts;
    case MultipartSplitStatus::EncodeFailed:
    default:
      return OutboundSmsPolicyStatus::EncodeFailed;
  }
}

// Startup and modem session ownership.
Modem::Modem(HardwareSerial& serial_port)
    : serial_(serial_port), pdu_(4096), urc_state_(UrcState::Idle), line_pos_(0) {
  line_buffer_[0] = 0;
  InitConcatBuffer();
}

void Modem::Begin() {
  serial_.begin(115200, SERIAL_8N1, kModemRxPin, kModemTxPin);
  serial_.setRxBufferSize(kSerialBufferSize);

  while (serial_.available()) {
    serial_.read();
  }
  ModemPowerCycle();
  while (serial_.available()) {
    serial_.read();
  }

  InitConcatBuffer();

  while (!SendAtAndWaitOK("AT", 1000)) {
    Serial.println("AT did not respond; retrying...");
    vTaskDelay(kRetryYieldDelay);
  }
  Serial.println("Modem AT handshake is ready");

  // Keep the modem in SMS-only mode. The ML307R manual documents autoconn and
  // host auto dial-up as the persistent switches for application-layer data.
  while (!SendAtAndWaitOK("AT+MUECONFIG=\"autoconn\",0", 2000)) {
    Serial.println("Failed to disable automatic module data bring-up; retrying...");
    vTaskDelay(kRetryYieldDelay);
  }
  Serial.println("Automatic module data bring-up disabled");

  while (!SendAtAndWaitOK("AT+MDIALUPCFG=\"auto\",0", 2000)) {
    Serial.println("Failed to disable automatic host dial-up; retrying...");
    vTaskDelay(kRetryYieldDelay);
  }
  Serial.println("Automatic host dial-up disabled");

  while (!SendAtAndWaitOK("AT+CGACT=0,1", 5000)) {
    Serial.println("Failed to disable the data connection; retrying...");
    vTaskDelay(kRetryYieldDelay);
  }
  Serial.println("Data connection disabled with AT+CGACT=0,1 to avoid traffic usage");

  while (!SendAtAndWaitOK("AT+CNMI=2,2,0,0,0", 1000)) {
    Serial.println("Failed to configure CNMI; retrying...");
    vTaskDelay(kRetryYieldDelay);
  }
  Serial.println("CNMI configuration applied");

  while (!SendAtAndWaitOK("AT+CMGF=0", 1000)) {
    Serial.println("Failed to switch to PDU mode; retrying...");
    vTaskDelay(kRetryYieldDelay);
  }
  Serial.println("PDU mode enabled");

  while (!WaitCereg()) {
    Serial.println("Waiting for network registration...");
    vTaskDelay(kRetryYieldDelay);
  }
  Serial.println("Network registration completed");
}

// Incoming SMS pipeline.
bool Modem::Poll(SmsMessage& message) {
  // Emit timed-out long SMS groups through the same path as live URCs.
  if (CheckConcatTimeout(message)) {
    return true;
  }

  const String line = ReadSerialLine();
  if (line.length() == 0) {
    return false;
  }

  Serial.println("Debug> " + line);

  if (urc_state_ == UrcState::Idle) {
    if (line.startsWith("+CMT:")) {
      Serial.println("Detected +CMT; waiting for PDU data...");
      urc_state_ = UrcState::WaitPdu;
    }
    return false;
  }

  if (!IsHexString(line)) {
    Serial.println("Received non-PDU data while waiting for PDU; returning to IDLE");
    urc_state_ = UrcState::Idle;
    return false;
  }

  Serial.println("Received PDU data: " + line);
  Serial.println("PDU length: " + String(line.length()) + " chars");

  bool has_message = false;
  if (!pdu_.decodePDU(line.c_str())) {
    Serial.println("PDU decode failed");
  } else {
    Serial.println("PDU decode succeeded");
    Serial.println("=== SMS payload ===");
    Serial.println("Sender: " + String(pdu_.getSender()));
    Serial.println("Timestamp: " + String(pdu_.getTimeStamp()));
    Serial.println("Message: " + String(pdu_.getText()));

    int* concat_info = pdu_.getConcatInfo();
    const int ref_number = concat_info[0];
    const int part_number = concat_info[1];
    const int reported_total_parts = concat_info[2];

    Serial.printf("Long SMS info: ref=%d, part=%d, total=%d\n", ref_number, part_number,
                  reported_total_parts);
    Serial.println("===================");

    if (reported_total_parts > 1 && part_number > 0) {
      Serial.printf("Received long SMS segment %d/%d\n", part_number, reported_total_parts);

      const int slot =
          FindOrCreateConcatSlot(ref_number, pdu_.getSender(), reported_total_parts);
      const int part_index = part_number - 1;
      const int expected_total_parts = concat_buffer_[slot].totalParts;
      if (part_index >= 0 && part_index < expected_total_parts) {
        if (!concat_buffer_[slot].parts[part_index].valid) {
          concat_buffer_[slot].parts[part_index].valid = true;
          concat_buffer_[slot].parts[part_index].text = String(pdu_.getText());
          concat_buffer_[slot].receivedParts++;

          if (concat_buffer_[slot].receivedParts == 1) {
            concat_buffer_[slot].timestamp = String(pdu_.getTimeStamp());
          }

          Serial.printf("  Cached segment %d; now have %d/%d\n", part_number,
                        concat_buffer_[slot].receivedParts, expected_total_parts);
        } else {
          Serial.printf("  Segment %d already exists; skipping duplicate\n", part_number);
        }
      } else if (part_index >= expected_total_parts &&
                 !concat_buffer_[slot].droppedPartLogged) {
        concat_buffer_[slot].droppedPartLogged = true;
        Serial.printf("Long SMS exceeds max parts; dropping segment %d (reported total=%d, "
                      "max=%d)\n",
                      part_number, concat_buffer_[slot].reportedParts, kMaxInboundSmsParts);
      }

      if (concat_buffer_[slot].receivedParts >= expected_total_parts) {
        Serial.println("Long SMS is complete; assembling and forwarding");
        FillSmsMessage(message, concat_buffer_[slot].sender, AssembleConcatSms(slot),
                       concat_buffer_[slot].timestamp);
        ClearConcatSlot(slot);
        has_message = true;
      }
    } else {
      FillSmsMessage(message, String(pdu_.getSender()), String(pdu_.getText()),
                     String(pdu_.getTimeStamp()));
      has_message = true;
    }
  }

  urc_state_ = UrcState::Idle;
  return has_message;
}

// Outbound commands and diagnostics.
bool Modem::SendSms(const char* phone_number, const char* message) {
  Serial.println("Preparing to send SMS...");
  Serial.print("Target phone: ");
  Serial.println(phone_number);
  Serial.print("SMS content: ");
  Serial.println(message);

  PDU encoder(512);
  encoder.setSCAnumber();
  const int pdu_len = encoder.encodePDU(phone_number, message);
  if (pdu_len >= 0) {
    return SendEncodedPdu(encoder, pdu_len);
  }

  if (pdu_len != PDU::GSM7_TOO_LONG && pdu_len != PDU::UCS2_TOO_LONG) {
    Serial.print("PDU encode failed, error code: ");
    Serial.println(pdu_len);
    return false;
  }

  String parts[kMaxOutboundSmsParts];
  uint8_t part_count = 0;
  switch (SplitMultipartSms(phone_number, message, parts, part_count)) {
    case MultipartSplitStatus::Ready:
      break;
    case MultipartSplitStatus::TooManyParts:
      Serial.printf("Outgoing SMS exceeds max parts; rejecting message (max=%d)\n",
                    kMaxOutboundSmsParts);
      return false;
    case MultipartSplitStatus::EncodeFailed:
    default:
      Serial.println("Failed to split multipart SMS");
      return false;
  }

  const unsigned short concat_ref =
      static_cast<unsigned short>((millis() & 0xFFFFu) == 0 ? 1 : (millis() & 0xFFFFu));
  Serial.printf("Sending multipart SMS with %u parts\n", part_count);
  for (uint8_t index = 0; index < part_count; ++index) {
    PDU part_encoder(512);
    part_encoder.setSCAnumber();
    const int part_length =
        part_encoder.encodePDU(phone_number, parts[index].c_str(), concat_ref,
                               part_count, index + 1);
    if (part_length < 0) {
      Serial.printf("Multipart PDU encode failed at part %u, error %d\n", index + 1,
                    part_length);
      return false;
    }
    if (!SendEncodedPdu(part_encoder, part_length)) {
      Serial.printf("Multipart SMS send failed at part %u/%u\n", index + 1, part_count);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kSmsMultipartInterPartDelayMs));
  }

  return true;
}

String Modem::SendAtCommand(const char* cmd, unsigned long timeout) {
  while (serial_.available()) {
    serial_.read();
  }
  serial_.println(cmd);

  const unsigned long start = millis();
  String response;
  while (millis() - start < timeout) {
    while (serial_.available()) {
      response += static_cast<char>(serial_.read());
      if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0) {
        delay(50);
        while (serial_.available()) {
          response += static_cast<char>(serial_.read());
        }
        return response;
      }
    }
  }
  return response;
}

void Modem::Reset() {
  Serial.println("Hard resetting the modem by toggling EN...");
  ModemPowerCycle();

  while (serial_.available()) {
    serial_.read();
  }

  bool ok = false;
  for (int i = 0; i < 10; ++i) {
    if (SendAtAndWaitOK("AT", 1000)) {
      ok = true;
      break;
    }
    Serial.println("AT is still not responding; waiting for the modem to boot...");
  }

  if (ok) {
    Serial.println("Modem AT handshake recovered");
  } else {
    Serial.println("Modem AT is still unavailable; check EN wiring, power, and baud rate");
  }
}

// Low-level serial helpers.
void Modem::WritePassthroughByte(uint8_t byte) {
  serial_.write(byte);
}

bool Modem::SendAtAndWaitOK(const char* cmd, unsigned long timeout) {
  while (serial_.available()) {
    serial_.read();
  }
  serial_.println(cmd);

  const unsigned long start = millis();
  String response;
  while (millis() - start < timeout) {
    while (serial_.available()) {
      response += static_cast<char>(serial_.read());
      if (response.indexOf("OK") >= 0) {
        return true;
      }
      if (response.indexOf("ERROR") >= 0) {
        return false;
      }
    }
  }
  return false;
}

bool Modem::SendEncodedPdu(PDU& encoder, int pdu_length) {
  Serial.print("PDU data: ");
  Serial.println(encoder.getSMS());
  Serial.print("PDU length: ");
  Serial.println(pdu_length);

  String cmgs_cmd = "AT+CMGS=";
  cmgs_cmd += pdu_length;

  while (serial_.available()) {
    serial_.read();
  }
  serial_.println(cmgs_cmd);

  unsigned long start = millis();
  bool got_prompt = false;
  while (millis() - start < kSmsSendPromptTimeoutMs) {
    if (!serial_.available()) {
      continue;
    }

    const char c = serial_.read();
    Serial.print(c);
    if (c == '>') {
      got_prompt = true;
      break;
    }
  }

  if (!got_prompt) {
    Serial.println("Did not receive the '>' prompt");
    return false;
  }

  serial_.print(encoder.getSMS());
  serial_.write(0x1A);

  start = millis();
  String response;
  while (millis() - start < kSmsSendResultTimeoutMs) {
    while (serial_.available()) {
      const char c = serial_.read();
      response += c;
      Serial.print(c);
      if (response.indexOf("OK") >= 0) {
        Serial.println("\nSMS sent successfully");
        return true;
      }
      if (response.indexOf("ERROR") >= 0) {
        Serial.println("\nSMS send failed");
        return false;
      }
    }
  }

  Serial.println("SMS send timed out");
  return false;
}

bool Modem::WaitCereg() {
  serial_.println("AT+CEREG?");
  const unsigned long start = millis();
  String response;
  while (millis() - start < 2000) {
    while (serial_.available()) {
      response += static_cast<char>(serial_.read());
      if (response.indexOf("+CEREG:") >= 0) {
        if (response.indexOf(",1") >= 0 || response.indexOf(",5") >= 0) {
          return true;
        }
        if (response.indexOf(",0") >= 0 || response.indexOf(",2") >= 0 ||
            response.indexOf(",3") >= 0 || response.indexOf(",4") >= 0) {
          return false;
        }
      }
    }
  }
  return false;
}

void Modem::ModemPowerCycle() {
  pinMode(kModemEnablePin, OUTPUT);

  Serial.println("Pulling EN low to power off the modem");
  digitalWrite(kModemEnablePin, LOW);
  delay(1200);

  Serial.println("Pulling EN high to power on the modem");
  digitalWrite(kModemEnablePin, HIGH);
  delay(6000);
}

// Long-SMS assembly state.
void Modem::InitConcatBuffer() {
  for (int i = 0; i < kMaxConcatMessages; ++i) {
    concat_buffer_[i].inUse = false;
    concat_buffer_[i].totalParts = 0;
    concat_buffer_[i].reportedParts = 0;
    concat_buffer_[i].receivedParts = 0;
    concat_buffer_[i].truncatedByPartLimit = false;
    concat_buffer_[i].droppedPartLogged = false;
    concat_buffer_[i].timestamp = "";
    for (int j = 0; j < kMaxInboundSmsParts; ++j) {
      concat_buffer_[i].parts[j].valid = false;
      concat_buffer_[i].parts[j].text = "";
    }
  }
}

int Modem::FindOrCreateConcatSlot(int ref_number, const char* sender,
                                  int reported_total_parts) {
  const int expected_total_parts = ClampConcatTotalParts(reported_total_parts);
  const bool truncated = reported_total_parts > kMaxInboundSmsParts;

  for (int i = 0; i < kMaxConcatMessages; ++i) {
    if (concat_buffer_[i].inUse && concat_buffer_[i].refNumber == ref_number &&
        concat_buffer_[i].sender.equals(sender)) {
      if (reported_total_parts > concat_buffer_[i].reportedParts) {
        concat_buffer_[i].reportedParts = reported_total_parts;
      }
      if (truncated && !concat_buffer_[i].truncatedByPartLimit) {
        concat_buffer_[i].truncatedByPartLimit = true;
        Serial.printf("Long SMS exceeds max parts; truncating to %d segments (reported=%d)\n",
                      kMaxInboundSmsParts, reported_total_parts);
      }
      return i;
    }
  }

  for (int i = 0; i < kMaxConcatMessages; ++i) {
    if (!concat_buffer_[i].inUse) {
      concat_buffer_[i].inUse = true;
      concat_buffer_[i].refNumber = ref_number;
      concat_buffer_[i].sender = String(sender);
      concat_buffer_[i].totalParts = expected_total_parts;
      concat_buffer_[i].reportedParts = reported_total_parts;
      concat_buffer_[i].receivedParts = 0;
      concat_buffer_[i].truncatedByPartLimit = truncated;
      concat_buffer_[i].droppedPartLogged = false;
      concat_buffer_[i].firstPartTime = millis();
      concat_buffer_[i].timestamp = "";
      if (truncated) {
        Serial.printf("Long SMS exceeds max parts; truncating to %d segments (reported=%d)\n",
                      kMaxInboundSmsParts, reported_total_parts);
      }
      for (int j = 0; j < kMaxInboundSmsParts; ++j) {
        concat_buffer_[i].parts[j].valid = false;
        concat_buffer_[i].parts[j].text = "";
      }
      return i;
    }
  }

  int oldest_slot = 0;
  unsigned long oldest_time = concat_buffer_[0].firstPartTime;
  for (int i = 1; i < kMaxConcatMessages; ++i) {
    if (concat_buffer_[i].firstPartTime < oldest_time) {
      oldest_time = concat_buffer_[i].firstPartTime;
      oldest_slot = i;
    }
  }

  Serial.println("Long SMS cache is full; reusing the oldest slot");
  concat_buffer_[oldest_slot].inUse = true;
  concat_buffer_[oldest_slot].refNumber = ref_number;
  concat_buffer_[oldest_slot].sender = String(sender);
  concat_buffer_[oldest_slot].totalParts = expected_total_parts;
  concat_buffer_[oldest_slot].reportedParts = reported_total_parts;
  concat_buffer_[oldest_slot].receivedParts = 0;
  concat_buffer_[oldest_slot].truncatedByPartLimit = truncated;
  concat_buffer_[oldest_slot].droppedPartLogged = false;
  concat_buffer_[oldest_slot].firstPartTime = millis();
  concat_buffer_[oldest_slot].timestamp = "";
  if (truncated) {
    Serial.printf("Long SMS exceeds max parts; truncating to %d segments (reported=%d)\n",
                  kMaxInboundSmsParts, reported_total_parts);
  }
  for (int j = 0; j < kMaxInboundSmsParts; ++j) {
    concat_buffer_[oldest_slot].parts[j].valid = false;
    concat_buffer_[oldest_slot].parts[j].text = "";
  }
  return oldest_slot;
}

String Modem::AssembleConcatSms(int slot) const {
  String result;
  for (int i = 0; i < concat_buffer_[slot].totalParts; ++i) {
    if (concat_buffer_[slot].parts[i].valid) {
      result += concat_buffer_[slot].parts[i].text;
    } else {
      result += "[缺失分段" + String(i + 1) + "]";
    }
  }
  if (concat_buffer_[slot].truncatedByPartLimit) {
    result += kTruncatedSuffixPrefix + String(concat_buffer_[slot].reportedParts) +
              ", max=" + String(kMaxInboundSmsParts) + "]";
  }
  return result;
}

void Modem::ClearConcatSlot(int slot) {
  concat_buffer_[slot].inUse = false;
  concat_buffer_[slot].totalParts = 0;
  concat_buffer_[slot].reportedParts = 0;
  concat_buffer_[slot].receivedParts = 0;
  concat_buffer_[slot].truncatedByPartLimit = false;
  concat_buffer_[slot].droppedPartLogged = false;
  concat_buffer_[slot].sender = "";
  concat_buffer_[slot].timestamp = "";
  for (int j = 0; j < kMaxInboundSmsParts; ++j) {
    concat_buffer_[slot].parts[j].valid = false;
    concat_buffer_[slot].parts[j].text = "";
  }
}

bool Modem::CheckConcatTimeout(SmsMessage& message) {
  const unsigned long now = millis();
  for (int i = 0; i < kMaxConcatMessages; ++i) {
    if (concat_buffer_[i].inUse &&
        now - concat_buffer_[i].firstPartTime >= kConcatTimeoutMs) {
      // Keep the outer loop on a one-message-at-a-time processing model.
      Serial.println("Long SMS timed out; forwarding the partial message");
      Serial.printf("  Ref: %d, received: %d/%d\n", concat_buffer_[i].refNumber,
                    concat_buffer_[i].receivedParts, concat_buffer_[i].totalParts);
      if (concat_buffer_[i].truncatedByPartLimit) {
        Serial.printf("  Reported parts: %d (max %d)\n", concat_buffer_[i].reportedParts,
                      kMaxInboundSmsParts);
      }

      FillSmsMessage(message, concat_buffer_[i].sender, AssembleConcatSms(i),
                     concat_buffer_[i].timestamp);
      ClearConcatSlot(i);
      return true;
    }
  }
  return false;
}

String Modem::ReadSerialLine() {
  while (serial_.available()) {
    const char c = serial_.read();
    if (c == '\n') {
      line_buffer_[line_pos_] = 0;
      const String result = String(line_buffer_);
      line_pos_ = 0;
      return result;
    }

    if (c != '\r') {
      if (line_pos_ < static_cast<int>(kSerialBufferSize) - 1) {
        line_buffer_[line_pos_++] = c;
      } else {
        line_pos_ = 0;
      }
    }
  }

  return "";
}

bool Modem::IsHexString(const String& value) const {
  if (value.length() == 0) {
    return false;
  }

  for (unsigned int i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
          (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}
