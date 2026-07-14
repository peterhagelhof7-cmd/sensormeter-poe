#include "BrandingManager.h"

#include <LittleFS.h>

#include "DefaultLogo.h"

namespace {
const char* LOGO_PATH = "/branding-logo.bin";
const char* LOGO_TMP_PATH = "/branding-logo.bin.tmp";
}  // namespace

BrandingManager::BrandingManager(ConfigManager& configManager) : _config(configManager) {}

void BrandingManager::begin() {
  _logoPresent = checkLogoOnDisk();
  if (!_logoPresent) {
    provisionDefaultLogo();
  }
}

bool BrandingManager::hasVendorName() const {
  return _config.getConfig().brandingVendorName.length() > 0;
}

String BrandingManager::vendorName() const {
  return _config.getConfig().brandingVendorName;
}

bool BrandingManager::checkLogoOnDisk() const {
  if (!LittleFS.exists(LOGO_PATH)) return false;
  File f = LittleFS.open(LOGO_PATH, "r");
  if (!f) return false;
  size_t size = f.size();
  f.close();
  return size == LOGO_BYTES;
}

bool BrandingManager::loadLogo(uint8_t* buffer, size_t bufferSize) const {
  if (bufferSize < LOGO_BYTES) return false;
  if (!_logoPresent) return false;

  File f = LittleFS.open(LOGO_PATH, "r");
  if (!f) return false;
  if (f.size() != LOGO_BYTES) {
    f.close();
    return false;
  }
  size_t read = f.read(buffer, LOGO_BYTES);
  f.close();
  return read == LOGO_BYTES;
}

bool BrandingManager::beginLogoUpload() {
  _uploadFile = LittleFS.open(LOGO_TMP_PATH, "w");
  _uploadBytesWritten = 0;
  _uploadOpen = static_cast<bool>(_uploadFile);
  if (!_uploadOpen) {
    Serial.println("[BRANDING] Konnte Logo-Tmp-Datei nicht oeffnen");
  }
  return _uploadOpen;
}

bool BrandingManager::writeLogoUploadChunk(const uint8_t* data, size_t len) {
  if (!_uploadOpen) return false;
  // Groesse waechst waehrend des Streamings nur zaehlend geprueft
  // (Ablehnung erst am Ende in endLogoUpload(), da die Gesamtlaenge des
  // Uploads dem Handler vorab nicht zuverlaessig bekannt ist) - trotzdem
  // schon hier ein Sicherheitsabbruch, falls eine offensichtlich zu grosse
  // Datei ankommt, um nicht unbegrenzt LittleFS-Platz zu verbrauchen.
  if (_uploadBytesWritten + len > LOGO_BYTES * 4) {
    Serial.println("[BRANDING] Logo-Upload abgebrochen (deutlich zu gross)");
    _uploadFile.close();
    LittleFS.remove(LOGO_TMP_PATH);
    _uploadOpen = false;
    return false;
  }
  size_t written = _uploadFile.write(data, len);
  _uploadBytesWritten += written;
  return written == len;
}

bool BrandingManager::endLogoUpload() {
  if (!_uploadOpen) return false;
  _uploadFile.close();
  _uploadOpen = false;

  if (_uploadBytesWritten != LOGO_BYTES) {
    Serial.printf("[BRANDING] Logo-Upload verworfen: %u Byte empfangen, erwartet %u (128x64, 1bpp)\n",
                   (unsigned)_uploadBytesWritten, (unsigned)LOGO_BYTES);
    LittleFS.remove(LOGO_TMP_PATH);
    return false;
  }

  LittleFS.remove(LOGO_PATH);
  if (!LittleFS.rename(LOGO_TMP_PATH, LOGO_PATH)) {
    Serial.println("[BRANDING] Konnte Logo-Tmp-Datei nicht umbenennen");
    return false;
  }
  Serial.println("[BRANDING] Logo gespeichert");
  _logoPresent = true;
  return true;
}

void BrandingManager::provisionDefaultLogo() {
  if (kDefaultLogoBytes != LOGO_BYTES) {
    Serial.println("[BRANDING] Standard-Logo-Groesse passt nicht zum Display, uebersprungen");
    return;
  }
  File f = LittleFS.open(LOGO_TMP_PATH, "w");
  if (!f) {
    Serial.println("[BRANDING] Konnte Standard-Logo nicht schreiben (Tmp-Datei)");
    return;
  }
  size_t written = f.write(kDefaultLogo, kDefaultLogoBytes);
  f.close();
  if (written != LOGO_BYTES) {
    Serial.println("[BRANDING] Standard-Logo unvollstaendig geschrieben, verworfen");
    LittleFS.remove(LOGO_TMP_PATH);
    return;
  }
  LittleFS.remove(LOGO_PATH);
  if (!LittleFS.rename(LOGO_TMP_PATH, LOGO_PATH)) {
    Serial.println("[BRANDING] Konnte Standard-Logo nicht aktivieren");
    return;
  }
  Serial.println("[BRANDING] Standard-Logo (Familienmarke) automatisch eingerichtet");
  _logoPresent = true;
}

bool BrandingManager::deleteLogo() {
  bool ok = !LittleFS.exists(LOGO_PATH) || LittleFS.remove(LOGO_PATH);
  if (ok) _logoPresent = false;
  return ok;
}
