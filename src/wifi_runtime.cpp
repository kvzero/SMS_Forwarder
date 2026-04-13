/**
 * @file wifi_runtime.cpp
 * @brief Wi-Fi STA/AP runtime owner and provisioning portal state machine.
 */

#include "wifi_runtime.h"

#include <esp_system.h>

namespace {

constexpr unsigned long kConnectTimeoutMs = 15000;
constexpr unsigned long kSavedRetryIntervalMs = 30000;
constexpr unsigned long kPortalHandoffMs = 60000;
constexpr unsigned long kScanIntervalMs = 10000;
constexpr unsigned long kNtpSyncTimeoutMs = 10000;
constexpr uint32_t kScanMaxMsPerChannel = 600;

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
      next_saved_index_(0),
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
  status_message_ = "正在尝试连接已保存的 Wi-Fi...";
  StartSavedCredentialCycle(false);
}

void WifiRuntime::Poll() {
  const unsigned long now = millis();

  if (connect_in_progress_) {
    HandleConnectAttempt(now);
  } else if (mode_ == WifiRuntimeMode::Connected || mode_ == WifiRuntimeMode::PortalHandoff) {
    HandleConnected(now);
  } else if (mode_ == WifiRuntimeMode::ProvisioningPortal && now >= next_saved_retry_ms_) {
    StartSavedCredentialCycle(true);
  }

  if (portal_active_) {
    if (!connect_in_progress_) {
      BeginScan(now);
    }
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
  snapshot.redirectIp = mode_ == WifiRuntimeMode::PortalHandoff
                            ? WiFi.localIP().toString()
                            : "";
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

  EnsurePortalStarted();
  mode_ = WifiRuntimeMode::ProvisioningPortal;
  handoff_deadline_ms_ = 0;
  next_saved_retry_ms_ = millis() + kSavedRetryIntervalMs;
  status_message_ = "Provisioning portal forced by BOOT button.";
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

  EnsurePortalStarted();
  StartConnectAttempt(ssid, password);
  message = "已开始连接 " + ssid + "，请稍候...";
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
    StartSavedCredentialCycle(true);
    message = "已删除网络 " + ssid + "，设备正在返回配网模式。";
    return true;
  }

  if (removed_connected_ssid) {
    Serial.println("Removed the currently connected Wi-Fi: " + ssid);
    WiFi.disconnect(false, false);
    delay(50);
    StartSavedCredentialCycle(true);
    message = "已删除当前联网 " + ssid + "，设备正在断开并返回配网模式。";
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
  message = "已清空所有已保存网络。";
  StartSavedCredentialCycle(true);
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

void WifiRuntime::StartSavedCredentialCycle(bool keep_portal_active) {
  mode_ = WifiRuntimeMode::TryingSavedNetworks;
  connect_in_progress_ = false;
  active_candidate_ = PendingCredential{};
  next_saved_index_ = 0;
  status_message_ = "正在尝试连接已保存的 Wi-Fi...";

  if (keep_portal_active) {
    EnsurePortalStarted();
  } else {
    StopPortal();
    WiFi.mode(WIFI_STA);
  }

  if (!StartNextSavedCredentialAttempt()) {
    EnsurePortalStarted();
    mode_ = WifiRuntimeMode::ProvisioningPortal;
    status_message_ = "未连接到任何 Wi-Fi，请使用热点进行配网。";
    next_saved_retry_ms_ = millis() + kSavedRetryIntervalMs;
  }
}

bool WifiRuntime::StartNextSavedCredentialAttempt() {
  AppConfig config;
  if (!LoadConfigSnapshot(config)) {
    status_message_ = "读取 Wi-Fi 配置失败。";
    return false;
  }

  while (next_saved_index_ < config.wifiCredentialCount) {
    const WifiCredential credential = config.wifiCredentials[next_saved_index_++];
    if (credential.ssid.length() == 0) {
      continue;
    }

    StartConnectAttempt(credential.ssid, credential.password);
    return true;
  }

  return false;
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

  if (StartNextSavedCredentialAttempt()) {
    status_message_ = "连接 " + failed_ssid + " 失败，正在尝试下一个网络...";
    return;
  }

  EnsurePortalStarted();
  mode_ = WifiRuntimeMode::ProvisioningPortal;
  status_message_ = "连接失败，请使用热点重新配网。";
  next_saved_retry_ms_ = now + kSavedRetryIntervalMs;
}

void WifiRuntime::HandleConnected(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected; reopening provisioning portal");
    StartSavedCredentialCycle(true);
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
  status_message_ = "配网热点已开启，请连接设备热点进行配网。";
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
  visible_networks_.scanInProgress = false;
  Serial.println("Provisioning AP stopped");

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
  }
}

void WifiRuntime::StartHandoff(const String& message) {
  mode_ = WifiRuntimeMode::PortalHandoff;
  handoff_deadline_ms_ = millis() + kPortalHandoffMs;
  status_message_ = message;
}

void WifiRuntime::BeginScan(unsigned long now) {
  if (!portal_active_ || visible_networks_.scanInProgress) {
    return;
  }

  if (now - last_scan_request_ms_ < kScanIntervalMs && visible_networks_.lastUpdatedMs != 0) {
    return;
  }

  const int scan_result = WiFi.scanNetworks(true, true, false, kScanMaxMsPerChannel);

  if (scan_result != WIFI_SCAN_FAILED) {
    visible_networks_.scanInProgress = true;
    last_scan_request_ms_ = now;
  }
}

void WifiRuntime::PollScan(unsigned long now) {
  if (!visible_networks_.scanInProgress) {
    return;
  }

  const int scan_state = WiFi.scanComplete();
  if (scan_state == WIFI_SCAN_RUNNING || scan_state == -1) {
    return;
  }

  visible_networks_ = VisibleWifiList{};
  visible_networks_.lastUpdatedMs = now;

  AppConfig config;
  LoadConfigSnapshot(config);
  const String current_ssid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "";

  if (scan_state > 0) {
    for (int i = 0; i < scan_state; ++i) {
      StoreScanResult(WiFi.SSID(i), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN,
                      config, current_ssid);
    }
  }

  WiFi.scanDelete();
}

void WifiRuntime::StoreScanResult(const String& ssid, int32_t rssi, bool secured,
                                  const AppConfig& config, const String& current_ssid) {
  if (ssid.length() == 0) {
    return;
  }

  for (uint8_t i = 0; i < visible_networks_.count; ++i) {
    if (visible_networks_.items[i].ssid == ssid) {
      if (rssi > visible_networks_.items[i].rssi) {
        visible_networks_.items[i].rssi = rssi;
        visible_networks_.items[i].secured = secured;
      }
      visible_networks_.items[i].current = ssid == current_ssid;
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

  if (visible_networks_.count < kMaxVisibleWifiNetworks) {
    visible_networks_.items[visible_networks_.count++] = network;
  } else {
    uint8_t weakest_index = 0;
    for (uint8_t i = 1; i < visible_networks_.count; ++i) {
      if (visible_networks_.items[i].rssi < visible_networks_.items[weakest_index].rssi) {
        weakest_index = i;
      }
    }

    if (rssi > visible_networks_.items[weakest_index].rssi) {
      visible_networks_.items[weakest_index] = network;
    } else {
      return;
    }
  }

  for (uint8_t i = 1; i < visible_networks_.count; ++i) {
    VisibleWifiNetwork current = visible_networks_.items[i];
    int j = i - 1;
    while (j >= 0 && visible_networks_.items[j].rssi < current.rssi) {
      visible_networks_.items[j + 1] = visible_networks_.items[j];
      --j;
    }
    visible_networks_.items[j + 1] = current;
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
