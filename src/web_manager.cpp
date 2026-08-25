#include "web_manager.h"
#include "config.h"
#include "audio_engine.h"
#include "midi_parser.h"
#include "midi_sequencer.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include "display_ui.h"
#include "led_indicator.h"
#include "logo_data.h"
#include "icon_data.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>

static AsyncWebServer server(WEB_SERVER_PORT);
static String g_lang = "ko";
static bool g_uploading = false; // 동시 업로드 방지 락
static unsigned long g_uploadStartTime = 0; // 업로드 시작 시간 (타임아웃 감지용)
static bool g_font_mgmt_enabled = false; // 사운드폰트 고급 관리 활성화 여부 (기본 false = 완전 숨김)
static const uint8_t GIF_1X1[43] = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x21, 0xf9, 0x04, 0x01, 0x00, 
    0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 
    0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3b
}; // 1x1 초경량 투명 GIF 바이너리 (IE 4.0 비콘 초고속 응답용)

bool WebManager::isFontManagementEnabled() {
    return g_font_mgmt_enabled;
}

void WebManager::setFontManagementEnabled(bool enabled) {
    g_font_mgmt_enabled = enabled;
    Preferences prefs;
    prefs.begin("system", false);
    prefs.putBool("font_mgmt_en", enabled);
    prefs.end();
    DisplayUI::setFontManagementEnabled(enabled);
}

static String urlEncode(const String& str) {
    String encoded = "";
    for (size_t i = 0; i < str.length(); i++) {
        char c = str[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            char code0 = "0123456789ABCDEF"[((unsigned char)c >> 4) & 0xF];
            char code1 = "0123456789ABCDEF"[(unsigned char)c & 0xF];
            encoded += '%';
            encoded += code0;
            encoded += code1;
        }
    }
    return encoded;
}

static String getSafeFileName(String filename, const char* defaultExt) {
    while (filename.startsWith("/")) filename = filename.substring(1);

    String ext = defaultExt;
    int dotIdx = filename.lastIndexOf('.');
    if (dotIdx >= 0) {
        ext = filename.substring(dotIdx);
        filename = filename.substring(0, dotIdx);
    }

    // LittleFS 및 URL 특수문자 안전 치환 (+, =, &, ?, %, #, ;, @, $, ,, 공백, 괄호 등 전부 _)
    for (size_t i = 0; i < filename.length(); i++) {
        char c = filename[i];
        if (c == ' ' || c == '(' || c == ')' || c == '[' || c == ']' ||
            c == '+' || c == '=' || c == '?' || c == '&' || c == '%' ||
            c == '#' || c == '/' || c == '\\' || c == '"' || c == '\'' ||
            c == '*' || c == ':' || c == '<' || c == '>' || c == '|' ||
            c == ';' || c == '@' || c == '$' || c == ',') {
            filename.setCharAt(i, '_');
        }
    }

    // LittleFS 31바이트 제한 (확장자 포함 최대 28바이트로 안전 절삭)
    size_t maxBaseBytes = (ext.length() < 28) ? (28 - ext.length()) : 20;
    while (filename.length() > maxBaseBytes) {
        filename.remove(filename.length() - 1);
    }

    // UTF-8 연속 바이트 절단 시 정리
    while (filename.length() > 0 && ((uint8_t)filename[filename.length() - 1] & 0xC0) == 0x80) {
        filename.remove(filename.length() - 1);
    }
    if (filename.length() > 0 && ((uint8_t)filename[filename.length() - 1] & 0x80) != 0 && ((uint8_t)filename[filename.length() - 1] & 0xC0) != 0) {
        filename.remove(filename.length() - 1);
    }

    if (filename.length() == 0) {
        filename = "file_" + String(millis() % 10000);
    }

    return "/" + filename + ext;
}

// 90년대 후반 (1998년) 정통 클래식 웹사이트 HTML 생성기 (IE 4.0 ~ 모던 브라우저 100% 호환)
static String generateHTML(const String& tab, const String& lang, const String& selSsid = "") {
    bool isKo = (lang == "ko");
    String activeTab = tab.length() ? tab : "player";

    String html;
    html.reserve(49152);
    html = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n<html>\n<head>\n";
    html += "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">\n";
    if ((activeTab == "wifi" && WiFiManager::isScanning()) || (activeTab == "fonts" && AudioEngine::isLoadingFont())) {
        html += "<meta http-equiv=\"refresh\" content=\"2;url=/?tab=" + activeTab + "&lang=" + lang + "\">\n";
    }
    html += "<title>WaveCanvas Nano RS - Settings</title>\n";
    html += "<style type=\"text/css\">\n";
    html += "body { background-color: #ffffff; color: #000000; font-family: Tahoma, 'MS Sans Serif', Dotum, Gulim, sans-serif; font-size: 12px; margin: 10px; min-width: 740px; }\n";
    html += "table { font-size: 12px; }\n";
    html += ".header-table { background-color: #f0f0f0; border-bottom: 2px solid #000080; padding: 4px; }\n";
    html += ".tab-active { background-color: #000080; color: #ffffff !important; font-weight: bold; padding: 5px 10px; text-decoration: none; border: 1px solid #000080; display: inline-block; }\n";
    html += ".tab-inactive { background-color: #e0e0e0; color: #000000 !important; padding: 5px 10px; text-decoration: none; border: 1px solid #999999; display: inline-block; }\n";
    html += ".tab-inactive:hover { background-color: #d0d0d0; }\n";
    html += ".btn98 { background-color: #d4d0c8; border-top: 2px solid #ffffff; border-left: 2px solid #ffffff; border-right: 2px solid #808080; border-bottom: 2px solid #808080; padding: 2px 6px; font-family: Tahoma, 'MS Sans Serif', Dotum, Gulim, sans-serif; font-size: 12px; color: #000000; cursor: pointer; vertical-align: middle; overflow: visible; }\n";
    html += ".btn98:active { border-color: #808080 #ffffff #ffffff #808080; }\n";
    html += ".inset-box { background-color: #ffffff; border: 2px solid; border-color: #808080 #ffffff #ffffff #808080; padding: 4px; }\n";
    html += ".section-table { border: 1px solid #999999; background-color: #fafafa; margin-bottom: 12px; }\n";
    html += ".section-hdr { background-color: #000080; color: #ffffff; font-weight: bold; padding: 4px 8px; }\n";
    html += ".status-bar { background-color: #d4d0c8; border-top: 1px solid #808080; padding: 4px 8px; font-size: 11px; margin-top: 15px; }\n";
    html += "img.ic { vertical-align: middle; margin-right: 4px; }\n";

    // 90년대 클래식 DHTML 3D 슬라이더 스타일 (IE 4~9 레거시용)
    html += ".sl-track { position: relative; width: 140px; height: 16px; display: inline; vertical-align: middle; cursor: pointer; }\n";
    html += ".sl-groove { position: absolute; top: 7px; left: 0px; width: 130px; height: 2px; background-color: #808080; border-bottom: 1px solid #ffffff; font-size: 1px; line-height: 1px; overflow: hidden; }\n";
    html += ".sl-thumb { position: absolute; top: 0px; width: 10px; height: 16px; background-color: #d4d0c8; border-top: 1px solid #ffffff; border-left: 1px solid #ffffff; border-right: 1px solid #404040; border-bottom: 1px solid #404040; font-size: 1px; line-height: 1px; overflow: hidden; cursor: pointer; }\n";

    // 가상 피아노 건반 스타일 (IE 4.0 ~ 모던 브라우저 100% 호환)
    html += ".piano-box { background: #222222; padding: 12px; border: 2px solid; border-color: #808080 #dfdfdf #dfdfdf #808080; display: block; width: 480px; user-select: none; -webkit-user-select: none; }\n";
    html += ".piano-kb { position: relative; width: 476px; height: 142px; display: block; overflow: hidden; }\n";
    html += ".w-key { position: absolute; top: 0px; width: 34px; height: 140px; background: #ffffff; border: 1px solid #7f7f7f; border-bottom: 5px solid #a0a0a0; cursor: pointer; text-align: center; }\n";
    html += ".w-lbl { position: absolute; top: 120px; left: 0; width: 100%; font-size: 10px; font-weight: bold; color: #333333; text-align: center; }\n";
    html += ".w-hk { position: absolute; top: 105px; left: 0; width: 100%; font-size: 9px; color: #888888; text-align: center; }\n";
    html += ".b-key { position: absolute; top: 0px; width: 22px; height: 85px; background: #111111; border: 1px solid #000000; border-bottom: 4px solid #333333; cursor: pointer; z-index: 2; text-align: center; }\n";
    html += ".b-lbl { position: absolute; top: 68px; left: 0; width: 100%; font-size: 8px; font-weight: bold; color: #ffffff; text-align: center; }\n";
    html += ".b-hk { position: absolute; top: 52px; left: 0; width: 100%; font-size: 8px; color: #ffcc00; text-align: center; }\n";
    html += "</style>\n";

    html += "<script type=\"text/javascript\">\n";
    html += "function getEl(id) {\n";
    html += "  if (document.getElementById) return document.getElementById(id);\n";
    html += "  if (document.all) return document.all[id];\n";
    html += "  return null;\n";
    html += "}\n";

    // 3개의 순환 비콘 객체 풀 (연타 시 패킷 씹힘 방지 및 무오류 비동기 전송)
    html += "var _bcPool = [new Image(), new Image(), new Image()], _bcIdx = 0;\n";
    html += "function sendApi(url) {\n";
    html += "  var u = url + (url.indexOf('?') >= 0 ? '&' : '?') + '_t=' + (new Date().getTime());\n";
    html += "  if (window.fetch) {\n";
    html += "    fetch(u, { cache: 'no-store' });\n";
    html += "  } else if (window.XMLHttpRequest) {\n";
    html += "    var xhr = new XMLHttpRequest();\n";
    html += "    xhr.open('GET', u, true);\n";
    html += "    xhr.send(null);\n";
    html += "  } else {\n";
    html += "    _bcPool[_bcIdx].src = u;\n";
    html += "    _bcIdx = (_bcIdx + 1) % 3;\n";
    html += "  }\n";
    html += "}\n";

    // 볼륨 UI 실시간 갱신 및 API 전송 쓰로틀링 (부하 차단)
    html += "var _lastVolTime = 0, _lastVolSent = -1;\n";
    html += "function sendVolThrottled(val, force) {\n";
    html += "  var now = new Date().getTime();\n";
    html += "  if (force || (now - _lastVolTime > 100 && _lastVolSent != val)) {\n";
    html += "    _lastVolTime = now;\n";
    html += "    _lastVolSent = val;\n";
    html += "    sendApi('/api/set_vol?val=' + val);\n";
    html += "  }\n";
    html += "}\n";

    html += "function updateVol(val, fromDrag) {\n";
    html += "  if (val < 0) val = 0; if (val > 100) val = 100;\n";
    html += "  var e1 = getEl('volVal'); if (e1) e1.innerText = val + '%';\n";
    html += "  var e2 = getEl('volVal2'); if (e2) e2.innerText = val + '%';\n";
    html += "  var r1 = getEl('volRange'); if (r1 && r1.value != val) r1.value = val;\n";
    html += "  var r2 = getEl('volRange2'); if (r2 && r2.value != val) r2.value = val;\n";
    html += "  var leftPx = Math.round((val * 130) / 100) + 'px';\n";
    html += "  var t1 = getEl('volThumb'); if (t1) t1.style.left = leftPx;\n";
    html += "  var t2 = getEl('volThumb2'); if (t2) t2.style.left = leftPx;\n";
    html += "  sendVolThrottled(val, !fromDrag);\n";
    html += "}\n";

    // IE 4.0 금지 커서 방지 및 마우스 드래그 추적
    html += "var _activeThumb = null, _startX = 0, _startL = 0, _curVal = 0, _wasDragged = false;\n";
    html += "function onThumbDown(e, thumbId) {\n";
    html += "  e = e || window.event;\n";
    html += "  _activeThumb = thumbId;\n";
    html += "  _wasDragged = false;\n";
    html += "  _startX = e.clientX;\n";
    html += "  var el = getEl(thumbId);\n";
    html += "  _startL = el ? parseInt(el.style.left || '0') : 0;\n";
    html += "  if (el && el.setCapture) el.setCapture();\n";
    html += "  document.onselectstart = function() { return false; };\n"; // 텍스트 드래그 선택 차단
    html += "  document.ondragstart = function() { return false; };\n";   // 브라우저 기본 드래그(금지커서) 차단
    html += "  if (e.preventDefault) e.preventDefault();\n";
    html += "  if (e.stopPropagation) e.stopPropagation();\n";
    html += "  e.returnValue = false;\n";
    html += "  e.cancelBubble = true;\n";
    html += "  return false;\n";
    html += "}\n";

    html += "function onTrackClick(e, trackId) {\n";
    html += "  if (_wasDragged) return;\n";
    html += "  e = e || window.event;\n";
    html += "  var tgt = e.target || e.srcElement;\n";
    html += "  if (tgt && tgt.id && tgt.id.indexOf('Thumb') >= 0) return;\n";
    html += "  var offX = e.offsetX;\n";
    html += "  if (typeof offX == 'undefined') offX = e.clientX - getEl(trackId).getBoundingClientRect().left;\n";
    html += "  var nL = offX - 5;\n";
    html += "  if (nL < 0) nL = 0; if (nL > 130) nL = 130;\n";
    html += "  updateVol(Math.round((nL / 130) * 100), false);\n";
    html += "}\n";

    html += "function onDocMouseMove(e) {\n";
    html += "  if (!_activeThumb) return;\n";
    html += "  e = e || window.event;\n";
    html += "  var diff = e.clientX - _startX;\n";
    html += "  if (Math.abs(diff) > 1) _wasDragged = true;\n";
    html += "  var nL = _startL + diff;\n";
    html += "  if (nL < 0) nL = 0; if (nL > 130) nL = 130;\n";
    html += "  _curVal = Math.round((nL / 130) * 100);\n";
    html += "  updateVol(_curVal, true);\n"; // 드래그 중에는 쓰로틀링 전송
    html += "}\n";

    html += "function onDocMouseUp(e) {\n";
    html += "  if (!_activeThumb) return;\n";
    html += "  var el = getEl(_activeThumb);\n";
    html += "  if (el && el.releaseCapture) el.releaseCapture();\n";
    html += "  _activeThumb = null;\n";
    html += "  document.onselectstart = null;\n";
    html += "  document.ondragstart = null;\n";
    html += "  sendVolThrottled(_curVal, true);\n"; // 마우스를 놓았을 때 최종 볼륨 확정 전송
    html += "  setTimeout(function() { _wasDragged = false; }, 150);\n";
    html += "}\n";

    html += "if (window.addEventListener) {\n";
    html += "  document.addEventListener('mousemove', onDocMouseMove, false);\n";
    html += "  document.addEventListener('mouseup', onDocMouseUp, false);\n";
    html += "} else {\n";
    html += "  var _oldMM = document.onmousemove, _oldMU = document.onmouseup;\n";
    html += "  document.onmousemove = function(e) { if (_oldMM) _oldMM(e); onDocMouseMove(e); };\n";
    html += "  document.onmouseup = function(e) { if (_oldMU) _oldMU(e); onDocMouseUp(e); };\n";
    html += "}\n";

    // 브라우저 슬라이더 지원 감지 및 자동 분기
    html += "function initSliderMode() {\n";
    html += "  var r1 = getEl('volRange');\n";
    html += "  if (r1 && r1.type != 'range') {\n";
    html += "    r1.style.display = 'none';\n";
    html += "    var d1 = getEl('volDhtml'); if (d1) d1.style.display = 'inline';\n";
    html += "    var r2 = getEl('volRange2'); if (r2) r2.style.display = 'none';\n";
    html += "    var d2 = getEl('volDhtml2'); if (d2) d2.style.display = 'inline';\n";
    html += "  }\n";
    html += "}\n";

    html += "function playTest(type) {\n";
    html += "  sendApi('/api/test_sound?type=' + type);\n";
    html += "}\n";

    html += "function updatePlayerStatus(st) {\n";
    html += "  var el = getEl('playerStatus'); if (!el) return;\n";
    html += "  if (st == 'PLAYING') el.innerHTML = '<font color=\"#008000\"><b>PLAYING</b></font>';\n";
    html += "  else if (st == 'PAUSED') el.innerHTML = '<font color=\"#ff9900\"><b>PAUSED</b></font>';\n";
    html += "  else el.innerHTML = '<font color=\"#800000\"><b>STOPPED</b></font>';\n";
    html += "}\n";

    html += "function updatePlayingSong(name) {\n";
    html += "  var el = getEl('currentTrackTitle'); if (el) el.innerText = name;\n";
    html += "  updatePlayerStatus('PLAYING');\n";
    html += "}\n";

    html += "var pClicks = 0, lastPClick = 0;\n";
    html += "function onPianoTestClick() {\n";
    html += "  playTest('piano');\n";
    html += "  var now = new Date().getTime();\n";
    html += "  if (now - lastPClick < 1500) { pClicks++; } else { pClicks = 1; }\n";
    html += "  lastPClick = now;\n";
    html += "  if (pClicks >= 3) {\n";
    html += "    pClicks = 0;\n";
    html += "    if (window.sessionStorage) { sessionStorage.setItem('pianoUnlocked', '1'); }\n";
    html += "    else { document.cookie = 'pianoUnlocked=1; path=/'; }\n";
    html += "    window.location.href = '/?tab=piano&lang=' + ('" + lang + "');\n";
    html += "  }\n";
    html += "}\n";

    html += "var activeNotes = {};\n";
    html += "var keyMap = {\n";
    html += "  'a':60, 'w':61, 's':62, 'e':63, 'd':64, 'f':65, 't':66, 'g':67, 'y':68, 'h':69, 'u':70, 'j':71,\n";
    html += "  'k':72, 'o':73, 'l':74, 'p':75, ';':76, \"'\":77, ']':78, 'z':79, 'x':81, 'c':83\n";
    html += "};\n";
    html += "var curOctShift = 0;\n";

    html += "function setKeyColor(id, isBlack, active) {\n";
    html += "  var el = getEl(id);\n";
    html += "  if (!el) return;\n";
    html += "  el.style.backgroundColor = active ? (isBlack ? '#555555' : '#d0e4ff') : (isBlack ? '#111111' : '#ffffff');\n";
    html += "}\n";

    html += "function pianoNoteOn(note) {\n";
    html += "  var n = note + curOctShift;\n";
    html += "  if (n < 0 || n > 127) return;\n";
    html += "  if (activeNotes[n]) return;\n";
    html += "  activeNotes[n] = true;\n";
    html += "  var s = note % 12;\n";
    html += "  var isBlack = (s == 1 || s == 3 || s == 6 || s == 8 || s == 10);\n";
    html += "  setKeyColor('k_' + note, isBlack, true);\n";
    html += "  sendApi('/api/note_on?note=' + n + '&vel=100');\n";
    html += "}\n";

    html += "function pianoNoteOff(note) {\n";
    html += "  var n = note + curOctShift;\n";
    html += "  if (!activeNotes[n]) return;\n";
    html += "  delete activeNotes[n];\n";
    html += "  var s = note % 12;\n";
    html += "  var isBlack = (s == 1 || s == 3 || s == 6 || s == 8 || s == 10);\n";
    html += "  setKeyColor('k_' + note, isBlack, false);\n";
    html += "  sendApi('/api/note_off?note=' + n);\n";
    html += "}\n";

    html += "function changeProg(prog) {\n";
    html += "  sendApi('/api/prog_change?prog=' + prog);\n";
    html += "}\n";

    html += "function onToggleFontMgmt(radio) {\n";
    html += "  if (radio.value == '1') {\n";
    html += "    var msg = '[Warning] SoundFont Advanced Mode\\n\\nAll synthesizer features and audio optimizations are strictly tailored to CT4MGM.SF2.\\nUsing other SoundFonts will disable MT-32 optimizations and may cause balance issues.\\n\\nAre you sure you want to enable Advanced SoundFont Management?';\n";
    if (isKo) {
        html += "    msg = '[주의] 사운드폰트 고급 관리 모드 경고\\n\\n본 기기의 모든 음원 엔진(GM/GS/MT-32) 및 밸런스는 CT4MGM.SF2에 완벽히 최적화되어 있습니다.\\n다른 사운드폰트로 교체 시 음량 불균형 및 MT-32 호환 기능이 비활성화됩니다.\\n\\n정말 사운드폰트 고급 관리 모드를 활성화하시겠습니까?';\n";
    }
    html += "    if (!confirm(msg)) {\n";
    html += "      var r0 = getEl('fm_0');\n";
    html += "      if (r0) r0.checked = true;\n";
    html += "      return false;\n";
    html += "    }\n";
    html += "  }\n";
    html += "}\n";

    html += "function shiftOct(diff) {\n";
    html += "  curOctShift += (diff * 12);\n";
    html += "  if (curOctShift < -24) curOctShift = -24;\n";
    html += "  if (curOctShift > 24) curOctShift = 24;\n";
    html += "  var el = getEl('octLabel');\n";
    html += "  if (el) {\n";
    html += "    var b = 4 + (curOctShift / 12);\n";
    html += "    el.innerText = 'C' + b + ' ~ B' + (b + 1);\n";
    html += "  }\n";
    html += "}\n";

    html += "function onKeyDn(e) {\n";
    html += "  e = e || window.event;\n";
    html += "  var tgt = e.target || e.srcElement;\n";
    html += "  if (tgt && (tgt.tagName == 'INPUT' || tgt.tagName == 'SELECT')) return;\n";
    html += "  var k = (e.key ? e.key : String.fromCharCode(e.keyCode)).toLowerCase();\n";
    html += "  if (typeof keyMap[k] != 'undefined' && !e.repeat) {\n";
    html += "    pianoNoteOn(keyMap[k]);\n";
    html += "  }\n";
    html += "}\n";

    html += "function onKeyUp(e) {\n";
    html += "  e = e || window.event;\n";
    html += "  var tgt = e.target || e.srcElement;\n";
    html += "  if (tgt && (tgt.tagName == 'INPUT' || tgt.tagName == 'SELECT')) return;\n";
    html += "  var k = (e.key ? e.key : String.fromCharCode(e.keyCode)).toLowerCase();\n";
    html += "  if (typeof keyMap[k] != 'undefined') {\n";
    html += "    pianoNoteOff(keyMap[k]);\n";
    html += "  }\n";
    html += "}\n";

    // 이벤트 등록 (W3C -> DOM Level 0 폴백)
    html += "if (window.addEventListener) {\n";
    html += "  window.addEventListener('keydown', onKeyDn, false);\n";
    html += "  window.addEventListener('keyup', onKeyUp, false);\n";
    html += "} else {\n";
    html += "  document.onkeydown = onKeyDn;\n";
    html += "  document.onkeyup = onKeyUp;\n";
    html += "}\n";

    html += "function initPianoTab() {\n";
    html += "  var unlocked = (window.sessionStorage && sessionStorage.getItem('pianoUnlocked') == '1') || (document.cookie.indexOf('pianoUnlocked=1') >= 0) || ('" + activeTab + "' == 'piano');\n";
    html += "  if (unlocked) {\n";
    html += "    var pt = getEl('tabPiano'); if (pt) pt.style.display = '';\n"; // inline-block -> inline으로 변경
    html += "  }\n";
    html += "}\n";

    html += "if (window.addEventListener) {\n";
    html += "  window.addEventListener('DOMContentLoaded', initPianoTab, false);\n";
    html += "} else {\n";
    html += "  var oldOnload = window.onload;\n";
    html += "  window.onload = function() { if (oldOnload) oldOnload(); initPianoTab(); initSliderMode(); };\n";
    html += "}\n";
    html += "</script>\n";

    if (activeTab == "fonts" && !g_font_mgmt_enabled) activeTab = "player";

    // 1. 상단 헤더: [왼쪽 로고] + [가운데/옆 탭 메뉴] + [오른쪽 언어 선택]
    // 각 <td>에 nowrap 추가하여 글자 쪼개짐 원천 차단
    html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"header-table\">\n";
    html += "<tr>\n";
    html += "<td width=\"190\" nowrap valign=\"top\">\n";
    html += "<a href=\"/?tab=player&lang=" + lang + "\"><img src=\"/logo.gif\" width=\"182\" height=\"42\" border=\"0\" alt=\"WaveCanvas Logo\"></a>\n";
    html += "</td>\n";
    html += "<td valign=\"middle\" style=\"padding-left: 10px;\">\n";
    html += "<nobr><a href=\"/?tab=player&lang=" + lang + "\" class=\"" + (activeTab == "player" ? "tab-active" : "tab-inactive") + "\"><img src=\"/icon/music.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "MIDI 플레이어" : "MIDI Player") + "</a></nobr> ";
    if (g_font_mgmt_enabled) {
        html += "<nobr><a href=\"/?tab=fonts&lang=" + lang + "\" class=\"" + (activeTab == "fonts" ? "tab-active" : "tab-inactive") + "\"><img src=\"/icon/piano.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "사운드폰트" : "SoundFonts") + "</a></nobr> ";
    }
    html += "<nobr><a href=\"/?tab=wifi&lang=" + lang + "\" class=\"" + (activeTab == "wifi" ? "tab-active" : "tab-inactive") + "\"><img src=\"/icon/wifi.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "와이파이 설정" : "Wi-Fi Setup") + "</a></nobr> ";
    html += "<nobr><a href=\"/?tab=settings&lang=" + lang + "\" class=\"" + (activeTab == "settings" ? "tab-active" : "tab-inactive") + "\"><img src=\"/icon/setup.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "시스템 설정" : "Settings") + "</a></nobr> ";
    html += "<nobr><a href=\"/?tab=piano&lang=" + lang + "\" id=\"tabPiano\" class=\"" + (activeTab == "piano" ? "tab-active" : "tab-inactive") + "\" style=\"" + (activeTab == "piano" ? "" : "display:none;") + "\"><img src=\"/icon/piano.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "가상 피아노" : "Virtual Piano") + "</a></nobr>\n";
    html += "</td>\n";
    html += "<td nowrap align=\"right\" valign=\"top\">\n";
    html += "<font size=\"1\">Language: </font>";
    html += "<a href=\"/?tab=" + activeTab + "&lang=ko\" class=\"btn98\" style=\"padding:2px 5px; font-size:11px;" + (isKo ? "font-weight:bold; background-color:#b0b0b0;" : "") + "\"><img src=\"/icon/korean.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">한국어</a> ";
    html += "<a href=\"/?tab=" + activeTab + "&lang=en\" class=\"btn98\" style=\"padding:2px 5px; font-size:11px;" + (!isKo ? "font-weight:bold; background-color:#b0b0b0;" : "") + "\"><img src=\"/icon/english.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">English</a>\n";
    html += "</td>\n";
    html += "</tr>\n";
    html += "</table>\n<br>\n";

    // 2. 메인 컨텐츠 영역
    html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\">\n<tr><td>\n";

    // ---------------- [탭 1: MIDI 플레이어] ----------------
    if (activeTab == "player") {
        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/music.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "표준 MIDI 파일 재생기 (SMF)" : "Standard MIDI File Player") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";

        html += "<form action=\"/upload_midi?lang=" + lang + "\" method=\"POST\" enctype=\"multipart/form-data\">\n";
        html += "<table width=\"100%\" cellpadding=\"2\" cellspacing=\"0\">\n";
        html += "<tr>\n";
        html += "<td width=\"140\" nowrap valign=\"middle\"><b>" + String(isKo ? "MIDI 파일 선택 (.mid):" : "Select .MID File:") + "</b></td>\n";
        html += "<td valign=\"middle\"><input type=\"file\" name=\"file\" size=\"15\" style=\"font-size:11px;\"></td>\n";
        html += "<td align=\"right\" nowrap valign=\"middle\"><button type=\"submit\" class=\"btn98\" style=\"font-weight:bold;\"><img src=\"/icon/upload.gif\" width=\"16\" height=\"16\" align=\"absmiddle\" border=\"0\" alt=\"\"> " + String(isKo ? "업로드 및 재생" : "Upload & Play") + "</button></td>\n";
        html += "</tr>\n";
        html += "</table>\n</form>\n<hr size=\"1\" color=\"#cccccc\">\n";

        html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\">\n";
        bool isGameBgm = (MIDISequencer::isLoopEnabled() || DisplayUI::getMode() == SCREEN_GAME_RUNNING || DisplayUI::getMode() == SCREEN_MENU_GAMES);
        String trackTitle = isGameBgm ? (isKo ? "아케이드 게임 실행 중 (웹 제어 비활성화)" : "Arcade Game Active (Web Control Disabled)") : String(MIDISequencer::getCurrentSongName());
        html += "<tr><td>" + String(isKo ? "현재 트랙:" : "Current Track:") + " <b><font color=\"#000080\" size=\"3\"><span id=\"currentTrackTitle\">" + trackTitle + "</span></font></b></td>";
        
        SequencerState st = isGameBgm ? SEQ_STOPPED : MIDISequencer::getState();
        String stStr = (st == SEQ_PLAYING) ? "<font color=\"#008000\"><b>PLAYING</b></font>" : ((st == SEQ_PAUSED) ? "<font color=\"#ff9900\"><b>PAUSED</b></font>" : "<font color=\"#800000\"><b>STOPPED</b></font>");
        html += "<td align=\"right\">" + String(isKo ? "상태:" : "Status:") + " <span id=\"playerStatus\">" + stStr + "</span></td></tr>\n";
        html += "<tr><td colspan=\"2\" style=\"padding-top:8px;\">\n";
        if (!isGameBgm) {
            html += "<button type=\"button\" onclick=\"sendApi('/action?cmd=play&ajax=1'); updatePlayerStatus('PLAYING');\" class=\"btn98\" style=\"font-weight:bold;\"><img src=\"/icon/play.gif\" width=\"16\" height=\"16\" align=\"absmiddle\" border=\"0\" alt=\"\"> " + String(isKo ? "재생" : "Play") + "</button> ";
            html += "<button type=\"button\" onclick=\"sendApi('/action?cmd=pause&ajax=1'); updatePlayerStatus('PAUSED');\" class=\"btn98\"><img src=\"/icon/pause.gif\" width=\"16\" height=\"16\" align=\"absmiddle\" border=\"0\" alt=\"\"> " + String(isKo ? "일시정지" : "Pause") + "</button> ";
            html += "<button type=\"button\" onclick=\"sendApi('/action?cmd=stop&ajax=1'); updatePlayerStatus('STOPPED');\" class=\"btn98\" style=\"color:#800000;\"><img src=\"/icon/stop.gif\" width=\"16\" height=\"16\" align=\"absmiddle\" border=\"0\" alt=\"\"> " + String(isKo ? "정지" : "Stop") + "</button>\n";
        } else {
            html += "<span style=\"color:#808080; font-size:11px;\">" + String(isKo ? "※ 기기 본체에서 게임을 종료하면 웹 플레이어가 활성화됩니다." : "※ Exit arcade game on device to enable web controls.") + "</span>\n";
        }
        html += "</td></tr>\n";
        
        // 실시간 볼륨 조절 슬라이더 바 (최신 Range + 레거시 DHTML 하이브리드)
        int curVol = AudioEngine::getMasterVolume();
        int curThumbLeft = (curVol * 130) / 100;
        html += "<tr><td colspan=\"2\" style=\"padding-top:10px;\">\n";
        html += "<table cellpadding=\"0\" cellspacing=\"0\">\n<tr>";
        html += "<td width=\"100\" nowrap><img src=\"/icon/speaker.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\"><b>" + String(isKo ? "마스터 볼륨:" : "Volume:") + "</b></td>";
        html += "<td nowrap>";
        html += "<input type=\"range\" min=\"0\" max=\"100\" value=\"" + String(curVol) + "\" id=\"volRange\" oninput=\"updateVol(this.value)\" style=\"width:180px; vertical-align:middle; cursor:pointer;\">";
        html += "<span id=\"volDhtml\" style=\"display:none;\"><span id=\"volTrack\" class=\"sl-track\" onclick=\"onTrackClick(event, 'volTrack')\"><span class=\"sl-groove\"></span><span id=\"volThumb\" class=\"sl-thumb\" style=\"left:" + String(curThumbLeft) + "px;\" onmousedown=\"return onThumbDown(event, 'volThumb')\"></span></span></span> ";
        html += "<script type=\"text/javascript\">var _r=getEl('volRange');if(_r&&_r.type!='range'){_r.style.display='none';getEl('volDhtml').style.display='inline';}</script> ";
        html += "<span id=\"volVal\" style=\"font-weight:bold; color:#000080; display:inline-block; width:45px; text-align:right; font-family:monospace;\">" + String(curVol) + "%</span></td>";
        html += "</tr></table>\n</td></tr>\n";
        
        // 빠른 사운드폰트 발음 테스트 버튼
        html += "<tr><td colspan=\"2\" style=\"padding-top:8px;\">\n";
        html += "<hr size=\"1\" color=\"#cccccc\">\n";
        html += "<b>" + String(isKo ? "오디오 테스트:" : "Audio Test:") + "</b><br>\n";
        html += "<table width=\"100%\" cellpadding=\"2\" cellspacing=\"0\" style=\"margin-top:4px; max-width:480px;\">\n";
        html += "<tr>\n";
        html += "<td width=\"50%\"><button type=\"button\" class=\"btn98\" onclick=\"onPianoTestClick()\" style=\"width:100%; font-weight:bold;\">" + String(isKo ? "피아노 C화음 (도미솔도)" : "Piano C Chord") + "</button></td>\n";
        html += "<td width=\"50%\"><button type=\"button\" class=\"btn98\" onclick=\"playTest('guitar')\" style=\"width:100%;\">" + String(isKo ? "기타 아르페지오" : "Guitar Arpeggio") + "</button></td>\n";
        html += "</tr>\n<tr>\n";
        html += "<td width=\"50%\"><button type=\"button\" class=\"btn98\" onclick=\"playTest('drum')\" style=\"width:100%;\">" + String(isKo ? "드럼 키트 (Kick/Snare)" : "Drums Kit") + "</button></td>\n";
        html += "<td width=\"50%\"><button type=\"button\" class=\"btn98\" onclick=\"playTest('stereo')\" style=\"width:100%;\">" + String(isKo ? "스테레오 테스트 (Left -> Right)" : "Stereo Test (Left -> Right)") + "</button></td>\n";
        html += "</tr>\n</table>\n";
        html += "</td></tr>\n";

        html += "</table>\n";
        html += "</td></tr></table>\n";

        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/folder.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "기기 내 저장된 MIDI 라이브러리" : "Stored MIDI Library") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<div class=\"inset-box\" style=\"height: 120px; overflow: auto;\">\n";
        html += "<table width=\"97%\" cellpadding=\"2\" cellspacing=\"0\">\n";
        html += "<tr bgcolor=\"#e0e0e0\"><th align=\"left\">" + String(isKo ? "파일명" : "File Name") + "</th><th width=\"55\">" + String(isKo ? "크기" : "Size") + "</th><th width=\"110\">" + String(isKo ? "관리" : "Action") + "</th></tr>\n";

        File root = LittleFS.open("/");
        int midiCount = 0;
        if (root && root.isDirectory()) {
            File f = root.openNextFile();
            while (f) {
                String name = f.name();
                if (name.endsWith(".mid") || name.endsWith(".MID")) {
                    if (name.startsWith("/")) name = name.substring(1);
                    bool isCurrent = (name == MIDISequencer::getCurrentSongName());
                    html += "<tr" + String(isCurrent ? " bgcolor=\"#e8f0fe\"" : "") + ">";
                    html += "<td style=\"word-break:break-all;\"><b>" + name + "</b></td><td align=\"center\">" + String((float)f.size() / 1024.0f, 1) + " KB</td>";
                    html += "<td align=\"center\" nowrap>";
                    html += "<button type=\"button\" onclick=\"sendApi('/action?cmd=play_midi&file=" + urlEncode(name) + "&ajax=1'); updatePlayingSong('" + name + "');\" class=\"btn98\" style=\"width:50px; padding:1px 0px;\"><img src=\"/icon/play.gif\" width=\"12\" height=\"12\" align=\"absmiddle\" border=\"0\" alt=\"\"> Play</button> ";
                    html += "<button type=\"button\" onclick=\"location.href='/action?cmd=delete_midi&file=" + urlEncode(name) + "&lang=" + lang + "';\" class=\"btn98\" style=\"width:50px; padding:1px 0px; color:#800000;\">" + String(isKo ? "삭제" : "Del") + "</button>";
                    html += "</td></tr>\n";
                    midiCount++;
                }
                f = root.openNextFile();
            }
        }
        if (!midiCount) {
            html += "<tr><td colspan=\"3\" align=\"center\" style=\"color:#808080; padding:10px;\">" + String(isKo ? "저장된 MIDI 파일이 없습니다." : "No saved MIDI files.") + "</td></tr>\n";
        }
        html += "</table>\n</div>\n</td></tr></table>\n";
    }

    // ---------------- [탭 2: 사운드폰트 관리] ----------------
    else if (activeTab == "fonts") {
        if (AudioEngine::isLoadingFont()) {
            html += "<div style=\"padding:6px 10px; background-color:#ffffe0; border:1px solid #c0c000; margin-bottom:8px;\"><b>" + String(isKo ? "사운드폰트를 메모리(PSRAM)에 적재 중입니다... (완료 시 자동 새로고침)" : "Loading SoundFont into PSRAM... (Auto refreshing)") + "</b></div>\n";
        }

        size_t totalBytes = LittleFS.totalBytes();
        size_t usedBytes = LittleFS.usedBytes();
        size_t freeBytes = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0;
        float totalMB = (float)totalBytes / (1024.0f * 1024.0f);
        float usedMB = (float)usedBytes / (1024.0f * 1024.0f);
        float freeMB = (float)freeBytes / (1024.0f * 1024.0f);
        int usedPercent = totalBytes ? (int)((usedBytes * 100) / totalBytes) : 0;

        // 플래시 스토리지 현황 박스 (90년대 클래식 게이지 바)
        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/folder.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "플래시 메모리 용량 현황 (LittleFS)" : "Flash Storage Status (LittleFS)") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<table width=\"100%\" cellpadding=\"2\" cellspacing=\"0\">\n";
        html += "<tr><td><b>" + String(isKo ? "사용량: " : "Used: ") + "</b>" + String(usedMB, 2) + " MB / " + String(totalMB, 2) + " MB (" + String(usedPercent) + "%)</td>";
        html += "<td align=\"right\"><b>" + String(isKo ? "남은 공간: " : "Free Space: ") + "</b><font color=\"#000080\"><b>" + String(freeMB, 2) + " MB</b></font></td></tr>\n";
        html += "</table>\n";
        html += "<div class=\"inset-box\" style=\"height: 16px; background-color:#ffffff; position:relative; margin-top:5px;\">\n";
        html += "<div style=\"height:100%; width:" + String(usedPercent) + "%; background-color:#000080;\"></div>\n";
        html += "</div>\n";
        html += "</td></tr></table>\n";

        // 공식 추천 사운드폰트 안내 배너
        html += "<div style=\"background-color:#e8f0fe; border:1px solid #7090d0; padding:10px 14px; margin-bottom:12px; font-size:12px; line-height:1.5;\">\n";
        html += "<b>★ " + String(isKo ? "공식 권장 사운드폰트: CT4MGM.SF2" : "Official Recommended SoundFont: CT4MGM.SF2") + "</b><br>\n";
        html += String(isKo ? "본 기기의 모든 음원 합성 엔진(GM / GS / MT-32) 및 사운드 최적화는 <b>CT4MGM.SF2</b>에 100% 최적화되어 있습니다.<br>다른 사운드폰트를 업로드하거나 교체할 경우 음량 불균형 및 MT-32 호환 기능이 비활성화되므로 <b>변경을 권장하지 않습니다.</b>" : "All synthesizer features (GM / GS / MT-32) and audio optimizations are 100% tailored to <b>CT4MGM.SF2</b>.<br>Using other SoundFonts may cause volume imbalances and will disable MT-32 optimizations. <b>Changing SoundFonts is strongly not recommended.</b>") + "\n";
        html += "</div>\n";

        // 사운드폰트 업로드 박스 (IE 4.0 호환 완성본)
        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/upload.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "사운드폰트 (.SF2) 파일 업로드" : "Install SoundFont (.SF2)") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<form action=\"/upload_font?lang=" + lang + "\" method=\"POST\" enctype=\"multipart/form-data\">\n";
        html += "<table width=\"100%\" cellpadding=\"2\" cellspacing=\"0\">\n";
        html += "<tr>\n";
        html += "<td width=\"150\" nowrap valign=\"middle\"><b>" + String(isKo ? "사운드폰트 선택:" : "Select SoundFont:") + "</b></td>\n";
        html += "<td valign=\"middle\"><input type=\"file\" name=\"file\" accept=\".sf2,.SF2\" size=\"15\" style=\"font-size:11px;\"></td>\n";
        html += "<td align=\"right\" nowrap valign=\"middle\"><button type=\"submit\" class=\"btn98\" style=\"font-weight:bold;\"><img src=\"/icon/upload.gif\" width=\"16\" height=\"16\" align=\"absmiddle\" border=\"0\" alt=\"\"> " + String(isKo ? "플래시 메모리에 저장" : "Upload to Flash") + "</button></td>\n";
        html += "</tr>\n";
        html += "</table>\n</form>\n";
        html += "</td></tr></table>\n";

        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/piano.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "플래시 메모리 사운드폰트 목록" : "SoundFonts in Flash Memory") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<div class=\"inset-box\" style=\"height: 150px; overflow: auto;\">\n";
        html += "<table width=\"100%\" cellpadding=\"3\" cellspacing=\"0\">\n";
        html += "<tr bgcolor=\"#e0e0e0\"><th align=\"left\">" + String(isKo ? "사운드폰트 이름" : "SoundFont Name") + "</th><th width=\"90\">" + String(isKo ? "크기" : "Size") + "</th><th width=\"70\">" + String(isKo ? "상태" : "Status") + "</th><th width=\"110\">" + String(isKo ? "관리" : "Action") + "</th></tr>\n";

        File root = LittleFS.open("/");
        int sfCount = 0;
        if (root && root.isDirectory()) {
            File f = root.openNextFile();
            while (f) {
                String name = f.name();
                if (name.endsWith(".sf2") || name.endsWith(".SF2")) {
                    if (name.startsWith("/")) name = name.substring(1);
                    bool isActive = (name == AudioEngine::getCurrentFontName());
                    bool isCoreFont = (name.indexOf("CT4MGM") >= 0 || name.indexOf("ct4mgm") >= 0);
                    html += "<tr" + String(isActive ? " bgcolor=\"#e8f0fe\"" : "") + ">";
                    html += "<td><b>" + name + "</b>" + String(isCoreFont ? " <font color=\"#000080\">[" + String(isKo ? "공식 기본" : "Core") + "]</font>" : "") + "</td>";
                    html += "<td align=\"center\">" + String((float)f.size() / (1024.0f * 1024.0f), 2) + " MB</td>";
                    html += "<td align=\"center\">" + String(isActive ? "<font color=\"#008000\"><b>ACTIVE</b></font>" : "-") + "</td>";
                    html += "<td align=\"center\">";
                    if (!isActive) {
                        html += "<a href=\"/action?cmd=select_font&name=" + urlEncode(name) + "&lang=" + lang + "\" class=\"btn98\" style=\"padding:1px 5px;\">" + String(isKo ? "로드" : "Load") + "</a> ";
                    }
                    if (isCoreFont) {
                        html += "<span style=\"color:#888888; font-size:11px;\">[" + String(isKo ? "보호됨" : "Protected") + "]</span>";
                    } else {
                        html += "<a href=\"/action?cmd=delete_font&name=" + urlEncode(name) + "&lang=" + lang + "\" class=\"btn98\" style=\"color:#800000; padding:1px 5px;\">" + String(isKo ? "삭제" : "Del") + "</a>";
                    }
                    html += "</td></tr>\n";
                    sfCount++;
                }
                f = root.openNextFile();
            }
        }
        if (!sfCount) {
            html += "<tr><td colspan=\"4\" align=\"center\" style=\"color:#808080; padding:10px;\">" + String(isKo ? "저장된 사운드폰트가 없습니다." : "No SoundFonts found.") + "</td></tr>\n";
        }
        html += "</table>\n</div>\n";

        // 사운드폰트 저작권 및 면책 고지문 (Copyright Notice)
        html += "<div style=\"background-color:#f8f9fa; border:1px solid #cccccc; padding:8px 12px; margin-top:10px; font-size:11px; color:#555555; line-height:1.4;\">\n";
        html += "<b>SoundFont Attribution & Copyright Disclaimer</b><br>\n";
        html += "• <b>CT4MGM.SF2</b> is based on the Creative Technology Ltd. / E-mu Systems SoundFont format.<br>\n";
        html += "• This SoundFont is bundled for personal evaluation, educational, and non-commercial research purposes only.<br>\n";
        html += "• All trademarks, sound sample copyrights, and intellectual properties belong to their respective copyright holders.\n";
        html += "</div>\n";

        html += "</td></tr></table>\n";
    }

    // ---------------- [탭 3: Wi-Fi 설정] ----------------
    else if (activeTab == "wifi") {
        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/wifi.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "주변 무선 네트워크 (2.4GHz Wi-Fi)" : "Nearby Wireless Networks") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<table width=\"100%\" cellpadding=\"2\" cellspacing=\"0\" style=\"margin-bottom:6px;\">\n";
        if (WiFiManager::isScanning()) {
            html += "<tr><td><b><font color=\"#000080\">" + String(isKo ? "주변 Wi-Fi 네트워크 검색 중... (완료 시 자동 새로고침)" : "Scanning nearby networks... (Auto refreshing)") + "</font></b></td>";
            html += "<td align=\"right\"><span class=\"btn98\" style=\"color:#808080; border-color:#808080 #ffffff #ffffff #808080;\">" + String(isKo ? "검색 중..." : "Scanning...") + "</span></td></tr>\n";
        } else {
            html += "<tr><td>" + String(isKo ? "주변의 2.4GHz Wi-Fi 공유기를 검색합니다." : "Search nearby 2.4GHz Access Points.") + "</td>";
            html += "<td align=\"right\"><a href=\"/action?cmd=scan_wifi&lang=" + lang + "\" class=\"btn98\" style=\"font-weight:bold;\"><img src=\"/icon/wifi.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "Wi-Fi 검색" : "Scan Networks") + "</a></td></tr>\n";
        }
        html += "</table>\n";

        html += "<div class=\"inset-box\" style=\"height: 120px; overflow: auto;\">\n";
        html += "<table width=\"100%\" cellpadding=\"3\" cellspacing=\"0\">\n";
        html += "<tr bgcolor=\"#e0e0e0\"><th align=\"left\">SSID (Network Name)</th><th width=\"90\">" + String(isKo ? "신호세기" : "Signal") + "</th><th width=\"80\">" + String(isKo ? "보안" : "Security") + "</th></tr>\n";

        auto nets = WiFiManager::getCachedScanResults();
        if (nets.size() > 0) {
            for (const auto& n : nets) {
                html += "<tr><td><a href=\"/?tab=wifi&sel_ssid=" + urlEncode(n.ssid) + "&lang=" + lang + "\" style=\"color:#000080;\"><img src=\"/icon/wifi.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\"><b>" + n.ssid + "</b></a></td>";
                html += "<td align=\"center\">" + String(n.rssi) + " dBm</td>";
                html += "<td align=\"center\">" + String(n.isEncrypted ? "WPA" : "Open") + "</td></tr>\n";
            }
        } else {
            html += "<tr><td colspan=\"3\" align=\"center\" style=\"color:#808080; padding:10px;\">" + String(isKo ? "[Wi-Fi 검색] 버튼을 누르면 주변 공유기가 탐색됩니다." : "Click [Scan Networks] to find nearby Wi-Fi.") + "</td></tr>\n";
        }
        html += "</table>\n</div>\n</td></tr></table>\n";

        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/setup.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "Wi-Fi 공유기 접속 설정" : "Connect to Network") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<form action=\"/connect_wifi?lang=" + lang + "\" method=\"POST\">\n";
        html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\">\n";
        html += "<tr><td width=\"100\"><b>SSID:</b></td><td><input type=\"text\" name=\"ssid\" value=\"" + selSsid + "\" style=\"width:250px;\" class=\"inset-box\"></td></tr>\n";
        html += "<tr><td><b>" + String(isKo ? "비밀번호:" : "Password:") + "</b></td><td><input type=\"password\" name=\"pass\" style=\"width:250px;\" class=\"inset-box\"></td></tr>\n";
        html += "<tr><td></td><td style=\"padding-top:6px;\"><button type=\"submit\" class=\"btn98\" style=\"font-weight:bold;\"><img src=\"/icon/setup.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "접속 및 NVRAM 영구 저장" : "Connect & Save to NVRAM") + "</button></td></tr>\n";
        html += "</table>\n</form>\n</td></tr></table>\n";

        // ---------------- [현재 연결된 Wi-Fi 상세 정보] ----------------
        bool isAP = WiFiManager::isAPMode();
        String currentSsid = WiFiManager::getSSID();
        String currentIp = WiFiManager::getIPAddress();
        String subnet = isAP ? "255.255.255.0" : WiFi.subnetMask().toString();
        String gateway = isAP ? currentIp : WiFi.gatewayIP().toString();
        String dns1 = isAP ? "-" : WiFi.dnsIP().toString();
        String dns2 = isAP ? "-" : WiFi.dnsIP(1).toString();
        String mac = isAP ? WiFi.softAPmacAddress() : WiFi.macAddress();
        String modeStr = isAP ? (isKo ? "SoftAP (자체 AP 모드)" : "SoftAP (Access Point)") 
                              : (isKo ? "STA (공유기 연결 모드)" : "STA (Station Mode)");

        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/port.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "현재 연결된 네트워크 상세 정보" : "Active Network Details") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<table width=\"100%\" cellpadding=\"3\" cellspacing=\"0\">\n";
        html += "<tr><td width=\"120\"><b>" + String(isKo ? "작동 모드:" : "Mode:") + "</b></td><td><font color=\"#000080\"><b>" + modeStr + "</b></font></td>";
        html += "<td width=\"110\"><b>" + String(isKo ? "MAC 주소:" : "MAC Address:") + "</b></td><td><code>" + mac + "</code></td></tr>\n";
        html += "<tr><td><b>" + String(isKo ? "네트워크 (SSID):" : "SSID:") + "</b></td><td><b>" + currentSsid + "</b></td>";
        html += "<td><b>" + String(isKo ? "신호 세기:" : "Signal (RSSI):") + "</b></td><td>" + (isAP ? "-" : (String(WiFi.RSSI()) + " dBm")) + "</td></tr>\n";
        html += "<tr><td><b>" + String(isKo ? "IP 주소:" : "IP Address:") + "</b></td><td><font color=\"#008000\"><b>" + currentIp + "</b></font></td>";
        html += "<td><b>" + String(isKo ? "서브넷 마스크:" : "Subnet Mask:") + "</b></td><td>" + subnet + "</td></tr>\n";
        html += "<tr><td><b>" + String(isKo ? "기본 게이트웨이:" : "Gateway:") + "</b></td><td>" + gateway + "</td>";
        html += "<td><b>" + String(isKo ? "기본 DNS:" : "Primary DNS:") + "</b></td><td>" + dns1 + "</td></tr>\n";
        if (!isAP && dns2 != "0.0.0.0" && dns2.length() > 0) {
            html += "<tr><td><b>" + String(isKo ? "보조 DNS:" : "Secondary DNS:") + "</b></td><td colspan=\"3\">" + dns2 + "</td></tr>\n";
        }
        html += "</table>\n</td></tr></table>\n";
    }

    // ---------------- [탭 4: 시스템 설정] ----------------
    else if (activeTab == "settings") {
        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/speaker.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "마스터 출력 볼륨 (Master Volume)" : "Master Output Volume") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        int curVol = AudioEngine::getMasterVolume();
        int curThumbLeft = (curVol * 130) / 100;
        html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\">\n";
        html += "<tr><td width=\"120\" nowrap><b>" + String(isKo ? "실시간 볼륨 조절:" : "Live Volume:") + "</b></td>";
        html += "<td nowrap>";
        html += "<input type=\"range\" min=\"0\" max=\"100\" value=\"" + String(curVol) + "\" id=\"volRange2\" oninput=\"updateVol(this.value)\" style=\"width:220px; vertical-align:middle; cursor:pointer;\">";
        html += "<span id=\"volDhtml2\" style=\"display:none;\"><span id=\"volTrack2\" class=\"sl-track\" onclick=\"onTrackClick(event, 'volTrack2')\"><span class=\"sl-groove\"></span><span id=\"volThumb2\" class=\"sl-thumb\" style=\"left:" + String(curThumbLeft) + "px;\" onmousedown=\"return onThumbDown(event, 'volThumb2')\"></span></span></span> ";
        html += "<script type=\"text/javascript\">var _r2=getEl('volRange2');if(_r2&&_r2.type!='range'){_r2.style.display='none';getEl('volDhtml2').style.display='inline';}</script> ";
        html += "<span id=\"volVal2\" style=\"font-weight:bold; color:#000080; display:inline-block; width:45px; text-align:right; font-family:monospace;\">" + String(curVol) + "%</span></td></tr>\n";
        html += "</table>\n";
        html += "</td></tr></table>\n";

        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/port.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "시리얼 MIDI 통신 포트 (UART2)" : "Serial MIDI Port (UART2)") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<form action=\"/action\" method=\"GET\">\n";
        html += "<input type=\"hidden\" name=\"tab\" value=\"settings\">\n";
        html += "<input type=\"hidden\" name=\"cmd\" value=\"set_baud\">\n";
        html += "<input type=\"hidden\" name=\"lang\" value=\"" + lang + "\">\n";
        html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\">\n";
        html += "<tr><td width=\"120\">Baud Rate:</td>";
        html += "<td><select name=\"baud\" class=\"inset-box\" style=\"width:240px;\">\n";
        html += "<option value=\"38400\"" + String(MIDIParser::getBaudRate() == 38400 ? " selected" : "") + ">38,400 bps (SoftMPU / DOS Serial COM)</option>\n";
        html += "<option value=\"31250\"" + String(MIDIParser::getBaudRate() == 31250 ? " selected" : "") + ">31,250 bps (Standard MIDI In)</option>\n";
        html += "<option value=\"115200\"" + String(MIDIParser::getBaudRate() == 115200 ? " selected" : "") + ">115,200 bps (High Speed MIDI)</option>\n";
        html += "</select> <button type=\"submit\" class=\"btn98\">" + String(isKo ? "적용" : "Apply") + "</button></td></tr>\n";
        html += "</table>\n</form>\n</td></tr></table>\n";

        // ---------------- [소리 출력 설정 (오디오 모드)] ----------------
        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/speaker.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "소리 출력 설정 (Audio Output Mode)" : "Audio Output Mode") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<form action=\"/action\" method=\"GET\">\n";
        html += "<input type=\"hidden\" name=\"tab\" value=\"settings\">\n";
        html += "<input type=\"hidden\" name=\"cmd\" value=\"set_audio_mode\">\n";
        html += "<input type=\"hidden\" name=\"lang\" value=\"" + lang + "\">\n";
        html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\">\n";
        html += "<tr><td width=\"120\"><b>" + String(isKo ? "오디오 모드:" : "Audio Mode:") + "</b></td>";
        html += "<td><label><input type=\"radio\" name=\"audio_mode\" value=\"stereo\"" + String(!AudioEngine::isMonoMode() ? " checked" : "") + "> " + String(isKo ? "스테레오" : "Stereo") + "</label> &nbsp;&nbsp; ";
        html += "<label><input type=\"radio\" name=\"audio_mode\" value=\"mono\"" + String(AudioEngine::isMonoMode() ? " checked" : "") + "> " + String(isKo ? "모노" : "Mono") + "</label> &nbsp;&nbsp; ";
        html += "<button type=\"submit\" class=\"btn98\" style=\"font-weight:bold;\">" + String(isKo ? "설정 저장" : "Save Mode") + "</button>";
        if (AudioEngine::isHardwareMonoDetected()) {
            html += " &nbsp;<font color=\"#800000\" size=\"1\"><b>[" + String(isKo ? "외장 모노 스피커 감지됨 - 하드웨어 자동 모노 활성 중" : "Ext Mono Speaker Detected - Auto Mono Active") + "]</b></font>";
        }
        html += "</td></tr>\n";
        html += "</table>\n</form>\n</td></tr></table>\n";

        // ---------------- [LED 상태표시등 설정] ----------------
        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/lamp.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "LED 상태표시등" : "LED Status Indicator") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<form action=\"/action\" method=\"GET\">\n";
        html += "<input type=\"hidden\" name=\"tab\" value=\"settings\">\n";
        html += "<input type=\"hidden\" name=\"cmd\" value=\"set_led\">\n";
        html += "<input type=\"hidden\" name=\"lang\" value=\"" + lang + "\">\n";
        html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\">\n";
        html += "<tr><td width=\"120\"><b>" + String(isKo ? "LED 상태표시등:" : "LED Status Lamp:") + "</b></td>";
        html += "<td><label><input type=\"radio\" name=\"led_en\" value=\"1\"" + String(LEDIndicator::isEnabled() ? " checked" : "") + "> " + String(isKo ? "켜기 (정상 표시)" : "Enabled (Normal Display)") + "</label> &nbsp;&nbsp; ";
        html += "<label><input type=\"radio\" name=\"led_en\" value=\"0\"" + String(!LEDIndicator::isEnabled() ? " checked" : "") + "> " + String(isKo ? "끄기 (LED 소등)" : "Disabled (Always Off)") + "</label> &nbsp;&nbsp; ";
        html += "<button type=\"submit\" class=\"btn98\" style=\"font-weight:bold;\">" + String(isKo ? "설정 저장" : "Save LED Config") + "</button></td></tr>\n";
        html += "</table>\n</form>\n</td></tr></table>\n";

        // ---------------- [시간 및 NTP 동기화 설정] ----------------
        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/setup.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "시스템 시간 및 NTP 동기화 설정" : "System Time & NTP Configuration") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<form action=\"/action\" method=\"GET\">\n";
        html += "<input type=\"hidden\" name=\"tab\" value=\"settings\">\n";
        html += "<input type=\"hidden\" name=\"cmd\" value=\"set_ntp\">\n";
        html += "<input type=\"hidden\" name=\"lang\" value=\"" + lang + "\">\n";
        html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\">\n";
        html += "<tr><td width=\"130\"><b>" + String(isKo ? "현재 시스템 시간:" : "Current System Time:") + "</b></td>";
        html += "<td><font color=\"#000080\" size=\"+1\"><b>" + TimeManager::getFormattedTime() + "</b></font></td></tr>\n";
        
        html += "<tr><td><b>" + String(isKo ? "동기화 방식:" : "Time Sync Mode:") + "</b></td>";
        html += "<td><label><input type=\"radio\" name=\"auto_ntp\" value=\"1\"" + String(TimeManager::isAutoNTP() ? " checked" : "") + "> " + String(isKo ? "인터넷 자동 동기화 (NTP)" : "Auto Internet Sync (NTP)") + "</label> &nbsp;&nbsp; ";
        html += "<label><input type=\"radio\" name=\"auto_ntp\" value=\"0\"" + String(!TimeManager::isAutoNTP() ? " checked" : "") + "> " + String(isKo ? "수동 시간 설정" : "Manual Time") + "</label></td></tr>\n";

        html += "<tr><td><b>" + String(isKo ? "표준시간대 (UTC):" : "Timezone (UTC):") + "</b></td>";
        html += "<td><select name=\"utc_off\" class=\"inset-box\" style=\"width:240px;\">\n";
        for (int tz = -12; tz <= 14; tz++) {
            String sign = (tz >= 0) ? "+" : "";
            String label = "UTC" + sign + String(tz);
            if (tz == 9) label += " (Korea, Japan, Tokyo, Seoul)";
            else if (tz == 0) label += " (London, GMT, UTC)";
            else if (tz == 1) label += " (Berlin, Paris, Rome)";
            else if (tz == 8) label += " (Beijing, Singapore, Hong Kong)";
            else if (tz == -5) label += " (New York, Eastern Time)";
            else if (tz == -8) label += " (Los Angeles, Pacific Time)";
            html += "<option value=\"" + String(tz) + "\"" + String(TimeManager::getUTCOffset() == tz ? " selected" : "") + ">" + label + "</option>\n";
        }
        html += "</select></td></tr>\n";

        html += "<tr><td><b>" + String(isKo ? "공인 NTP 서버:" : "NTP Server:") + "</b></td>";
        html += "<td><select name=\"ntp_srv\" class=\"inset-box\" style=\"width:240px;\">\n";
        for (int i = 0; i < TimeManager::NTP_SERVER_COUNT; i++) {
            const char* srv = TimeManager::NTP_SERVERS[i];
            html += "<option value=\"" + String(srv) + "\"" + String(TimeManager::getNTPServer() == srv ? " selected" : "") + ">" + String(srv) + "</option>\n";
        }
        html += "</select> <button type=\"submit\" class=\"btn98\" style=\"font-weight:bold;\">" + String(isKo ? "NTP 설정 저장" : "Save NTP Config") + "</button></td></tr>\n";
        html += "</table>\n</form>\n";

        // 수동 시간 직접 입력 폼
        html += "<hr style=\"border:0; border-top:1px dashed #999999; margin:8px 0;\">\n";
        html += "<form action=\"/action\" method=\"GET\">\n";
        html += "<input type=\"hidden\" name=\"tab\" value=\"settings\">\n";
        html += "<input type=\"hidden\" name=\"cmd\" value=\"set_manual_time\">\n";
        html += "<input type=\"hidden\" name=\"lang\" value=\"" + lang + "\">\n";
        html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\">\n";
        html += "<tr><td width=\"130\"><b>" + String(isKo ? "수동 날짜/시간 입력:" : "Manual Date/Time:") + "</b></td>";
        html += "<td><input type=\"datetime-local\" name=\"datetime\" step=\"1\" class=\"inset-box\" style=\"width:240px;\"> ";
        html += "<button type=\"submit\" class=\"btn98\">" + String(isKo ? "수동 시간 즉시 적용" : "Set Manual Time") + "</button></td></tr>\n";
        html += "</table>\n</form>\n";
        html += "</td></tr></table>\n";

        // ---------------- [사운드폰트 고급 관리 모드 설정] ----------------
        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/piano.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "사운드폰트 고급 관리 모드 (SoundFont Advanced Mode)" : "SoundFont Advanced Mode") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<form action=\"/action\" method=\"GET\">\n";
        html += "<input type=\"hidden\" name=\"tab\" value=\"settings\">\n";
        html += "<input type=\"hidden\" name=\"cmd\" value=\"set_font_mgmt\">\n";
        html += "<input type=\"hidden\" name=\"lang\" value=\"" + lang + "\">\n";
        html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\">\n";
        html += "<tr><td width=\"130\" valign=\"top\"><b>" + String(isKo ? "사운드폰트 탭:" : "SoundFont Tab:") + "</b></td>";
        html += "<td><label><input type=\"radio\" name=\"font_mgmt_en\" id=\"fm_0\" value=\"0\"" + String(!g_font_mgmt_enabled ? " checked" : "") + "> <b>" + String(isKo ? "비활성화 (기본 권장 - CT4MGM 최적화 고정)" : "Disabled (Recommended - Locked to CT4MGM)") + "</b></label><br>\n";
        html += "<label><input type=\"radio\" name=\"font_mgmt_en\" id=\"fm_1\" value=\"1\"" + String(g_font_mgmt_enabled ? " checked" : "") + " onclick=\"return onToggleFontMgmt(this)\" onchange=\"onToggleFontMgmt(this)\"> <b>" + String(isKo ? "고급 모드 활성화 (사운드폰트 업로드/삭제 메뉴 노출)" : "Enabled (Expose Upload / Delete Menu)") + "</b></label></td></tr>\n";
        html += "<tr><td></td><td><button type=\"submit\" class=\"btn98\" style=\"font-weight:bold;\">" + String(isKo ? "설정 저장" : "Save Setting") + "</button></td></tr>\n";
        html += "</table>\n</form>\n";
        html += "<div style=\"font-size:11px; color:#555555; line-height:1.4; margin-top:6px;\">\n";
        html += "• " + String(isKo ? "비활성화 시 웹 상단 및 OLED 메뉴에서 사운드폰트 탭/메뉴가 <b>완전히 숨겨집니다.</b>" : "When disabled, SoundFont tab is <b>completely hidden</b> from Web and OLED menus.") + "<br>\n";
        html += "• " + String(isKo ? "본 기기는 <b>CT4MGM.SF2</b>에 100% 최적화되어 있으므로, 특별한 사유가 없는 한 변경하지 마십시오." : "This device is 100% tailored to <b>CT4MGM.SF2</b>. Changing SoundFonts is not recommended.") + "\n";
        html += "</div>\n";
        html += "</td></tr></table>\n";

        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/warning.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "하드웨어 제어 및 진단" : "Diagnostics") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 10px;\">\n";
        html += "<a href=\"/action?cmd=panic&tab=settings&lang=" + lang + "\" class=\"btn98\" style=\"color:#800000; font-weight:bold;\"><img src=\"/icon/warning.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "MIDI 패닉 (강제 무음 리셋 / All Notes Off)" : "MIDI Panic (All Sound Off)") + "</a>\n";
        html += "</td></tr></table>\n";
    }

    // ---------------- [탭 5: 히든 가상 피아노 건반 (Easter Egg)] ----------------
    else if (activeTab == "piano") {
        html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
        html += "<tr><td class=\"section-hdr\"><img src=\"/icon/piano.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "히든 메뉴! 가상 피아노" : "Hidden Menu! Virtual Piano") + "</td></tr>\n";
        html += "<tr><td style=\"padding: 15px;\">\n";

        // 상단 컨트롤 바 (악기 선택, 옥타브 쉬프트)
        html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\" style=\"margin-bottom:12px;\">\n";
        html += "<tr><td width=\"110\"><b>" + String(isKo ? "GM 악기 선택:" : "Instrument:") + "</b></td>";
        html += "<td><select onchange=\"changeProg(this.value)\" class=\"inset-box\" style=\"width:240px; font-weight:bold;\">\n";
        html += "<option value=\"0\">0: Acoustic Grand Piano</option>\n";
        html += "<option value=\"4\">4: Electric Piano 1 (Rhodes)</option>\n";
        html += "<option value=\"5\">5: Electric Piano 2 (DX7 FM)</option>\n";
        html += "<option value=\"6\">6: Harpsichord</option>\n";
        html += "<option value=\"11\">11: Vibraphone</option>\n";
        html += "<option value=\"16\">16: Drawbar Organ</option>\n";
        html += "<option value=\"19\">19: Church Pipe Organ</option>\n";
        html += "<option value=\"24\">24: Nylon String Guitar</option>\n";
        html += "<option value=\"27\">27: Clean Electric Guitar</option>\n";
        html += "<option value=\"30\">30: Distortion Guitar</option>\n";
        html += "<option value=\"33\">33: Electric Finger Bass</option>\n";
        html += "<option value=\"40\">40: Violin</option>\n";
        html += "<option value=\"48\">48: String Ensemble 1</option>\n";
        html += "<option value=\"52\">52: Choir Aahs</option>\n";
        html += "<option value=\"56\">56: Trumpet</option>\n";
        html += "<option value=\"65\">65: Alto Sax</option>\n";
        html += "<option value=\"73\">73: Flute</option>\n";
        html += "<option value=\"80\">80: Lead 1 (Square)</option>\n";
        html += "<option value=\"81\">81: Lead 2 (Sawtooth)</option>\n";
        html += "<option value=\"88\">88: Pad 1 (New Age)</option>\n";
        html += "</select></td>\n";

        html += "<td align=\"right\"><b>" + String(isKo ? "옥타브:" : "Octave:") + "</b> ";
        html += "<button type=\"button\" class=\"btn98\" onclick=\"shiftOct(-1)\" style=\"font-weight:bold;\">◀ Oct -1</button> ";
        html += "<span id=\"octLabel\" style=\"display:inline-block; width:80px; text-align:center; font-weight:bold; color:#000080;\">C4 ~ B5</span> ";
        html += "<button type=\"button\" class=\"btn98\" onclick=\"shiftOct(1)\" style=\"font-weight:bold;\">Oct +1 ▶</button></td></tr>\n";
        html += "</table>\n";

        // 안내문
        html += "<div style=\"margin-bottom:12px; color:#333333; font-size:11px;\">" + 
                String(isKo ? "<img src=\"/icon/lamp.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\"><b>키보드 연주 지원:</b> PC 키보드 <code>A S D F G H J K L ; '</code> 및 <code>W E T Y U O P ]</code> 키를 누르면 실시간으로 건반이 연주됩니다! (마우스 클릭/터치 가능)"
                            : "<img src=\"/icon/lamp.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\"><b>Keyboard Supported:</b> Play notes using keys <code>A S D F G H J K L ; '</code> and <code>W E T Y U O P ]</code>! Mouse and touch also supported.") + 
                "</div>\n";

        // 2옥타브 인터랙티브 건반 렌더링
        html += "<div class=\"piano-box\">\n";
        html += "<div class=\"piano-kb\">\n";

        // 하얀 건반 14개 (공백 누적 제거를 위해 개행 없이 직렬 렌더링)
        const char* wNotes[] = {"C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5", "E5", "F5", "G5", "A5", "B5"};
        const char* wKeys[]  = {"A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "Z", "X", "C"};
        int wMidi[]          = {60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79, 81, 83};

        for (int i = 0; i < 14; i++) {
            html += "<div class=\"w-key\" id=\"k_" + String(wMidi[i]) + "\" style=\"left:" + String(i * 34) + "px;\" onmousedown=\"pianoNoteOn(" + String(wMidi[i]) + ")\" onmouseup=\"pianoNoteOff(" + String(wMidi[i]) + ")\" onmouseout=\"pianoNoteOff(" + String(wMidi[i]) + ")\" ontouchstart=\"pianoNoteOn(" + String(wMidi[i]) + "); event.preventDefault();\" ontouchend=\"pianoNoteOff(" + String(wMidi[i]) + ")\">";
            html += "<span class=\"w-hk\">[" + String(wKeys[i]) + "]</span><span class=\"w-lbl\">" + String(wNotes[i]) + "</span></div>";
        }

        // 검은 건반 10개 (34px 하얀 건반 경계선 중심에 완벽 정렬)
        struct BlackKeyDef { int midi; const char* note; const char* key; int pos; };
        BlackKeyDef bKeysDef[] = {
            {61, "C#4", "W", 23},
            {63, "D#4", "E", 57},
            {66, "F#4", "T", 125},
            {68, "G#4", "Y", 159},
            {70, "A#4", "U", 193},
            {73, "C#5", "O", 261},
            {75, "D#5", "P", 295},
            {78, "F#5", "]", 363},
            {80, "G#5", "N", 397},
            {82, "A#5", "M", 431}
        };

        for (int i = 0; i < 10; i++) {
            html += "<div class=\"b-key\" id=\"k_" + String(bKeysDef[i].midi) + "\" style=\"left:" + String(bKeysDef[i].pos) + "px;\" onmousedown=\"pianoNoteOn(" + String(bKeysDef[i].midi) + ")\" onmouseup=\"pianoNoteOff(" + String(bKeysDef[i].midi) + ")\" onmouseout=\"pianoNoteOff(" + String(bKeysDef[i].midi) + ")\" ontouchstart=\"pianoNoteOn(" + String(bKeysDef[i].midi) + "); event.preventDefault();\" ontouchend=\"pianoNoteOff(" + String(bKeysDef[i].midi) + ")\">";
            html += "<span class=\"b-hk\">[" + String(bKeysDef[i].key) + "]</span><span class=\"b-lbl\">" + String(bKeysDef[i].note) + "</span></div>";
        }

        html += "</div>\n</div>\n";
        html += "</td></tr></table>\n";
    }

    html += "</td></tr></table>\n";

    // 3. 하단 상태 표시줄
    html += "<table width=\"100%\" cellpadding=\"3\" cellspacing=\"0\" class=\"status-bar\">\n<tr>\n";
    html += "<td nowrap>Active Font: <b>" + String(AudioEngine::getCurrentFontName()) + "</b></td>\n";
    html += "<td nowrap align=\"center\">IP: <b>" + WiFiManager::getIPAddress() + "</b> (" + String(WiFiManager::getModeString()) + ")</td>\n";
    html += "<td nowrap align=\"right\">MIDI: <b>" + String(MIDIParser::getBaudRate()) + " bps</b></td>\n";
    html += "</tr></table>\n";

    // 4. 레트로 브라우저 권장 문구 및 저작권 표시 (Copyright)
    html += "<div align=\"center\" style=\"font-size:11px; color:#777777; margin-top:10px; margin-bottom:4px; line-height:1.4;\">\n";
    html += String(isKo ? "본 사이트는 <b>Microsoft Internet Explorer 4.0 이상</b>, 800×600 해상도에 최적화되어 있습니다." 
                        : "This site is best viewed with <b>Microsoft Internet Explorer 4.0 or higher</b> (800x600 resolution).") + "<br>\n";
    html += "Copyright (C) 1998 Nexisson Tech Co., Ltd. All rights reserved.\n";
    html += "</div>\n";

    html += "</body>\n</html>\n";
    return html;
}

void WebManager::begin() {
    Preferences prefs;
    prefs.begin("system", true);
    g_font_mgmt_enabled = prefs.getBool("font_mgmt_en", false);
    prefs.end();
    DisplayUI::setFontManagementEnabled(g_font_mgmt_enabled);

    // 1. 로고 이미지 라우트 (/logo.gif)
    server.on("/logo.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_LOGO_GIF_DATA, ICON_LOGO_GIF_SIZE);
    });

    // 2. 16x16 아이콘 이미지 라우트 등록 (/icon/...gif)
    server.on("/icon/music.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_MUSIC_GIF_DATA, ICON_MUSIC_GIF_SIZE);
    });
    server.on("/icon/piano.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_PIANO_GIF_DATA, ICON_PIANO_GIF_SIZE);
    });
    server.on("/icon/wifi.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_WIFI_GIF_DATA, ICON_WIFI_GIF_SIZE);
    });
    server.on("/icon/setup.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_SETUP_GIF_DATA, ICON_SETUP_GIF_SIZE);
    });
    server.on("/icon/korean.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_KOREAN_GIF_DATA, ICON_KOREAN_GIF_SIZE);
    });
    server.on("/icon/english.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_ENGLIGH_GIF_DATA, ICON_ENGLIGH_GIF_SIZE);
    });
    server.on("/icon/play.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_PLAY_GIF_DATA, ICON_PLAY_GIF_SIZE);
    });
    server.on("/icon/pause.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_PAUSE_GIF_DATA, ICON_PAUSE_GIF_SIZE);
    });
    server.on("/icon/stop.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_STOP_GIF_DATA, ICON_STOP_GIF_SIZE);
    });
    server.on("/icon/upload.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_UPLOAD_GIF_DATA, ICON_UPLOAD_GIF_SIZE);
    });
    server.on("/icon/folder.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_FOLDER_GIF_DATA, ICON_FOLDER_GIF_SIZE);
    });
    server.on("/icon/speaker.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_SPEAKER_GIF_DATA, ICON_SPEAKER_GIF_SIZE);
    });
    server.on("/icon/port.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_PORT_GIF_DATA, ICON_PORT_GIF_SIZE);
    });
    server.on("/icon/warning.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_WARNING_GIF_DATA, ICON_WARNING_GIF_SIZE);
    });
    server.on("/icon/lamp.gif", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "image/gif", ICON_LAMP_GIF_DATA, ICON_LAMP_GIF_SIZE);
    });

    // 3. 메인 웹페이지 라우트 (GET /)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String tab = request->hasParam("tab") ? request->getParam("tab")->value() : "player";
        String selSsid = request->hasParam("sel_ssid") ? request->getParam("sel_ssid")->value() : "";
        if (request->hasParam("lang")) {
            g_lang = request->getParam("lang")->value();
        }
        String html = generateHTML(tab, g_lang, selSsid);
        request->send(200, "text/html; charset=utf-8", html);
    });

    // 3. 비동기 실시간 볼륨 API (/api/set_vol)
    server.on("/api/set_vol", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("val")) {
            int vol = request->getParam("val")->value().toInt();
            if (vol < 0) vol = 0;
            if (vol > 100) vol = 100;
            AudioEngine::setMasterVolume((uint8_t)vol);
        }
        request->send(200, "text/plain", "OK");
    });

    // 3-1. 비동기 즉시 오디오 발음 테스트 API (/api/test_sound)
    server.on("/api/test_sound", HTTP_GET, [](AsyncWebServerRequest *request) {
        String type = request->hasParam("type") ? request->getParam("type")->value() : "piano";
        int t = 1;
        if (type == "guitar") t = 2;
        else if (type == "drum") t = 3;
        else if (type == "stereo") t = 4;

        AudioEngine::playTestSound(t);
        request->send(200, "text/plain", "OK");
    });

    // 3-2. 가상 피아노 실시간 Note On / Off / Program Change API (1x1 GIF 즉시 응답)
    server.on("/api/note_on", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint8_t note = request->hasParam("note") ? request->getParam("note")->value().toInt() : 60;
        uint8_t vel  = request->hasParam("vel")  ? request->getParam("vel")->value().toInt() : 100;
        uint8_t ch   = request->hasParam("ch")   ? request->getParam("ch")->value().toInt() : 0;
        AudioEngine::noteOn(ch, note, vel);
        request->send(200, "image/gif", GIF_1X1, sizeof(GIF_1X1));
    });

    server.on("/api/note_off", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint8_t note = request->hasParam("note") ? request->getParam("note")->value().toInt() : 60;
        uint8_t ch   = request->hasParam("ch")   ? request->getParam("ch")->value().toInt() : 0;
        AudioEngine::noteOff(ch, note);
        request->send(200, "image/gif", GIF_1X1, sizeof(GIF_1X1));
    });

    server.on("/api/prog_change", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint8_t prog = request->hasParam("prog") ? request->getParam("prog")->value().toInt() : 0;
        uint8_t ch   = request->hasParam("ch")   ? request->getParam("ch")->value().toInt() : 0;
        AudioEngine::programChange(ch, prog);
        request->send(200, "image/gif", GIF_1X1, sizeof(GIF_1X1));
    });

    // 4. 클래식 버튼/명령 라우트 (/action)
    server.on("/action", HTTP_GET, [](AsyncWebServerRequest *request) {
        String cmd = request->hasParam("cmd") ? request->getParam("cmd")->value() : "";
        String tab = request->hasParam("tab") ? request->getParam("tab")->value() : "player";
        String lang = request->hasParam("lang") ? request->getParam("lang")->value() : g_lang;

        bool isGameActive = (MIDISequencer::isLoopEnabled() || DisplayUI::getMode() == SCREEN_GAME_RUNNING || DisplayUI::getMode() == SCREEN_MENU_GAMES);

        if (isGameActive && (cmd == "play" || cmd == "pause" || cmd == "stop")) {
            // 게임 플레이 중에는 웹 재생 컨트롤 무시
        } else if (cmd == "play") {
            MIDISequencer::play();
        } else if (cmd == "pause") {
            MIDISequencer::pause();
        } else if (cmd == "stop") {
            MIDISequencer::stop();
        } else if (cmd == "play_midi" && request->hasParam("file")) {
            DisplayUI::onExternalMIDIActivity(); // 게임 중이면 게임 즉시 종료 후 메인화면으로 복귀하여 재생
            String path = "/" + request->getParam("file")->value();
            MIDISequencer::loadFile(path.c_str());
            MIDISequencer::play();
        } else if (cmd == "delete_midi" && request->hasParam("file")) {
            String fileName = request->getParam("file")->value();
            if (fileName == MIDISequencer::getCurrentSongName() && !isGameActive) {
                MIDISequencer::stop();
            }
            String path = "/" + fileName;
            LittleFS.remove(path.c_str());
            DisplayUI::invalidateFileListCache();
            tab = "player";
        } else if (cmd == "select_font" && request->hasParam("name")) {
            if (MIDISequencer::getState() == SEQ_PLAYING || AudioEngine::getActiveVoiceCount() > 0) {
                DisplayUI::showToast(DisplayUI::isKoreanMode() ? "먼저 재생을 멈추십시오!" : "Stop Music First!", 2000);
            } else {
                String fontName = request->getParam("name")->value();
                String path = fontName.startsWith("/") ? fontName : ("/" + fontName);
                AudioEngine::loadSoundFontAsync(path.c_str());
            }
        } else if (cmd == "delete_font" && request->hasParam("name")) {
            String fontName = request->getParam("name")->value();
            if (fontName.indexOf("CT4MGM") >= 0 || fontName.indexOf("ct4mgm") >= 0) {
                DisplayUI::showToast(DisplayUI::isKoreanMode() ? "기본 폰트는 삭제 불가!" : "Core Font Protected!");
            } else {
                String path = "/" + fontName;
                LittleFS.remove(path.c_str());
                DisplayUI::invalidateFileListCache();
            }
            tab = "fonts";
        } else if (cmd == "set_font_mgmt" && request->hasParam("font_mgmt_en")) {
            bool en = (request->getParam("font_mgmt_en")->value() == "1");
            WebManager::setFontManagementEnabled(en);
            DisplayUI::showToast(DisplayUI::isKoreanMode() ? (en ? "사운드폰트 관리 켜짐" : "사운드폰트 관리 숨김") : (en ? "Font Mgmt Enabled" : "Font Mgmt Hidden"));
            tab = "settings";
        } else if (cmd == "set_vol" && request->hasParam("vol")) {
            uint8_t vol = request->getParam("vol")->value().toInt();
            AudioEngine::setMasterVolume(vol);
            tab = "settings";
        } else if (cmd == "set_baud" && request->hasParam("baud")) {
            uint32_t baud = request->getParam("baud")->value().toInt();
            MIDIParser::setBaudRate(baud);
            tab = "settings";
        } else if (cmd == "set_audio_mode" && request->hasParam("audio_mode")) {
            bool isMono = (request->getParam("audio_mode")->value() == "mono");
            AudioEngine::setMonoMode(isMono);
            DisplayUI::showToast(DisplayUI::isKoreanMode() ? (isMono ? "소리: 모노" : "소리: 스테레오") : (isMono ? "Audio: Mono" : "Audio: Stereo"));
            tab = "settings";
        } else if (cmd == "set_led" && request->hasParam("led_en")) {
            bool en = (request->getParam("led_en")->value() == "1");
            LEDIndicator::setEnabled(en);
            DisplayUI::showToast(DisplayUI::isKoreanMode() ? (en ? "LED 켜짐" : "LED 꺼짐") : (en ? "LED Enabled" : "LED Disabled"));
            tab = "settings";
        } else if (cmd == "set_ntp") {
            bool autoNtp = request->hasParam("auto_ntp") && (request->getParam("auto_ntp")->value() == "1");
            int utcOff = request->hasParam("utc_off") ? request->getParam("utc_off")->value().toInt() : 9;
            String ntpSrv = request->hasParam("ntp_srv") ? request->getParam("ntp_srv")->value() : "pool.ntp.org";
            TimeManager::setConfig(autoNtp, utcOff, ntpSrv);
            DisplayUI::showToast(DisplayUI::isKoreanMode() ? "NTP 시간 저장됨!" : "NTP Saved!");
            tab = "settings";
        } else if (cmd == "set_manual_time" && request->hasParam("datetime")) {
            String dt = request->getParam("datetime")->value();
            int yr = 2026, mo = 1, dy = 1, hr = 0, mn = 0, sc = 0;
            if (sscanf(dt.c_str(), "%d-%d-%dT%d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc) >= 5) {
                TimeManager::setManualTime(yr, mo, dy, hr, mn, sc);
                DisplayUI::showToast(DisplayUI::isKoreanMode() ? "시간 설정 완료!" : "Time Set!");
            }
            tab = "settings";
        } else if (cmd == "panic") {
            AudioEngine::panic();
            DisplayUI::showToast(DisplayUI::isKoreanMode() ? "초기화 완료!" : "MIDI PANIC!");
        } else if (cmd == "scan_wifi") {
            WiFiManager::triggerScan(); // 비동기 백그라운드 스캔 (크래시 방지)
            tab = "wifi";
        }

        if (request->hasParam("ajax")) {
            request->send(200, "text/plain", "OK");
        } else {
            request->redirect("/?tab=" + tab + "&lang=" + lang);
        }
    });

    // 5. MIDI 파일 업로드 핸들러
    static File s_midiUploadFile;
    server.on("/upload_midi", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (s_midiUploadFile) {
            s_midiUploadFile.close();
        }
        g_uploading = false;
        String lang = request->hasParam("lang") ? request->getParam("lang")->value() : g_lang;
        request->redirect("/?tab=player&lang=" + lang);
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        static String savedPath;
        if (!index) {
            if (s_midiUploadFile) s_midiUploadFile.close();
            if (g_uploading) {
                if (millis() - g_uploadStartTime > 30000) { // 30초 타임아웃 만료 시 강제 복구
                    g_uploading = false;
                } else {
                    request->send(503, "text/plain", "Upload in progress");
                    return;
                }
            }
            g_uploading = true;
            g_uploadStartTime = millis();
            // 플래시 쓰기 버스 충돌 및 오디오 드드득 깨짐 방지를 위해 재생 즉시 안전 정지
            MIDISequencer::stop();
            AudioEngine::panic();
            DisplayUI::showToast(DisplayUI::isKoreanMode() ? "미디 업로드 중..." : "Uploading MIDI...", 5000);
            savedPath = getSafeFileName(filename, ".mid");
            s_midiUploadFile = LittleFS.open(savedPath, "w");
        }
        if (s_midiUploadFile) {
            s_midiUploadFile.write(data, len);
        }
        if (final) {
            if (s_midiUploadFile) {
                s_midiUploadFile.close();
            }
            DisplayUI::invalidateFileListCache();
            MIDISequencer::loadFile(savedPath.c_str());
            MIDISequencer::play();
            g_uploading = false;
        }
    });

    // 6. SoundFont 업로드 핸들러
    static File s_fontUploadFile;
    server.on("/upload_font", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (s_fontUploadFile) {
            s_fontUploadFile.close();
        }
        g_uploading = false;
        String lang = request->hasParam("lang") ? request->getParam("lang")->value() : g_lang;
        request->redirect("/?tab=fonts&lang=" + lang);
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        static String savedPath;
        if (!index) {
            if (s_fontUploadFile) s_fontUploadFile.close();
            if (g_uploading) {
                if (millis() - g_uploadStartTime > 30000) { // 30초 타임아웃 만료 시 강제 복구
                    g_uploading = false;
                } else {
                    request->send(503, "text/plain", "Upload in progress");
                    return;
                }
            }
            g_uploading = true;
            g_uploadStartTime = millis();
            // 사운드폰트 대용량 업로드 중 안전 정지
            MIDISequencer::stop();
            AudioEngine::panic();
            DisplayUI::showToast(DisplayUI::isKoreanMode() ? "폰트 업로드 중..." : "Uploading Font...", 10000);
            savedPath = getSafeFileName(filename, ".sf2");
            s_fontUploadFile = LittleFS.open(savedPath, "w");
        }
        if (s_fontUploadFile) {
            s_fontUploadFile.write(data, len);
        }
        if (final) {
            if (s_fontUploadFile) {
                s_fontUploadFile.close();
            }
            DisplayUI::invalidateFileListCache();
            g_uploading = false;
        }
    });

    // 7. Wi-Fi 연결 요청 핸들러 (비동기 연결 + 리다이렉트 안내 페이지)
    server.on("/connect_wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
        String ssid = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : "";
        String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";

        if (ssid.length() > 0) {
            String html = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n";
            html += "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">\n";
            html += "<title>WaveCanvas Nano RS - Connecting...</title></head>\n";
            html += "<body style=\"background:#ffffff; font-family:Tahoma,sans-serif; padding:40px; text-align:center;\">\n";
            html += "<h2>Wi-Fi Connecting...</h2>\n";
            html += "<p>Connecting to <b>" + ssid + "</b>...</p>\n";
            html += "<hr>\n";
            html += "<p>If successful, reconnect to <b>" + ssid + "</b> and access the new IP address.</p>\n";
            html += "<p>If failed, AP mode will restart automatically.<br>Reconnect to <b>" + String(DEFAULT_AP_SSID) + "</b> (IP: 192.168.4.1)</p>\n";
            html += "</body></html>";
            request->send(200, "text/html; charset=utf-8", html);
            WiFiManager::connectAsync(ssid, pass);
        } else {
            String lang = request->hasParam("lang") ? request->getParam("lang")->value() : g_lang;
            request->redirect("/?tab=wifi&lang=" + lang);
        }
    });

    // 8. REST API 엔드포인트 유지 (최신 기기용 호환성)
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["soundfont"] = AudioEngine::getCurrentFontName();
        doc["volume"] = AudioEngine::getMasterVolume();
        doc["ip"] = WiFiManager::getIPAddress();
        doc["mode"] = WiFiManager::getModeString();
        doc["baud"] = MIDIParser::getBaudRate();
        doc["voices"] = AudioEngine::getActiveVoiceCount();
        SequencerState st = MIDISequencer::getState();
        doc["playerState"] = (st == SEQ_PLAYING) ? "PLAYING" : ((st == SEQ_PAUSED) ? "PAUSED" : "STOPPED");
        doc["songTitle"] = MIDISequencer::getCurrentSongName();
        String json; serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server.begin();
}
