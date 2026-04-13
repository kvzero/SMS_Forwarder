/**
 * @file status_led.cpp
 * @brief Non-blocking system status LED patterns.
 */

#include "status_led.h"

namespace {

constexpr unsigned long kConnectingBlinkMs = 300;
constexpr unsigned long kProvisioningOnMs = 100;
constexpr unsigned long kProvisioningGapMs = 120;
constexpr unsigned long kProvisioningPauseMs = 1200;
constexpr unsigned long kFaultBlinkMs = 120;

}  // namespace

void StatusLed::Begin() {
  pinMode(kStatusLedPin, OUTPUT);
  pin_ready_ = true;
  ApplyMode(mode_);
}

void StatusLed::SetMode(StatusLedMode mode) {
  if (mode_ == mode && pin_ready_) {
    return;
  }

  mode_ = mode;
  ApplyMode(mode_);
}

void StatusLed::Poll(unsigned long now) {
  if (!pin_ready_) {
    return;
  }

  switch (mode_) {
    case StatusLedMode::Off:
    case StatusLedMode::Booting:
      return;

    case StatusLedMode::Connecting:
      if (now - last_transition_ms_ >= kConnectingBlinkMs) {
        phase_ ^= 1;
        Write(phase_ == 0);
        last_transition_ms_ = now;
      }
      return;

    case StatusLedMode::Provisioning: {
      unsigned long phase_duration = kProvisioningPauseMs;
      switch (phase_) {
        case 0:
          phase_duration = kProvisioningOnMs;
          break;
        case 1:
          phase_duration = kProvisioningGapMs;
          break;
        case 2:
          phase_duration = kProvisioningOnMs;
          break;
        default:
          phase_duration = kProvisioningPauseMs;
          break;
      }

      if (now - last_transition_ms_ < phase_duration) {
        return;
      }

      phase_ = (phase_ + 1) % 4;
      last_transition_ms_ = now;

      switch (phase_) {
        case 0:
        case 2:
          Write(true);
          break;
        default:
          Write(false);
          break;
      }
      return;
    }
  }
}

[[noreturn]] void StatusLed::RunBlockingFaultPattern() {
  if (!pin_ready_) {
    Begin();
  }

  while (true) {
    Write(true);
    delay(kFaultBlinkMs);
    Write(false);
    delay(kFaultBlinkMs);
  }
}

void StatusLed::ApplyMode(StatusLedMode mode) {
  if (!pin_ready_) {
    return;
  }

  phase_ = 0;
  last_transition_ms_ = millis();

  switch (mode) {
    case StatusLedMode::Off:
      Write(false);
      return;

    case StatusLedMode::Booting:
      Write(true);
      return;

    case StatusLedMode::Connecting:
      Write(true);
      return;

    case StatusLedMode::Provisioning:
      Write(true);
      return;
  }
}

void StatusLed::Write(bool on) {
  digitalWrite(kStatusLedPin, on ? kStatusLedOnLevel : OffLevel());
}

uint8_t StatusLed::OffLevel() const {
  return kStatusLedOnLevel == HIGH ? LOW : HIGH;
}
