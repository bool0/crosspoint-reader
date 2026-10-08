// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <string>
#include <vector>

class GfxRenderer;

// Memos are plain UTF-8 .txt files in MEMO_DIR, written from a phone through the
// web server's /memo page (which uses the generic /upload, /download and /delete
// endpoints). PIN_FILE holds the file name of the memo shown on the clock.
namespace memostore {

constexpr const char* MEMO_DIR = "/memos";
constexpr const char* PIN_FILE = "/memos/pinned.cfg";
// The user's memo order, one file name per line (set by dragging on the /memo page).
constexpr const char* ORDER_FILE = "/memos/order.cfg";
// Upper bound on what the device reads of one memo; longer files are truncated.
constexpr size_t MAX_MEMO_BYTES = 16 * 1024;
constexpr int MAX_MEMOS = 64;

// File names (with .txt) of every memo: in ORDER_FILE order, then the rest by name.
std::vector<std::string> list();

// Title shown for a memo file name: the name without its .txt extension.
std::string titleOf(const std::string& fileName);

// Reads a memo's text (up to MAX_MEMO_BYTES). Returns false when unreadable.
bool read(const std::string& fileName, std::string& out);

// File name of the memo to show on the clock: the pinned one when it still
// exists, otherwise the first memo. Empty when there are no memos.
std::string pinned();

// Makes fileName the memo shown on the clock.
bool setPinned(const std::string& fileName);

// Font the memo screens draw text with: the reader font, so a CJK SD-card font
// selected for reading also renders Chinese memos. Loads the SD family if needed.
int textFontId(GfxRenderer& renderer);

// Replaces a memo's text (written to a temporary file first).
bool write(const std::string& fileName, const std::string& text);

}  // namespace memostore
