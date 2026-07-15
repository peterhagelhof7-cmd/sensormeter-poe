#include "OtaManager.h"

#include <Update.h>

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

bool OtaManager::beginLocalUpdate(size_t contentLength) {
  _markerFound = false;
  _identityMatches = false;
  _versionAllowed = false;
  _capturing = false;
  _tail = "";
  _capture = "";
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
void OtaManager::scanChunkForMarker(uint8_t* data, size_t len) {
  String chunkText;
  chunkText.reserve(_tail.length() + len);
  chunkText += _tail;
  for (size_t i = 0; i < len; i++) chunkText += (char)data[i];

  if (!_capturing) {
    int prefixPos = chunkText.indexOf(kMarkerPrefix);
    if (prefixPos < 0) {
      // Ueberlappungspuffer fuer den naechsten Chunk - der Marker koennte
      // genau an der Chunk-Grenze zerschnitten sein.
      size_t keep = kMarkerPrefixLen > 0 ? kMarkerPrefixLen - 1 : 0;
      _tail = chunkText.length() > keep ? chunkText.substring(chunkText.length() - keep) : chunkText;
      return;
    }
    _capturing = true;
    chunkText = chunkText.substring(prefixPos + kMarkerPrefixLen);
  }

  _capture += chunkText;
  int suffixPos = _capture.indexOf(kMarkerSuffix);
  if (suffixPos >= 0) {
    handleMarkerPayload(_capture.substring(0, suffixPos));
    _capturing = false;
    _capture = "";
    _tail = "";
    return;
  }
  if (_capture.length() > kMaxCaptureLen) {
    // Kein gueltiger Marker in plausibler Laenge - wie "nicht gefunden"
    // behandeln, nicht endlos weiter aufsammeln.
    _capturing = false;
    _capture = "";
    _tail = "";
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
