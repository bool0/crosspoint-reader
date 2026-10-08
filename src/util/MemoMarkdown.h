// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;

// A small Markdown dialect for memos, laid out for the e-ink screens.
//
// Blocks, one per source line: "# " .. "###### " headings, "- " / "* " / "+ "
// bullets (two leading spaces per nesting level), "1. " numbers, "- [ ] " and
// "- [x] " checklist items, "> " quotes, "---" rules, blank lines.
// Inline: **bold**, __bold__, *italic*, ~~strike~~, `code`, [text](url),
// and backslash escapes. Anything else is plain text.
namespace memomd {

enum class Kind : uint8_t { Text, Heading, Quote, Bullet, Number, Check, Rule, Blank };

// A run of same-styled text at a fixed x offset within its line.
struct Piece {
  int16_t x = 0;
  int16_t width = 0;
  uint8_t style = 0;  // EpdFontFamily::Style
  bool strike = false;
  std::string text;
};

// One display line. Continuation lines of a wrapped block repeat the block's
// kind with `first` false, so markers and quote bars draw correctly.
struct Line {
  Kind kind = Kind::Text;
  uint8_t level = 0;  // heading level or list nesting
  bool first = true;
  bool checked = false;
  int16_t checkIndex = -1;  // checklist item number, on every line of the item
  int16_t indent = 0;       // x where text starts
  int16_t height = 0;
  std::string marker;  // "1." for numbered items
  std::vector<Piece> pieces;
};

struct Doc {
  std::vector<Line> lines;
  int checkCount = 0;
};

// Lays out `text` for `width` pixels in `fontId` (its bold/italic styles are
// used for emphasis). Stops after maxLines display lines.
Doc layout(const GfxRenderer& renderer, int fontId, const std::string& text, int width, int maxLines = 2000);

// Draws one line with its top-left at (x, y) in a column `width` wide. `ink`
// false draws light on dark; `selected` outlines a checklist item's box.
void drawLine(const GfxRenderer& renderer, int fontId, const Line& line, int x, int y, int width, bool ink = true,
              bool selected = false);

// Flips the index-th checklist item ([ ] <-> [x]) in the Markdown source.
// Sub-items (indented below it) follow it, and every parent is then ticked
// exactly when all of its sub-items are.
bool toggleCheck(std::string& text, int index);

}  // namespace memomd
