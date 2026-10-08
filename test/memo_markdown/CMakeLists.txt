# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
add_executable(MemoMarkdownTest
  MemoMarkdownTest.cpp
  ${REPO_ROOT}/src/util/MemoMarkdown.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(MemoMarkdownTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(MemoMarkdownTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MemoMarkdownTest)
