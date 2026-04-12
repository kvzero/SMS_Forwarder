/**
 * @file web_admin.cpp
 * @brief HTTP route handlers for configuration, diagnostics, and modem tools.
 */

#include "web_admin.h"

#include <WiFi.h>

#include "web_pages.h"

WebAdmin::WebAdmin(ConfigStore& config_store, AppConfig& config, Modem& modem,
                   Notifier& notifier, bool& config_valid)
    : config_store_(config_store),
      config_(config),
      modem_(modem),
      notifier_(notifier),
      config_valid_(config_valid),
      server_(80) {}

// HTTP server lifecycle and shared helpers.
void WebAdmin::Begin() {
  server_.on("/", [this]() { HandleRoot(); });
  server_.on("/save", HTTP_POST, [this]() { HandleSave(); });
  server_.on("/tools", [this]() { HandleToolsPage(); });
  server_.on("/sms", [this]() { HandleToolsPage(); });
  server_.on("/sendsms", HTTP_POST, [this]() { HandleSendSms(); });
  server_.on("/ping", HTTP_POST, [this]() { HandlePing(); });
  server_.on("/query", [this]() { HandleQuery(); });
  server_.on("/flight", [this]() { HandleFlightMode(); });
  server_.on("/at", [this]() { HandleATCommand(); });
  server_.begin();
  Serial.println("HTTP server started");
}

void WebAdmin::HandleClient() {
  server_.handleClient();
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
  if (!server_.authenticate(config_.webUser.c_str(), config_.webPass.c_str())) {
    server_.requestAuthentication(BASIC_AUTH, "SMS Forwarding", "请输入管理员账号密码");
    return false;
  }
  return true;
}

// Page rendering.
void WebAdmin::HandleRoot() {
  if (!CheckAuth()) return;

  String html = String(kConfigPageHtml);
  html.replace("%IP%", WiFi.localIP().toString());
  html.replace("%WEB_USER%", config_.webUser);
  html.replace("%WEB_PASS%", config_.webPass);
  html.replace("%SMTP_SERVER%", config_.smtpServer);
  html.replace("%SMTP_PORT%", String(config_.smtpPort));
  html.replace("%SMTP_USER%", config_.smtpUser);
  html.replace("%SMTP_PASS%", config_.smtpPass);
  html.replace("%SMTP_SEND_TO%", config_.smtpSendTo);
  html.replace("%ADMIN_PHONE%", config_.adminPhone);
  html.replace("%NUMBER_BLACK_LIST%", config_.numberBlackList);

  String channels_html;
  // Render the push channel section from the persisted configuration so the UI
  // always reflects the current stored values.
  for (int i = 0; i < kMaxPushChannels; i++) {
    const String idx = String(i);
    const String enabled_class = config_.pushChannels[i].enabled ? " enabled" : "";
    const String checked = config_.pushChannels[i].enabled ? " checked" : "";

    channels_html += "<div class=\"push-channel" + enabled_class + "\" id=\"channel" + idx + "\">";
    channels_html += "<div class=\"push-channel-header\">";
    channels_html += "<input type=\"checkbox\" name=\"push" + idx + "en\" id=\"push" + idx + "en\" onchange=\"toggleChannel(" + idx + ")\"" + checked + ">";
    channels_html += "<label for=\"push" + idx + "en\" class=\"label-inline\">启用推送通道 " + String(i + 1) + "</label>";
    channels_html += "</div>";
    channels_html += "<div class=\"push-channel-body\">";

    channels_html += "<div class=\"form-group\">";
    channels_html += "<label>通道名称</label>";
    channels_html += "<input type=\"text\" name=\"push" + idx + "name\" value=\"" + config_.pushChannels[i].name + "\" placeholder=\"自定义名称\">";
    channels_html += "</div>";

    channels_html += "<div class=\"form-group\">";
    channels_html += "<label>推送方式</label>";
    channels_html += "<select name=\"push" + idx + "type\" id=\"push" + idx + "type\" onchange=\"updateTypeHint(" + idx + ")\">";
    channels_html += "<option value=\"1\"" + String(config_.pushChannels[i].type == PUSH_TYPE_POST_JSON ? " selected" : "") + ">POST JSON（通用格式）</option>";
    channels_html += "<option value=\"2\"" + String(config_.pushChannels[i].type == PUSH_TYPE_BARK ? " selected" : "") + ">Bark（iOS推送）</option>";
    channels_html += "<option value=\"3\"" + String(config_.pushChannels[i].type == PUSH_TYPE_GET ? " selected" : "") + ">GET请求（参数在URL中）</option>";
    channels_html += "<option value=\"4\"" + String(config_.pushChannels[i].type == PUSH_TYPE_DINGTALK ? " selected" : "") + ">钉钉机器人</option>";
    channels_html += "<option value=\"5\"" + String(config_.pushChannels[i].type == PUSH_TYPE_PUSHPLUS ? " selected" : "") + ">PushPlus</option>";
    channels_html += "<option value=\"6\"" + String(config_.pushChannels[i].type == PUSH_TYPE_SERVERCHAN ? " selected" : "") + ">Server酱</option>";
    channels_html += "<option value=\"7\"" + String(config_.pushChannels[i].type == PUSH_TYPE_CUSTOM ? " selected" : "") + ">自定义模板</option>";
    channels_html += "<option value=\"8\"" + String(config_.pushChannels[i].type == PUSH_TYPE_FEISHU ? " selected" : "") + ">飞书机器人</option>";
    channels_html += "<option value=\"9\"" + String(config_.pushChannels[i].type == PUSH_TYPE_GOTIFY ? " selected" : "") + ">Gotify</option>";
    channels_html += "<option value=\"10\"" + String(config_.pushChannels[i].type == PUSH_TYPE_TELEGRAM ? " selected" : "") + ">Telegram Bot</option>";
    channels_html += "</select>";
    channels_html += "<div class=\"push-type-hint\" id=\"hint" + idx + "\"></div>";
    channels_html += "</div>";

    channels_html += "<div class=\"form-group\">";
    channels_html += "<label>推送URL/Webhook</label>";
    channels_html += "<input type=\"text\" name=\"push" + idx + "url\" value=\"" + config_.pushChannels[i].url + "\" placeholder=\"http://your-server.com/api 或 webhook地址\">";
    channels_html += "</div>";

    channels_html += "<div id=\"extra" + idx + "\" style=\"display:none;\">";
    channels_html += "<div class=\"form-group\">";
    channels_html += "<label id=\"key1label" + idx + "\">参数1</label>";
    channels_html += "<input type=\"text\" name=\"push" + idx + "key1\" id=\"key1" + idx + "\" value=\"" + config_.pushChannels[i].key1 + "\">";
    channels_html += "</div>";
    channels_html += "<div class=\"form-group\" id=\"key2group" + idx + "\">";
    channels_html += "<label id=\"key2label" + idx + "\">参数2</label>";
    channels_html += "<input type=\"text\" name=\"push" + idx + "key2\" id=\"key2" + idx + "\" value=\"" + config_.pushChannels[i].key2 + "\">";
    channels_html += "</div>";
    channels_html += "</div>";

    channels_html += "<div id=\"custom" + idx + "\" style=\"display:none;\">";
    channels_html += "<div class=\"form-group\">";
    channels_html += "<label>请求体模板（使用 {sender} {message} {timestamp} 占位符）</label>";
    channels_html += "<textarea name=\"push" + idx + "body\" rows=\"4\" style=\"width:100%;font-family:monospace;\">" + config_.pushChannels[i].customBody + "</textarea>";
    channels_html += "</div>";
    channels_html += "</div>";

    channels_html += "</div></div>";
  }
  html.replace("%PUSH_CHANNELS%", channels_html);

  server_.send(200, "text/html", html);
}

void WebAdmin::HandleToolsPage() {
  if (!CheckAuth()) return;

  String html = String(kToolsPageHtml);
  html.replace("%IP%", WiFi.localIP().toString());
  server_.send(200, "text/html", html);
}

// Modem tools and diagnostic endpoints.
void WebAdmin::HandleFlightMode() {
  if (!CheckAuth()) return;

  String action = server_.arg("action");
  String json = "{";
  bool success = false;
  String message;

  if (action == "query") {
    String resp = modem_.SendAtCommand("AT+CFUN?", 2000);
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
    String resp = modem_.SendAtCommand("AT+CFUN?", 2000);
    Serial.println("CFUN query response: " + resp);

    if (resp.indexOf("+CFUN:") >= 0) {
      const int idx = resp.indexOf("+CFUN:");
      const int current_mode = resp.substring(idx + 6).toInt();

      const int new_mode = (current_mode == 1) ? 4 : 1;
      const String cmd = "AT+CFUN=" + String(new_mode);

      Serial.println("Toggling flight mode with command: " + cmd);
      const String set_resp = modem_.SendAtCommand(cmd.c_str(), 5000);
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
    const String resp = modem_.SendAtCommand("AT+CFUN=4", 5000);
    if (resp.indexOf("OK") >= 0) {
      success = true;
      message = "已开启飞行模式 ✈️";
    } else {
      message = "开启失败: " + resp;
    }
  } else if (action == "off") {
    const String resp = modem_.SendAtCommand("AT+CFUN=1", 5000);
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
  bool success = false;
  String message;

  if (cmd.length() == 0) {
    message = "错误：指令不能为空";
  } else {
    Serial.println("Web requested AT command: " + cmd);
    const String resp = modem_.SendAtCommand(cmd.c_str(), 5000);
    Serial.println("Modem AT response: " + resp);

    if (resp.length() > 0) {
      success = true;
      message = resp;
    } else {
      message = "超时或无响应";
    }
  }

  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + EscapeJson(message) + "\"";
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
    const String resp = modem_.SendAtCommand("ATI", 2000);
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
    const String resp = modem_.SendAtCommand("AT+CESQ", 2000);
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

    String resp = modem_.SendAtCommand("AT+CIMI", 2000);
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

    resp = modem_.SendAtCommand("AT+ICCID", 2000);
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

    resp = modem_.SendAtCommand("AT+CNUM", 2000);
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

    String resp = modem_.SendAtCommand("AT+CEREG?", 2000);
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

    resp = modem_.SendAtCommand("AT+COPS?", 2000);
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

    resp = modem_.SendAtCommand("AT+CGACT?", 2000);
    String pdp_status = "未激活";
    if (resp.indexOf("+CGACT: 1,1") >= 0) {
      pdp_status = "已激活";
    } else if (resp.indexOf("+CGACT:") >= 0) {
      pdp_status = "未激活";
    }
    message += "<tr><td>数据连接</td><td>" + pdp_status + "</td></tr>";

    resp = modem_.SendAtCommand("AT+CGDCONT?", 2000);
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

    success = modem_.SendSms(phone.c_str(), content.c_str());
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

void WebAdmin::HandleSave() {
  if (!CheckAuth()) return;

  String new_web_user = server_.arg("webUser");
  String new_web_pass = server_.arg("webPass");

  if (new_web_user.length() == 0) new_web_user = DEFAULT_WEB_USER;
  if (new_web_pass.length() == 0) new_web_pass = DEFAULT_WEB_PASS;

  config_.webUser = new_web_user;
  config_.webPass = new_web_pass;
  config_.smtpServer = server_.arg("smtpServer");
  config_.smtpPort = server_.arg("smtpPort").toInt();
  if (config_.smtpPort == 0) config_.smtpPort = 465;
  config_.smtpUser = server_.arg("smtpUser");
  config_.smtpPass = server_.arg("smtpPass");
  config_.smtpSendTo = server_.arg("smtpSendTo");
  config_.adminPhone = server_.arg("adminPhone");
  config_.numberBlackList = server_.arg("numberBlackList");

  for (int i = 0; i < kMaxPushChannels; i++) {
    const String idx = String(i);
    config_.pushChannels[i].enabled = server_.arg("push" + idx + "en") == "on";
    config_.pushChannels[i].type = (PushType)server_.arg("push" + idx + "type").toInt();
    config_.pushChannels[i].url = server_.arg("push" + idx + "url");
    config_.pushChannels[i].name = server_.arg("push" + idx + "name");
    config_.pushChannels[i].key1 = server_.arg("push" + idx + "key1");
    config_.pushChannels[i].key2 = server_.arg("push" + idx + "key2");
    config_.pushChannels[i].customBody = server_.arg("push" + idx + "body");
    if (config_.pushChannels[i].name.length() == 0) {
      config_.pushChannels[i].name = "通道" + String(i + 1);
    }
  }

  config_store_.Save(config_);
  config_valid_ = IsConfigValid(config_);

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

  if (config_valid_) {
    Serial.println("Config is valid; sending update notice...");
    String subject = "短信转发器配置已更新";
    String body = "设备配置已更新\n设备地址: " + GetDeviceUrl();
    notifier_.SendEmail(config_, subject.c_str(), body.c_str());
  }
}

void WebAdmin::HandlePing() {
  if (!CheckAuth()) return;

  const ModemPingResult result = modem_.Ping();
  String json = "{";
  json += "\"success\":" + String(result.success ? "true" : "false") + ",";
  json += "\"message\":\"" + EscapeJson(result.message) + "\"";
  json += "}";

  server_.send(200, "application/json", json);
}
