/**
 * @file wifi_runtime.cpp
 * @brief Wi-Fi STA/AP runtime owner and provisioning portal state machine.
 */

#include "wifi_runtime.h"

#include <esp_system.h>
#include <esp_wifi.h>

namespace {

constexpr unsigned long kConnectTimeoutMs = 15000;
constexpr unsigned long kSavedRetryIntervalMs = 30000;
constexpr unsigned long kPortalHandoffMs = 60000;
constexpr unsigned long kScanIntervalMs = 10000;
constexpr unsigned long kNtpSyncTimeoutMs = 10000;
constexpr uint32_t kScanMaxMsPerChannel = 600;
constexpr uint8_t kFallbackScanStartChannel = 1;
constexpr uint8_t kFallbackScanEndChannel = 13;
constexpr uint8_t kMaxSupportedScanChannel = 14;

bool QueueAppEvent(QueueHandle_t queue, const AppEvent& event,
                   TickType_t timeout_ticks = pdMS_TO_TICKS(100)) {
  if (queue == nullptr) {
    return false;
  }

  return xQueueSend(queue, &event, timeout_ticks) == pdTRUE;
}

}  // namespace

WifiRuntime::WifiRuntime(ConfigStore& config_store, SharedConfigState& shared_state,
                         QueueHandle_t app_event_queue)
    : config_store_(config_store),
      shared_state_(shared_state),
      app_event_queue_(app_event_queue),
      mode_(WifiRuntimeMode::TryingSavedNetworks),
      portal_active_(false),
      connect_in_progress_(false),
      startup_notice_sent_(false),
      ntp_sync_in_progress_(false),
      ntp_sync_completed_(false),
      connect_started_ms_(0),
      next_saved_retry_ms_(0),
      handoff_deadline_ms_(0),
      last_scan_request_ms_(0),
      ntp_sync_started_ms_(0) {}

void WifiRuntime::Begin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);

  ap_ssid_ = BuildApSsid();
  visible_networks_ = VisibleWifiList{};
  status_message_ = "正在查找已保存的 Wi-Fi...";
  StartStartupDiscovery();
}

void WifiRuntime::Poll() {
  const unsigned long now = millis();

  if (connect_in_progress_) {
    HandleConnectAttempt(now);
  } else if (mode_ == WifiRuntimeMode::Connected || mode_ == WifiRuntimeMode::PortalHandoff) {
    HandleConnected(now);
  } else if (mode_ == WifiRuntimeMode::ProvisioningPortal && now >= next_saved_retry_ms_) {
    TryPortalAutoRecovery(now);
  }

  if (portal_active_ && !connect_in_progress_ && !scan_cycle_.active) {
    BeginScan(now);
  }

  if (scan_cycle_.active || visible_networks_.scanInProgress) {
    PollScan(now);
  }

  if (ntp_sync_in_progress_) {
    PollNtpSync(now);
  }
}

bool WifiRuntime::ShouldServeProvisioningPortal() const {
  return portal_active_;
}

bool WifiRuntime::AllowsOpenProvisioningAccess(const IPAddress& local_ip) const {
  return portal_active_ && local_ip == WiFi.softAPIP();
}

String WifiRuntime::GetPrimaryIpString() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }

  if (portal_active_) {
    return WiFi.softAPIP().toString();
  }

  return "0.0.0.0";
}

String WifiRuntime::GetPrimaryUrl() const {
  return "http://" + GetPrimaryIpString() + "/";
}

void WifiRuntime::GetStatusSnapshot(WifiStatusSnapshot& snapshot) const {
  snapshot.mode = mode_;
  snapshot.portalActive = portal_active_;
  snapshot.staConnected = WiFi.status() == WL_CONNECTED;
  snapshot.connectInProgress = connect_in_progress_;
  snapshot.scanInProgress = visible_networks_.scanInProgress;
  snapshot.message = status_message_;
  snapshot.attemptingSsid = active_candidate_.valid ? active_candidate_.ssid : "";
  snapshot.staSsid = snapshot.staConnected ? WiFi.SSID() : "";
  snapshot.staIp = snapshot.staConnected ? WiFi.localIP().toString() : "";
  snapshot.apSsid = ap_ssid_;
  snapshot.apIp = portal_active_ ? WiFi.softAPIP().toString() : "";
  snapshot.redirectIp =
      mode_ == WifiRuntimeMode::PortalHandoff ? WiFi.localIP().toString() : "";
  snapshot.handoffRemainingSec =
      (mode_ == WifiRuntimeMode::PortalHandoff && handoff_deadline_ms_ > millis())
          ? static_cast<uint32_t>((handoff_deadline_ms_ - millis() + 999) / 1000)
          : 0;
}

void WifiRuntime::GetVisibleNetworks(VisibleWifiList& list) const {
  list = visible_networks_;
}

void WifiRuntime::GetSavedCredentials(SavedWifiList& list) const {
  list = SavedWifiList{};

  AppConfig config;
  if (!LoadConfigSnapshot(config)) {
    return;
  }

  const String current_ssid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "";
  list.count = config.wifiCredentialCount;
  for (uint8_t i = 0; i < config.wifiCredentialCount && i < kMaxWifiCredentials; ++i) {
    list.items[i].ssid = config.wifiCredentials[i].ssid;
    list.items[i].current = list.items[i].ssid == current_ssid;
  }
}

void WifiRuntime::ForceProvisioningPortal() {
  if (connect_in_progress_) {
    CancelConnectAttempt(true);
  }

  EnterProvisioningPortal("已通过 BOOT 按键强制打开配网热点。", millis(),
                          kSavedRetryIntervalMs);
  Serial.println("Provisioning portal forced by BOOT button");
}

bool WifiRuntime::SubmitCredential(const String& raw_ssid, const String& password,
                                   String& message) {
  String ssid = raw_ssid;
  ssid.trim();
  if (ssid.length() == 0) {
    message = "SSID 不能为空。";
    return false;
  }

  ResetStartupDiscovery();
  ResetSavedAttemptQueue();
  EnsurePortalStarted();
  StartConnectAttempt(ssid, password);
  message = "已开始连接 " + ssid + "，请稍候。";
  return true;
}

bool WifiRuntime::DeleteCredential(const String& raw_ssid, String& message) {
  String ssid = raw_ssid;
  ssid.trim();
  if (ssid.length() == 0) {
    message = "缺少要删除的网络。";
    return false;
  }

  AppConfig config;
  if (!LoadConfigSnapshot(config)) {
    message = "配置忙，请稍后重试。";
    return false;
  }

  const uint8_t old_count = config.wifiCredentialCount;
  config_store_.RemoveWifiCredential(config, ssid);
  if (config.wifiCredentialCount == old_count) {
    message = "未找到对应的已保存网络。";
    return false;
  }

  const bool removed_active_candidate =
      connect_in_progress_ && active_candidate_.valid && active_candidate_.ssid == ssid;
  const bool removed_connected_ssid =
      WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid;

  SaveConfigSnapshot(config);

  if (removed_active_candidate) {
    CancelConnectAttempt(true);
    EnterProvisioningPortal("已删除该网络，设备正在返回配网模式。", millis(),
                            kSavedRetryIntervalMs);
    message = "已删除网络 " + ssid + "，设备正在返回配网模式。";
    return true;
  }

  if (removed_connected_ssid) {
    Serial.println("Removed the currently connected Wi-Fi: " + ssid);
    WiFi.disconnect(false, false);
    delay(50);
    EnterProvisioningPortal("已删除当前连接网络，设备正在返回配网模式。", millis(),
                            kSavedRetryIntervalMs);
    message = "已删除当前连接的网络 " + ssid + "，设备正在断开并返回配网模式。";
    return true;
  }

  message = "已删除网络 " + ssid + "。";
  return true;
}

bool WifiRuntime::ClearCredentials(String& message) {
  AppConfig config;
  if (!LoadConfigSnapshot(config)) {
    message = "配置忙，请稍后重试。";
    return false;
  }

  config_store_.ClearWifiCredentials(config);
  SaveConfigSnapshot(config);
  CancelConnectAttempt(true);
  EnterProvisioningPortal("已清空所有已保存网络。", millis(), kSavedRetryIntervalMs);
  message = "已清空所有已保存网络。";
  return true;
}

bool WifiRuntime::LoadConfigSnapshot(AppConfig& config) const {
  if (shared_state_.mutex == nullptr) {
    return false;
  }

  if (xSemaphoreTake(shared_state_.mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  config = shared_state_.config;
  xSemaphoreGive(shared_state_.mutex);
  return true;
}

void WifiRuntime::SaveConfigSnapshot(const AppConfig& config) {
  config_store_.Save(config);

  if (shared_state_.mutex != nullptr &&
      xSemaphoreTake(shared_state_.mutex, portMAX_DELAY) == pdTRUE) {
    shared_state_.config = config;
    shared_state_.configValid = IsConfigValid(config);
    xSemaphoreGive(shared_state_.mutex);
  }
}

void WifiRuntime::CancelConnectAttempt(bool disconnect_sta) {
  const bool had_active_attempt = connect_in_progress_ || active_candidate_.valid;

  connect_in_progress_ = false;
  active_candidate_ = PendingCredential{};

  if (!disconnect_sta || !had_active_attempt) {
    return;
  }

  Serial.println("Cancelling the active Wi-Fi connection attempt");
  WiFi.disconnect(false, false);
  delay(50);
}

void WifiRuntime::ResetSavedAttemptQueue() {
  saved_attempt_queue_ = SavedAttemptQueue{};
}

bool WifiRuntime::QueueAttemptCandidate(const WifiCredential& credential) {
  if (credential.ssid.length() == 0) {
    return false;
  }

  for (uint8_t i = 0; i < saved_attempt_queue_.count; ++i) {
    if (saved_attempt_queue_.items[i].ssid == credential.ssid) {
      return false;
    }
  }

  if (saved_attempt_queue_.count >= kMaxWifiCredentials) {
    return false;
  }

  saved_attempt_queue_.items[saved_attempt_queue_.count++] = credential;
  return true;
}

void WifiRuntime::CollectAttemptCandidatesFromNetworks(const VisibleWifiNetwork* networks,
                                                       uint8_t count,
                                                       const AppConfig& config) {
  for (uint8_t credential_index = 0; credential_index < config.wifiCredentialCount;
       ++credential_index) {
    const WifiCredential& credential = config.wifiCredentials[credential_index];
    if (credential.ssid.length() == 0) {
      continue;
    }

    for (uint8_t network_index = 0; network_index < count; ++network_index) {
      if (networks[network_index].ssid == credential.ssid) {
        QueueAttemptCandidate(credential);
        break;
      }
    }
  }
}

bool WifiRuntime::StartNextQueuedAttempt() {
  while (saved_attempt_queue_.nextIndex < saved_attempt_queue_.count) {
    const WifiCredential credential =
        saved_attempt_queue_.items[saved_attempt_queue_.nextIndex++];
    if (credential.ssid.length() == 0) {
      continue;
    }

    StartConnectAttempt(credential.ssid, credential.password);
    return true;
  }

  return false;
}

void WifiRuntime::ResetStartupDiscovery() {
  startup_discovery_ = StartupDiscoveryState{};
}

void WifiRuntime::StartStartupDiscovery() {
  CancelConnectAttempt(true);
  ResetStartupDiscovery();
  ResetSavedAttemptQueue();

  if (scan_cycle_.active || visible_networks_.scanInProgress) {
    WiFi.scanDelete();
  }
  ResetScanCycle();
  visible_networks_.scanInProgress = false;

  StopPortal();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(50);

  AppConfig config;
  if (!LoadConfigSnapshot(config) || config.wifiCredentialCount == 0) {
    EnterProvisioningPortal("未发现已保存的 Wi-Fi，请使用热点进行配网。", millis(),
                            kSavedRetryIntervalMs);
    return;
  }

  mode_ = WifiRuntimeMode::TryingSavedNetworks;
  handoff_deadline_ms_ = 0;
  next_saved_retry_ms_ = 0;
  status_message_ = "正在查找已保存的 Wi-Fi...";

  startup_discovery_.active = true;
  if (!StartScanCycle(ScanCyclePurpose::StartupDiscovery, millis())) {
    FinalizeStartupDiscovery(millis(), false);
  }
}

void WifiRuntime::FinalizeStartupDiscovery(unsigned long now, bool round_succeeded) {
  if (round_succeeded) {
    AppConfig config;
    if (LoadConfigSnapshot(config)) {
      CollectAttemptCandidatesFromNetworks(scan_cycle_.items, scan_cycle_.count, config);
    }
  }

  ResetScanCycle();
  startup_discovery_.active = false;

  if (StartNextQueuedAttempt()) {
    return;
  }

  EnterProvisioningPortal("未发现可用的已保存 Wi-Fi，请使用热点进行配网。", now,
                          kSavedRetryIntervalMs);
}

void WifiRuntime::TryPortalAutoRecovery(unsigned long now) {
  if (!portal_active_ || connect_in_progress_ || startup_discovery_.active) {
    return;
  }

  AppConfig config;
  if (!LoadConfigSnapshot(config)) {
    status_message_ = "读取 Wi-Fi 配置失败。";
    next_saved_retry_ms_ = now + kSavedRetryIntervalMs;
    return;
  }

  ResetSavedAttemptQueue();
  CollectAttemptCandidatesFromNetworks(visible_networks_.items, visible_networks_.count, config);
  next_saved_retry_ms_ = now + kSavedRetryIntervalMs;

  if (StartNextQueuedAttempt()) {
    return;
  }

  status_message_ = "正在等待已保存的 Wi-Fi 出现在附近...";
}

void WifiRuntime::EnterProvisioningPortal(const String& message, unsigned long now,
                                          unsigned long retry_delay_ms) {
  ResetStartupDiscovery();
  ResetSavedAttemptQueue();

  if (scan_cycle_.active || visible_networks_.scanInProgress) {
    WiFi.scanDelete();
  }
  ResetScanCycle();
  visible_networks_.scanInProgress = false;

  EnsurePortalStarted();
  mode_ = WifiRuntimeMode::ProvisioningPortal;
  handoff_deadline_ms_ = 0;
  next_saved_retry_ms_ = now + retry_delay_ms;
  status_message_ = message;
}

void WifiRuntime::StartConnectAttempt(const String& ssid, const String& password) {
  if (ssid.length() == 0) {
    return;
  }

  active_candidate_.valid = true;
  active_candidate_.ssid = ssid;
  active_candidate_.password = password;
  connect_in_progress_ = true;
  connect_started_ms_ = millis();
  mode_ = WifiRuntimeMode::TryingSavedNetworks;
  status_message_ = "正在连接 " + ssid + "...";

  WiFi.mode(portal_active_ ? WIFI_AP_STA : WIFI_STA);
  WiFi.disconnect(false, false);
  delay(50);

  if (password.length() == 0) {
    WiFi.begin(ssid.c_str(), nullptr, 0, nullptr, true);
  } else {
    WiFi.begin(ssid.c_str(), password.c_str(), 0, nullptr, true);
  }

  Serial.println("Starting Wi-Fi connection attempt: " + ssid);
}

void WifiRuntime::HandleConnectAttempt(unsigned long now) {
  if (!active_candidate_.valid) {
    connect_in_progress_ = false;
    return;
  }

  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    AppConfig config;
    if (LoadConfigSnapshot(config)) {
      config_store_.UpsertWifiCredential(config, active_candidate_.ssid,
                                         active_candidate_.password);
      SaveConfigSnapshot(config);
    }

    connect_in_progress_ = false;
    ResetStartupDiscovery();
    ResetSavedAttemptQueue();
    next_saved_retry_ms_ = 0;
    status_message_ = "已连接到 " + active_candidate_.ssid + "。";
    Serial.println("Wi-Fi connected: " + active_candidate_.ssid);
    active_candidate_ = PendingCredential{};
    StartNtpSync();
    QueueStartupNoticeOnce();

    if (portal_active_) {
      StartHandoff("设备已连接新网络，请将手机切回局域网 Wi-Fi。");
    } else {
      mode_ = WifiRuntimeMode::Connected;
    }
    return;
  }

  if (now - connect_started_ms_ < kConnectTimeoutMs) {
    return;
  }

  Serial.println("Wi-Fi connection timed out: " + active_candidate_.ssid);
  const String failed_ssid = active_candidate_.ssid;
  active_candidate_ = PendingCredential{};
  connect_in_progress_ = false;

  if (StartNextQueuedAttempt()) {
    status_message_ = "连接 " + failed_ssid + " 失败，正在尝试下一个可用网络。";
    return;
  }

  EnterProvisioningPortal("连接失败，请使用热点重新配网。", now,
                          kSavedRetryIntervalMs);
}

void WifiRuntime::HandleConnected(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected; reopening provisioning portal");
    EnterProvisioningPortal("Wi-Fi 已断开，热点已重新打开。", now,
                            kSavedRetryIntervalMs);
    return;
  }

  if (mode_ != WifiRuntimeMode::PortalHandoff) {
    mode_ = WifiRuntimeMode::Connected;
    return;
  }

  if (now < handoff_deadline_ms_) {
    return;
  }

  StopPortal();
  mode_ = WifiRuntimeMode::Connected;
  status_message_ = "Wi-Fi 已连接。";
}

void WifiRuntime::EnsurePortalStarted() {
  if (portal_active_) {
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid_.c_str());
  portal_active_ = true;
  status_message_ = "配网热点已开启，请连接设备热点进行配置。";
  last_scan_request_ms_ = 0;
  visible_networks_ = VisibleWifiList{};
  Serial.println("Provisioning AP started: " + ap_ssid_);
}

void WifiRuntime::StopPortal() {
  if (!portal_active_) {
    return;
  }

  WiFi.softAPdisconnect(true);
  portal_active_ = false;
  ResetScanCycle();
  visible_networks_.scanInProgress = false;
  Serial.println("Provisioning AP stopped");

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
  }
}

void WifiRuntime::StartHandoff(const String& message) {
  mode_ = WifiRuntimeMode::PortalHandoff;
  handoff_deadline_ms_ = millis() + kPortalHandoffMs;
  next_saved_retry_ms_ = 0;
  status_message_ = message;
}

bool WifiRuntime::StartScanCycle(ScanCyclePurpose purpose, unsigned long now) {
  if (scan_cycle_.active) {
    return false;
  }

  uint8_t start_channel = kFallbackScanStartChannel;
  uint8_t end_channel = kFallbackScanEndChannel;
  ResolveScanChannelRange(start_channel, end_channel);

  ResetScanCycle();
  scan_cycle_.active = true;
  scan_cycle_.channelStartPending = true;
  scan_cycle_.purpose = purpose;
  scan_cycle_.currentChannel = start_channel;
  scan_cycle_.finalChannel = end_channel;

  if (purpose == ScanCyclePurpose::VisibleList) {
    visible_networks_.scanInProgress = true;
    last_scan_request_ms_ = now;
  }

  return true;
}

bool WifiRuntime::ResolveScanChannelRange(uint8_t& start_channel,
                                          uint8_t& end_channel) const {
  wifi_country_t country{};
  if (esp_wifi_get_country(&country) == ESP_OK && country.nchan > 0 &&
      country.schan > 0) {
    start_channel = country.schan;

    const uint16_t computed_end =
        static_cast<uint16_t>(country.schan) + country.nchan - 1;
    end_channel = computed_end > kMaxSupportedScanChannel
                      ? kMaxSupportedScanChannel
                      : static_cast<uint8_t>(computed_end);

    if (end_channel >= start_channel) {
      return true;
    }
  }

  start_channel = kFallbackScanStartChannel;
  end_channel = kFallbackScanEndChannel;
  return false;
}

void WifiRuntime::ResetScanCycle() {
  scan_cycle_ = ScanCycleState{};
}

bool WifiRuntime::StartChannelScan(uint8_t channel) {
  const int scan_result =
      WiFi.scanNetworks(true, false, false, kScanMaxMsPerChannel, channel);
  if (scan_result == WIFI_SCAN_FAILED) {
    return false;
  }

  scan_cycle_.channelInProgress = true;
  scan_cycle_.channelStartPending = false;
  return true;
}

void WifiRuntime::FinalizeScanCycle(unsigned long now, bool publish_results) {
  const ScanCyclePurpose purpose = scan_cycle_.purpose;

  if (publish_results && purpose == ScanCyclePurpose::VisibleList) {
    VisibleWifiList published_results;
    published_results.count = scan_cycle_.count;
    published_results.lastUpdatedMs = now;

    for (uint8_t i = 0; i < scan_cycle_.count; ++i) {
      published_results.items[i] = scan_cycle_.items[i];
    }

    visible_networks_ = published_results;
  }

  if (purpose == ScanCyclePurpose::VisibleList) {
    visible_networks_.scanInProgress = false;
  }

  ResetScanCycle();
}

void WifiRuntime::BeginScan(unsigned long now) {
  if (!portal_active_ || visible_networks_.scanInProgress || scan_cycle_.active) {
    return;
  }

  if (now - last_scan_request_ms_ < kScanIntervalMs && visible_networks_.lastUpdatedMs != 0) {
    return;
  }

  StartScanCycle(ScanCyclePurpose::VisibleList, now);
}

void WifiRuntime::PollScan(unsigned long now) {
  if (!scan_cycle_.active) {
    if (visible_networks_.scanInProgress) {
      visible_networks_.scanInProgress = false;
    }
    return;
  }

  // Start each channel on a fresh WebTask iteration so the HTTP handler gets a
  // chance to run between channels instead of chaining one long radio burst.
  if (scan_cycle_.channelStartPending && !scan_cycle_.channelInProgress) {
    if (!StartChannelScan(scan_cycle_.currentChannel)) {
      if (scan_cycle_.purpose == ScanCyclePurpose::StartupDiscovery) {
        FinalizeStartupDiscovery(now, false);
      } else {
        FinalizeScanCycle(now, false);
      }
    }
    return;
  }

  const int scan_state = WiFi.scanComplete();
  if (scan_state == WIFI_SCAN_RUNNING || scan_state == -1) {
    return;
  }

  scan_cycle_.channelInProgress = false;
  const ScanCyclePurpose purpose = scan_cycle_.purpose;

  if (scan_state == WIFI_SCAN_FAILED) {
    WiFi.scanDelete();
    if (purpose == ScanCyclePurpose::StartupDiscovery) {
      FinalizeStartupDiscovery(now, false);
    } else {
      FinalizeScanCycle(now, false);
    }
    return;
  }

  AppConfig config;
  LoadConfigSnapshot(config);
  const String current_ssid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "";

  for (int i = 0; i < scan_state; ++i) {
    StoreScanResult(scan_cycle_, WiFi.SSID(i), WiFi.RSSI(i),
                    WiFi.encryptionType(i) != WIFI_AUTH_OPEN, config, current_ssid);
  }

  WiFi.scanDelete();

  if (scan_cycle_.currentChannel < scan_cycle_.finalChannel) {
    ++scan_cycle_.currentChannel;
    scan_cycle_.channelStartPending = true;
    return;
  }

  if (purpose == ScanCyclePurpose::StartupDiscovery) {
    FinalizeStartupDiscovery(now, true);
  } else {
    FinalizeScanCycle(now, true);
  }
}

void WifiRuntime::StoreScanResult(ScanCycleState& cycle, const String& ssid,
                                  int32_t rssi, bool secured, const AppConfig& config,
                                  const String& current_ssid) {
  if (ssid.length() == 0) {
    return;
  }

  for (uint8_t i = 0; i < cycle.count; ++i) {
    if (cycle.items[i].ssid == ssid) {
      if (rssi > cycle.items[i].rssi) {
        cycle.items[i].rssi = rssi;
        cycle.items[i].secured = secured;
      }
      cycle.items[i].current = ssid == current_ssid;
      return;
    }
  }

  VisibleWifiNetwork network;
  network.ssid = ssid;
  network.rssi = rssi;
  network.secured = secured;
  network.current = ssid == current_ssid;

  for (uint8_t i = 0; i < config.wifiCredentialCount; ++i) {
    if (config.wifiCredentials[i].ssid == ssid) {
      network.saved = true;
      break;
    }
  }

  if (cycle.count < kMaxVisibleWifiNetworks) {
    cycle.items[cycle.count++] = network;
  } else {
    uint8_t weakest_index = 0;
    for (uint8_t i = 1; i < cycle.count; ++i) {
      if (cycle.items[i].rssi < cycle.items[weakest_index].rssi) {
        weakest_index = i;
      }
    }

    if (rssi > cycle.items[weakest_index].rssi) {
      cycle.items[weakest_index] = network;
    } else {
      return;
    }
  }

  for (uint8_t i = 1; i < cycle.count; ++i) {
    VisibleWifiNetwork current = cycle.items[i];
    int j = i - 1;
    while (j >= 0 && cycle.items[j].rssi < current.rssi) {
      cycle.items[j + 1] = cycle.items[j];
      --j;
    }
    cycle.items[j + 1] = current;
  }
}

void WifiRuntime::StartNtpSync() {
  configTime(0, 0, "ntp.ntsc.ac.cn", "ntp.aliyun.com", "pool.ntp.org");
  ntp_sync_in_progress_ = true;
  ntp_sync_completed_ = false;
  ntp_sync_started_ms_ = millis();
  Serial.println("Starting NTP sync...");
}

void WifiRuntime::PollNtpSync(unsigned long now) {
  if (time(nullptr) >= 100000) {
    ntp_sync_in_progress_ = false;
    ntp_sync_completed_ = true;
    Serial.println("NTP sync succeeded");
    Serial.print("Current UTC timestamp: ");
    Serial.println(time(nullptr));
    return;
  }

  if (now - ntp_sync_started_ms_ < kNtpSyncTimeoutMs) {
    return;
  }

  ntp_sync_in_progress_ = false;
  ntp_sync_completed_ = false;
  Serial.println("NTP sync failed; falling back to device time");
}

void WifiRuntime::QueueStartupNoticeOnce() {
  if (startup_notice_sent_) {
    return;
  }

  AppConfig config;
  if (!LoadConfigSnapshot(config) || !IsConfigValid(config)) {
    return;
  }

  AppEvent event;
  event.type = AppEventType::StartupNotice;
  if (QueueAppEvent(app_event_queue_, event, pdMS_TO_TICKS(1000))) {
    startup_notice_sent_ = true;
  }
}

String WifiRuntime::BuildApSsid() const {
  const uint64_t chip_id = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06llX",
           static_cast<unsigned long long>(chip_id & 0xFFFFFFULL));
  return "SMS-Forwarder-" + String(suffix);
}
