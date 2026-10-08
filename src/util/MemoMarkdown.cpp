// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "MemoMarkdown.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cstring>

namespace memomd {

namespace {

constexpr int INDENT_PER_LEVEL = 24;
constexpr int QUOTE_INDENT = 16;

struct Run {
  std::string text;
  uint8_t style;
  bool strike;
};

// Line-start code points that must never begin a line (CJK closing punctuation).
bool isNoBreakBefore(const uint32_t cp) {
  static constexpr uint32_t kNoStart[] = {0xFF0C, 0x3002, 0x3001, 0xFF1B, 0xFF1A, 0xFF01, 0xFF1F, 0xFF09,
                                          0x300D, 0x300F, 0x201D, 0x2019, 0x300B, 0x3009, 0x3011, 0x2026,
                                          ',',    '.',    ';',    ':',    '!',    '?',    ')'};
  return std::find(std::begin(kNoStart), std::end(kNoStart), cp) != std::end(kNoStart);
}

uint32_t decode(const std::string& s, size_t& i) {
  const auto c = static_cast<unsigned char>(s[i]);
  int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  uint32_t cp = extra == 3 ? c & 0x07 : extra == 2 ? c & 0x0F : extra == 1 ? c & 0x1F : c;
  ++i;
  while (extra-- > 0 && i < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
  return cp;
}

// Parses inline emphasis into styled runs.
std::vector<Run> parseInline(const std::string& src, uint8_t baseStyle, const bool baseStrike) {
  std::vector<Run> runs;
  bool bold = false, italic = false, strike = false;
  std::string cur;
  const auto style = [&] {
    uint8_t s = baseStyle;
    if (bold) s |= EpdFontFamily::BOLD;
    if (italic) s |= EpdFontFamily::ITALIC;
    return s;
  };
  const auto flush = [&] {
    if (cur.empty()) return;
    runs.push_back({cur, style(), strike || baseStrike});
    cur.clear();
  };
  for (size_t i = 0; i < src.size();) {
    const char c = src[i];
    if (c == '\\' && i + 1 < src.size() && strchr("\\*_~`[]()#>-+!", src[i + 1])) {
      cur += src[i + 1];
      i += 2;
      continue;
    }
    if ((c == '*' || c == '_') && i + 1 < src.size() && src[i + 1] == c) {
      flush();
      bold = !bold;
      i += 2;
      continue;
    }
    // Single '*' only: underscores are common inside words and file names.
    if (c == '*' &&
        (italic || (i + 1 < src.size() && src[i + 1] != ' ' && src.find('*', i + 1) != std::string::npos))) {
      flush();
      italic = !italic;
      ++i;
      continue;
    }
    if (c == '~' && i + 1 < src.size() && src[i + 1] == '~') {
      flush();
      strike = !strike;
      i += 2;
      continue;
    }
    if (c == '`') {
      const size_t end = src.find('`', i + 1);
      if (end != std::string::npos) {
        flush();
        runs.push_back({src.substr(i + 1, end - i - 1), style(), strike || baseStrike});
        i = end + 1;
        continue;
      }
    }
    if (c == '[') {
      const size_t close = src.find("](", i + 1);
      const size_t paren = close == std::string::npos ? close : src.find(')', close + 2);
      if (paren != std::string::npos) {
        cur += src.substr(i + 1, close - i - 1);
        i = paren + 1;
        continue;
      }
    }
    cur += c;
    ++i;
  }
  flush();
  return runs;
}

struct Atom {
  std::string text;
  uint8_t style;
  bool strike;
  bool space;
  bool noBreakBefore;
};

// Splits runs into wrap units: Latin words, single spaces, and single
// non-ASCII code points (CJK breaks anywhere).
std::vector<Atom> atomize(const std::vector<Run>& runs) {
  std::vector<Atom> atoms;
  for (const auto& run : runs) {
    const std::string& s = run.text;
    size_t i = 0;
    while (i < s.size()) {
      const auto c = static_cast<unsigned char>(s[i]);
      if (c == ' ') {
        atoms.push_back({" ", run.style, run.strike, true, false});
        ++i;
      } else if (c < 0x80) {
        const size_t start = i;
        while (i < s.size() && static_cast<unsigned char>(s[i]) < 0x80 && s[i] != ' ') ++i;
        const std::string word = s.substr(start, i - start);
        const bool punct = word.size() == 1 && isNoBreakBefore(static_cast<unsigned char>(word[0]));
        atoms.push_back({word, run.style, run.strike, false, punct});
      } else {
        const size_t start = i;
        const uint32_t cp = decode(s, i);
        atoms.push_back({s.substr(start, i - start), run.style, run.strike, false, isNoBreakBefore(cp)});
      }
    }
  }
  return atoms;
}

size_t leadingSpaces(const std::string& s) {
  size_t n = 0;
  while (n < s.size() && (s[n] == ' ' || s[n] == '\t')) n += 1;
  return n;
}

bool startsWith(const std::string& s, const size_t at, const char* prefix) {
  return s.compare(at, strlen(prefix), prefix) == 0;
}

// Checklist marker at `at` ("- [ ] ", "* [x] ", "[ ] " ...). Returns the marker
// length and sets `checked`, or 0 when there is none.
size_t checkMarker(const std::string& s, const size_t at, bool& checked) {
  size_t p = at;
  if (p + 1 < s.size() && strchr("-*+", s[p]) && s[p + 1] == ' ') p += 2;
  if (p + 3 > s.size() || s[p] != '[' || s[p + 2] != ']') return 0;
  const char mark = s[p + 1];
  if (mark != ' ' && mark != 'x' && mark != 'X') return 0;
  checked = mark != ' ';
  p += 3;
  if (p < s.size() && s[p] == ' ') ++p;
  return p - at;
}

}  // namespace

Doc layout(const GfxRenderer& renderer, const int fontId, const std::string& text, const int width,
           const int maxLines) {
  Doc doc;
  renderer.ensureSdCardFontReady(fontId, text.c_str());
  const int lineHeight = std::max(1, renderer.getLineHeight(fontId));
  const int markerBox = lineHeight;  // room for a bullet, number or checkbox

  size_t start = 0;
  while (start <= text.size() && static_cast<int>(doc.lines.size()) < maxLines) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string raw = text.substr(start, end - start);
    raw.erase(std::remove(raw.begin(), raw.end(), '\r'), raw.end());
    std::replace(raw.begin(), raw.end(), '\t', ' ');
    start = end + 1;

    Line proto;
    proto.height = static_cast<int16_t>(lineHeight);
    std::string body;
    uint8_t baseStyle = EpdFontFamily::REGULAR;
    bool strikeAll = false;

    const size_t lead = leadingSpaces(raw);
    const uint8_t nest = static_cast<uint8_t>(std::min<size_t>(lead / 2, 4));
    bool checked = false;
    size_t markLen = 0;
    std::string trimmed = raw.substr(lead);
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();

    if (trimmed.empty()) {
      proto.kind = Kind::Blank;
      proto.height = static_cast<int16_t>(lineHeight / 2);
    } else if (trimmed.size() >= 3 && (trimmed.find_first_not_of('-') == std::string::npos ||
                                       trimmed.find_first_not_of('*') == std::string::npos ||
                                       trimmed.find_first_not_of('_') == std::string::npos)) {
      proto.kind = Kind::Rule;
      proto.height = static_cast<int16_t>(lineHeight / 2 + 2);
    } else if (trimmed[0] == '#') {
      size_t level = trimmed.find_first_not_of('#');
      if (level != std::string::npos && level <= 6 && trimmed[level] == ' ') {
        proto.kind = Kind::Heading;
        proto.level = static_cast<uint8_t>(level);
        body = trimmed.substr(level + 1);
        baseStyle = EpdFontFamily::BOLD;
      } else {
        body = trimmed;
      }
    } else if ((markLen = checkMarker(trimmed, 0, checked)) != 0) {
      proto.kind = Kind::Check;
      proto.level = nest;
      proto.checked = checked;
      proto.checkIndex = static_cast<int16_t>(doc.checkCount++);
      proto.indent = static_cast<int16_t>(nest * INDENT_PER_LEVEL + markerBox + 6);
      body = trimmed.substr(markLen);
      strikeAll = checked;
    } else if (trimmed.size() >= 2 && strchr("-*+", trimmed[0]) && trimmed[1] == ' ') {
      proto.kind = Kind::Bullet;
      proto.level = nest;
      proto.indent = static_cast<int16_t>(nest * INDENT_PER_LEVEL + markerBox);
      body = trimmed.substr(2);
    } else if (startsWith(trimmed, 0, ">")) {
      proto.kind = Kind::Quote;
      proto.indent = QUOTE_INDENT;
      body = trimmed.substr(trimmed.size() > 1 && trimmed[1] == ' ' ? 2 : 1);
    } else {
      const size_t digits = trimmed.find_first_not_of("0123456789");
      if (digits != std::string::npos && digits > 0 && digits <= 3 &&
          (trimmed[digits] == '.' || trimmed[digits] == ')') && digits + 1 < trimmed.size() &&
          trimmed[digits + 1] == ' ') {
        proto.kind = Kind::Number;
        proto.level = nest;
        proto.marker = trimmed.substr(0, digits + 1);
        const int markerW = renderer.getTextWidth(fontId, proto.marker.c_str()) + 8;
        proto.indent = static_cast<int16_t>(nest * INDENT_PER_LEVEL + std::max(markerBox, markerW));
        body = trimmed.substr(digits + 2);
      } else {
        body = trimmed;
      }
    }

    if (proto.kind == Kind::Blank || proto.kind == Kind::Rule) {
      doc.lines.push_back(proto);
      continue;
    }

    // Wrap the block's atoms into display lines.
    const auto atoms = atomize(parseInline(body, baseStyle, strikeAll));
    const int avail = std::max(lineHeight, width - proto.indent);
    Line line = proto;
    int x = 0;
    const auto emit = [&] {
      doc.lines.push_back(line);
      line = proto;
      line.first = false;
      line.marker.clear();
      x = 0;
    };
    for (const auto& atom : atoms) {
      if (atom.space && x == 0) continue;  // no leading spaces on wrapped lines
      const int w = renderer.getTextWidth(fontId, atom.text.c_str(), static_cast<EpdFontFamily::Style>(atom.style));
      if (x > 0 && x + w > avail && !atom.noBreakBefore) {
        // Drop a trailing space before wrapping.
        if (!line.pieces.empty() && line.pieces.back().text == " ") line.pieces.pop_back();
        emit();
        if (static_cast<int>(doc.lines.size()) >= maxLines) break;
        if (atom.space) continue;
      }
      Piece* last = line.pieces.empty() ? nullptr : &line.pieces.back();
      if (last && last->style == atom.style && last->strike == atom.strike) {
        last->text += atom.text;
        last->width = static_cast<int16_t>(last->width + w);
      } else {
        line.pieces.push_back({static_cast<int16_t>(x), static_cast<int16_t>(w), atom.style, atom.strike, atom.text});
      }
      x += w;
    }
    if (static_cast<int>(doc.lines.size()) < maxLines) emit();
  }
  while (!doc.lines.empty() && doc.lines.back().kind == Kind::Blank) doc.lines.pop_back();
  return doc;
}

void drawLine(const GfxRenderer& renderer, const int fontId, const Line& line, const int x, const int y,
              const int width, const bool ink, const bool selected) {
  const int lh = line.height;
  const int fullLh = renderer.getLineHeight(fontId);
  const int asc = renderer.getFontAscenderSize(fontId);
  const Color color = ink ? Color::Black : Color::White;
  const int nestX = x + line.level * INDENT_PER_LEVEL;

  switch (line.kind) {
    case Kind::Blank:
      return;
    case Kind::Rule:
      renderer.fillRect(x, y + lh / 2 - 1, width, 2, ink);
      return;
    case Kind::Quote:
      renderer.fillRect(x + 2, y, 4, lh, ink);
      break;
    case Kind::Bullet:
      if (line.first) {
        const int d = std::max(5, fullLh / 6);
        renderer.fillRoundedRect(nestX + (fullLh - d) / 2, y + asc / 2 + fullLh / 8 - d / 2 + 2, d, d, d / 2, color);
      }
      break;
    case Kind::Number:
      if (line.first) renderer.drawText(fontId, nestX, y, line.marker.c_str(), ink);
      break;
    case Kind::Check:
      if (line.first) {
        const int box = std::max(12, fullLh * 3 / 5);
        const int bx = nestX + (fullLh - box) / 2;
        const int by = y + (fullLh - box) / 2;
        renderer.drawRect(bx, by, box, box, 2, ink);
        if (line.checked) {
          // Tick mark inside the box.
          renderer.drawLine(bx + box / 5, by + box / 2, bx + box * 2 / 5, by + box * 3 / 4, 3, ink);
          renderer.drawLine(bx + box * 2 / 5, by + box * 3 / 4, bx + box * 4 / 5, by + box / 4, 3, ink);
        }
        if (selected) renderer.drawRoundedRect(bx - 5, by - 5, box + 10, box + 10, 2, 4, ink);
      }
      break;
    default:
      break;
  }

  const int textX = x + line.indent;
  for (const auto& piece : line.pieces) {
    renderer.drawText(fontId, textX + piece.x, y, piece.text.c_str(), ink,
                      static_cast<EpdFontFamily::Style>(piece.style));
    if (piece.strike) {
      // Through the middle of CJK glyphs, thick enough to read on e-ink.
      const int t = std::max(2, fullLh / 12);
      renderer.fillRect(textX + piece.x, y + asc * 5 / 8 - t / 2, piece.width, t, ink);
    }
  }
  if (line.kind == Kind::Heading && line.level == 1 && line.first) {
    renderer.fillRect(textX, y + lh - 3, std::max(0, width - line.indent), 1, ink);
  }
}

bool toggleCheck(std::string& text, const int index) {
  struct Item {
    size_t markPos;  // byte offset of the ' ' / 'x' between the brackets
    size_t line;     // source line number
    size_t lead;     // indentation
    bool checked;
    size_t descEnd;  // one past the last item nested under this one
  };
  struct SourceLine {
    size_t lead;
    bool blank;
  };
  std::vector<Item> items;
  std::vector<SourceLine> lines;

  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string raw = text.substr(start, end - start);
    const size_t lead = leadingSpaces(raw);
    lines.push_back({lead, raw.find_first_not_of(" \t\r") == std::string::npos});
    bool checked = false;
    if (checkMarker(raw, lead, checked) != 0) {
      // The mark sits at '[' + 1, after an optional "- " bullet.
      items.push_back({start + raw.find('[', lead) + 1, lines.size() - 1, lead, checked, 0});
    }
    if (end == text.size()) break;
    start = end + 1;
  }
  if (index < 0 || index >= static_cast<int>(items.size())) return false;

  // An item's sub-items are the items below it until a non-blank line that is
  // indented no deeper than the item itself.
  for (size_t i = 0; i < items.size(); i++) {
    size_t j = i + 1;
    for (; j < items.size(); j++) {
      bool inside = true;
      for (size_t l = items[i].line + 1; l <= items[j].line && inside; l++) {
        inside = lines[l].blank || lines[l].lead > items[i].lead;
      }
      if (!inside) break;
    }
    items[i].descEnd = j;
  }

  // Ticking an item ticks everything under it; unticking unticks it all.
  const bool state = !items[index].checked;
  for (size_t j = index; j < items[index].descEnd; j++) items[j].checked = state;
  // A parent is done exactly when all of its sub-items are, at every level.
  for (size_t i = items.size(); i-- > 0;) {
    if (items[i].descEnd == i + 1) continue;
    bool all = true;
    for (size_t j = i + 1; j < items[i].descEnd && all; j++) all = items[j].checked;
    items[i].checked = all;
  }

  for (const auto& item : items) {
    const bool wasChecked = text[item.markPos] != ' ';
    if (wasChecked != item.checked) text[item.markPos] = item.checked ? 'x' : ' ';
  }
  return true;
}

}  // namespace memomd
