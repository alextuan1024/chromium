// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/overview/tab_overview_gesture_detector.h"

#include <algorithm>
#include <utility>

TabOverviewGestureDetector::TabOverviewGestureDetector(
    ProgressCallback on_progress,
    EndCallback on_end)
    : on_progress_(std::move(on_progress)), on_end_(std::move(on_end)) {}

TabOverviewGestureDetector::~TabOverviewGestureDetector() = default;

void TabOverviewGestureDetector::OnPinchBegin(bool page_is_unzoomed) {
  Cancel();
  tracking_ = page_is_unzoomed;
  accumulated_scale_ = 1.0f;
}

void TabOverviewGestureDetector::OnPinchUpdate(float scale,
                                               bool consumed_by_page) {
  if (!tracking_) {
    return;
  }
  if (consumed_by_page) {
    Cancel();
    return;
  }
  accumulated_scale_ *= scale;
  const float progress = GetProgress();
  if (progress > 0.0f || reported_) {
    reported_ = true;
    on_progress_.Run(progress);
  }
}

void TabOverviewGestureDetector::OnPinchEnd() {
  Finish(GetProgress() >= kCommitProgress);
}

void TabOverviewGestureDetector::Cancel() {
  Finish(false);
}

float TabOverviewGestureDetector::GetProgress() const {
  return std::clamp((1.0f - accumulated_scale_) / (1.0f - kFullScale), 0.0f,
                    1.0f);
}

void TabOverviewGestureDetector::Finish(bool commit) {
  const bool owes_end = tracking_ && reported_;
  tracking_ = false;
  reported_ = false;
  if (owes_end) {
    on_end_.Run(commit);
  }
}
