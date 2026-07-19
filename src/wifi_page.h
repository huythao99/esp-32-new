#ifndef WIFI_PAGE_H
#define WIFI_PAGE_H

#include <Arduino.h>

// WiFi setup page served at "/connect". The UID is passed in via the page URL
// (e.g. http://192.168.4.1/connect?uid=abc123) and forwarded to /wifi on submit.
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Cài đặt WiFi thiết bị</title>
<style>
  body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#f1f5f9;color:#0f172a;margin:0;padding:20px;}
  .card{max-width:420px;margin:24px auto;background:#ffffff;border-radius:14px;padding:24px;box-shadow:0 10px 30px rgba(15,23,42,.12);}
  h1{font-size:20px;margin:0 0 4px;}
  .sub{color:#64748b;font-size:13px;margin-bottom:20px;}
  label{display:block;font-size:13px;margin:14px 0 6px;color:#334155;}
  select,input{width:100%;box-sizing:border-box;padding:12px;border-radius:10px;border:1px solid #cbd5e1;background:#ffffff;color:#0f172a;font-size:15px;}
  .row{display:flex;gap:8px;}
  .row select{flex:1;}
  button{margin-top:20px;width:100%;padding:13px;border:0;border-radius:10px;background:#2563eb;color:#fff;font-size:16px;font-weight:600;cursor:pointer;}
  button:disabled{opacity:.5;cursor:not-allowed;}
  .ghost{background:#e2e8f0;color:#0f172a;width:auto;padding:12px 14px;margin-top:0;}
  .status{margin-top:16px;font-size:14px;text-align:center;min-height:20px;}
  .ok{color:#16a34a;} .err{color:#dc2626;}
  .uid{font-size:12px;color:#94a3b8;margin-top:6px;word-break:break-all;}
</style>
</head>
<body>
<div class="card">
  <h1>Cài đặt WiFi</h1>
  <div class="sub">Chọn mạng WiFi và nhập mật khẩu để kết nối thiết bị.</div>

  <label>Mạng WiFi</label>
  <div class="row">
    <select id="ssid"><option value="">Đang quét...</option></select>
    <button type="button" class="ghost" id="refresh">&#8635;</button>
  </div>

  <label>Mật khẩu</label>
  <input id="password" type="password" placeholder="Mật khẩu WiFi" autocomplete="off">

  <div class="uid" id="uidLabel">UID: (chưa có)</div>

  <button id="save" disabled>Kết nối</button>
  <button type="button" class="ghost" id="check" style="width:100%;margin-top:10px;">Kiểm tra trạng thái</button>
  <div class="status" id="status"></div>
</div>

<script>
  var q = new URLSearchParams(location.search);
  var uid = q.get('uid') || '';
  document.getElementById('uidLabel').textContent = 'UID: ' + (uid || '(chưa có)');

  var ssidSel = document.getElementById('ssid');
  // NOTE: do NOT name this "status" — that collides with window.status (a
  // string-only property), which silently breaks all status updates.
  var statusEl = document.getElementById('status');
  var saveBtn = document.getElementById('save');

  function scan(){
    ssidSel.innerHTML = '<option value="">Đang quét...</option>';
    saveBtn.disabled = true;
    // Ask the device for ONE fresh scan, then poll the cached result.
    fetch('/scan?refresh=1').catch(function(){});
    var tries = 0;
    function poll(){
      fetch('/scan').then(function(r){return r.json();}).then(function(list){
        if(list.length === 0 && tries < 10){ tries++; return setTimeout(poll, 1000); }
        ssidSel.innerHTML = '';
        if(list.length === 0){
          ssidSel.innerHTML = '<option value="">Không tìm thấy mạng nào</option>';
          return;
        }
        list.sort(function(a,b){return b.rssi - a.rssi;});
        var seen = {};
        list.forEach(function(n){
          if(!n.ssid || seen[n.ssid]) return;
          seen[n.ssid] = true;
          var o = document.createElement('option');
          o.value = n.ssid;
          o.textContent = n.ssid + (n.secure ? ' 🔒' : '');
          ssidSel.appendChild(o);
        });
        saveBtn.disabled = false;
      }).catch(function(){ if(tries < 10){ tries++; setTimeout(poll, 1000); } });
    }
    // Give the device ~1.8s to finish the scan before the first poll.
    setTimeout(poll, 1800);
  }

  document.getElementById('refresh').onclick = scan;

  // wl_status_t codes reported by the device.
  var WL_CONNECTED = 3;
  var wifiText = {
    0:'chờ', 1:'không tìm thấy mạng', 2:'quét xong',
    3:'đã kết nối', 4:'sai mật khẩu / kết nối thất bại',
    5:'mất kết nối', 6:'đã ngắt kết nối'
  };

  function setStatus(cls, msg){ statusEl.className = 'status ' + cls; statusEl.textContent = msg; }

  // Poll the device until WiFi connects, then wait for MQTT (device online).
  // NOTE: on a successful connect the ESP32 moves its AP to the router's channel
  // and your phone may drop off the setup WiFi. If so, rejoin it and reopen the
  // page — /connect-status will then report the real state.
  function pollStatus(ssid){
    var tries = 0;
    var wifiOkTries = 0;
    var maxTries = 40;      // ~60s at 1.5s intervals
    function check(){
      fetch('/connect-status').then(function(r){return r.json();}).then(function(s){
        if(s.result === 1 || s.wifi === WL_CONNECTED){
          if(s.mqtt === 1){
            setStatus('ok', '✅ Đã kết nối tới ' + ssid + ' — thiết bị đang trực tuyến.');
            return;
          }
          // WiFi is up; give MQTT a few seconds, else report WiFi success anyway.
          wifiOkTries++;
          if(wifiOkTries >= 4){
            setStatus('ok', '✅ Đã kết nối tới ' + ssid + '. (Đang khởi động dịch vụ...)');
            return;
          }
          setStatus('', '📶 Đã kết nối tới ' + ssid + '. Đang đưa thiết bị trực tuyến...');
        } else if(s.result === 0){
          // Device tried for 15s and gave up: wrong password / not found / range.
          setStatus('err', '❌ Không thể kết nối tới ' + ssid + '. Vui lòng kiểm tra mật khẩu và thử lại.');
          saveBtn.disabled = false;
          return;
        } else {
          setStatus('', 'Đang kết nối tới ' + ssid + '...');
        }
        tries++;
        if(tries >= maxTries){
          setStatus('err', '⌛ Hết thời gian chờ. Nếu điện thoại đã rời mạng cài đặt, hãy kết nối lại và mở lại trang này để kiểm tra.');
          saveBtn.disabled = false;
          return;
        }
        setTimeout(check, 1500);
      }).catch(function(){
        // Fetch failed: the phone likely dropped off the AP when the radio moved
        // to the router's channel. Keep retrying and tell the user what to do.
        tries++;
        if(tries % 3 === 0){
          setStatus('', '📡 Đã mất kết nối mạng cài đặt (điều này bình thường khi kết nối). Hãy kết nối lại mạng cài đặt nếu không tự khôi phục...');
        }
        if(tries >= maxTries){
          setStatus('err', 'Không xác nhận được trạng thái. Hãy kết nối lại mạng cài đặt và mở lại trang này để kiểm tra.');
          saveBtn.disabled = false;
          return;
        }
        setTimeout(check, 1500);
      });
    }
    check();
  }

  saveBtn.onclick = function(){
    var ssid = ssidSel.value;
    var password = document.getElementById('password').value;
    if(!ssid){ setStatus('err', 'Vui lòng chọn một mạng WiFi'); return; }
    saveBtn.disabled = true;
    setStatus('', 'Đang gửi thông tin...');
    var url = '/wifi?ssid=' + encodeURIComponent(ssid) +
              '&password=' + encodeURIComponent(password) +
              '&uid=' + encodeURIComponent(uid);
    fetch(url, {method:'POST'}).then(function(r){return r.text();}).then(function(t){
      if(t.trim() === 'success'){
        setStatus('', 'Đang kết nối tới ' + ssid + '... Nếu bạn bị ngắt khỏi mạng cài đặt, nghĩa là đã thành công — hãy kết nối lại mạng cài đặt và mở lại trang này để xác nhận.');
        pollStatus(ssid);
      } else {
        setStatus('err', 'Thất bại: ' + t);
        saveBtn.disabled = false;
      }
    }).catch(function(){
      setStatus('err', 'Yêu cầu thất bại');
      saveBtn.disabled = false;
    });
  };

  // On load, reflect the device's CURRENT connection state. This is what makes
  // the two-step flow work: after you press Connect the phone may drop off the
  // setup WiFi; once you rejoin it and reopen this page, you'll see the result.
  function showCurrentStatus(){
    setStatus('', 'Đang kiểm tra trạng thái thiết bị...');
    fetch('/connect-status').then(function(r){return r.json();}).then(function(s){
      if(s.result === 1 || s.wifi === WL_CONNECTED){
        setStatus('ok', s.mqtt === 1
          ? '✅ Thiết bị đã kết nối WiFi và đang trực tuyến.'
          : '✅ Thiết bị đã kết nối WiFi.');
      } else if(s.result === 0){
        setStatus('err', '❌ Lần kết nối gần nhất thất bại. Vui lòng kiểm tra mật khẩu và thử lại.');
      } else {
        // No attempt yet (or still testing). Show raw values so state is visible.
        setStatus('', 'ℹ️ Sẵn sàng. (wifi=' + s.wifi + ', mqtt=' + s.mqtt + ', result=' + s.result + ')');
      }
    }).catch(function(){
      setStatus('err', '⚠️ Không thể kết nối tới thiết bị. Hãy chắc chắn bạn vẫn đang ở trong mạng cài đặt.');
    });
  }
  document.getElementById('check').onclick = showCurrentStatus;

  showCurrentStatus();
  scan();
</script>
</body>
</html>
)rawliteral";

#endif  // WIFI_PAGE_H
