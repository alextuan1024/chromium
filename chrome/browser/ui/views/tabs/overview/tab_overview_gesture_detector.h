// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_OVERVIEW_TAB_OVERVIEW_GESTURE_DETECTOR_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_OVERVIEW_TAB_OVERVIEW_GESTURE_DETECTOR_H_

#include "base/functional/callback.h"

// Turns a trackpad pinch on a page into progress towards the tab overview.
//
// The gesture qualifies only when the page cannot zoom out any further and
// has not claimed the pinch for itself (maps and canvas editors cancel the
// ctrl+wheel event that a trackpad pinch is first offered as). While a
// qualifying pinch closes, progress runs from 0 (fingers where they started)
// to 1 (closed to `kFullScale`). Lifting the fingers commits to the overview
// at or beyond `kCommitProgress` and abandons it otherwise.
class TabOverviewGestureDetector {
 public:
  // Accumulated pinch scale at which the overview is fully open.
  static constexpr float kFullScale = 0.5f;
  // Progress at or beyond which ending the pinch opens the overview.
  static constexpr float kCommitProgress = 0.7f;

  using ProgressCallback = base::RepeatingCallback<void(float progress)>;
  using EndCallback = base::RepeatingCallback<void(bool commit)>;

  // `on_end` runs once for every pinch that reported progress.
  TabOverviewGestureDetector(ProgressCallback on_progress, EndCallback on_end);
  TabOverviewGestureDetector(const TabOverviewGestureDetector&) = delete;
  TabOverviewGestureDetector& operator=(const TabOverviewGestureDetector&) =
      delete;
  ~TabOverviewGestureDetector();

  // A pinch started. `page_is_unzoomed` is whether the page scale factor is 1.
  void OnPinchBegin(bool page_is_unzoomed);

  // The page was offered one step of the pinch, scaling by `scale`.
  // `consumed_by_page` is whether the page handled it itself.
  void OnPinchUpdate(float scale, bool consumed_by_page);

  // The fingers lifted.
  void OnPinchEnd();

  // The pinch stopped qualifying (page zoomed, tab changed).
  void Cancel();

 private:
  float GetProgress() const;
  void Finish(bool commit);

  ProgressCallback on_progress_;
  EndCallback on_end_;
  bool tracking_ = false;
  // Whether this pinch has reported progress, and so owes an `on_end_`.
  bool reported_ = false;
  float accumulated_scale_ = 1.0f;
};

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_OVERVIEW_TAB_OVERVIEW_GESTURE_DETECTOR_H_
