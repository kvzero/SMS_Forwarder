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
    body {
      font-family: Arial, sans-serif;
      margin: 0;
      padding: 16px;
      background: #f4f6fb;
      color: #243042;
    }
    .container {
      max-width: 760px;
      margin: 0 auto;
    }
    .hero {
      background: linear-gradient(135deg, #1f7ae0, #38b6ff);
      color: white;
      padding: 20px;
      border-radius: 16px;
      margin-bottom: 16px;
      box-shadow: 0 12px 30px rgba(31, 122, 224, 0.2);
    }
    .hero h1 {
      margin: 0 0 8px;
      font-size: 28px;
    }
    .hero p {
      margin: 0;
      line-height: 1.5;
      opacity: 0.95;
    }
    .card {
      background: white;
      border-radius: 16px;
      padding: 16px;
      margin-bottom: 16px;
      box-shadow: 0 8px 20px rgba(36, 48, 66, 0.08);
    }
    .section-title {
      font-size: 18px;
      font-weight: bold;
      color: #243042;
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
      border: 2px solid #cfdcf3;
      border-top-color: #1f7ae0;
      border-radius: 50%;
      opacity: 0;
      transition: opacity 0.2s ease;
      animation: scan-spin 0.9s linear infinite;
    }
    .scan-indicator.active {
      opacity: 1;
    }
    .status-box {
      border-left: 4px solid #1f7ae0;
      background: #eef5ff;
      padding: 12px;
      border-radius: 10px;
      line-height: 1.6;
    }
    .status-box.warn {
      border-left-color: #c98300;
      background: #fff6e1;
    }
    .handoff {
      display: none;
      margin-top: 12px;
      border-left: 4px solid #2e7d32;
      background: #eaf8ee;
      padding: 12px;
      border-radius: 10px;
      line-height: 1.6;
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
      padding: 12px;
      border: 1px solid #d9e2ef;
      border-radius: 12px;
      background: #fbfdff;
    }
    .network-meta,
    .credential-meta {
      flex: 1;
      min-width: 0;
    }
    .network-name,
    .credential-name {
      font-weight: bold;
      color: #243042;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .network-extra,
    .credential-extra {
      font-size: 12px;
      color: #6a778b;
      margin-top: 4px;
    }
    .actions {
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
    }
    button {
      border: none;
      border-radius: 10px;
      padding: 10px 14px;
      cursor: pointer;
      font-size: 14px;
    }
    button.primary {
      background: #1f7ae0;
      color: white;
    }
    button.danger {
      background: #ffe8e8;
      color: #b3261e;
    }
    button:disabled {
      opacity: 0.6;
      cursor: not-allowed;
    }
    .hint {
      font-size: 12px;
      color: #6a778b;
      line-height: 1.6;
    }
    .toolbar {
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
      margin-top: 12px;
    }
    .empty {
      color: #6a778b;
      font-size: 14px;
    }
    .badge {
      display: inline-block;
      font-size: 11px;
      border-radius: 999px;
      padding: 2px 8px;
      margin-left: 6px;
      background: #e8f0fe;
      color: #1f7ae0;
    }
    a.link {
      color: #0f5fc6;
      word-break: break-all;
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
    var kStatusRequestTimeoutMs = 5000;
    var kStatusDisconnectThresholdMs = 7000;
    var kScanDisconnectThresholdMs = 12000;
    var kHandoffDisconnectThresholdMs = 4000;
    var kBackgroundRequestTimeoutMs = 5000;
    var kStatusFailureThreshold = 3;
    var kScanFailureThreshold = 4;

    var uiState = {
      deviceReachable: true,
      statusFailures: 0,
      lastStatus: null,
      lastStatusSuccessAt: 0,
      statusRequestInFlight: false
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

    function renderStatus(data) {
      uiState.deviceReachable = true;
      uiState.statusFailures = 0;
      uiState.lastStatus = data;
      uiState.lastStatusSuccessAt = Date.now();

      var statusBox = document.getElementById('statusBox');
      var lines = [];
      lines.push('状态：' + modeText(data.mode));
      if (data.message) lines.push(data.message);
      if (data.portalActive) lines.push('设备热点：' + data.apSsid + '（' + data.apIp + '）');
      if (data.connectInProgress && data.attemptingSsid) lines.push('当前尝试：' + data.attemptingSsid);
      if (data.staConnected) lines.push('当前联网：' + data.staSsid + '（' + data.staIp + '）');

      statusBox.className = 'status-box';
      statusBox.innerHTML = lines.join('<br>');
      setScanIndicator(data.scanInProgress);
      setInteractiveCardsVisible(true);

      var handoffBox = document.getElementById('handoffBox');
      if (data.mode === 'handoff') {
        handoffBox.style.display = 'block';
        handoffBox.innerHTML =
          '设备已连接到 <b>' + data.staSsid + '</b>，请将手机切回该局域网 Wi-Fi。' +
          '<br>新的设备地址：<a class="link" href="http://' + data.redirectIp + '/">http://' + data.redirectIp + '/</a>' +
          '<br>热点将在 ' + data.handoffRemainingSec + ' 秒后自动关闭。';
      } else {
        handoffBox.style.display = 'none';
        handoffBox.innerHTML = '';
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

        var meta = 'RSSI ' + item.rssi + ' dBm，' + signalText(item.rssi);
        meta += item.secured ? '，需要密码' : '，开放网络';

        var row = document.createElement('div');
        row.className = 'network-item';
        row.innerHTML =
          '<div class="network-meta">' +
            '<div class="network-name">' + item.ssid + badges + '</div>' +
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
        var row = document.createElement('div');
        row.className = 'credential-item';
        row.innerHTML =
          '<div class="credential-meta">' +
            '<div class="credential-name">' + item.ssid +
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
          refreshSavedCredentials();
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
