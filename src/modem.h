/**
 * @file modem.h
 * @brief Modem serial owner, AT command flow, and SMS receive pipeline.
 */

#pragma once

#include <Arduino.h>
#include <pdulib.h>

#include "board_config.h"
#include "sms_inbox.h"

/** @brief Result of checking whether one outbound SMS fits the send policy. */
enum class OutboundSmsPolicyStatus : uint8_t {
  CanSend = 0,
  TooManyParts,
  EncodeFailed,
};

/**
 * @brief Checks whether an outbound SMS fits the shared modem send policy.
 * @param phone_number Destination phone number used for PDU sizing.
 * @param message SMS body text to analyze.
 * @return Shared transport-side verdict.
 */
OutboundSmsPolicyStatus AnalyzeOutboundSms(const char* phone_number, const String& message);

/**
 * @brief Owns the modem serial session, AT command flow, and SMS receive state.
 */
class Modem {
 public:
  /**
   * @brief Creates a modem owner around the provided UART instance.
   * @param serial_port UART connected to the cellular modem.
   */
  explicit Modem(HardwareSerial& serial_port);

  /** @brief Brings the modem to a known state and waits for network registration. */
  void Begin();
  /**
   * @brief Polls the modem and emits at most one completed SMS per call.
   * @param message Output SMS when a complete or timed-out message becomes ready.
   * @return True when @p message has been populated.
   */
  bool Poll(SmsMessage& message);
  /**
   * @brief Sends an SMS in PDU mode.
   * @param phone_number Destination phone number.
   * @param message SMS body text.
   * @return True when the modem reports an OK result.
   */
  bool SendSms(const char* phone_number, const char* message);
  /**
   * @brief Sends one AT command and collects the response until OK/ERROR/timeout.
   * @param cmd Full AT command string.
   * @param timeout Maximum wait time in milliseconds.
   * @return Raw modem response captured during the wait window.
   */
  String SendAtCommand(const char* cmd, unsigned long timeout);
  /** @brief Power-cycles the modem and waits for the AT interface to recover. */
  void Reset();
  /** @brief Forwards one byte from the USB serial console to the modem UART. */
  void WritePassthroughByte(uint8_t byte);

 private:
  struct SmsPart {
    bool valid;
    String text;
  };

  struct ConcatSms {
    bool inUse;
    int refNumber;
    String sender;
    String timestamp;
    int totalParts;
    int reportedParts;
    int receivedParts;
    bool truncatedByPartLimit;
    bool droppedPartLogged;
    unsigned long firstPartTime;
    SmsPart parts[kMaxInboundSmsParts];
  };

  enum class UrcState {
    Idle,
    WaitPdu,
  };

  bool SendAtAndWaitOK(const char* cmd, unsigned long timeout);
  bool WaitCereg();
  void ModemPowerCycle();
  bool SendEncodedPdu(PDU& encoder, int pdu_length);

  void InitConcatBuffer();
  int FindOrCreateConcatSlot(int ref_number, const char* sender, int reported_total_parts);
  String AssembleConcatSms(int slot) const;
  void ClearConcatSlot(int slot);
  bool CheckConcatTimeout(SmsMessage& message);

  String ReadSerialLine();
  bool IsHexString(const String& value) const;

  HardwareSerial& serial_;
  PDU pdu_;
  ConcatSms concat_buffer_[kMaxConcatMessages];
  UrcState urc_state_;
  char line_buffer_[kSerialBufferSize];
  int line_pos_;
};
