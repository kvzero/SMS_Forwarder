/**
 * @file web_admin.cpp
 * @brief HTTP route handlers for configuration, diagnostics, and modem tools.
 */

#include "web_admin.h"

#include <WiFi.h>

#include "wifi_pages.h"
#include "web_pages.h"

namespace {

constexpr TickType_t kWebSyncPollSlice = pdMS_TO_TICKS(50);
constexpr unsigned long kPendingRequestLifetimeMs = 60000;
constexpr TickType_t kSendSmsWaitTicks = pdMS_TO_TICKS(40000);

String EscapeHtml(const String& value) {
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#39;");
  return escaped;
}

const char* CheckedAttr(bool value) {
  return value ? " checked" : "";
}

const char* SelectedAttr(bool value) {
  return value ? " selected" : "";
}

String FormatTimeLabel(time_t utc) {
  if (utc <= 0) {
    return "未设置";
  }

  struct tm parts;
#if defined(_WIN32)
  localtime_s(&parts, &utc);
#else
  localtime_r(&utc, &parts);
#endif

  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d",
                parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                parts.tm_hour, parts.tm_min);
  return String(buffer);
}

String RepeatSummary(const ScheduledTaskRecord& task) {
  if (!task.repeat_enabled) {
    return "仅发送一次";
  }

  String unit;
  switch (task.repeat_unit) {
    case ScheduledIntervalUnit::Minutes:
      unit = "分钟";
      break;
    case ScheduledIntervalUnit::Hours:
      unit = "小时";
      break;
    case ScheduledIntervalUnit::Days:
      unit = "天";
      break;
    case ScheduledIntervalUnit::Weeks:
      unit = "周";
      break;
    case ScheduledIntervalUnit::Months:
      unit = "个月";
      break;
  }

  String summary = "每";
  summary += String(task.repeat_every);
  summary += unit;
  summary += "，";
  switch (task.end_policy) {
    case ScheduledEndPolicy::Never:
      summary += "不结束";
      break;
    case ScheduledEndPolicy::OnDate:
      summary += "到 ";
      summary += FormatTimeLabel(task.end_at_utc);
      summary += " 结束";
      break;
    case ScheduledEndPolicy::AfterRuns:
      summary += "发送 ";
      summary += String(task.max_runs);
      summary += " 次后结束";
      break;
  }
  return summary;
}

struct ScheduledToolsPageState {
  ScheduledTaskDraft draft;
  bool editing = false;
  bool scheduled_mode = false;
  time_t now_utc = 0;
  bool clock_valid = false;
  size_t task_count = 0;
  ScheduledTaskRecord tasks[kMaxScheduledTasks];
  String message;
  bool message_success = false;
};

void BuildScheduledToolsPageState(WebServer& server, ScheduledSms& scheduled_sms,
                                  const ScheduledTaskDraft* draft,
                                  const String& scheduled_message,
                                  bool scheduled_success,
                                  ScheduledToolsPageState& state) {
  if (draft != nullptr) {
    state.draft = *draft;
    state.editing = state.draft.id != 0;
  } else if (server.hasArg("edit")) {
    const uint32_t task_id = static_cast<uint32_t>(server.arg("edit").toInt());
    String load_message;
    if (scheduled_sms.LoadTask(task_id, state.draft, load_message)) {
      state.editing = true;
    }
  }

  if (!state.editing && draft == nullptr) {
    state.draft.enabled = true;
    state.draft.repeat_unit = ScheduledIntervalUnit::Days;
  }

  state.scheduled_mode = state.editing || scheduled_message.length() > 0 ||
                         server.arg("mode") == "scheduled";
  state.task_count = scheduled_sms.CopyTasks(state.tasks, kMaxScheduledTasks);
  state.now_utc = time(nullptr);
  state.clock_valid = state.now_utc >= 100000;
  state.message = scheduled_message;
  state.message_success = scheduled_success;
}

String BuildScheduledTaskListHtml(const ScheduledTaskRecord* tasks, size_t task_count) {
  if (task_count == 0) {
    return "<div class=\"hint\">当前还没有定时任务。</div>";
  }

  String html;
  for (size_t index = 0; index < task_count; ++index) {
    const ScheduledTaskRecord& task = tasks[index];
    html += "<div class=\"task-card\">";
    html += "<div class=\"task-card-header\">";
    html += "<div class=\"task-title\">" +
            EscapeHtml(task.name.length() > 0 ? task.name : task.phone) + "</div>";
    html += "<div class=\"task-badge ";
    html += task.enabled ? "active" : "paused";
    html += "\">";
    html += task.enabled ? "启用中" : "已暂停";
    html += "</div></div>";
    html += "<div class=\"task-meta\"><strong>号码：</strong>" + EscapeHtml(task.phone) +
            "</div>";
    html += "<div class=\"task-meta\"><strong>内容摘要：</strong>" +
            EscapeHtml(task.preview) + "</div>";
    html += "<div class=\"task-meta\"><strong>下次执行：</strong>" +
            EscapeHtml(FormatTimeLabel(task.next_run_utc)) + "</div>";
    html += "<div class=\"task-meta\"><strong>重复规则：</strong>" +
            EscapeHtml(RepeatSummary(task)) + "</div>";
    if (task.last_result.length() > 0) {
      html += "<div class=\"task-meta\"><strong>最近结果：</strong>" +
              EscapeHtml(task.last_result) + "</div>";
    }
    html += "<div class=\"task-actions\">";
    html += "<a href=\"/tools?mode=scheduled&edit=" + String(task.id) +
            "#scheduled-section\"><button type=\"button\" class=\"btn-secondary\">编辑</button></a>";
    html += "<form action=\"/tools/scheduled/toggle\" method=\"POST\"><input type=\"hidden\" name=\"taskId\" value=\"" +
            String(task.id) + "\"><input type=\"hidden\" name=\"enabled\" value=\"" +
            String(task.enabled ? 0 : 1) +
            "\"><button type=\"submit\" class=\"btn-secondary\">" +
            String(task.enabled ? "暂停" : "启用") + "</button></form>";
    html += "<form action=\"/tools/scheduled/run\" method=\"POST\"><input type=\"hidden\" name=\"taskId\" value=\"" +
            String(task.id) +
            "\"><button type=\"submit\" class=\"btn-secondary\">立即执行</button></form>";
    html += "<form action=\"/tools/scheduled/delete\" method=\"POST\" onsubmit=\"return confirm('确定删除这个定时任务吗？');\"><input type=\"hidden\" name=\"taskId\" value=\"" +
            String(task.id) +
            "\"><button type=\"submit\" class=\"btn-danger\">删除</button></form>";
    html += "</div></div>";
  }

  return html;
}

String RenderScheduledToolsSection(const ScheduledToolsPageState& state) {
  String cancel_block;
  if (state.editing) {
    cancel_block =
        "<a href=\"/tools?mode=scheduled#scheduled-section\"><button type=\"button\" class=\"btn-secondary\">取消编辑</button></a>";
  }

  String section = String(kScheduledToolsSectionHtml);
  section.replace("%SCHEDULED_TASK_ID%", String(state.draft.id));
  section.replace("%SCHEDULED_FIRST_RUN_EPOCH%",
                  String(static_cast<long long>(state.draft.first_run_utc)));
  section.replace("%SCHEDULED_END_AT_EPOCH%",
                  String(static_cast<long long>(state.draft.end_at_utc)));
  section.replace("%SCHEDULED_TASK_NAME%", EscapeHtml(state.draft.name));
  section.replace("%SCHEDULED_TASK_PHONE%", EscapeHtml(state.draft.phone));
  section.replace("%SCHEDULED_TASK_BODY%", EscapeHtml(state.draft.body));
  section.replace("%SCHEDULED_TASK_ENABLED_VALUE%",
                  String(state.draft.enabled ? 1 : 0));
  section.replace("%SCHEDULED_REPEAT_ENABLED_CHECKED%",
                  String(CheckedAttr(state.draft.repeat_enabled)));
  section.replace("%SCHEDULED_REPEAT_EVERY%",
                  String(state.draft.repeat_every == 0 ? 1 : state.draft.repeat_every));
  section.replace(
      "%SCHEDULED_REPEAT_UNIT_MINUTES%",
      String(SelectedAttr(state.draft.repeat_unit == ScheduledIntervalUnit::Minutes)));
  section.replace(
      "%SCHEDULED_REPEAT_UNIT_HOURS%",
      String(SelectedAttr(state.draft.repeat_unit == ScheduledIntervalUnit::Hours)));
  section.replace(
      "%SCHEDULED_REPEAT_UNIT_DAYS%",
      String(SelectedAttr(state.draft.repeat_unit == ScheduledIntervalUnit::Days)));
  section.replace(
      "%SCHEDULED_REPEAT_UNIT_WEEKS%",
      String(SelectedAttr(state.draft.repeat_unit == ScheduledIntervalUnit::Weeks)));
  section.replace(
      "%SCHEDULED_REPEAT_UNIT_MONTHS%",
      String(SelectedAttr(state.draft.repeat_unit == ScheduledIntervalUnit::Months)));
  section.replace("%SCHEDULED_END_POLICY_NEVER%",
                  String(SelectedAttr(state.draft.end_policy ==
                                      ScheduledEndPolicy::Never)));
  section.replace("%SCHEDULED_END_POLICY_DATE%",
                  String(SelectedAttr(state.draft.end_policy ==
                                      ScheduledEndPolicy::OnDate)));
  section.replace("%SCHEDULED_END_POLICY_COUNT%",
                  String(SelectedAttr(state.draft.end_policy ==
                                      ScheduledEndPolicy::AfterRuns)));
  section.replace("%SCHEDULED_MAX_RUNS%",
                  String(state.draft.max_runs == 0 ? 1 : state.draft.max_runs));
  section.replace("%SCHEDULED_PRIMARY_BUTTON%",
                  state.editing ? "保存定时任务" : "创建定时任务");
  section.replace("%SCHEDULED_CANCEL_BLOCK%", cancel_block);
  return section;
}

String RenderScheduledTaskListSection(const ScheduledToolsPageState& state) {
  String section = String(kScheduledTaskListSectionHtml);
  section.replace("%SCHEDULED_TASK_LIST%",
                  BuildScheduledTaskListHtml(state.tasks, state.task_count));
  return section;
}

const char* WifiModeToString(WifiRuntimeMode mode) {
  switch (mode) {
    case WifiRuntimeMode::TryingSavedNetworks:
      return "trying";
    case WifiRuntimeMode::Connected:
      return "connected";
    case WifiRuntimeMode::PortalHandoff:
      return "handoff";
    case WifiRuntimeMode::ProvisioningPortal:
    default:
      return "portal";
  }
}

}  // namespace

WebAdmin::WebAdmin(ConfigStore& config_store, SharedConfigState& shared_state,
                   WifiRuntime& wifi_runtime, ScheduledSms& scheduled_sms,
                   QueueHandle_t modem_request_queue, QueueHandle_t web_response_queue,
                   QueueHandle_t app_event_queue)
    : config_store_(config_store),
      shared_state_(shared_state),
      wifi_runtime_(wifi_runtime),
      scheduled_sms_(scheduled_sms),
      modem_request_queue_(modem_request_queue),
      web_response_queue_(web_response_queue),
      app_event_queue_(app_event_queue),
      next_request_id_(1),
      server_(80) {}

// HTTP server lifecycle and shared helpers.
void WebAdmin::Begin() {
  server_.on("/", [this]() { HandleRoot(); });
  server_.on("/admin", [this]() { HandleAdminPage(); });
  server_.on("/save", HTTP_POST, [this]() { HandleSave(); });
  server_.on("/tools", [this]() { HandleToolsPage(); });
  server_.on("/sms", [this]() { HandleToolsPage(); });
  server_.on("/sendsms", HTTP_POST, [this]() { HandleSendSms(); });
  server_.on("/tools/scheduled/save", HTTP_POST, [this]() { HandleScheduledSave(); });
  server_.on("/tools/scheduled/delete", HTTP_POST, [this]() { HandleScheduledDelete(); });
  server_.on("/tools/scheduled/toggle", HTTP_POST, [this]() { HandleScheduledToggle(); });
  server_.on("/tools/scheduled/run", HTTP_POST, [this]() { HandleScheduledRun(); });
  server_.on("/query", [this]() { HandleQuery(); });
  server_.on("/flight", [this]() { HandleFlightMode(); });
  server_.on("/at", [this]() { HandleATCommand(); });
  server_.on("/modem_result", [this]() { HandleModemResult(); });
  server_.on("/provision", [this]() { HandleProvisionPage(); });
  server_.on("/provision/status", [this]() { HandleProvisionStatus(); });
  server_.on("/provision/networks", [this]() { HandleProvisionNetworks(); });
  server_.on("/provision/credentials", [this]() { HandleProvisionCredentials(); });
  server_.on("/provision/connect", HTTP_POST, [this]() { HandleProvisionConnect(); });
  server_.on("/provision/delete", HTTP_POST, [this]() { HandleProvisionDelete(); });
  server_.on("/provision/clear", HTTP_POST, [this]() { HandleProvisionClear(); });
  server_.begin();
  Serial.println("HTTP server started");
}

void WebAdmin::HandleClient() {
  DrainModemResponses();
  CleanupPendingRequests();
  server_.handleClient();
}

bool WebAdmin::LoadConfigSnapshot(AppConfig& config, bool* config_valid) const {
  if (shared_state_.mutex == nullptr) {
    return false;
  }

  if (xSemaphoreTake(shared_state_.mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  config = shared_state_.config;
  if (config_valid != nullptr) {
    *config_valid = shared_state_.configValid;
  }
  xSemaphoreGive(shared_state_.mutex);
  return true;
}

bool WebAdmin::SubmitModemRequest(const ModemRequest& request, TickType_t timeout_ticks) {
  if (modem_request_queue_ == nullptr) {
    return false;
  }

  return xQueueSend(modem_request_queue_, &request, timeout_ticks) == pdTRUE;
}

bool WebAdmin::WaitForModemResponse(uint32_t request_id, ModemResponse& response,
                                    TickType_t timeout_ticks) {
  const TickType_t start = xTaskGetTickCount();
  while (true) {
    const TickType_t now = xTaskGetTickCount();
    const TickType_t elapsed = now - start;
    if (elapsed >= timeout_ticks) {
      break;
    }

    const TickType_t remaining = timeout_ticks - elapsed;
    const TickType_t wait_time =
        remaining > kWebSyncPollSlice ? kWebSyncPollSlice : remaining;

    ModemResponse incoming;
    if (xQueueReceive(web_response_queue_, &incoming, wait_time) != pdTRUE) {
      continue;
    }

    if (incoming.requestId == request_id) {
      response = incoming;
      return true;
    }

    StorePendingResponse(incoming);
  }

  return false;
}

bool WebAdmin::RequestAtCommand(const String& cmd, unsigned long timeout_ms, String& response) {
  ModemRequest request;
  request.requestId = AllocateRequestId();
  request.requester = ModemRequester::Web;
  request.type = ModemRequestType::SendAtCommand;
  request.timeoutMs = timeout_ms;
  CopyString(request.command, cmd);

  if (!SubmitModemRequest(request)) {
    return false;
  }

  ModemResponse result;
  if (!WaitForModemResponse(request.requestId, result, pdMS_TO_TICKS(timeout_ms + 1000))) {
    return false;
  }

  response = result.message;
  return result.success;
}

bool WebAdmin::RequestSendSms(const String& phone, const String& content) {
  ModemRequest request;
  request.requestId = AllocateRequestId();
  request.requester = ModemRequester::Web;
  request.type = ModemRequestType::SendSms;
  CopyString(request.phone, phone);
  CopyString(request.text, content);

  if (!SubmitModemRequest(request)) {
    return false;
  }

  ModemResponse response;
  if (!WaitForModemResponse(request.requestId, response, kSendSmsWaitTicks)) {
    return false;
  }

  return response.success;
}

bool WebAdmin::RequestSendStoredSms(uint32_t task_id, const String& phone,
                                    String& response_message) {
  ModemRequest request;
  request.requestId = AllocateRequestId();
  request.requester = ModemRequester::Web;
  request.type = ModemRequestType::SendStoredSms;
  request.storedTaskId = task_id;
  CopyString(request.phone, phone);

  if (!SubmitModemRequest(request)) {
    response_message = "无法提交定时短信发送请求。";
    return false;
  }

  ModemResponse response;
  if (!WaitForModemResponse(request.requestId, response, kSendSmsWaitTicks)) {
    response_message = "定时短信发送超时。";
    return false;
  }

  response_message = response.message;
  return response.success;
}

uint32_t WebAdmin::StartAsyncAtCommand(const String& cmd, unsigned long timeout_ms) {
  PendingWebRequest* slot = nullptr;
  uint32_t request_id = 0;

  for (PendingWebRequest& pending_request : pending_requests_) {
    if (!pending_request.inUse) {
      slot = &pending_request;
      request_id = AllocateRequestId();
      pending_request.inUse = true;
      pending_request.completed = false;
      pending_request.requestId = request_id;
      pending_request.success = false;
      pending_request.createdAtMs = millis();
      pending_request.message = "";
      break;
    }
  }

  if (slot == nullptr) {
    return 0;
  }

  ModemRequest request;
  request.requestId = request_id;
  request.requester = ModemRequester::Web;
  request.type = ModemRequestType::SendAtCommand;
  request.timeoutMs = timeout_ms;
  CopyString(request.command, cmd);

  if (!SubmitModemRequest(request)) {
    slot->inUse = false;
    slot->message = "";
    return 0;
  }

  return request_id;
}

uint32_t WebAdmin::AllocateRequestId() {
  if (next_request_id_ == 0) {
    next_request_id_ = 1;
  }

  return next_request_id_++;
}

void WebAdmin::DrainModemResponses() {
  if (web_response_queue_ == nullptr) {
    return;
  }

  ModemResponse response;
  while (xQueueReceive(web_response_queue_, &response, 0) == pdTRUE) {
    StorePendingResponse(response);
  }
}

void WebAdmin::StorePendingResponse(const ModemResponse& response) {
  PendingWebRequest* slot = FindPendingRequest(response.requestId);
  if (slot == nullptr) {
    return;
  }

  slot->completed = true;
  slot->success = response.success;
  slot->message = response.message;
}

WebAdmin::PendingWebRequest* WebAdmin::FindPendingRequest(uint32_t request_id) {
  for (PendingWebRequest& pending_request : pending_requests_) {
    if (pending_request.inUse && pending_request.requestId == request_id) {
      return &pending_request;
    }
  }

  return nullptr;
}

void WebAdmin::CleanupPendingRequests() {
  const unsigned long now = millis();
  for (PendingWebRequest& pending_request : pending_requests_) {
    if (!pending_request.inUse) {
      continue;
    }

    if (now - pending_request.createdAtMs < kPendingRequestLifetimeMs) {
      continue;
    }

    pending_request = PendingWebRequest{};
  }
}
String WebAdmin::GetDeviceUrl() const {
  return "http://" + WiFi.localIP().toString() + "/";
}

String WebAdmin::EscapeJson(const String& value) const {
  String result;
  for (unsigned int i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == '"') result += "\\\"";
    else if (c == '\\') result += "\\\\";
    else if (c == '\n') result += "\\n";
    else if (c == '\r') result += "\\r";
    else if (c == '\t') result += "\\t";
    else result += c;
  }
  return result;
}

bool WebAdmin::CheckAuth() {
  AppConfig config;
  if (!LoadConfigSnapshot(config, nullptr)) {
    server_.send(500, "text/plain", "配置忙，请稍后重试");
    return false;
  }

  if (!server_.authenticate(config.webUser.c_str(), config.webPass.c_str())) {
    server_.requestAuthentication(BASIC_AUTH, "SMS Forwarding", "请输入管理员账号密码");
    return false;
  }
  return true;
}

bool WebAdmin::CheckProvisionAccess() {
  if (wifi_runtime_.AllowsOpenProvisioningAccess(server_.client().localIP())) {
    return true;
  }

  return CheckAuth();
}
// Page rendering.
void WebAdmin::HandleRoot() {
  if (wifi_runtime_.ShouldServeProvisioningPortal()) {
    HandleProvisionPage();
    return;
  }

  HandleAdminPage();
}

void WebAdmin::HandleAdminPage() {
  if (!CheckAuth()) return;

  AppConfig config;
  if (!LoadConfigSnapshot(config, nullptr)) {
    server_.send(500, "text/plain", "配置忙，请稍后重试");
    return;
  }

  String html = String(kConfigPageHtml);
  const time_t now_utc = time(nullptr);
  String config_clock_hint = "当前设备时间：未同步";
  if (now_utc >= 100000) {
    config_clock_hint = "当前设备时间：" + FormatTimeLabel(now_utc);
  }
  html.replace("%IP%", wifi_runtime_.GetPrimaryIpString());
  html.replace("%CONFIG_CLOCK_HINT%",
               "<span id=\"configClockHint\" data-epoch=\"" +
                   String(static_cast<long long>(now_utc)) +
                   "\" data-valid=\"" + String(now_utc >= 100000 ? "1" : "0") +
                   "\">" + EscapeHtml(config_clock_hint) + "</span>");
  html.replace("%WEB_USER%", config.webUser);
  html.replace("%WEB_PASS%", config.webPass);
  html.replace("%SMTP_SERVER%", config.smtpServer);
  html.replace("%SMTP_PORT%", String(config.smtpPort));
  html.replace("%SMTP_USER%", config.smtpUser);
  html.replace("%SMTP_PASS%", config.smtpPass);
  html.replace("%SMTP_SEND_TO%", config.smtpSendTo);
  html.replace("%ADMIN_PHONE%", config.adminPhone);
  html.replace("%NUMBER_BLACK_LIST%", config.numberBlackList);

  String channels_html;
  // Render the push channel section from the persisted configuration so the UI
  // always reflects the current stored values.
  for (int i = 0; i < kMaxPushChannels; i++) {
    const String idx = String(i);
    const String enabled_class = config.pushChannels[i].enabled ? " enabled" : "";
    const String checked = config.pushChannels[i].enabled ? " checked" : "";

    channels_html += "<div class=\"push-channel" + enabled_class + "\" id=\"channel" + idx + "\">";
    channels_html += "<div class=\"push-channel-header\">";
    channels_html += "<input type=\"checkbox\" name=\"push" + idx + "en\" id=\"push" + idx + "en\" onchange=\"toggleChannel(" + idx + ")\"" + checked + ">";
    channels_html += "<label for=\"push" + idx + "en\" class=\"label-inline\">启用推送通道 " + String(i + 1) + "</label>";
    channels_html += "</div>";
    channels_html += "<div class=\"push-channel-body\">";

    channels_html += "<div class=\"form-group\">";
    channels_html += "<label>通道名称</label>";
    channels_html += "<input type=\"text\" name=\"push" + idx + "name\" value=\"" + config.pushChannels[i].name + "\" placeholder=\"自定义名称\">";
    channels_html += "</div>";

    channels_html += "<div class=\"form-group\">";
    channels_html += "<label>推送方式</label>";
    channels_html += "<select name=\"push" + idx + "type\" id=\"push" + idx + "type\" onchange=\"updateTypeHint(" + idx + ")\">";
    channels_html += "<option value=\"1\"" + String(config.pushChannels[i].type == PUSH_TYPE_POST_JSON ? " selected" : "") + ">POST JSON（通用格式）</option>";
    channels_html += "<option value=\"2\"" + String(config.pushChannels[i].type == PUSH_TYPE_BARK ? " selected" : "") + ">Bark（iOS推送）</option>";
    channels_html += "<option value=\"3\"" + String(config.pushChannels[i].type == PUSH_TYPE_GET ? " selected" : "") + ">GET请求（参数在URL中）</option>";
    channels_html += "<option value=\"4\"" + String(config.pushChannels[i].type == PUSH_TYPE_DINGTALK ? " selected" : "") + ">钉钉机器人</option>";
    channels_html += "<option value=\"5\"" + String(config.pushChannels[i].type == PUSH_TYPE_PUSHPLUS ? " selected" : "") + ">PushPlus</option>";
    channels_html += "<option value=\"6\"" + String(config.pushChannels[i].type == PUSH_TYPE_SERVERCHAN ? " selected" : "") + ">Server酱</option>";
    channels_html += "<option value=\"7\"" + String(config.pushChannels[i].type == PUSH_TYPE_CUSTOM ? " selected" : "") + ">自定义模板</option>";
    channels_html += "<option value=\"8\"" + String(config.pushChannels[i].type == PUSH_TYPE_FEISHU ? " selected" : "") + ">飞书机器人</option>";
    channels_html += "<option value=\"9\"" + String(config.pushChannels[i].type == PUSH_TYPE_GOTIFY ? " selected" : "") + ">Gotify</option>";
    channels_html += "<option value=\"10\"" + String(config.pushChannels[i].type == PUSH_TYPE_TELEGRAM ? " selected" : "") + ">Telegram Bot</option>";
    channels_html += "</select>";
    channels_html += "<div class=\"push-type-hint\" id=\"hint" + idx + "\"></div>";
    channels_html += "</div>";

    channels_html += "<div class=\"form-group\">";
    channels_html += "<label>推送URL/Webhook</label>";
    channels_html += "<input type=\"text\" name=\"push" + idx + "url\" value=\"" + config.pushChannels[i].url + "\" placeholder=\"http://your-server.com/api 或 webhook地址\">";
    channels_html += "</div>";

    channels_html += "<div id=\"extra" + idx + "\" style=\"display:none;\">";
    channels_html += "<div class=\"form-group\">";
    channels_html += "<label id=\"key1label" + idx + "\">参数1</label>";
    channels_html += "<input type=\"text\" name=\"push" + idx + "key1\" id=\"key1" + idx + "\" value=\"" + config.pushChannels[i].key1 + "\">";
    channels_html += "</div>";
    channels_html += "<div class=\"form-group\" id=\"key2group" + idx + "\">";
    channels_html += "<label id=\"key2label" + idx + "\">参数2</label>";
    channels_html += "<input type=\"text\" name=\"push" + idx + "key2\" id=\"key2" + idx + "\" value=\"" + config.pushChannels[i].key2 + "\">";
    channels_html += "</div>";
    channels_html += "</div>";

    channels_html += "<div id=\"custom" + idx + "\" style=\"display:none;\">";
    channels_html += "<div class=\"form-group\">";
    channels_html += "<label>请求体模板（使用 {sender} {message} {timestamp} 占位符）</label>";
    channels_html += "<textarea name=\"push" + idx + "body\" rows=\"4\" style=\"width:100%;font-family:monospace;\">" + config.pushChannels[i].customBody + "</textarea>";
    channels_html += "</div>";
    channels_html += "</div>";

    channels_html += "</div></div>";
  }
  html.replace("%PUSH_CHANNELS%", channels_html);

  server_.send(200, "text/html", html);
}

void WebAdmin::SendToolsPage(const ScheduledTaskDraft* draft,
                             const String& scheduled_message,
                             bool scheduled_success) {
  ScheduledToolsPageState state;
  BuildScheduledToolsPageState(server_, scheduled_sms_, draft, scheduled_message,
                               scheduled_success, state);

  String html = String(kToolsPageHtml);
  String tools_clock_hint = "当前设备时间：未同步";
  if (state.clock_valid) {
    tools_clock_hint = "当前设备时间：" + FormatTimeLabel(state.now_utc);
  }
  html.replace("%IP%", wifi_runtime_.GetPrimaryIpString());
  html.replace("%TOOLS_CLOCK_HINT%",
               "<span id=\"scheduledClockHint\" data-epoch=\"" +
                   String(static_cast<long long>(state.now_utc)) +
                   "\" data-valid=\"" + String(state.clock_valid ? "1" : "0") +
                   "\">" + EscapeHtml(tools_clock_hint) + "</span>");
  String message_toast;
  if (state.message.length() > 0) {
    message_toast = "<div id=\"scheduledToast\" class=\"scheduled-toast ";
    message_toast += state.message_success ? "scheduled-toast-success" : "scheduled-toast-error";
    message_toast += "\">";
    message_toast += EscapeHtml(state.message);
    message_toast += "</div>";
  }
  html.replace("%SCHEDULED_MESSAGE_TOAST%", message_toast);
  html.replace("%SCHEDULED_SECTION%", RenderScheduledToolsSection(state));
  html.replace("%SCHEDULED_TASKS_SECTION%", RenderScheduledTaskListSection(state));
  html.replace("%SCHEDULED_SCRIPT%", kScheduledToolsScript);
  html.replace("%TOOLS_INITIAL_MODE%", state.scheduled_mode ? "scheduled" : "now");
  server_.send(200, "text/html", html);
}

void WebAdmin::HandleToolsPage() {
  if (!CheckAuth()) return;
  SendToolsPage(nullptr, "", false);
}

// Modem tools and diagnostic endpoints.
void WebAdmin::HandleFlightMode() {
  if (!CheckAuth()) return;

  String action = server_.arg("action");
  String json = "{";
  bool success = false;
  String message;

  if (action == "query") {
    String resp;
    RequestAtCommand("AT+CFUN?", 2000, resp);
    Serial.println("CFUN query response: " + resp);

    if (resp.indexOf("+CFUN:") >= 0) {
      success = true;
      const int idx = resp.indexOf("+CFUN:");
      const int mode = resp.substring(idx + 6).toInt();

      String mode_str;
      String status_icon;
      if (mode == 0) {
        mode_str = "最小功能模式（关机）";
        status_icon = "🔴";
      } else if (mode == 1) {
        mode_str = "全功能模式（正常）";
        status_icon = "🟢";
      } else if (mode == 4) {
        mode_str = "飞行模式（射频关闭）";
        status_icon = "✈️";
      } else {
        mode_str = "未知模式 (" + String(mode) + ")";
        status_icon = "❓";
      }

      message = "<table class='info-table'>";
      message += "<tr><td>当前状态</td><td>" + status_icon + " " + mode_str + "</td></tr>";
      message += "<tr><td>CFUN值</td><td>" + String(mode) + "</td></tr>";
      message += "</table>";
    } else {
      message = "查询失败";
    }
  } else if (action == "toggle") {
    String resp;
    RequestAtCommand("AT+CFUN?", 2000, resp);
    Serial.println("CFUN query response: " + resp);

    if (resp.indexOf("+CFUN:") >= 0) {
      const int idx = resp.indexOf("+CFUN:");
      const int current_mode = resp.substring(idx + 6).toInt();

      const int new_mode = (current_mode == 1) ? 4 : 1;
      const String cmd = "AT+CFUN=" + String(new_mode);

      Serial.println("Toggling flight mode with command: " + cmd);
      String set_resp;
      RequestAtCommand(cmd, 5000, set_resp);
      Serial.println("CFUN set response: " + set_resp);

      if (set_resp.indexOf("OK") >= 0) {
        success = true;
        if (new_mode == 4) {
          message = "已开启飞行模式 ✈️<br>模组射频已关闭，无法收发短信";
        } else {
          message = "已关闭飞行模式 🟢<br>模组恢复正常工作";
        }
      } else {
        message = "切换失败: " + set_resp;
      }
    } else {
      message = "无法获取当前状态";
    }
  } else if (action == "on") {
    String resp;
    RequestAtCommand("AT+CFUN=4", 5000, resp);
    if (resp.indexOf("OK") >= 0) {
      success = true;
      message = "已开启飞行模式 ✈️";
    } else {
      message = "开启失败: " + resp;
    }
  } else if (action == "off") {
    String resp;
    RequestAtCommand("AT+CFUN=1", 5000, resp);
    if (resp.indexOf("OK") >= 0) {
      success = true;
      message = "已关闭飞行模式 🟢";
    } else {
      message = "关闭失败: " + resp;
    }
  } else {
    message = "未知操作";
  }

  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + message + "\"";
  json += "}";

  server_.send(200, "application/json", json);
}

void WebAdmin::HandleATCommand() {
  if (!CheckAuth()) return;

  const String cmd = server_.arg("cmd");
  String json = "{";

  if (cmd.length() == 0) {
    json += "\"accepted\":false,";
    json += "\"message\":\"请输入 AT 指令。\"";
  } else {
    Serial.println("Web queued AT command: " + cmd);
    const uint32_t request_id = StartAsyncAtCommand(cmd, 5000);
    if (request_id == 0) {
      json += "\"accepted\":false,";
      json += "\"message\":\"系统忙，请稍后重试。\"";
    } else {
      json += "\"accepted\":true,";
      json += "\"requestId\":" + String(request_id);
    }
  }

  json += "}";
  server_.send(200, "application/json", json);
}

void WebAdmin::HandleQuery() {
  if (!CheckAuth()) return;

  const String type = server_.arg("type");
  String json = "{";
  bool success = false;
  String message;

  if (type == "ati") {
    String resp;
    RequestAtCommand("ATI", 2000, resp);
    Serial.println("ATI response: " + resp);

    if (resp.indexOf("OK") >= 0) {
      success = true;
      String manufacturer = "未知";
      String model = "未知";
      String version = "未知";

      int line_start = 0;
      int line_num = 0;
      for (int i = 0; i < resp.length(); i++) {
        if (resp.charAt(i) == '\n' || i == resp.length() - 1) {
          String line = resp.substring(line_start, i);
          line.trim();
          if (line.length() > 0 && line != "ATI" && line != "OK") {
            line_num++;
            if (line_num == 1) manufacturer = line;
            else if (line_num == 2) model = line;
            else if (line_num == 3) version = line;
          }
          line_start = i + 1;
        }
      }

      message = "<table class='info-table'>";
      message += "<tr><td>制造商</td><td>" + manufacturer + "</td></tr>";
      message += "<tr><td>模组型号</td><td>" + model + "</td></tr>";
      message += "<tr><td>固件版本</td><td>" + version + "</td></tr>";
      message += "</table>";
    } else {
      message = "查询失败";
    }
  } else if (type == "signal") {
    String resp;
    RequestAtCommand("AT+CESQ", 2000, resp);
    Serial.println("CESQ response: " + resp);

    if (resp.indexOf("+CESQ:") >= 0) {
      success = true;
      // +CESQ reports <rxlev>,<ber>,<rscp>,<ecno>,<rsrq>,<rsrp>. The UI only
      // exposes the LTE-specific fields that are useful for troubleshooting.
      const int idx = resp.indexOf("+CESQ:");
      String params = resp.substring(idx + 6);
      int end_idx = params.indexOf('\r');
      if (end_idx < 0) end_idx = params.indexOf('\n');
      if (end_idx > 0) params = params.substring(0, end_idx);
      params.trim();

      String values[6];
      int val_idx = 0;
      int start_pos = 0;
      for (int i = 0; i <= params.length() && val_idx < 6; i++) {
        if (i == params.length() || params.charAt(i) == ',') {
          values[val_idx] = params.substring(start_pos, i);
          values[val_idx].trim();
          val_idx++;
          start_pos = i + 1;
        }
      }

      const int rsrp = values[5].toInt();
      String rsrp_str;
      if (rsrp == 99 || rsrp == 255) {
        rsrp_str = "未知";
      } else {
        const int rsrp_dbm = -140 + rsrp;
        rsrp_str = String(rsrp_dbm) + " dBm";
        if (rsrp_dbm >= -80) rsrp_str += " (信号极好)";
        else if (rsrp_dbm >= -90) rsrp_str += " (信号良好)";
        else if (rsrp_dbm >= -100) rsrp_str += " (信号一般)";
        else if (rsrp_dbm >= -110) rsrp_str += " (信号较弱)";
        else rsrp_str += " (信号很差)";
      }

      const int rsrq = values[4].toInt();
      String rsrq_str;
      if (rsrq == 99 || rsrq == 255) {
        rsrq_str = "未知";
      } else {
        const float rsrq_db = -19.5f + rsrq * 0.5f;
        rsrq_str = String(rsrq_db, 1) + " dB";
      }

      message = "<table class='info-table'>";
      message += "<tr><td>信号强度 (RSRP)</td><td>" + rsrp_str + "</td></tr>";
      message += "<tr><td>信号质量 (RSRQ)</td><td>" + rsrq_str + "</td></tr>";
      message += "<tr><td>原始数据</td><td>" + params + "</td></tr>";
      message += "</table>";
    } else {
      message = "查询失败";
    }
  } else if (type == "siminfo") {
    success = true;
    message = "<table class='info-table'>";

    String resp;
    RequestAtCommand("AT+CIMI", 2000, resp);
    String imsi = "未知";
    if (resp.indexOf("OK") >= 0) {
      const int start = resp.indexOf('\n');
      if (start >= 0) {
        int end = resp.indexOf('\n', start + 1);
        if (end < 0) end = resp.indexOf('\r', start + 1);
        if (end > start) {
          imsi = resp.substring(start + 1, end);
          imsi.trim();
          if (imsi == "OK" || imsi.length() < 10) imsi = "未知";
        }
      }
    }
    message += "<tr><td>IMSI</td><td>" + imsi + "</td></tr>";

    RequestAtCommand("AT+ICCID", 2000, resp);
    String iccid = "未知";
    if (resp.indexOf("+ICCID:") >= 0) {
      const int idx = resp.indexOf("+ICCID:");
      String tmp = resp.substring(idx + 7);
      int end_idx = tmp.indexOf('\r');
      if (end_idx < 0) end_idx = tmp.indexOf('\n');
      if (end_idx > 0) iccid = tmp.substring(0, end_idx);
      iccid.trim();
    }
    message += "<tr><td>ICCID</td><td>" + iccid + "</td></tr>";

    RequestAtCommand("AT+CNUM", 2000, resp);
    String phone_num = "未存储或不支持";
    if (resp.indexOf("+CNUM:") >= 0) {
      const int idx = resp.indexOf(",\"");
      if (idx >= 0) {
        const int end_idx = resp.indexOf("\"", idx + 2);
        if (end_idx > idx) {
          phone_num = resp.substring(idx + 2, end_idx);
        }
      }
    }
    message += "<tr><td>本机号码</td><td>" + phone_num + "</td></tr>";

    message += "</table>";
  } else if (type == "network") {
    success = true;
    message = "<table class='info-table'>";

    String resp;
    RequestAtCommand("AT+CEREG?", 2000, resp);
    String reg_status = "未知";
    if (resp.indexOf("+CEREG:") >= 0) {
      const int idx = resp.indexOf("+CEREG:");
      String tmp = resp.substring(idx + 7);
      const int comma_idx = tmp.indexOf(',');
      if (comma_idx >= 0) {
        const String stat = tmp.substring(comma_idx + 1, comma_idx + 2);
        const int s = stat.toInt();
        switch (s) {
          case 0: reg_status = "未注册，未搜索"; break;
          case 1: reg_status = "已注册，本地网络"; break;
          case 2: reg_status = "未注册，正在搜索"; break;
          case 3: reg_status = "注册被拒绝"; break;
          case 4: reg_status = "未知"; break;
          case 5: reg_status = "已注册，漫游"; break;
          default: reg_status = "状态码: " + stat; break;
        }
      }
    }
    message += "<tr><td>网络注册</td><td>" + reg_status + "</td></tr>";

    RequestAtCommand("AT+COPS?", 2000, resp);
    String oper = "未知";
    if (resp.indexOf("+COPS:") >= 0) {
      const int idx = resp.indexOf(",\"");
      if (idx >= 0) {
        const int end_idx = resp.indexOf("\"", idx + 2);
        if (end_idx > idx) {
          oper = resp.substring(idx + 2, end_idx);
        }
      }
    }
    message += "<tr><td>运营商</td><td>" + oper + "</td></tr>";

    RequestAtCommand("AT+CGACT?", 2000, resp);
    String pdp_status = "未激活";
    if (resp.indexOf("+CGACT: 1,1") >= 0) {
      pdp_status = "已激活";
    } else if (resp.indexOf("+CGACT:") >= 0) {
      pdp_status = "未激活";
    }
    message += "<tr><td>数据连接</td><td>" + pdp_status + "</td></tr>";

    RequestAtCommand("AT+CGDCONT?", 2000, resp);
    String apn = "未知";
    if (resp.indexOf("+CGDCONT:") >= 0) {
      int idx = resp.indexOf(",\"");
      if (idx >= 0) {
        idx = resp.indexOf(",\"", idx + 2);
        if (idx >= 0) {
          const int end_idx = resp.indexOf("\"", idx + 2);
          if (end_idx > idx) {
            apn = resp.substring(idx + 2, end_idx);
            if (apn.length() == 0) apn = "(自动)";
          }
        }
      }
    }
    message += "<tr><td>APN</td><td>" + apn + "</td></tr>";

    message += "</table>";
  } else if (type == "wifi") {
    success = true;
    message = "<table class='info-table'>";

    const String wifi_status = WiFi.isConnected() ? "已连接" : "未连接";
    message += "<tr><td>连接状态</td><td>" + wifi_status + "</td></tr>";

    String ssid = WiFi.SSID();
    if (ssid.length() == 0) ssid = "未知";
    message += "<tr><td>当前SSID</td><td>" + ssid + "</td></tr>";

    const int rssi = WiFi.RSSI();
    String rssi_str = String(rssi) + " dBm";
    if (rssi >= -50) rssi_str += " (信号极好)";
    else if (rssi >= -60) rssi_str += " (信号很好)";
    else if (rssi >= -70) rssi_str += " (信号良好)";
    else if (rssi >= -80) rssi_str += " (信号一般)";
    else if (rssi >= -90) rssi_str += " (信号较弱)";
    else rssi_str += " (信号很差)";
    message += "<tr><td>信号强度 (RSSI)</td><td>" + rssi_str + "</td></tr>";

    message += "<tr><td>IP地址</td><td>" + WiFi.localIP().toString() + "</td></tr>";
    message += "<tr><td>网关</td><td>" + WiFi.gatewayIP().toString() + "</td></tr>";
    message += "<tr><td>子网掩码</td><td>" + WiFi.subnetMask().toString() + "</td></tr>";
    message += "<tr><td>DNS服务器</td><td>" + WiFi.dnsIP().toString() + "</td></tr>";
    message += "<tr><td>MAC地址</td><td>" + WiFi.macAddress() + "</td></tr>";
    message += "<tr><td>路由器BSSID</td><td>" + WiFi.BSSIDstr() + "</td></tr>";
    message += "<tr><td>WiFi信道</td><td>" + String(WiFi.channel()) + "</td></tr>";

    message += "</table>";
  } else {
    message = "未知的查询类型";
  }

  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + message + "\"";
  json += "}";

  server_.send(200, "application/json", json);
}

// Mutating actions.
void WebAdmin::HandleSendSms() {
  if (!CheckAuth()) return;

  String phone = server_.arg("phone");
  String content = server_.arg("content");

  phone.trim();
  content.trim();

  bool success = false;
  String result_msg;

  if (phone.length() == 0) {
    result_msg = "错误：请输入目标号码";
  } else if (content.length() == 0) {
    result_msg = "错误：请输入短信内容";
  } else {
    Serial.println("Web requested an SMS send");
    Serial.println("Target phone: " + phone);
    Serial.println("SMS content: " + content);

    success = RequestSendSms(phone, content);
    result_msg = success ? "短信发送成功！" : "短信发送失败，请检查模组状态";
  }

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta http-equiv="refresh" content="3;url=/sms">
  <title>发送结果</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; padding-top: 100px; background: #f5f5f5; }
    .result { padding: 20px; border-radius: 10px; display: inline-block; }
    .success { background: #4CAF50; color: white; }
    .error { background: #f44336; color: white; }
  </style>
</head>
<body>
  <div class="result %CLASS%">
    <h2>%ICON% %MSG%</h2>
    <p>3秒后返回发送页面...</p>
  </div>
</body>
</html>
)rawliteral";

  html.replace("%CLASS%", success ? "success" : "error");
  html.replace("%ICON%", success ? "✅" : "❌");
  html.replace("%MSG%", result_msg);

  server_.send(200, "text/html", html);
}

void WebAdmin::HandleScheduledSave() {
  if (!CheckAuth()) return;

  ScheduledTaskDraft draft;
  draft.id = static_cast<uint32_t>(server_.arg("taskId").toInt());
  draft.enabled = !server_.hasArg("taskEnabled") || server_.arg("taskEnabled").toInt() != 0;
  draft.name = server_.arg("taskName");
  draft.phone = server_.arg("taskPhone");
  draft.body = server_.arg("taskBody");
  draft.first_run_utc = static_cast<time_t>(server_.arg("firstRunEpoch").toInt());
  draft.repeat_enabled = server_.hasArg("repeatEnabled");
  draft.repeat_every = static_cast<uint32_t>(server_.arg("repeatEvery").toInt());

  const String repeat_unit = server_.arg("repeatUnit");
  if (repeat_unit == "minutes") {
    draft.repeat_unit = ScheduledIntervalUnit::Minutes;
  } else if (repeat_unit == "hours") {
    draft.repeat_unit = ScheduledIntervalUnit::Hours;
  } else if (repeat_unit == "weeks") {
    draft.repeat_unit = ScheduledIntervalUnit::Weeks;
  } else if (repeat_unit == "months") {
    draft.repeat_unit = ScheduledIntervalUnit::Months;
  } else {
    draft.repeat_unit = ScheduledIntervalUnit::Days;
  }

  const String end_policy = server_.arg("endPolicy");
  if (end_policy == "date") {
    draft.end_policy = ScheduledEndPolicy::OnDate;
  } else if (end_policy == "count") {
    draft.end_policy = ScheduledEndPolicy::AfterRuns;
  } else {
    draft.end_policy = ScheduledEndPolicy::Never;
  }
  draft.end_at_utc = static_cast<time_t>(server_.arg("endAtEpoch").toInt());
  draft.max_runs = static_cast<uint32_t>(server_.arg("maxRuns").toInt());

  String message;
  uint32_t saved_task_id = 0;
  const bool success = scheduled_sms_.UpsertTask(draft, message, &saved_task_id);
  if (success) {
    draft = ScheduledTaskDraft{};
    draft.enabled = true;
    draft.repeat_unit = ScheduledIntervalUnit::Days;
  }
  SendToolsPage(success ? nullptr : &draft, message, success);
}

void WebAdmin::HandleScheduledDelete() {
  if (!CheckAuth()) return;

  String message;
  const uint32_t task_id = static_cast<uint32_t>(server_.arg("taskId").toInt());
  if (!scheduled_sms_.DeleteTask(task_id, message)) {
    SendToolsPage(nullptr, message, false);
    return;
  }
  SendToolsPage(nullptr, message, true);
}

void WebAdmin::HandleScheduledToggle() {
  if (!CheckAuth()) return;

  String message;
  const uint32_t task_id = static_cast<uint32_t>(server_.arg("taskId").toInt());
  const bool enabled = server_.arg("enabled").toInt() != 0;
  if (!scheduled_sms_.SetTaskEnabled(task_id, enabled, message)) {
    SendToolsPage(nullptr, message, false);
    return;
  }
  SendToolsPage(nullptr, message, true);
}

void WebAdmin::HandleScheduledRun() {
  if (!CheckAuth()) return;

  const uint32_t task_id = static_cast<uint32_t>(server_.arg("taskId").toInt());
  ScheduledTaskDispatch dispatch;
  String message;
  bool success = scheduled_sms_.PrepareManualRun(task_id, dispatch, message);
  if (success) {
    success = RequestSendStoredSms(task_id, dispatch.phone, message);
    scheduled_sms_.CompleteManualRun(task_id, time(nullptr), success, message);
  }
  SendToolsPage(nullptr, message, success);
}

void WebAdmin::HandleSave() {
  if (!CheckAuth()) return;

  AppConfig config;
  if (!LoadConfigSnapshot(config, nullptr)) {
    server_.send(500, "text/plain", "配置忙，请稍后重试");
    return;
  }

  String new_web_user = server_.arg("webUser");
  String new_web_pass = server_.arg("webPass");

  if (new_web_user.length() == 0) new_web_user = DEFAULT_WEB_USER;
  if (new_web_pass.length() == 0) new_web_pass = DEFAULT_WEB_PASS;

  config.webUser = new_web_user;
  config.webPass = new_web_pass;
  config.smtpServer = server_.arg("smtpServer");
  config.smtpPort = server_.arg("smtpPort").toInt();
  if (config.smtpPort == 0) config.smtpPort = 465;
  config.smtpUser = server_.arg("smtpUser");
  config.smtpPass = server_.arg("smtpPass");
  config.smtpSendTo = server_.arg("smtpSendTo");
  config.adminPhone = server_.arg("adminPhone");
  config.numberBlackList = server_.arg("numberBlackList");

  for (int i = 0; i < kMaxPushChannels; i++) {
    const String idx = String(i);
    config.pushChannels[i].enabled = server_.arg("push" + idx + "en") == "on";
    config.pushChannels[i].type = (PushType)server_.arg("push" + idx + "type").toInt();
    config.pushChannels[i].url = server_.arg("push" + idx + "url");
    config.pushChannels[i].name = server_.arg("push" + idx + "name");
    config.pushChannels[i].key1 = server_.arg("push" + idx + "key1");
    config.pushChannels[i].key2 = server_.arg("push" + idx + "key2");
    config.pushChannels[i].customBody = server_.arg("push" + idx + "body");
    if (config.pushChannels[i].name.length() == 0) {
      config.pushChannels[i].name = "通道" + String(i + 1);
    }
  }

  const bool new_config_valid = IsConfigValid(config);
  if (!config_store_.Save(config)) { server_.send(500, "text/plain", "配置写入失败，请检查存储空间"); return; }

  if (shared_state_.mutex != nullptr && xSemaphoreTake(shared_state_.mutex, portMAX_DELAY) == pdTRUE) {
    shared_state_.config = config;
    shared_state_.configValid = new_config_valid;
    xSemaphoreGive(shared_state_.mutex);
  }

  // Save first, then show the success page so the browser only reports success
  // after NVS has been updated.
  const String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta http-equiv="refresh" content="3;url=/">
  <title>保存成功</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; padding-top: 100px; background: #f5f5f5; }
    .success { background: #4CAF50; color: white; padding: 20px; border-radius: 10px; display: inline-block; }
  </style>
</head>
<body>
  <div class="success">
    <h2>✅ 配置保存成功！</h2>
    <p>3秒后返回配置页面...</p>
    <p>如果修改了账号密码，请使用新的账号密码登录</p>
  </div>
</body>
</html>
)rawliteral";
  server_.send(200, "text/html", html);

  if (new_config_valid && app_event_queue_ != nullptr) {
    AppEvent event;
    event.type = AppEventType::ConfigUpdated;
    if (xQueueSend(app_event_queue_, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println("Failed to queue config-updated event for AppTask");
    }
  }
}

void WebAdmin::HandleModemResult() {
  if (!CheckAuth()) return;

  const String request_id_arg = server_.arg("id");
  String json = "{";

  if (request_id_arg.length() == 0) {
    json += "\"ready\":true,";
    json += "\"success\":false,";
    json += "\"message\":\"缺少请求编号。\"";
    json += "}";
    server_.send(200, "application/json", json);
    return;
  }

  const uint32_t request_id = static_cast<uint32_t>(request_id_arg.toInt());
  PendingWebRequest* pending_request = FindPendingRequest(request_id);
  if (pending_request == nullptr) {
    json += "\"ready\":true,";
    json += "\"success\":false,";
    json += "\"message\":\"请求不存在或已过期。\"";
    json += "}";
    server_.send(200, "application/json", json);
    return;
  }

  if (!pending_request->completed) {
    json += "\"ready\":false";
    json += "}";
    server_.send(200, "application/json", json);
    return;
  }

  json += "\"ready\":true,";
  json += "\"success\":" + String(pending_request->success ? "true" : "false") + ",";
  json += "\"message\":\"" + EscapeJson(pending_request->message) + "\"";
  json += "}";
  pending_request->inUse = false;
  pending_request->completed = false;
  pending_request->message = "";

  server_.send(200, "application/json", json);
}

void WebAdmin::HandleProvisionPage() {
  if (!CheckProvisionAccess()) return;

  server_.send(200, "text/html", String(kProvisionPageHtml));
}

void WebAdmin::HandleProvisionStatus() {
  if (!CheckProvisionAccess()) return;

  WifiStatusSnapshot snapshot;
  wifi_runtime_.GetStatusSnapshot(snapshot);

  String json = "{";
  json += "\"mode\":\"" + String(WifiModeToString(snapshot.mode)) + "\",";
  json += "\"portalActive\":" + String(snapshot.portalActive ? "true" : "false") + ",";
  json += "\"staConnected\":" + String(snapshot.staConnected ? "true" : "false") + ",";
  json += "\"connectInProgress\":" + String(snapshot.connectInProgress ? "true" : "false") + ",";
  json += "\"scanInProgress\":" + String(snapshot.scanInProgress ? "true" : "false") + ",";
  json += "\"message\":\"" + EscapeJson(snapshot.message) + "\",";
  json += "\"attemptingSsid\":\"" + EscapeJson(snapshot.attemptingSsid) + "\",";
  json += "\"staSsid\":\"" + EscapeJson(snapshot.staSsid) + "\",";
  json += "\"staIp\":\"" + EscapeJson(snapshot.staIp) + "\",";
  json += "\"apSsid\":\"" + EscapeJson(snapshot.apSsid) + "\",";
  json += "\"apIp\":\"" + EscapeJson(snapshot.apIp) + "\",";
  json += "\"redirectIp\":\"" + EscapeJson(snapshot.redirectIp) + "\",";
  json += "\"handoffRemainingSec\":" + String(snapshot.handoffRemainingSec);
  json += "}";

  server_.send(200, "application/json", json);
}

void WebAdmin::HandleProvisionNetworks() {
  if (!CheckProvisionAccess()) return;

  VisibleWifiList list;
  wifi_runtime_.GetVisibleNetworks(list);

  String json = "{";
  json += "\"scanInProgress\":" + String(list.scanInProgress ? "true" : "false") + ",";
  json += "\"lastUpdatedMs\":" + String(list.lastUpdatedMs) + ",";
  json += "\"networks\":[";
  for (uint8_t i = 0; i < list.count; ++i) {
    if (i > 0) {
      json += ",";
    }

    json += "{";
    json += "\"ssid\":\"" + EscapeJson(list.items[i].ssid) + "\",";
    json += "\"rssi\":" + String(list.items[i].rssi) + ",";
    json += "\"secured\":" + String(list.items[i].secured ? "true" : "false") + ",";
    json += "\"saved\":" + String(list.items[i].saved ? "true" : "false") + ",";
    json += "\"current\":" + String(list.items[i].current ? "true" : "false");
    json += "}";
  }
  json += "]}";

  server_.send(200, "application/json", json);
}

void WebAdmin::HandleProvisionCredentials() {
  if (!CheckProvisionAccess()) return;

  SavedWifiList list;
  wifi_runtime_.GetSavedCredentials(list);

  String json = "{";
  json += "\"credentials\":[";
  for (uint8_t i = 0; i < list.count; ++i) {
    if (i > 0) {
      json += ",";
    }

    json += "{";
    json += "\"ssid\":\"" + EscapeJson(list.items[i].ssid) + "\",";
    json += "\"current\":" + String(list.items[i].current ? "true" : "false");
    json += "}";
  }
  json += "]}";

  server_.send(200, "application/json", json);
}

void WebAdmin::HandleProvisionConnect() {
  if (!CheckProvisionAccess()) return;

  String ssid = server_.arg("ssid");
  String password = server_.arg("password");
  String message;
  const bool success = wifi_runtime_.SubmitCredential(ssid, password, message);

  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + EscapeJson(message) + "\"";
  json += "}";

  server_.send(200, "application/json", json);
}

void WebAdmin::HandleProvisionDelete() {
  if (!CheckProvisionAccess()) return;

  String message;
  const bool success = wifi_runtime_.DeleteCredential(server_.arg("ssid"), message);

  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + EscapeJson(message) + "\"";
  json += "}";

  server_.send(200, "application/json", json);
}

void WebAdmin::HandleProvisionClear() {
  if (!CheckProvisionAccess()) return;

  String message;
  const bool success = wifi_runtime_.ClearCredentials(message);

  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + EscapeJson(message) + "\"";
  json += "}";

  server_.send(200, "application/json", json);
}
