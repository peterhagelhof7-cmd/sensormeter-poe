#include "OtaManager.h"

#include <Update.h>

bool OtaManager::beginLocalUpdate(size_t contentLength) {
  return Update.begin(contentLength);
}

bool OtaManager::writeLocalUpdateChunk(uint8_t* data, size_t len) {
  return Update.write(data, len) == len;
}

bool OtaManager::endLocalUpdate() {
  return Update.end(true);
}
