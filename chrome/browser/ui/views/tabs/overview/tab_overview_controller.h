// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_OVERVIEW_TAB_OVERVIEW_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_OVERVIEW_TAB_OVERVIEW_CONTROLLER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/tabs/overview/tab_overview_gesture_detector.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_observer.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
class BrowserWindowInterface;
class TabOverviewView;

// Window-scoped feature, owned by BrowserWindowFeatures, that shows a grid of
// tab thumbnails over the page area.
//
// Pinching in on a page that cannot zoom out any further (see
// TabOverviewGestureDetector) shrinks the page into its tile in step with the
// fingers; lifting them either completes the move into the overview or lets
// the page spring back. The overview closes when a tab is chosen, on Escape,
// or when it loses activation.
class TabOverviewController
    : public TabStripModelObserver,
      public content::WebContentsObserver,
      public content::RenderWidgetHost::InputEventObserver,
      public content::RenderWidgetHostObserver,
      public views::WidgetObserver {
 public:
  DECLARE_USER_DATA(TabOverviewController);

  TabOverviewController(BrowserWindowInterface* browser,
                        BrowserView* browser_view);
  TabOverviewController(const TabOverviewController&) = delete;
  TabOverviewController& operator=(const TabOverviewController&) = delete;
  ~TabOverviewController() override;

  // Returns the instance attached to `browser`, or nullptr if none.
  static TabOverviewController* From(BrowserWindowInterface* browser);

  // Opens the overview at once, without following a pinch.
  void Show();
  void Hide();
  // Whether the overview is on screen, including while a pinch is still
  // moving it.
  bool IsShowing() const { return !!widget_; }
  // Whether the overview is open and taking input.
  bool IsInteractive() const { return widget_ && !in_transition_; }

  TabOverviewView* view_for_testing() { return view_; }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChanged(tabs::TabHandle tab, TabChangeType change_type) override;

  // content::WebContentsObserver:
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;
  void OnPageScaleFactorChanged(float page_scale_factor) override;

  // content::RenderWidgetHost::InputEventObserver:
  void OnInputEventAck(const content::RenderWidgetHost& host,
                       blink::mojom::InputEventResultSource source,
                       blink::mojom::InputEventResultState state,
                       const blink::WebInputEvent& event) override;

  // content::RenderWidgetHostObserver:
  void RenderWidgetHostDestroyed(
      content::RenderWidgetHost* widget_host) override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

 private:
  // Follows the pinch gestures of `contents`, the active tab.
  void ObserveContents(content::WebContents* contents);
  void ObserveHost(content::RenderWidgetHost* host);

  // Builds the overview widget, hidden. Returns false if it cannot be shown.
  bool CreateWidget();
  // Gives the shown overview keyboard focus and starts watching for the
  // events that dismiss it.
  void MakeInteractive();

  void OnGestureProgress(float progress);
  void OnGestureEnd(bool commit);
  // The pinch stopped reporting without ending; settle as if it had ended.
  void OnGestureStalled();
  void OnPageSnapshot(float device_scale_factor,
                      const content::CopyFromSurfaceResult& result);
  void OnTransitionSettled(bool commit);

  void RebuildTiles();
  void OnTabChosen(tabs::TabHandle tab);
  void OnTabCloseRequested(tabs::TabHandle tab);

  const raw_ptr<BrowserWindowInterface> browser_;
  const raw_ptr<BrowserView> browser_view_;
  TabOverviewGestureDetector gesture_detector_;

  raw_ptr<content::RenderWidgetHost> observed_host_ = nullptr;
  base::ScopedObservation<content::RenderWidgetHost,
                          content::RenderWidgetHostObserver>
      host_observation_{this};

  // A qualifying pinch is under way, at `gesture_progress_`.
  bool gesture_active_ = false;
  float gesture_progress_ = 0.0f;
  // The overview is on screen but still moving with, or settling after, a
  // pinch; it does not take input yet.
  bool in_transition_ = false;
  base::OneShotTimer gesture_stall_timer_;

  std::unique_ptr<views::Widget> widget_;
  raw_ptr<TabOverviewView> view_ = nullptr;
  // Keeps the browser window drawn as active while the overview has focus.
  std::unique_ptr<views::Widget::PaintAsActiveLock> paint_as_active_lock_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      overview_widget_observation_{this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      browser_widget_observation_{this};

  ui::ScopedUnownedUserData<TabOverviewController> scoped_unowned_user_data_;
  // Invalidated when a pinch ends, dropping its pending page snapshot.
  base::WeakPtrFactory<TabOverviewController> gesture_weak_factory_{this};
  base::WeakPtrFactory<TabOverviewController> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_OVERVIEW_TAB_OVERVIEW_CONTROLLER_H_
