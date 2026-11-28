// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/overview/tab_overview_gesture_detector.h"

#include <vector>

#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"

class TabOverviewGestureDetectorTest : public testing::Test {
 protected:
  std::vector<float> progress_;
  std::vector<bool> ends_;
  TabOverviewGestureDetector detector_{
      base::BindRepeating([](std::vector<float>* progress,
                             float value) { progress->push_back(value); },
                          &progress_),
      base::BindRepeating(
          [](std::vector<bool>* ends, bool commit) { ends->push_back(commit); },
          &ends_)};
};

TEST_F(TabOverviewGestureDetectorTest, ReportsProgressAndCommits) {
  detector_.OnPinchBegin(/*page_is_unzoomed=*/true);
  detector_.OnPinchUpdate(0.9f, /*consumed_by_page=*/false);
  ASSERT_EQ(1u, progress_.size());
  EXPECT_NEAR(0.2f, progress_.back(), 0.001f);
  detector_.OnPinchUpdate(0.5f, false);
  EXPECT_EQ(1.0f, progress_.back());
  EXPECT_TRUE(ends_.empty());
  detector_.OnPinchEnd();
  EXPECT_EQ(std::vector<bool>{true}, ends_);
}

TEST_F(TabOverviewGestureDetectorTest, ShortPinchIsAbandoned) {
  detector_.OnPinchBegin(true);
  detector_.OnPinchUpdate(0.9f, false);
  detector_.OnPinchEnd();
  EXPECT_EQ(std::vector<bool>{false}, ends_);
}

TEST_F(TabOverviewGestureDetectorTest, ReopeningFingersAbandons) {
  detector_.OnPinchBegin(true);
  detector_.OnPinchUpdate(0.6f, false);
  detector_.OnPinchUpdate(1.6f, false);
  EXPECT_NEAR(0.08f, progress_.back(), 0.001f);
  detector_.OnPinchEnd();
  EXPECT_EQ(std::vector<bool>{false}, ends_);
}

TEST_F(TabOverviewGestureDetectorTest, IgnoresZoomedPage) {
  detector_.OnPinchBegin(/*page_is_unzoomed=*/false);
  detector_.OnPinchUpdate(0.3f, false);
  detector_.OnPinchEnd();
  EXPECT_TRUE(progress_.empty());
  EXPECT_TRUE(ends_.empty());
}

TEST_F(TabOverviewGestureDetectorTest, IgnoresPinchThePageHandles) {
  detector_.OnPinchBegin(true);
  detector_.OnPinchUpdate(0.9f, /*consumed_by_page=*/true);
  detector_.OnPinchUpdate(0.3f, false);
  detector_.OnPinchEnd();
  EXPECT_TRUE(progress_.empty());
  EXPECT_TRUE(ends_.empty());
}

TEST_F(TabOverviewGestureDetectorTest, IgnoresPinchOut) {
  detector_.OnPinchBegin(true);
  detector_.OnPinchUpdate(1.6f, false);
  detector_.OnPinchEnd();
  EXPECT_TRUE(progress_.empty());
  EXPECT_TRUE(ends_.empty());
}

TEST_F(TabOverviewGestureDetectorTest, CancelAbandonsOnce) {
  detector_.OnPinchBegin(true);
  detector_.OnPinchUpdate(0.5f, false);
  detector_.Cancel();
  detector_.Cancel();
  detector_.OnPinchUpdate(0.5f, false);
  detector_.OnPinchEnd();
  EXPECT_EQ(1u, progress_.size());
  EXPECT_EQ(std::vector<bool>{false}, ends_);
}
