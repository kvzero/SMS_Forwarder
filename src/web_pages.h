/**
 * @file web_pages.h
 * @brief Embedded HTML templates for the admin web interface.
 */
#pragma once

#include "config_store.h"

static const char kConfigPageHtml[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>短信转发配置</title>
  <style>
    :root {
      --primary: #3b82f6;
      --glass-bg: rgba(255, 255, 255, 0.6);
      --glass-border-light: rgba(255, 255, 255, 0.8);
      --glass-border-dark: rgba(255, 255, 255, 0.2);
      --text-main: #1e293b;
      --text-sub: #64748b;
    }

    body { 
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; 
      margin: 0; padding: 20px; 
      background-color: #e2e8f0;
      background-image: 
        radial-gradient(at 10% 10%, rgba(191, 219, 254, 0.8) 0px, transparent 50%),
        radial-gradient(at 90% 10%, rgba(221, 214, 254, 0.8) 0px, transparent 50%),
        radial-gradient(at 50% 90%, rgba(204, 251, 241, 0.8) 0px, transparent 50%);
      background-attachment: fixed;
      color: var(--text-main);
    }

    .container { 
      max-width: 600px; margin: 0 auto; 
      background: var(--glass-bg); 
      backdrop-filter: blur(24px) saturate(120%); 
      -webkit-backdrop-filter: blur(24px) saturate(120%);
      padding: 32px; 
      border-radius: 24px; 
      border-top: 1.5px solid var(--glass-border-light);
      border-left: 1.5px solid var(--glass-border-light);
      border-right: 1px solid var(--glass-border-dark);
      border-bottom: 1px solid var(--glass-border-dark);
      box-shadow: 0 25px 50px -12px rgba(15, 23, 42, 0.15), 0 0 0 1px rgba(255,255,255,0.3) inset; 
    }

    h1 { font-size: 22px; font-weight: 600; text-align: center; margin: 0 0 28px 0; letter-spacing: 0.5px; }
    
    .nav { display: flex; gap: 12px; margin-bottom: 28px; padding: 6px; background: rgba(255,255,255,0.4); border-radius: 16px; box-shadow: inset 0 2px 4px rgba(0,0,0,0.02); border: 1px solid rgba(255,255,255,0.5); }
    .nav a { flex: 1; text-align: center; padding: 10px; border-radius: 12px; text-decoration: none; color: var(--text-sub); font-size: 14px; transition: all 0.2s; font-weight: 500; }
    .nav a.active { background: #ffffff; color: var(--primary); box-shadow: 0 4px 12px rgba(0,0,0,0.05); }

    .status { padding: 14px; background: rgba(59, 130, 246, 0.08); border-radius: 14px; margin-bottom: 24px; font-size: 13px; text-align: center; color: #1e3a8a; border: 1px solid rgba(59, 130, 246, 0.15); }

    .section { 
      background: rgba(255, 255, 255, 0.35); 
      padding: 24px; margin-bottom: 24px; border-radius: 20px; 
      border: 1px solid var(--glass-border-light);
      box-shadow: 0 4px 12px rgba(0,0,0,0.02);
    }
    .section-title { font-size: 15px; font-weight: 600; margin-bottom: 16px; color: var(--text-main); }
    .form-group { margin-bottom: 16px; }
    label { display: block; margin-bottom: 8px; font-size: 12px; font-weight: 500; color: var(--text-sub); }
    
    input, textarea, select { 
      width: 100%; padding: 14px; 
      background: rgba(255, 255, 255, 0.5);
      border: 1px solid rgba(255,255,255,0.8); 
      border-radius: 12px; box-sizing: border-box;
      font-size: 14px; color: var(--text-main);
      box-shadow: inset 0 2px 5px rgba(0,0,0,0.02);
      transition: all 0.2s ease;
    }
    input:focus, textarea:focus { outline: none; background: rgba(255, 255, 255, 0.9); border-color: #93c5fd; box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.1), inset 0 2px 5px rgba(0,0,0,0.01); }
    textarea { resize: vertical; min-height: 96px; }
    
    .warning { padding: 12px 16px; background: rgba(245, 158, 11, 0.1); border-radius: 12px; margin-bottom: 16px; font-size: 12px; color: #b45309; line-height: 1.6; border: 1px solid rgba(245, 158, 11, 0.2); }
    .hint { font-size: 12px; color: var(--text-sub); margin-bottom: 12px; line-height: 1.5; }
    
    button { 
      width: 100%; padding: 16px; 
      background: var(--primary); 
      color: white; border: none; border-radius: 14px; 
      cursor: pointer; font-size: 15px; font-weight: 600;
      box-shadow: 0 8px 16px rgba(59, 130, 246, 0.25), inset 0 1px 1px rgba(255,255,255,0.2);
      transition: all 0.2s; margin-top: 8px;
    }
    button:hover { transform: translateY(-1px); box-shadow: 0 10px 20px rgba(59, 130, 246, 0.3), inset 0 1px 1px rgba(255,255,255,0.2); }
    button:active { transform: translateY(1px); box-shadow: 0 2px 4px rgba(59, 130, 246, 0.2); }

    .push-channel { border: 1px solid var(--glass-border-light); padding: 16px; margin-bottom: 16px; border-radius: 16px; background: rgba(255,255,255,0.2); }
    .push-channel-header { display: flex; align-items: center; margin-bottom: 12px; font-size: 14px; font-weight: 500; }
    .push-channel-header input[type="checkbox"] { width: auto; height: 16px; margin-right: 12px; accent-color: var(--primary); }
    .push-channel-body { display: none; }
    .push-channel.enabled .push-channel-body { display: block; }
    .push-type-hint { font-size: 12px; color: var(--text-sub); margin-top: 10px; padding: 12px; background: rgba(255,255,255,0.4); border-radius: 10px; border: 1px solid rgba(255,255,255,0.5); line-height: 1.5; }
  </style>
</head>
<body>
  <div class="container">
    <h1>短信转发器</h1>
    <div class="nav">
      <a href="/" class="active">系统配置</a>
      <a href="/tools">工具箱</a>
    </div>
    <div class="status" id="status">设备IP: <strong>%IP%</strong></div>
    
    <form action="/save" method="POST">
      <div class="section">
        <div class="section-title">管理账号设置</div>
        <div class="warning">首次使用请修改默认密码！<br>默认账号: )rawliteral" DEFAULT_WEB_USER R"rawliteral(<br>默认密码: )rawliteral" DEFAULT_WEB_PASS R"rawliteral(
        </div>
        <div class="form-group">
          <label>管理账号</label>
          <input type="text" name="webUser" value="%WEB_USER%" placeholder="admin">
        </div>
        <div class="form-group">
          <label>管理密码</label>
          <input type="password" name="webPass" value="%WEB_PASS%" placeholder="请设置复杂密码">
        </div>
      </div>
      
      <div class="section">
        <div class="section-title">邮件通知设置</div>
        <div class="form-group">
          <label>SMTP服务器</label>
          <input type="text" name="smtpServer" value="%SMTP_SERVER%" placeholder="smtp.qq.com">
        </div>
        <div class="form-group">
          <label>SMTP端口</label>
          <input type="number" name="smtpPort" value="%SMTP_PORT%" placeholder="465">
        </div>
        <div class="form-group">
          <label>邮箱账号</label>
          <input type="text" name="smtpUser" value="%SMTP_USER%" placeholder="your@qq.com">
        </div>
        <div class="form-group">
          <label>邮箱密码/授权码</label>
          <input type="password" name="smtpPass" value="%SMTP_PASS%" placeholder="授权码">
        </div>
        <div class="form-group">
          <label>接收邮件地址</label>
          <input type="text" name="smtpSendTo" value="%SMTP_SEND_TO%" placeholder="receiver@example.com">
        </div>
      </div>
      
      <div class="section">
        <div class="section-title">HTTP 推送通道设置</div>
        <div class="hint" style="margin-bottom:15px;">支持 POST JSON、Bark、GET、钉钉、PushPlus、Server酱等多种方式。</div>
        %PUSH_CHANNELS%
      </div>
      
      <div class="section">
        <div class="section-title">管理员设置</div>
        <div class="form-group">
          <label>管理员手机号</label>
          <input type="text" name="adminPhone" value="%ADMIN_PHONE%" placeholder="请输入发送命令的完整手机号">
        </div>
      </div>
      
      <div class="section">
        <div class="section-title">号码黑名单</div>
        <div class="hint" style="margin-bottom:15px;">每行一个号码，来自黑名单的短信将被忽略。</div>
        <div class="form-group">
          <label>黑名单号码</label>
          <textarea name="numberBlackList" rows="5">%NUMBER_BLACK_LIST%</textarea>
        </div>
      </div>
      
      <button type="submit">保存配置</button>
    </form>
  </div>
  <script>
    function toggleChannel(idx) {
      var ch = document.getElementById('channel' + idx);
      var cb = document.getElementById('push' + idx + 'en');
      if (cb.checked) {
        ch.classList.add('enabled');
      } else {
        ch.classList.remove('enabled');
      }
    }
    function updateTypeHint(idx) {
      var sel = document.getElementById('push' + idx + 'type');
      var hint = document.getElementById('hint' + idx);
      var extraFields = document.getElementById('extra' + idx);
      var customFields = document.getElementById('custom' + idx);
      var type = parseInt(sel.value);
      
      extraFields.style.display = 'none';
      customFields.style.display = 'none';
      document.getElementById('key1label' + idx).innerText = '参数1';
      document.getElementById('key2label' + idx).innerText = '参数2';
      document.getElementById('key1' + idx).placeholder = '';
      document.getElementById('key2' + idx).placeholder = '';
      document.getElementById('key2' + idx).closest('.form-group').style.display = 'none';
      
      if (type == 1) {
        hint.innerHTML = '<b>POST JSON格式：</b><br>{"sender":"发送者号码","message":"短信内容","timestamp":"时间戳"}';
      } else if (type == 2) {
        hint.innerHTML = '<b>Bark格式：</b><br>POST {"title":"发送者号码","body":"短信内容"}';
      } else if (type == 3) {
        hint.innerHTML = '<b>GET请求格式：</b><br>URL?sender=xxx&message=xxx&timestamp=xxx';
      } else if (type == 4) {
        hint.innerHTML = '<b>钉钉机器人：</b><br>填写Webhook地址，如需加签请填Secret';
        extraFields.style.display = 'block';
        document.getElementById('key1label' + idx).innerText = 'Secret（加签密钥，可选）';
        document.getElementById('key1' + idx).placeholder = 'SEC...';
      } else if (type == 5) {
        hint.innerHTML = '<b>PushPlus：</b><br>填写Token，URL留空使用默认';
        extraFields.style.display = 'block';
        document.getElementById('key1label' + idx).innerText = 'Token';
        document.getElementById('key1' + idx).placeholder = 'pushplus的token';
        document.getElementById('key2' + idx).closest('.form-group').style.display = 'block';
        document.getElementById('key2label' + idx).innerText = '发送渠道';
        document.getElementById('key2' + idx).placeholder = 'wechat(default), extension, app';
      } else if (type == 6) {
        hint.innerHTML = '<b>Server酱：</b><br>填写SendKey，URL留空使用默认';
        extraFields.style.display = 'block';
        document.getElementById('key1label' + idx).innerText = 'SendKey';
        document.getElementById('key1' + idx).placeholder = 'SCT...';
      } else if (type == 7) {
        hint.innerHTML = '<b>自定义模板：</b><br>在请求体模板中使用 {sender} {message} {timestamp} 作为占位符';
        customFields.style.display = 'block';
      } else if (type == 8) {
        hint.innerHTML = '<b>飞书机器人：</b><br>填写Webhook地址，如需签名验证请填Secret';
        extraFields.style.display = 'block';
        document.getElementById('key1label' + idx).innerText = 'Secret（签名密钥，可选）';
        document.getElementById('key1' + idx).placeholder = '飞书机器人的签名密钥';
      } else if (type == 9) {
        hint.innerHTML = '<b>Gotify：</b><br>填写服务器地址（如 http://gotify.example.com），Token填写应用Token';
        extraFields.style.display = 'block';
        document.getElementById('key1label' + idx).innerText = 'Token（应用Token）';
        document.getElementById('key1' + idx).placeholder = 'A...';
      } else if (type == 10) {
        hint.innerHTML = '<b>Telegram Bot：</b><br>填写Chat ID（参数1）和Bot Token（参数2），URL留空默认使用官方API';
        extraFields.style.display = 'block';
        document.getElementById('key1label' + idx).innerText = 'Chat ID';
        document.getElementById('key1' + idx).placeholder = '123456789';
        document.getElementById('key2label' + idx).innerText = 'Bot Token';
        document.getElementById('key2' + idx).placeholder = '12345678:ABC...';
      }
    }
    document.addEventListener('DOMContentLoaded', function() {
      for (var i = 0; i < 5; i++) {
        toggleChannel(i);
        updateTypeHint(i);
      }
    });
  </script>
</body>
</html>
)rawliteral";

static const char kToolsPageHtml[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>工具箱</title>
  <style>
    :root {
      --primary: #3b82f6;
      --glass-bg: rgba(255, 255, 255, 0.6);
      --glass-border-light: rgba(255, 255, 255, 0.8);
      --glass-border-dark: rgba(255, 255, 255, 0.2);
      --text-main: #1e293b;
      --text-sub: #64748b;
    }

    body { 
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; 
      margin: 0; padding: 20px; 
      background-color: #e2e8f0;
      background-image: 
        radial-gradient(at 10% 10%, rgba(191, 219, 254, 0.8) 0px, transparent 50%),
        radial-gradient(at 90% 10%, rgba(221, 214, 254, 0.8) 0px, transparent 50%),
        radial-gradient(at 50% 90%, rgba(204, 251, 241, 0.8) 0px, transparent 50%);
      background-attachment: fixed;
      color: var(--text-main);
    }

    .container { 
      max-width: 600px; margin: 0 auto; 
      background: var(--glass-bg); 
      backdrop-filter: blur(24px) saturate(120%); 
      -webkit-backdrop-filter: blur(24px) saturate(120%);
      padding: 32px; 
      border-radius: 24px; 
      border-top: 1.5px solid var(--glass-border-light);
      border-left: 1.5px solid var(--glass-border-light);
      border-right: 1px solid var(--glass-border-dark);
      border-bottom: 1px solid var(--glass-border-dark);
      box-shadow: 0 25px 50px -12px rgba(15, 23, 42, 0.15), 0 0 0 1px rgba(255,255,255,0.3) inset; 
    }

    h1 { font-size: 22px; font-weight: 600; text-align: center; margin: 0 0 28px 0; letter-spacing: 0.5px; }
    
    .nav { display: flex; gap: 12px; margin-bottom: 28px; padding: 6px; background: rgba(255,255,255,0.4); border-radius: 16px; box-shadow: inset 0 2px 4px rgba(0,0,0,0.02); border: 1px solid rgba(255,255,255,0.5); }
    .nav a { flex: 1; text-align: center; padding: 10px; border-radius: 12px; text-decoration: none; color: var(--text-sub); font-size: 14px; transition: all 0.2s; font-weight: 500; }
    .nav a.active { background: #ffffff; color: var(--primary); box-shadow: 0 4px 12px rgba(0,0,0,0.05); }

    .status { padding: 14px; background: rgba(59, 130, 246, 0.08); border-radius: 14px; margin-bottom: 24px; font-size: 13px; text-align: center; color: #1e3a8a; border: 1px solid rgba(59, 130, 246, 0.15); }

    .section { 
      background: rgba(255, 255, 255, 0.35); 
      padding: 24px; margin-bottom: 24px; border-radius: 20px; 
      border: 1px solid var(--glass-border-light);
      box-shadow: 0 4px 12px rgba(0,0,0,0.02);
    }
    .section-title { font-size: 15px; font-weight: 600; margin-bottom: 16px; color: var(--text-main); }
    .form-group { margin-bottom: 16px; }
    label { display: block; margin-bottom: 8px; font-size: 12px; font-weight: 500; color: var(--text-sub); }
    
    input, textarea { 
      width: 100%; padding: 14px; 
      background: rgba(255, 255, 255, 0.5);
      border: 1px solid rgba(255,255,255,0.8); 
      border-radius: 12px; box-sizing: border-box;
      font-size: 14px; color: var(--text-main);
      box-shadow: inset 0 2px 5px rgba(0,0,0,0.02);
      transition: all 0.2s ease;
    }
    input:focus, textarea:focus { outline: none; background: rgba(255, 255, 255, 0.9); border-color: #93c5fd; box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.1), inset 0 2px 5px rgba(0,0,0,0.01); }
    textarea { resize: vertical; min-height: 110px; }
    
    .char-count { font-size: 12px; color: var(--text-sub); text-align: right; margin-top: 5px; }
    .hint { font-size: 12px; color: var(--text-sub); margin-bottom: 12px; line-height: 1.5; }
    
    button { 
      width: 100%; padding: 14px; 
      background: var(--primary); 
      color: white; border: none; border-radius: 12px; 
      cursor: pointer; font-size: 14px; font-weight: 600;
      box-shadow: 0 8px 16px rgba(59, 130, 246, 0.25), inset 0 1px 1px rgba(255,255,255,0.2);
      transition: all 0.2s; margin-top: 8px;
    }
    button:hover { transform: translateY(-1px); box-shadow: 0 10px 20px rgba(59, 130, 246, 0.3), inset 0 1px 1px rgba(255,255,255,0.2); }
    button:active { transform: translateY(1px); box-shadow: 0 2px 4px rgba(59, 130, 246, 0.2); }
    button:disabled { background: #94a3b8; box-shadow: none; cursor: not-allowed; transform: none; }

    .btn-group { display: flex; gap: 12px; margin-bottom: 12px; flex-wrap: wrap; }
    .btn-group button { flex: 1; margin-top: 0; min-width: 120px; }
    .btn-secondary {
      background: rgba(255, 255, 255, 0.4);
      color: var(--text-main);
      border: 1px solid rgba(255, 255, 255, 0.6);
      box-shadow: 0 4px 6px rgba(0,0,0,0.02);
    }
    .btn-secondary:hover {
      background: rgba(255, 255, 255, 0.7);
      border-color: rgba(255, 255, 255, 0.9);
      transform: translateY(-1px);
    }
    .btn-danger {
      background: rgba(239, 68, 68, 0.15);
      color: #b91c1c;
      border: 1px solid rgba(239, 68, 68, 0.3);
    }
    .btn-danger:hover {
      background: rgba(239, 68, 68, 0.25);
    }
    
    .result-box { margin-top: 15px; padding: 15px; border-radius: 12px; display: none; font-size: 13px; line-height: 1.6; background: rgba(255,255,255,0.4); border: 1px solid rgba(255,255,255,0.6); box-shadow: inset 0 2px 5px rgba(0,0,0,0.02); }
    .result-success { background: rgba(187, 247, 208, 0.6); border-left: 4px solid #22c55e; color: #166534; }
    .result-error { background: rgba(254, 202, 202, 0.6); border-left: 4px solid #ef4444; color: #991b1b; }
    .result-loading { background: rgba(254, 240, 138, 0.6); border-left: 4px solid #eab308; color: #854d0e; }
    .result-info { background: rgba(191, 219, 254, 0.6); border-left: 4px solid #3b82f6; color: #1e40af; }
    
    #atLog { background: rgba(15, 23, 42, 0.8); color: #4ade80; font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; min-height: 160px; max-height: 300px; overflow-y: auto; padding: 16px; border-radius: 14px; margin-bottom: 12px; font-size: 13px; line-height: 1.6; border: 1px solid rgba(255,255,255,0.1); word-break: break-all; white-space: pre-wrap; }
    .at-input-group { display: flex; gap: 12px; }
    .at-input-group input { flex: 1; font-family: monospace; }
    .at-input-group button { width: auto; min-width: 90px; margin-top: 0; }
  </style>
</head>
<body>
  <div class="container">
    <h1>短信转发器</h1>
    <div class="nav">
      <a href="/">系统配置</a>
      <a href="/tools" class="active">工具箱</a>
    </div>
    <div class="status" id="status">设备IP: <strong>%IP%</strong></div>
    
    <form action="/sendsms" method="POST">
      <div class="section">
        <div class="section-title">发送短信</div>
        <div class="form-group">
          <label>目标号码</label>
          <input type="text" name="phone" placeholder="请输入接收短信的完整手机号" required>
        </div>
        <div class="form-group">
          <label>短信内容</label>
          <textarea name="content" placeholder="请输入短信内容..." required oninput="updateCount(this)"></textarea>
          <div class="char-count">已输入 <span id="charCount">0</span> 字符</div>
        </div>
        <button type="submit">发送短信</button>
      </div>
    </form>
    
<div class="section">
      <div class="section-title">模组信息查询</div>
      <div class="btn-group">
        <button type="button" class="btn-secondary" onclick="queryInfo('ati')">固件信息</button>
        <button type="button" class="btn-secondary" onclick="queryInfo('signal')">信号质量</button>
      </div>
      <div class="btn-group">
        <button type="button" class="btn-secondary" onclick="queryInfo('siminfo')">SIM卡信息</button>
        <button type="button" class="btn-secondary" onclick="queryInfo('network')">网络状态</button>
      </div>
      <div class="btn-group">
        <button type="button" class="btn-secondary" onclick="queryInfo('wifi')">WiFi状态</button>
      </div>
      <div class="result-box" id="queryResult"></div>
    </div>
    
    <div class="section">
      <div class="section-title">模组控制</div>
      <div class="btn-group">
        <button type="button" id="flightBtn" class="btn-danger" onclick="toggleFlightMode()">切换飞行模式</button>
        <button type="button" class="btn-secondary" onclick="queryFlightMode()">查询状态</button>
      </div>
      <div class="hint">
        开启后将关闭模组蜂窝射频，SIM 不会驻网，无法收发短信。<br>
        该操作不影响设备自身的 Wi‑Fi 管理页面。
      </div>
      <div class="result-box" id="flightResult"></div>
    </div>

    <div class="section">
      <div class="section-title">AT 指令调试</div>
      <div id="atLog">等待输入指令...</div>
      <div class="at-input-group">
        <input type="text" id="atCmd" placeholder="输入 AT 指令，如: AT+CSQ">
        <button type="button" onclick="sendAT()" id="atBtn">发送</button>
      </div>
      <div class="btn-group" style="margin-top:12px;">
        <button type="button" class="btn-secondary" onclick="clearATLog()">清空日志</button>
      </div>
      <div class="hint">直接向模组串口发送指令并接收响应，请谨慎操作</div>
    </div>
  </div>
  <script>
    function updateCount(el) {
      document.getElementById('charCount').textContent = el.value.length;
    }
    
    function queryInfo(type) {
      var result = document.getElementById('queryResult');
      result.className = 'result-box result-loading';
      result.style.display = 'block';
      result.textContent = '正在查询，请稍候...';
      
      fetch('/query?type=' + type)
        .then(response => response.json())
        .then(data => {
          if (data.success) {
            result.className = 'result-box result-info';
            result.innerHTML = data.message;
          } else {
            result.className = 'result-box result-error';
            result.innerHTML = '❌ 查询失败<br>' + data.message;
          }
        })
        .catch(error => {
          result.className = 'result-box result-error';
          result.textContent = '❌ 请求失败: ' + error;
        });
    }

    function pollModemResult(requestId, onReady, onError, attempt = 0) {
      if (attempt > 80) {
        onError('请求超时。');
        return;
      }

      fetch('/modem_result?id=' + requestId)
        .then(response => response.json())
        .then(data => {
          if (data.ready) {
            onReady(data);
            return;
          }

          setTimeout(function() {
            pollModemResult(requestId, onReady, onError, attempt + 1);
          }, 500);
        })
        .catch(error => {
          onError(error);
        });
    }
    function queryFlightMode() {
      var result = document.getElementById('flightResult');
      result.className = 'result-box result-loading';
      result.style.display = 'block';
      result.textContent = '正在查询飞行模式状态...';
      
      fetch('/flight?action=query')
        .then(response => response.json())
        .then(data => {
          if (data.success) {
            result.className = 'result-box result-info';
            result.innerHTML = data.message;
          } else {
            result.className = 'result-box result-error';
            result.innerHTML = '❌ 查询失败: ' + data.message;
          }
        })
        .catch(error => {
          result.className = 'result-box result-error';
          result.textContent = '❌ 请求失败: ' + error;
        });
    }
    
    function toggleFlightMode() {
      if (!confirm('确定要切换飞行模式吗？\n\n开启飞行模式后模组将无法收发短信。')) return;
      
      var btn = document.getElementById('flightBtn');
      var result = document.getElementById('flightResult');
      btn.disabled = true;
      result.className = 'result-box result-loading';
      result.style.display = 'block';
      result.textContent = '正在切换飞行模式...';
      
      fetch('/flight?action=toggle')
        .then(response => response.json())
        .then(data => {
          btn.disabled = false;
          if (data.success) {
            result.className = 'result-box result-success';
            result.innerHTML = '✅ ' + data.message;
          } else {
            result.className = 'result-box result-error';
            result.innerHTML = '❌ 切换失败: ' + data.message;
          }
        })
        .catch(error => {
          btn.disabled = false;
          result.className = 'result-box result-error';
          result.textContent = '❌ 请求失败: ' + error;
        });
    }

    function addLog(msg, type = 'resp') {
      var log = document.getElementById('atLog');
      var div = document.createElement('div');
      var b = document.createElement('b');
      
      if (type === 'user') {
        b.style.color = '#fff';
        b.textContent = '> ';
      } else if (type === 'error') {
        b.style.color = '#f87171';
        b.textContent = '❌ ';
      } else {
        b.style.color = '#4ade80';
        b.textContent = '[RESP] ';
      }
      
      div.appendChild(b);
      var textNode = document.createTextNode(msg);
      div.appendChild(textNode);
      
      log.appendChild(div);
      log.scrollTop = log.scrollHeight;
    }

    function sendAT() {
      var input = document.getElementById('atCmd');
      var cmd = input.value.trim();
      if (!cmd) return;
      
      var btn = document.getElementById('atBtn');
      btn.disabled = true;
      btn.textContent = '...';
      
      addLog(cmd, 'user');
      input.value = '';
      
      fetch('/at?cmd=' + encodeURIComponent(cmd))
        .then(response => response.json())
        .then(data => {
          if (!data.accepted) {
            throw new Error(data.message || '提交 AT 指令失败。');
          }

          pollModemResult(
            data.requestId,
            function(job) {
              if (job.success) {
                addLog(job.message);
              } else {
                addLog(job.message, 'error');
              }
            },
            function(error) {
              addLog('网络错误: ' + error, 'error');
            }
          );
        })
        .catch(error => {
          addLog('网络错误: ' + error, 'error');
        })
        .finally(() => {
          btn.disabled = false;
          btn.textContent = '发送';
        });
    }

    function clearATLog() {
      document.getElementById('atLog').innerHTML = '';
    }
    document.getElementById('atCmd').addEventListener('keydown', function(event) {
      if (event.key === 'Enter') {
        sendAT();
      }
    });
  </script>
</body>
</html>
)rawliteral";
