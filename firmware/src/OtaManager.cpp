#include "OtaManager.h"

#include <Update.h>
#include <cstring>

#if __has_include("config.h")
#include "config.h"
#endif
#ifndef DEVICE_FIRMWARE_VERSION
#define DEVICE_FIRMWARE_VERSION "0.0.0"
#endif
#ifndef FIRMWARE_PROJECT_ID
#define FIRMWARE_PROJECT_ID "UNKNOWN"
#endif

namespace {
const char* const kMarkerPrefix = "SM-FW-ID:";
const size_t kMarkerPrefixLen = 9;
const char* const kMarkerSuffix = ":SM-FW-END";
// Obergrenze fuer "<FIRMWARE_PROJECT_ID>:<DEVICE_FIRMWARE_VERSION>" - reicht
// grosszuegig fuer alle Projektnamen/Versionsstrings der Familie.
const size_t kMaxCaptureLen = 96;

// Zerlegt "MAJOR.MINOR.PATCH[-SUFFIX]" in seine Teile - kein vollstaendiger
// Semver-Parser (z.B. keine Build-Metadaten "+..."), deckt aber das in
// diesem Projekt genutzte a.b.c[-rcN]-Schema ab.
void parseVersion(const String& v, int& major, int& minor, int& patch, String& suffix) {
  major = minor = patch = 0;
  suffix = "";
  int dashPos = v.indexOf('-');
  String core = (dashPos >= 0) ? v.substring(0, dashPos) : v;
  if (dashPos >= 0) suffix = v.substring(dashPos + 1);
  int firstDot = core.indexOf('.');
  int secondDot = (firstDot >= 0) ? core.indexOf('.', firstDot + 1) : -1;
  if (firstDot < 0 || secondDot < 0) return;
  major = core.substring(0, firstDot).toInt();
  minor = core.substring(firstDot + 1, secondDot).toInt();
  patch = core.substring(secondDot + 1).toInt();
}

// Vergleicht zwei Versionen nach obigem Schema, liefert <0/0/>0 wie strcmp.
// Bei gleichem a.b.c hat "kein Suffix" Vorrang vor "mit Suffix" (ein
// Release gilt als neuer als jede Vorabversion derselben Kernversion), bei
// zwei Suffixen entscheidet der lexikografische Vergleich (deckt
// "rc3" < "rc4" ab).
int compareVersions(const String& a, const String& b) {
  int aMajor, aMinor, aPatch, bMajor, bMinor, bPatch;
  String aSuffix, bSuffix;
  parseVersion(a, aMajor, aMinor, aPatch, aSuffix);
  parseVersion(b, bMajor, bMinor, bPatch, bSuffix);
  if (aMajor != bMajor) return aMajor - bMajor;
  if (aMinor != bMinor) return aMinor - bMinor;
  if (aPatch != bPatch) return aPatch - bPatch;
  if (aSuffix.length() == 0 && bSuffix.length() > 0) return 1;
  if (aSuffix.length() > 0 && bSuffix.length() == 0) return -1;
  return aSuffix.compareTo(bSuffix);
}
}  // namespace

namespace {
// Byte-sichere Teilstring-Suche (memmem-Ersatz - nicht auf allen Plattformen
// verfuegbar) - im Unterschied zu String::indexOf()/strstr() bricht das
// NICHT am ersten eingebetteten Null-Byte ab.
int findBytes(const uint8_t* haystack, size_t haystackLen, const char* needle, size_t needleLen) {
  if (needleLen == 0 || haystackLen < needleLen) return -1;
  for (size_t i = 0; i + needleLen <= haystackLen; i++) {
    if (memcmp(haystack + i, needle, needleLen) == 0) return (int)i;
  }
  return -1;
}
}  // namespace

bool OtaManager::beginLocalUpdate(size_t contentLength) {
  _markerFound = false;
  _identityMatches = false;
  _versionAllowed = false;
  _capturing = false;
  _tailLen = 0;
  _captureLen = 0;
  return Update.begin(contentLength);
}

bool OtaManager::writeLocalUpdateChunk(uint8_t* data, size_t len) {
  if (!_markerFound) scanChunkForMarker(data, len);
  return Update.write(data, len) == len;
}

// Sucht ueber Chunk-Grenzen hinweg nach kMarkerPrefix, faengt danach alles
// bis kMarkerSuffix ab (bzw. bricht ab, wenn kMaxCaptureLen ueberschritten
// wird, ohne dass ein Suffix gefunden wurde) und wertet den eingefangenen
// Text dann in handleMarkerPayload() aus. Das eigentliche Update.write()
// laeuft unabhaengig davon weiter - erst endLocalUpdate() entscheidet
// anhand des Scan-Ergebnisses, ob committet oder verworfen wird.
//
// Arbeitet bewusst auf rohen Bytes (nicht Arduino String/strstr) - eine
// .bin ist Binaerdaten mit reichlich eingebetteten Null-Bytes (bereits ab
// Byte 9 im ESP32-Image-Header gesehen), String-basierte Suche wuerde dort
// abbrechen und den Marker nie finden, egal wie weit hinten er im File
// liegt. Siehe docs/entscheidungen.md fuer den Befund, der das aufgedeckt
// hat (uebernommen aus sensormeter).
void OtaManager::scanChunkForMarker(uint8_t* data, size_t len) {
  static const size_t kJoinCap = kTailCap + 512;
  if (!_capturing) {
    uint8_t joinBuf[kJoinCap];
    size_t offset = 0;
    if (_tailLen > 0) {
      memcpy(joinBuf, _tailBuf, _tailLen);
      offset = _tailLen;
    }
    size_t copyLen = len;
    if (offset + copyLen > kJoinCap) copyLen = kJoinCap - offset;
    memcpy(joinBuf + offset, data, copyLen);
    size_t joinLen = offset + copyLen;

    int prefixPos = findBytes(joinBuf, joinLen, kMarkerPrefix, kMarkerPrefixLen);
    if (prefixPos < 0) {
      size_t keep = kMarkerPrefixLen > 0 ? kMarkerPrefixLen - 1 : 0;
      if (keep > kTailCap) keep = kTailCap;
      size_t start = joinLen > keep ? joinLen - keep : 0;
      _tailLen = joinLen - start;
      memcpy(_tailBuf, joinBuf + start, _tailLen);
      return;
    }
    _capturing = true;
    size_t afterPrefix = prefixPos + kMarkerPrefixLen;
    size_t remaining = joinLen - afterPrefix;
    if (remaining > kCaptureCap) remaining = kCaptureCap;
    memcpy(_captureBuf, joinBuf + afterPrefix, remaining);
    _captureLen = remaining;
    _tailLen = 0;
  } else {
    size_t copyLen = len;
    if (_captureLen + copyLen > kCaptureCap) copyLen = kCaptureCap - _captureLen;
    memcpy(_captureBuf + _captureLen, data, copyLen);
    _captureLen += copyLen;
  }

  int suffixPos = findBytes(_captureBuf, _captureLen, kMarkerSuffix, strlen(kMarkerSuffix));
  if (suffixPos >= 0) {
    String payload;
    payload.reserve(suffixPos);
    for (int i = 0; i < suffixPos; i++) payload += (char)_captureBuf[i];
    handleMarkerPayload(payload);
    _capturing = false;
    _captureLen = 0;
    _tailLen = 0;
    return;
  }
  if (_captureLen >= kCaptureCap) {
    // Kein gueltiger Marker in plausibler Laenge - wie "nicht gefunden"
    // behandeln, nicht endlos weiter aufsammeln.
    _capturing = false;
    _captureLen = 0;
    _tailLen = 0;
  }
}

void OtaManager::handleMarkerPayload(const String& payload) {
  int sep = payload.indexOf(':');
  if (sep < 0) return;
  String projectId = payload.substring(0, sep);
  String version = payload.substring(sep + 1);

  _markerFound = true;
  _identityMatches = (projectId == FIRMWARE_PROJECT_ID);
  _versionAllowed = _allowDowngrade || compareVersions(version, DEVICE_FIRMWARE_VERSION) >= 0;
}

bool OtaManager::endLocalUpdate() {
  if (!_markerFound || !_identityMatches || !_versionAllowed) {
    Update.abort();
    return false;
  }
  return Update.end(true);
}
