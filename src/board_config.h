/**
 * @file board_config.h
 * @brief Board wiring plus device-wide modem and SMS transport limits.
 */

#pragma once

#include <Arduino.h>

constexpr int kModemTxPin = 6;       ///< UART TX pin connected to the modem RX.
constexpr int kModemRxPin = 7;       ///< UART RX pin connected to the modem TX.
constexpr int kModemEnablePin = 5;   ///< Modem EN pin used for power cycling.
constexpr int kBootButtonPin = 9;    ///< BOOT button used for manual recovery actions.
constexpr int kStatusLedPin = 4;     ///< On-board status LED pin.
constexpr uint8_t kStatusLedOnLevel = HIGH;  ///< Electrical level that turns the LED on.

constexpr size_t kSerialBufferSize = 500;  ///< Shared modem UART line buffer size.
constexpr int kMaxPduLength = 300;         ///< Reserved maximum SMS payload length.

// SMS transport limits. Inbound messages are truncated after the configured
// number of concatenated segments. Outbound messages must fit within the send
// limit or the modem owner rejects them.
constexpr size_t kSmsConcatPartUtf8MaxBytes =
    67 * 3;  ///< Worst-case UTF-8 bytes for one concatenated UCS2 segment.
constexpr int kMaxInboundSmsParts = 15;    ///< Maximum segments accepted for one long SMS.
constexpr int kMaxOutboundSmsParts = 5;    ///< Maximum segments allowed for one outbound SMS.
constexpr size_t kMaxInboundSmsUtf8Bytes =
    static_cast<size_t>(kMaxInboundSmsParts) * kSmsConcatPartUtf8MaxBytes;
constexpr size_t kMaxOutboundSmsUtf8Bytes =
    static_cast<size_t>(kMaxOutboundSmsParts) * kSmsConcatPartUtf8MaxBytes;

constexpr unsigned long kConcatTimeoutMs = 30000;  ///< Long-SMS wait timeout in milliseconds.
constexpr int kMaxConcatMessages = 5;              ///< Number of long-SMS groups cached at once.
