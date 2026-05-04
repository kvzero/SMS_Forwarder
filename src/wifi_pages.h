/**
 * @file wifi_pages.h
 * @brief Embedded HTML templates for Wi-Fi provisioning.
 */

#pragma once

static const char kProvisionPageHtml[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Wi-Fi 配网</title>
  <style>
    :root {
      --primary: #3b82f6;
      --primary-strong: #1f7ae0;
      --text-main: #1e293b;
      --text-sub: #64748b;
      --glass-bg: rgba(255, 255, 255, 0.56);
      --glass-border-light: rgba(255, 255, 255, 0.78);
      --glass-border-dark: rgba(148, 163, 184, 0.14);
    }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      margin: 0;
      padding: 16px;
      background-color: #e2e8f0;
      background-image:
        radial-gradient(at 10% 10%, rgba(191, 219, 254, 0.82) 0px, transparent 50%),
        radial-gradient(at 90% 10%, rgba(221, 214, 254, 0.78) 0px, transparent 48%),
        radial-gradient(at 50% 90%, rgba(204, 251, 241, 0.72) 0px, transparent 52%);
      background-attachment: fixed;
      color: var(--text-main);
    }
    .container {
      max-width: 760px;
      margin: 0 auto;
    }
    .hero {
      background: linear-gradient(135deg, #1f7ae0, #38b6ff 58%, #7dd3fc);
      color: white;
      padding: 22px;
      border-radius: 20px;
      margin-bottom: 18px;
      border-top: 1px solid rgba(255, 255, 255, 0.35);
      border-left: 1px solid rgba(255, 255, 255, 0.22);
      box-shadow: 0 20px 36px rgba(31, 122, 224, 0.22);
    }
    .hero h1 {
      margin: 0 0 8px;
      font-size: 28px;
    }
    .hero p {
      margin: 0;
      line-height: 1.5;
      opacity: 0.96;
      max-width: 54ch;
    }
    .card {
      background: var(--glass-bg);
      backdrop-filter: blur(22px) saturate(120%);
      -webkit-backdrop-filter: blur(22px) saturate(120%);
      border-radius: 18px;
      padding: 16px;
      margin-bottom: 16px;
      border-top: 1px solid var(--glass-border-light);
      border-left: 1px solid var(--glass-border-light);
      border-right: 1px solid var(--glass-border-dark);
      border-bottom: 1px solid var(--glass-border-dark);
      box-shadow: 0 12px 28px rgba(15, 23, 42, 0.08);
    }
    .section-title {
      font-size: 18px;
      font-weight: 600;
      color: var(--text-main);
    }
    .section-head {
      display: flex;
      align-items: center;
      gap: 8px;
      margin-bottom: 12px;
    }
    .scan-indicator {
      width: 14px;
      height: 14px;
      border: 2px solid rgba(59, 130, 246, 0.18);
      border-top-color: var(--primary-strong);
      border-radius: 50%;
      opacity: 0;
      transition: opacity 0.2s ease;
      animation: scan-spin 0.9s linear infinite;
    }
    .scan-indicator.active {
      opacity: 1;
    }
    .status-box {
      border-left: 4px solid var(--primary-strong);
      background: rgba(59, 130, 246, 0.08);
      padding: 13px 14px;
      border-radius: 12px;
      line-height: 1.6;
      border: 1px solid rgba(59, 130, 246, 0.12);
    }
    .status-box.warn {
      border-left-color: #d97706;
      background: rgba(245, 158, 11, 0.09);
      border-color: rgba(245, 158, 11, 0.16);
    }
    .handoff {
      display: none;
      margin-top: 12px;
      border-left: 4px solid #10b981;
      background: rgba(16, 185, 129, 0.09);
      padding: 13px 14px;
      border-radius: 12px;
      line-height: 1.6;
      border: 1px solid rgba(16, 185, 129, 0.14);
    }
    .network-list,
    .credential-list {
      display: grid;
      gap: 10px;
    }
    .network-item,
    .credential-item {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      padding: 13px 14px;
      border: 1px solid rgba(255, 255, 255, 0.72);
      border-radius: 14px;
      background: rgba(255, 255, 255, 0.48);
      box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.35);
      width: 100%; box-sizing: border-box; min-width: 0;
    }
    .network-meta,
    .credential-meta {
      flex: 1;
      min-width: 0;
    }
    .network-name,
    .credential-name {
      font-weight: 600;
      color: var(--text-main);
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .network-extra,
    .credential-extra {
      font-size: 12px;
      color: var(--text-sub);
      margin-top: 5px;
      line-height: 1.45;
    }
    .actions {
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
    }
    button {
      border: none;
      border-radius: 12px;
      padding: 10px 14px;
      cursor: pointer;
      font-size: 14px;
      font-weight: 600;
      transition: transform 0.18s ease, box-shadow 0.18s ease,
                  background-color 0.18s ease;
    }
    button.primary {
      background: var(--primary-strong);
      color: white;
      box-shadow: 0 8px 16px rgba(31, 122, 224, 0.22);
    }
    button.primary:hover {
      transform: translateY(-1px);
      box-shadow: 0 10px 18px rgba(31, 122, 224, 0.26);
    }
    button.danger {
      background: rgba(239, 68, 68, 0.13);
      color: #b91c1c;
      border: 1px solid rgba(239, 68, 68, 0.22);
    }
    button.danger:hover {
      background: rgba(239, 68, 68, 0.2);
    }
    button:disabled {
      opacity: 0.6;
      cursor: not-allowed;
      transform: none;
      box-shadow: none;
    }
    .hint {
      font-size: 12px;
      color: var(--text-sub);
      line-height: 1.6;
    }
    .toolbar {
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
      margin-top: 12px;
    }
    .empty {
      color: var(--text-sub);
      font-size: 14px;
      text-align: center;
      padding: 6px 0;
    }
    .badge {
      display: inline-block;
      font-size: 11px;
      border-radius: 999px;
      padding: 3px 9px;
      margin-left: 6px;
      background: rgba(59, 130, 246, 0.12);
      color: var(--primary-strong);
      border: 1px solid rgba(59, 130, 246, 0.16);
    }
    a.link {
      color: #0f5fc6;
      word-break: break-all;
      text-decoration: none;
      font-weight: 500;
    }
    a.link:hover {
      text-decoration: underline;
    }
    @media (max-width: 640px) {
      body {
        padding: 14px;
      }
      .hero {
        padding: 18px;
      }
      .hero h1 {
        font-size: 24px;
      }
    }
    @keyframes scan-spin {
      from { transform: rotate(0deg); }
      to { transform: rotate(360deg); }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="hero">
      <h1>Wi-Fi 配网</h1>
      <p>设备会优先尝试已保存的网络。若无法联网，可在下方选择附近 Wi-Fi 并输入密码完成配网。</p>
    </div>

    <div class="card">
      <div class="section-title">当前状态</div>
      <div id="statusBox" class="status-box">正在读取设备状态...</div>
      <div id="handoffBox" class="handoff"></div>
    </div>

    <div id="networkCard" class="card">
      <div class="section-head">
        <div class="section-title">附近 Wi-Fi</div>
        <div id="scanIndicator" class="scan-indicator" aria-hidden="true"></div>
      </div>
      <div class="hint">列表会自动刷新。点击某个网络后输入密码即可发起连接。</div>
      <div id="networkList" class="network-list" style="margin-top:12px;"></div>
    </div>

    <div id="credentialCard" class="card">
      <div class="section-title">已保存网络</div>
      <div class="hint">设备会优先尝试列表靠前的网络，最近一次成功联网的网络会自动置顶。</div>
      <div id="credentialList" class="credential-list" style="margin-top:12px;"></div>
      <div class="toolbar">
        <button class="danger" type="button" onclick="clearCredentials()">清空全部记录</button>
      </div>
    </div>
  </div>

  <script>
    var kStatusRequestTimeoutMs = 2000;
    var kStatusDisconnectThresholdMs = 3000;
    var kScanDisconnectThresholdMs = 3000;
    var kHandoffDisconnectThresholdMs = 5500;
    var kBackgroundRequestTimeoutMs = 2000;
    var kStatusFailureThreshold = 3;
    var kScanFailureThreshold = 4;

    var uiState = {
      deviceReachable: true,
      statusFailures: 0,
      lastStatus: null,
      lastStatusSuccessAt: 0,
      statusRequestInFlight: false,
      postConnectRefreshQueued: false
    };

    function encodeForm(data) {
      return Object.keys(data).map(function(key) {
        return encodeURIComponent(key) + '=' + encodeURIComponent(data[key]);
      }).join('&');
    }

    function makeTimeoutError() {
      var error = new Error('timeout');
      error.name = 'TimeoutError';
      return error;
    }

    function fetchJson(url, options) {
      var requestOptions = Object.assign({ cache: 'no-store' }, options || {});
      var timeoutMs = requestOptions.timeoutMs || 0;
      delete requestOptions.timeoutMs;

      if (!timeoutMs) {
        return fetch(url, requestOptions).then(function(response) {
          return response.json();
        });
      }

      return new Promise(function(resolve, reject) {
        var controller = (typeof AbortController !== 'undefined')
          ? new AbortController()
          : null;
        var settled = false;
        var timeoutHandle = setTimeout(function() {
          if (controller) {
            controller.abort();
          }
          if (settled) {
            return;
          }
          settled = true;
          reject(makeTimeoutError());
        }, timeoutMs);

        if (controller) {
          requestOptions.signal = controller.signal;
        }

        fetch(url, requestOptions)
          .then(function(response) {
            if (settled) {
              return;
            }
            settled = true;
            clearTimeout(timeoutHandle);
            resolve(response.json());
          })
          .catch(function(error) {
            if (settled) {
              return;
            }
            settled = true;
            clearTimeout(timeoutHandle);
            reject(error);
          });
      });
    }

    function postForm(url, data) {
      return fetchJson(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: encodeForm(data)
      });
    }

    function modeText(mode) {
      if (mode === 'trying') return '正在尝试连接历史网络';
      if (mode === 'connected') return '已连接';
      if (mode === 'handoff') return '已连接，等待退出配网模式';
      return '配网模式';
    }

    function setScanIndicator(active) {
      var indicator = document.getElementById('scanIndicator');
      indicator.classList.toggle('active', !!active);
    }

    function setInteractiveCardsVisible(visible) {
      document.getElementById('networkCard').style.display = visible ? '' : 'none';
      document.getElementById('credentialCard').style.display = visible ? '' : 'none';
    }

    function shouldPauseBackgroundRefresh() {
      return !!(uiState.lastStatus && uiState.lastStatus.connectInProgress);
    }

    function queuePostConnectRefresh() {
      if (uiState.postConnectRefreshQueued) {
        return;
      }

      uiState.postConnectRefreshQueued = true;
      setTimeout(function() {
        uiState.postConnectRefreshQueued = false;
        refreshNetworks();
        refreshSavedCredentials();
      }, 50);
    }

    function renderStatus(data) {
      var previousStatus = uiState.lastStatus;
      var connectionFinished =
        !!(previousStatus && previousStatus.connectInProgress && !data.connectInProgress);
      var staChanged =
        !!(data.staConnected &&
           (!previousStatus ||
            !previousStatus.staConnected ||
            previousStatus.staSsid !== data.staSsid));
      var enteredStableMode =
        !!(previousStatus &&
           previousStatus.mode !== data.mode &&
           (data.mode === 'handoff' || data.mode === 'connected'));

      uiState.deviceReachable = true;
      uiState.statusFailures = 0;
      uiState.lastStatus = data;
      uiState.lastStatusSuccessAt = Date.now();

      var statusBox = document.getElementById('statusBox');
      var lines = [];
      lines.push('状态：' + modeText(data.mode));
      if (data.message) lines.push(escapeHtml(data.message));
      if (data.portalActive) lines.push('设备热点：' + escapeHtml(data.apSsid) + '（' + escapeHtml(data.apIp) + '）');
      if (data.connectInProgress && data.attemptingSsid) lines.push('当前尝试：' + escapeHtml(data.attemptingSsid));
      if (data.staConnected) lines.push('当前联网：' + escapeHtml(data.staSsid) + '（' + escapeHtml(data.staIp) + '）');

      statusBox.className = 'status-box';
      statusBox.innerHTML = lines.join('<br>');
      setScanIndicator(data.scanInProgress);
      setInteractiveCardsVisible(true);

      var handoffBox = document.getElementById('handoffBox');
      if (data.mode === 'handoff') {
        var connectedSsid = escapeHtml(data.staSsid || '');
        handoffBox.style.display = 'block';
        handoffBox.innerHTML =
          '设备已连接到 <b>' + connectedSsid + '</b>，请将手机切回该局域网 Wi-Fi。' +
          '<br>新的设备地址：<a class="link" href="http://' + data.redirectIp + '/">http://' + data.redirectIp + '/</a>' +
          '<br>热点将在 ' + data.handoffRemainingSec + ' 秒后自动关闭。';
      } else {
        handoffBox.style.display = 'none';
        handoffBox.innerHTML = '';
      }

      if ((connectionFinished && data.staConnected) || staChanged || enteredStableMode) {
        queuePostConnectRefresh();
      }
    }

    function renderDisconnectedState() {
      uiState.deviceReachable = false;
      setScanIndicator(false);
      setInteractiveCardsVisible(false);

      var statusBox = document.getElementById('statusBox');
      var handoffBox = document.getElementById('handoffBox');
      statusBox.className = 'status-box warn';

      if (uiState.lastStatus && uiState.lastStatus.mode === 'handoff' && uiState.lastStatus.redirectIp) {
        statusBox.innerHTML = '设备热点可能已关闭，请将手机切回目标局域网 Wi-Fi 后继续访问。';
        handoffBox.style.display = 'block';
        handoffBox.innerHTML =
          '如果设备已经连上新网络，请在切回同一局域网后访问：' +
          '<br><a class="link" href="http://' + uiState.lastStatus.redirectIp + '/">http://' + uiState.lastStatus.redirectIp + '/</a>';
        return;
      }

      statusBox.innerHTML = '已与设备断开连接，请重新连接设备热点后继续配网。';
      handoffBox.style.display = 'none';
      handoffBox.innerHTML = '';
    }

    function handleStatusFailure() {
      uiState.statusFailures += 1;
      var lastSuccessAt = uiState.lastStatusSuccessAt || 0;
      var staleForMs = lastSuccessAt ? (Date.now() - lastSuccessAt) : 0;
      var inHandoff = !!(uiState.lastStatus && uiState.lastStatus.mode === 'handoff');
      var scanInProgress = !!(uiState.lastStatus && uiState.lastStatus.scanInProgress);
      var disconnectThresholdMs = kStatusDisconnectThresholdMs;
      var failureThreshold = kStatusFailureThreshold;

      if (inHandoff) {
        disconnectThresholdMs = kHandoffDisconnectThresholdMs;
        failureThreshold = 1;
      } else if (scanInProgress) {
        disconnectThresholdMs = kScanDisconnectThresholdMs;
        failureThreshold = kScanFailureThreshold;
      }

      if (uiState.statusFailures >= failureThreshold &&
          staleForMs >= disconnectThresholdMs) {
        renderDisconnectedState();
      }
    }

    function signalText(rssi) {
      if (rssi >= -55) return '信号极好';
      if (rssi >= -65) return '信号良好';
      if (rssi >= -75) return '信号一般';
      return '信号较弱';
    }

    function escapeHtml(value) {
      return String(value || '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
    }

    function renderNetworks(items) {
      var list = document.getElementById('networkList');
      list.innerHTML = '';

      if (!uiState.deviceReachable) {
        return;
      }

      if (!items.length) {
        list.innerHTML = '<div class="empty">暂无扫描结果，设备会继续自动刷新。</div>';
        return;
      }

      items.forEach(function(item) {
        var badges = '';
        if (item.current) badges += '<span class="badge">当前网络</span>';
        if (item.saved) badges += '<span class="badge">已保存</span>';
        var displaySsid = escapeHtml(item.ssid);

        var meta = 'RSSI ' + item.rssi + ' dBm，' + signalText(item.rssi);
        meta += item.secured ? '，需要密码' : '，开放网络';

        var row = document.createElement('div');
        row.className = 'network-item';
        row.innerHTML =
          '<div class="network-meta">' +
            '<div class="network-name">' + displaySsid + badges + '</div>' +
            '<div class="network-extra">' + meta + '</div>' +
          '</div>' +
          '<div class="actions">' +
            '<button class="primary" type="button">连接</button>' +
          '</div>';

        row.querySelector('button').addEventListener('click', function() {
          connectNetwork(item.ssid, item.secured);
        });

        list.appendChild(row);
      });
    }

    function renderCredentials(items) {
      var list = document.getElementById('credentialList');
      list.innerHTML = '';

      if (!uiState.deviceReachable) {
        return;
      }

      if (!items.length) {
        list.innerHTML = '<div class="empty">当前没有已保存网络。</div>';
        return;
      }

      items.forEach(function(item) {
        var displaySsid = escapeHtml(item.ssid);
        var row = document.createElement('div');
        row.className = 'credential-item';
        row.innerHTML =
          '<div class="credential-meta">' +
            '<div class="credential-name">' + displaySsid +
              (item.current ? '<span class="badge">当前网络</span>' : '') +
            '</div>' +
            '<div class="credential-extra">设备会优先尝试列表靠前的网络</div>' +
          '</div>' +
          '<div class="actions">' +
            '<button class="danger" type="button">删除</button>' +
          '</div>';

        row.querySelector('button').addEventListener('click', function() {
          deleteCredential(item.ssid);
        });

        list.appendChild(row);
      });
    }

    function connectNetwork(ssid, secured) {
      var password = '';
      if (secured) {
        password = prompt('请输入 Wi-Fi 密码：', '');
        if (password === null) return;
      }

      postForm('/provision/connect', { ssid: ssid, password: password })
        .then(function(data) {
          alert(data.message);
          refreshAll();
        })
        .catch(function(error) {
          alert('请求失败：' + error);
        });
    }

    function deleteCredential(ssid) {
      if (!confirm('确定要删除已保存网络“' + ssid + '”吗？')) return;

      postForm('/provision/delete', { ssid: ssid })
        .then(function(data) {
          alert(data.message);
          refreshAll();
        })
        .catch(function(error) {
          alert('请求失败：' + error);
        });
    }

    function clearCredentials() {
      if (!confirm('确定要清空所有已保存网络吗？')) return;

      postForm('/provision/clear', {})
        .then(function(data) {
          alert(data.message);
          refreshAll();
        })
        .catch(function(error) {
          alert('请求失败：' + error);
        });
    }

    function refreshStatus() {
      if (uiState.statusRequestInFlight) {
        return Promise.resolve();
      }

      uiState.statusRequestInFlight = true;

      return fetchJson('/provision/status', { timeoutMs: kStatusRequestTimeoutMs })
        .then(function(data) {
          renderStatus(data);
        })
        .catch(function() {
          handleStatusFailure();
        })
        .then(function() {
          uiState.statusRequestInFlight = false;
        });
    }

    function refreshNetworks() {
      if (!uiState.deviceReachable) {
        return Promise.resolve();
      }

      if (shouldPauseBackgroundRefresh()) {
        setScanIndicator(false);
        return Promise.resolve();
      }

      return fetchJson('/provision/networks', { timeoutMs: kBackgroundRequestTimeoutMs })
        .then(function(data) {
          if (typeof data.scanInProgress === 'boolean') {
            setScanIndicator(data.scanInProgress);
          }
          renderNetworks(data.networks || []);
        })
        .catch(function() {
          setScanIndicator(false);
        });
    }

    function refreshSavedCredentials() {
      if (!uiState.deviceReachable) {
        return Promise.resolve();
      }

      if (shouldPauseBackgroundRefresh()) {
        return Promise.resolve();
      }

      return fetchJson('/provision/credentials', { timeoutMs: kBackgroundRequestTimeoutMs })
        .then(function(data) {
          renderCredentials(data.credentials || []);
        })
        .catch(function() {
          return Promise.resolve();
        });
    }

    function refreshAll() {
      refreshStatus();
      refreshNetworks();
      refreshSavedCredentials();
    }

    refreshAll();
    setInterval(refreshStatus, 1000);
    setInterval(refreshNetworks, 8000);
    setInterval(refreshSavedCredentials, 5000);
  </script>
</body>
</html>
)rawliteral";
