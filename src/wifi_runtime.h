/**
 * @file wifi_runtime.h
 * @brief Wi-Fi STA/AP runtime owner and provisioning portal state.
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "config_store.h"
#include "task_protocol.h"

constexpr int kMaxVisibleWifiNetworks = 16;  ///< Maximum scan results exposed to the UI.

/** @brief High-level Wi-Fi runtime mode shown to the provisioning UI. */
enum class WifiRuntimeMode : uint8_t {
  TryingSavedNetworks,
  Connected,
  ProvisioningPortal,
  PortalHandoff,
};

/** @brief One visible Wi-Fi network shown in the provisioning UI. */
struct VisibleWifiNetwork {
  String ssid;
  int32_t rssi = -127;
  bool secured = true;
  bool saved = false;
  bool current = false;
};

/** @brief Snapshot of the latest Wi-Fi scan results. */
struct VisibleWifiList {
  VisibleWifiNetwork items[kMaxVisibleWifiNetworks];
  uint8_t count = 0;
  bool scanInProgress = false;
  unsigned long lastUpdatedMs = 0;
};

/** @brief Lightweight list of saved credentials for the provisioning UI. */
struct SavedWifiEntry {
  String ssid;
  bool current = false;
};

/** @brief Saved credentials exposed to the provisioning UI. */
struct SavedWifiList {
  SavedWifiEntry items[kMaxWifiCredentials];
  uint8_t count = 0;
};

/** @brief Runtime snapshot rendered by the provisioning UI. */
struct WifiStatusSnapshot {
  WifiRuntimeMode mode = WifiRuntimeMode::TryingSavedNetworks;
  bool portalActive = false;
  bool staConnected = false;
  bool connectInProgress = false;
  bool scanInProgress = false;
  String message;
  String attemptingSsid;
  String staSsid;
  String staIp;
  String apSsid;
  String apIp;
  String redirectIp;
  uint32_t handoffRemainingSec = 0;
};

/**
 * @brief Owns Wi-Fi credentials, STA/AP mode switching, scans, and provisioning.
 */
class WifiRuntime {
 public:
  /**
   * @brief Creates the Wi-Fi runtime owner.
   * @param config_store Configuration persistence owner.
   * @param shared_state Shared configuration snapshot guarded by a mutex.
   * @param app_event_queue Queue used to emit startup notifications after connect.
   */
  WifiRuntime(ConfigStore& config_store, SharedConfigState& shared_state,
              QueueHandle_t app_event_queue);

  /** @brief Initializes Wi-Fi runtime state and starts the first connection cycle. */
  void Begin();
  /** @brief Advances connection attempts, portal lifecycle, scans, and NTP sync. */
  void Poll();

  /** @brief Returns true while the provisioning portal should be the primary UI. */
  bool ShouldServeProvisioningPortal() const;
  /**
   * @brief Returns true when a provisioning request may bypass admin auth.
   * @param local_ip Local interface address that accepted the HTTP request.
   *
   * Provisioning stays open on the AP interface so a phone can onboard the
   * device without credentials, but the same routes still require auth when
   * reached through the STA-side LAN address.
   */
  bool AllowsOpenProvisioningAccess(const IPAddress& local_ip) const;
  /** @brief Returns the most relevant IP address for the current mode. */
  String GetPrimaryIpString() const;
  /** @brief Returns a best-effort device URL for logs or UI links. */
  String GetPrimaryUrl() const;

  /** @brief Copies the current runtime status into a UI-friendly snapshot. */
  void GetStatusSnapshot(WifiStatusSnapshot& snapshot) const;
  /** @brief Copies the latest scan cache into a UI-friendly snapshot. */
  void GetVisibleNetworks(VisibleWifiList& list) const;
  /** @brief Copies the saved credential pool into a UI-friendly snapshot. */
  void GetSavedCredentials(SavedWifiList& list) const;
  /** @brief Forces the provisioning portal open without clearing saved credentials. */
  void ForceProvisioningPortal();

  /**
   * @brief Starts a connection attempt for one user-provided credential.
   * @param ssid Target SSID.
   * @param password Password for the SSID. Empty means an open network.
   * @param message Result message for immediate UI feedback.
   * @return True when the request was accepted.
   */
  bool SubmitCredential(const String& ssid, const String& password, String& message);

  /**
   * @brief Removes one saved Wi-Fi credential by SSID.
   * @param ssid Target SSID.
   * @param message Result message for immediate UI feedback.
   * @return True when a credential was removed.
   */
  bool DeleteCredential(const String& ssid, String& message);

  /**
   * @brief Clears every saved Wi-Fi credential.
   * @param message Result message for immediate UI feedback.
   * @return True when the pool was cleared.
   */
  bool ClearCredentials(String& message);

 private:
  struct PendingCredential {
    bool valid = false;
    String ssid;
    String password;
  };

  bool LoadConfigSnapshot(AppConfig& config) const;
  void SaveConfigSnapshot(const AppConfig& config);
  void CancelConnectAttempt(bool disconnect_sta);

  void StartSavedCredentialCycle(bool keep_portal_active);
  bool StartNextSavedCredentialAttempt();
  void StartConnectAttempt(const String& ssid, const String& password);
  void HandleConnectAttempt(unsigned long now);
  void HandleConnected(unsigned long now);

  void EnsurePortalStarted();
  void StopPortal();
  void StartHandoff(const String& message);

  void BeginScan(unsigned long now);
  void PollScan(unsigned long now);
  void StoreScanResult(const String& ssid, int32_t rssi, bool secured,
                       const AppConfig& config, const String& current_ssid);

  void StartNtpSync();
  void PollNtpSync(unsigned long now);
  void QueueStartupNoticeOnce();

  String BuildApSsid() const;

  ConfigStore& config_store_;
  SharedConfigState& shared_state_;
  QueueHandle_t app_event_queue_;

  WifiRuntimeMode mode_;
  bool portal_active_;
  bool connect_in_progress_;
  bool startup_notice_sent_;
  bool ntp_sync_in_progress_;
  bool ntp_sync_completed_;
  int next_saved_index_;
  unsigned long connect_started_ms_;
  unsigned long next_saved_retry_ms_;
  unsigned long handoff_deadline_ms_;
  unsigned long last_scan_request_ms_;
  unsigned long ntp_sync_started_ms_;
  PendingCredential active_candidate_;
  VisibleWifiList visible_networks_;
  String ap_ssid_;
  String status_message_;
};
