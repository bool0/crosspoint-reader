// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "MemoStore.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cctype>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"

namespace memostore {

namespace {

bool hasTxtExtension(const std::string& name) {
  if (name.size() < 5) return false;
  std::string ext = name.substr(name.size() - 4);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
  return ext == ".txt";
}

std::string pathOf(const std::string& fileName) { return std::string(MEMO_DIR) + "/" + fileName; }

}  // namespace

std::vector<std::string> list() {
  std::vector<std::string> names;
  if (!Storage.exists(MEMO_DIR)) return names;

  const auto files = Storage.listFiles(MEMO_DIR, MAX_MEMOS);
  names.reserve(files.size());
  for (const auto& file : files) {
    std::string name(file.c_str());
    if (name.empty() || name[0] == '.' || !hasTxtExtension(name)) continue;
    names.push_back(std::move(name));
  }
  std::sort(names.begin(), names.end());

  // Apply the user's order (one file name per line, written by the /memo page).
  // Names it lists come first in that order; the rest follow alphabetically.
  std::string order;
  if (!Storage.readFileToString("MEMO", ORDER_FILE, 8 * 1024, order)) return names;
  std::vector<std::string> ordered;
  ordered.reserve(names.size());
  size_t start = 0;
  while (start < order.size()) {
    size_t end = order.find('\n', start);
    if (end == std::string::npos) end = order.size();
    std::string name = order.substr(start, end - start);
    while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
    const auto it = std::find(names.begin(), names.end(), name);
    if (it != names.end()) {
      ordered.push_back(std::move(*it));
      names.erase(it);
    }
    start = end + 1;
  }
  for (auto& name : names) ordered.push_back(std::move(name));
  return ordered;
}

std::string titleOf(const std::string& fileName) {
  return hasTxtExtension(fileName) ? fileName.substr(0, fileName.size() - 4) : fileName;
}

bool read(const std::string& fileName, std::string& out) {
  out.clear();
  HalFile file;
  if (!Storage.openFileForRead("MEMO", pathOf(fileName), file)) return false;
  const size_t size = std::min(file.fileSize(), MAX_MEMO_BYTES);
  if (size == 0) return true;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < size + 1024) {
    LOG_ERR("MEMO", "OOM reading %u bytes of %s", static_cast<unsigned>(size), fileName.c_str());
    return false;
  }
  out.resize(size);
  const int got = file.read(out.data(), size);
  if (got < 0) {
    out.clear();
    return false;
  }
  out.resize(static_cast<size_t>(got));

  // A truncated read can split a multi-byte UTF-8 sequence; drop the partial tail.
  size_t end = out.size();
  size_t lead = end;
  while (lead > 0 && (static_cast<unsigned char>(out[lead - 1]) & 0xC0) == 0x80) lead--;
  if (lead > 0) {
    const auto c = static_cast<unsigned char>(out[lead - 1]);
    const size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (end - (lead - 1) < need) out.resize(lead - 1);
  }
  // Skip a UTF-8 BOM some phone editors prepend.
  if (out.size() >= 3 && out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);
  return true;
}

std::string pinned() {
  const auto names = list();
  if (names.empty()) return {};

  std::string pin;
  if (Storage.readFileToString("MEMO", PIN_FILE, 256, pin)) {
    while (!pin.empty() && (pin.back() == '\n' || pin.back() == '\r' || pin.back() == ' ')) pin.pop_back();
    if (std::find(names.begin(), names.end(), pin) != names.end()) return pin;
  }
  return names.front();
}

bool setPinned(const std::string& fileName) {
  Storage.ensureDirectoryExists(MEMO_DIR);
  return Storage.writeFile(PIN_FILE, String(fileName.c_str()));
}

int textFontId(GfxRenderer& renderer) {
  sdFontSystem.ensureLoaded(renderer);
  return SETTINGS.getReaderFontId();
}

bool write(const std::string& fileName, const std::string& text) {
  const std::string path = pathOf(fileName);
  const std::string part = path + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("MEMO", part, file)) return false;
    if (file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(part.c_str());
      return false;
    }
  }
  return Storage.replaceFile(part.c_str(), path.c_str());
}

}  // namespace memostore
