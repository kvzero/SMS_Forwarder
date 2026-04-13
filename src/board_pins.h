/**
 * @file board_pins.h
 * @brief Board-specific pin and buffer settings for the ESP32-C3 target.
 */

#pragma once

#include <Arduino.h>

constexpr int kModemTxPin = 6;       ///< UART TX pin connected to the modem RX.
constexpr int kModemRxPin = 7;       ///< UART RX pin connected to the modem TX.
constexpr int kModemEnablePin = 5;   ///< Modem EN pin used for power cycling.
constexpr int kBootButtonPin = 9;    ///< BOOT button used for manual recovery actions.

#ifndef LED_BUILTIN
/// Fallback LED pin for targets that do not define LED_BUILTIN.
#define LED_BUILTIN 4
#endif

constexpr size_t kSerialBufferSize = 500;         ///< Shared modem UART line buffer size.
constexpr int kMaxPduLength = 300;                ///< Reserved maximum SMS payload length.
constexpr int kMaxConcatParts = 10;               ///< Maximum segments accepted for one long SMS.
constexpr unsigned long kConcatTimeoutMs = 30000; ///< Long-SMS wait timeout in milliseconds.
constexpr int kMaxConcatMessages = 5;             ///< Number of long-SMS groups cached at once.
