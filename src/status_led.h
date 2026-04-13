/**
 * @file status_led.h
 * @brief Non-blocking system status LED patterns.
 */

#pragma once

#include <Arduino.h>

#include "board_pins.h"

/** @brief High-level LED states exposed by the runtime. */
enum class StatusLedMode : uint8_t {
  Off,
  Booting,
  Connecting,
  Provisioning,
};

/**
 * @brief Owns the board status LED and renders non-blocking blink patterns.
 */
class StatusLed {
 public:
  /** @brief Configures the LED pin and applies the current mode. */
  void Begin();
  /** @brief Changes the active LED mode. */
  void SetMode(StatusLedMode mode);
  /** @brief Advances the active blink pattern without blocking. */
  void Poll(unsigned long now = millis());
  /** @brief Runs a blocking fault blink when startup cannot continue. */
  [[noreturn]] void RunBlockingFaultPattern();

 private:
  void ApplyMode(StatusLedMode mode);
  void Write(bool on);
  uint8_t OffLevel() const;

  StatusLedMode mode_ = StatusLedMode::Off;
  unsigned long last_transition_ms_ = 0;
  uint8_t phase_ = 0;
  bool pin_ready_ = false;
};
