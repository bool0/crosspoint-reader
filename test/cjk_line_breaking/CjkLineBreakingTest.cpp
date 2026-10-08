// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include <Epub/ParsedText.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

struct Line {
  std::vector<std::string> words;
  std::vector<int16_t> xpos;
};

// Fixture metrics (stub renderer): every glyph is 8 px wide and a space is 4 px.
constexpr int GLYPH = 8;

std::vector<Line> layout(const std::vector<const char*>& words, const uint16_t width) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, false, style, 0);
  for (const char* word : words) text.addWord(word, EpdFontFamily::REGULAR);
  std::vector<Line> lines;
  text.layoutAndExtractLines(renderer, 0, width, [&](std::unique_ptr<TextBlock> block, auto) {
    auto& line = lines.emplace_back();
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      line.words.emplace_back(block->wordText(i));
      line.xpos.push_back(block->wordXpos(i));
    }
  });
  return lines;
}

size_t codepoints(const std::string& s) {
  size_t n = 0;
  for (const unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

size_t totalCodepoints(const std::vector<Line>& lines) {
  size_t n = 0;
  for (const auto& line : lines)
    for (const auto& w : line.words) n += codepoints(w);
  return n;
}

}  // namespace

// The mixed sentence that showed wide inter-character gaps on the device:
// "1942 年 6 月，罗斯福下令将情报协调局与军方情报力量整合，成立了美国战略情报局".
// Its glued Latin/number runs made the slack-balancing optimum end a line early.
TEST(CjkLineBreaking, MixedChineseFillsEachLine) {
  Hyphenator::setPreferredLanguage("zh");
  const std::vector<const char*> words = {
      "1942", "年", "6",
      "月，罗斯福下令将情报协调局与军方情报力量整合，成立了美国战略情报局（简称OSS），多诺万任局长。"};
  constexpr uint16_t WIDTH = 13 * GLYPH;  // 13 ideographs per line
  const auto lines = layout(words, WIDTH);
  ASSERT_GE(lines.size(), 3u);

  // Every non-final line is filled to within one glyph of the margin: the
  // first token of the next line (one ideograph, or punctuation glued to one)
  // must not have fitted.
  for (size_t i = 0; i + 1 < lines.size(); ++i) {
    const auto& line = lines[i];
    size_t glyphs = 0;
    for (const auto& w : line.words) glyphs += codepoints(w);
    const size_t spaces = i == 0 ? 3 : 0;  // "1942 年 6 月" keeps its three source spaces
    const int natural = static_cast<int>(glyphs) * GLYPH + static_cast<int>(spaces) * 4;
    const int nextWidth = static_cast<int>(codepoints(lines[i + 1].words.front())) * GLYPH;
    EXPECT_GT(natural + nextWidth, WIDTH) << "line " << i << " ends early with " << (WIDTH - natural) << " px spare";
  }
  EXPECT_EQ(totalCodepoints(lines), 4u + 1 + 1 + codepoints(words[3]));
}

TEST(CjkLineBreaking, PureChineseBreaksAfterTheLastFittingCharacter) {
  Hyphenator::setPreferredLanguage("zh");
  const auto lines = layout({"一二三四五六七八九十甲乙丙丁戊己庚辛"}, 6 * GLYPH);
  ASSERT_EQ(lines.size(), 3u);
  for (const auto& line : lines) EXPECT_EQ(line.words.size(), 6u);
}

TEST(CjkLineBreaking, ClosingPunctuationNeverStartsALine) {
  Hyphenator::setPreferredLanguage("zh");
  // Six ideographs fill the line exactly; the comma must stay with the sixth one.
  const auto lines = layout({"一二三四五六，七八九"}, 6 * GLYPH);
  ASSERT_GE(lines.size(), 2u);
  EXPECT_NE(lines[1].words.front().rfind("，", 0), 0u);
}

TEST(CjkLineBreaking, EnglishKeepsBalancedBreaking) {
  Hyphenator::setPreferredLanguage("en");
  // Latin paragraphs still use the slack-balancing optimum.
  const auto lines = layout({"aaa", "bb", "cc", "dddddd"}, 9 * GLYPH);
  ASSERT_EQ(lines.size(), 2u);
}

// An unbreakable Latin run further down (here 9 glyphs on a 10-glyph line) left
// slack the balancing optimum pushed back onto earlier lines, so the first line
// stopped after 7 of the 10 ideographs that fit and justification spread three
// glyphs of space between its characters.
TEST(CjkLineBreaking, LatinRunDoesNotLoosenEarlierLines) {
  Hyphenator::setPreferredLanguage("zh");
  const auto lines = layout({"一二三四五六七八九十甲乙丙丁", "abcdefghi", "戊己"}, 10 * GLYPH);
  ASSERT_GE(lines.size(), 2u);
  EXPECT_EQ(lines[0].words.size(), 10u);
  // Fully set: the ideographs sit edge to edge, no justification stretch.
  for (size_t i = 0; i < lines[0].xpos.size(); ++i) EXPECT_EQ(lines[0].xpos[i], static_cast<int16_t>(i * GLYPH));
}
