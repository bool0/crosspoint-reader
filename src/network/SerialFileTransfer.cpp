// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "SerialFileTransfer.h"

#include <HalPowerManager.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_rom_crc.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <string>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "network/ProtectedPaths.h"
#include "util/BookCacheUtils.h"
#include "util/TaskWatchdog.h"

namespace serialfiles {

namespace {

constexpr size_t CHUNK = 4096;
constexpr unsigned long IDLE_TIMEOUT_MS = 5000;
// Raw bytes per "D" line; a multiple of 3 so lines carry whole base64 groups.
constexpr size_t GET_LINE_BYTES = 3 * 256;
constexpr const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};
constexpr const char* WEB_URL_FILE = "/.crosspoint/usbweb.txt";

unsigned long lastHostActivityMs = 0;
bool sawHostActivity = false;

// Absolute, no parent references, not a credential store. "/" itself is allowed
// for listing only.
bool validPath(const std::string& path) { return protectedpaths::isPluginPath(path); }

bool isLibraryBook(const std::string& path) {
  const auto ends = [&](const char* ext) {
    const size_t n = strlen(ext);
    if (path.size() < n) return false;
    return std::equal(path.end() - n, path.end(), ext,
                      [](char a, char b) { return tolower(static_cast<unsigned char>(a)) == b; });
  };
  return ends(".epub") || ends(".txt") || ends(".md") || ends(".xtc");
}

// HWCDC writes give up after a short timeout when the host is slow to read, so
// keep writing until everything is out (or the host has stopped reading).
bool writeAll(const uint8_t* data, size_t len) {
  unsigned long lastProgress = millis();
  while (len > 0) {
    const size_t n = logSerial.write(data, std::min<size_t>(len, 256));
    if (n > 0) {
      data += n;
      len -= n;
      lastProgress = millis();
    } else if (millis() - lastProgress > IDLE_TIMEOUT_MS) {
      return false;
    } else {
      delay(1);
    }
  }
  return true;
}

bool writeLine(const char* text) {
  return writeAll(reinterpret_cast<const uint8_t*>(text), strlen(text)) &&
         writeAll(reinterpret_cast<const uint8_t*>("\n"), 1);
}

size_t base64Encode(const uint8_t* in, const size_t len, char* out) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t v = (static_cast<uint32_t>(in[i]) << 16) | (i + 1 < len ? in[i + 1] << 8 : 0) |
                       (i + 2 < len ? in[i + 2] : 0);
    out[o++] = kAlphabet[(v >> 18) & 0x3F];
    out[o++] = kAlphabet[(v >> 12) & 0x3F];
    out[o++] = i + 1 < len ? kAlphabet[(v >> 6) & 0x3F] : '=';
    out[o++] = i + 2 < len ? kAlphabet[v & 0x3F] : '=';
  }
  out[o] = '\0';
  return o;
}

void listDir(const std::string& path) {
  if (path != "/" && !validPath(path)) {
    logSerial.printf("LS_ERR bad path\n");
    return;
  }
  HalFile dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    logSerial.printf("LS_ERR not a folder\n");
    return;
  }
  writeLine("LS_BEGIN");
  char name[256];
  char line[300];
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    bool hidden = name[0] == '.' && !SETTINGS.showHiddenFiles;
    for (const char* item : HIDDEN_ITEMS) hidden = hidden || strcmp(name, item) == 0;
    if (hidden) continue;
    const bool isDir = entry.isDirectory();
    snprintf(line, sizeof(line), "E\t%c\t%lu\t%s", isDir ? 'd' : 'f',
             isDir ? 0UL : static_cast<unsigned long>(entry.fileSize()), name);
    if (!writeLine(line)) return;
    resetTaskWatchdogIfSubscribed();
  }
  writeLine("LS_END");
}

void sendFile(const std::string& path) {
  if (!validPath(path)) {
    logSerial.printf("GET_ERR bad path\n");
    return;
  }
  HalFile file;
  if (!Storage.openFileForRead("USB", path, file) || file.isDirectory()) {
    logSerial.printf("GET_ERR cannot open\n");
    return;
  }
  auto raw = makeUniqueNoThrow<uint8_t[]>(GET_LINE_BYTES);
  auto encoded = makeUniqueNoThrow<char[]>(GET_LINE_BYTES / 3 * 4 + 4);
  if (!raw || !encoded) {
    LOG_ERR("USB", "OOM: GET buffers");
    logSerial.printf("GET_ERR oom\n");
    return;
  }
  HalPowerManager::Lock powerLock;
  const unsigned long size = static_cast<unsigned long>(file.fileSize());
  char header[32];
  snprintf(header, sizeof(header), "GET_BEGIN %lu", size);
  writeLine(header);
  uint32_t crc = 0;
  unsigned long sent = 0;
  while (sent < size) {
    const int got = file.read(raw.get(), std::min<size_t>(GET_LINE_BYTES, size - sent));
    if (got <= 0) break;
    crc = esp_rom_crc32_le(crc, raw.get(), got);
    base64Encode(raw.get(), got, encoded.get());
    if (!writeAll(reinterpret_cast<const uint8_t*>("D "), 2) || !writeLine(encoded.get())) return;
    sent += got;
    resetTaskWatchdogIfSubscribed();
    noteHostActivity();
  }
  if (sent != size) {
    logSerial.printf("GET_ERR read failed\n");
    return;
  }
  snprintf(header, sizeof(header), "GET_END %08lx", static_cast<unsigned long>(crc));
  writeLine(header);
}

void receiveFile(const char* args) {
  unsigned long size = 0;
  int pathStart = 0;
  if (sscanf(args, "%lu %n", &size, &pathStart) < 1 || pathStart == 0) {
    logSerial.printf("PUT_ERR usage\n");
    return;
  }
  const std::string path(args + pathStart);
  if (!validPath(path) || path.back() == '/') {
    logSerial.printf("PUT_ERR bad path\n");
    return;
  }
  auto buffer = makeUniqueNoThrow<uint8_t[]>(CHUNK);
  if (!buffer) {
    LOG_ERR("USB", "OOM: %u bytes", static_cast<unsigned>(CHUNK));
    logSerial.printf("PUT_ERR oom\n");
    return;
  }
  const size_t slash = path.rfind('/');
  if (slash > 0) Storage.ensureDirectoryExists(path.substr(0, slash).c_str());
  const std::string partPath = path + ".part";
  HalFile file;
  if (!Storage.openFileForWrite("USB", partPath, file)) {
    logSerial.printf("PUT_ERR open\n");
    return;
  }

  HalPowerManager::Lock powerLock;
  // HWCDC drops whatever does not fit its RX queue (256 bytes by default), so
  // hold a whole chunk while the transfer runs; the host waits for each ACK.
  logSerial.setRxBufferSize(CHUNK + 512);
  logSerial.printf("PUT_READY\n");
  unsigned long received = 0;
  uint32_t crc = 0;
  bool ok = true;
  while (received < size && ok) {
    const size_t want = std::min<size_t>(CHUNK, size - received);
    size_t got = 0;
    unsigned long lastData = millis();
    while (got < want) {
      const int avail = logSerial.available();
      if (avail > 0) {
        got += logSerial.read(buffer.get() + got, std::min<size_t>(static_cast<size_t>(avail), want - got));
        lastData = millis();
      } else if (millis() - lastData > IDLE_TIMEOUT_MS) {
        ok = false;
        break;
      } else {
        delay(1);
      }
    }
    if (!ok || file.write(buffer.get(), got) != got) {
      ok = false;
      break;
    }
    crc = esp_rom_crc32_le(crc, buffer.get(), got);
    received += got;
    resetTaskWatchdogIfSubscribed();
    noteHostActivity();
    logSerial.printf("ACK %lu\n", received);
  }
  file.close();
  logSerial.setRxBufferSize(256);

  if (!ok || !Storage.replaceFile(partPath.c_str(), path.c_str())) {
    Storage.remove(partPath.c_str());
    LOG_ERR("USB", "Failed after %lu of %lu bytes: %s", received, size, path.c_str());
    logSerial.printf("PUT_ERR transfer\n");
    return;
  }
  clearBookCache(path);
  if (isLibraryBook(path)) library::markLibraryIndexDirty();
  sdFontSystem.markRegistryDirty();  // a new .cpfont shows up in the font list
  LOG_INF("USB", "Received %lu bytes: %s", size, path.c_str());
  logSerial.printf("PUT_OK %08lx\n", static_cast<unsigned long>(crc));
}

void removeEntry(const std::string& path) {
  if (!validPath(path)) {
    logSerial.printf("RM_ERR bad path\n");
    return;
  }
  bool isDir = false;
  {
    HalFile f = Storage.open(path.c_str());
    if (!f) {
      logSerial.printf("RM_ERR not found\n");
      return;
    }
    isDir = f.isDirectory();
  }
  const bool ok = isDir ? Storage.removeDir(path.c_str()) : Storage.remove(path.c_str());
  if (!ok) {
    logSerial.printf("RM_ERR failed\n");
    return;
  }
  if (!isDir) clearBookCache(path);
  if (isDir || isLibraryBook(path)) library::markLibraryIndexDirty();
  logSerial.printf("RM_OK\n");
}

void makeDir(const std::string& path) {
  if (!validPath(path) || !Storage.mkdir(path.c_str())) {
    logSerial.printf("MKDIR_ERR failed\n");
    return;
  }
  logSerial.printf("MKDIR_OK\n");
}

void moveEntry(const std::string& args) {
  const size_t tab = args.find('\t');
  if (tab == std::string::npos) {
    logSerial.printf("MV_ERR usage\n");
    return;
  }
  const std::string from = args.substr(0, tab);
  const std::string to = args.substr(tab + 1);
  if (!validPath(from) || !validPath(to) || Storage.exists(to.c_str())) {
    logSerial.printf("MV_ERR bad path\n");
    return;
  }
  if (!Storage.rename(from.c_str(), to.c_str())) {
    logSerial.printf("MV_ERR failed\n");
    return;
  }
  clearBookCache(from);
  library::markLibraryIndexDirty();
  logSerial.printf("MV_OK\n");
}

}  // namespace

// Handlers avoid std:: names (move, remove, get, ...): with a std::string
// argument, argument-dependent lookup would pick the std:: function instead.
void noteHostActivity() {
  lastHostActivityMs = millis();
  sawHostActivity = true;
}

unsigned long msSinceHostActivity() { return sawHostActivity ? millis() - lastHostActivityMs : ULONG_MAX; }

std::string webAppUrl() {
  std::string url;
  if (!Storage.readFileToString("USB", WEB_URL_FILE, 512, url)) return {};
  while (!url.empty() && (url.back() == '\n' || url.back() == '\r' || url.back() == ' ')) url.pop_back();
  return url;
}

bool handle(const String& cmd) {
  const std::string c(cmd.c_str());
  if (c == "HELLO") {
    logSerial.printf("HELLO_OK %s\n", CROSSPOINT_VERSION);
    return true;
  }
  if (c.rfind("WEBURL ", 0) == 0) {
    const std::string url = c.substr(7);
    const bool web = url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
    if (web && url.size() < 400 && (url == webAppUrl() || Storage.writeFile(WEB_URL_FILE, String(url.c_str())))) {
      logSerial.printf("WEBURL_OK\n");
    } else {
      logSerial.printf("WEBURL_ERR\n");
    }
    return true;
  }
  if (c.rfind("LS ", 0) == 0) {
    listDir(c.substr(3));
  } else if (c.rfind("GET ", 0) == 0) {
    sendFile(c.substr(4));
  } else if (c.rfind("PUT ", 0) == 0) {
    receiveFile(c.c_str() + 4);
  } else if (c.rfind("RM ", 0) == 0) {
    removeEntry(c.substr(3));
  } else if (c.rfind("MKDIR ", 0) == 0) {
    makeDir(c.substr(6));
  } else if (c.rfind("MV ", 0) == 0) {
    moveEntry(c.substr(3));
  } else {
    return false;
  }
  return true;
}

}  // namespace serialfiles
