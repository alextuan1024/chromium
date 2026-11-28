// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/overview/tab_overview_controller.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/overview/tab_overview_view.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_gesture_event.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/animation/animation_builder.h"

DEFINE_USER_DATA(TabOverviewController);

TabOverviewController::TabOverviewController(BrowserWindowInterface* browser,
                                             BrowserView* browser_view)
    : browser_(browser),
      browser_view_(browser_view),
      gesture_detector_(
          base::BindRepeating(&TabOverviewController::OnGestureProgress,
                              base::Unretained(this)),
          base::BindRepeating(&TabOverviewController::OnGestureEnd,
                              base::Unretained(this))),
      scoped_unowned_user_data_(browser->GetUnownedUserDataHost(), *this) {
  browser_->GetTabStripModel()->AddObserver(this);
  ObserveContents(browser_->GetTabStripModel()->GetActiveWebContents());
}

TabOverviewController::~TabOverviewController() {
  ObserveHost(nullptr);
}

// static
TabOverviewController* TabOverviewController::From(
    BrowserWindowInterface* browser) {
  return Get(browser->GetUnownedUserDataHost());
}

void TabOverviewController::Show() {
  if (widget_ || !CreateWidget()) {
    return;
  }
  view_->SetPaintToLayer();
  view_->layer()->SetOpacity(0.0f);
  widget_->Show();
  MakeInteractive();
  views::AnimationBuilder()
      .Once()
      .SetDuration(base::Milliseconds(150))
      .SetOpacity(view_.get(), 1.0f);
}

void TabOverviewController::Hide() {
  gesture_active_ = false;
  gesture_stall_timer_.Stop();
  gesture_weak_factory_.InvalidateWeakPtrs();
  gesture_detector_.Cancel();
  if (!widget_) {
    return;
  }
  const bool was_active = widget_->IsActive();
  in_transition_ = false;
  overview_widget_observation_.Reset();
  browser_widget_observation_.Reset();
  view_ = nullptr;
  widget_->Hide();
  // This can run from one of the overview's own event handlers, so let the
  // widget outlive the current call stack.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(widget_));
  if (was_active) {
    browser_view_->GetWidget()->Activate();
  }
  paint_as_active_lock_.reset();
}

void TabOverviewController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    ObserveContents(selection.new_contents);
  }
  if (!widget_) {
    return;
  }
  // Closing tabs keeps the overview open; switching tabs by any other means
  // (a keyboard shortcut, an extension) dismisses it. So does any change
  // while a pinch is still moving the overview, whose tile positions are
  // fixed for the duration.
  if (tab_strip_model->empty() || in_transition_ ||
      (selection.active_tab_changed() &&
       change.type() != TabStripModelChange::kRemoved)) {
    Hide();
  } else if (change.type() != TabStripModelChange::kSelectionOnly) {
    // The change may come from a tile's own close button, which must outlive
    // its click, so rebuild the tiles afterwards.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&TabOverviewController::RebuildTiles,
                                  weak_factory_.GetWeakPtr()));
  }
}

void TabOverviewController::RebuildTiles() {
  if (view_) {
    view_->SetTabs(browser_->GetTabStripModel());
  }
}

void TabOverviewController::OnTabChanged(tabs::TabHandle tab,
                                         TabChangeType change_type) {
  if (view_ && tab.Get()) {
    view_->UpdateTab(tab.Get());
  }
}

void TabOverviewController::RenderFrameHostChanged(
    content::RenderFrameHost* old_host,
    content::RenderFrameHost* new_host) {
  if (new_host->IsInPrimaryMainFrame()) {
    ObserveHost(new_host->GetRenderWidgetHost());
  }
}

void TabOverviewController::OnPageScaleFactorChanged(float page_scale_factor) {
  if (page_scale_factor != 1.0f) {
    gesture_detector_.Cancel();
  }
}

void TabOverviewController::OnInputEventAck(
    const content::RenderWidgetHost& host,
    blink::mojom::InputEventResultSource source,
    blink::mojom::InputEventResultState state,
    const blink::WebInputEvent& event) {
  if (!blink::WebInputEvent::IsPinchGestureEventType(event.GetType())) {
    return;
  }
  const auto& gesture = static_cast<const blink::WebGestureEvent&>(event);
  // A trackpad pinch is acked twice: after the page was offered it as a wheel
  // event, and again after it zoomed the page. Only the first says whether the
  // page handled the pinch itself.
  if (!gesture.IsTouchpadZoomEvent() || !gesture.NeedsWheelEvent()) {
    return;
  }
  switch (event.GetType()) {
    case blink::WebInputEvent::Type::kGesturePinchBegin:
      gesture_detector_.OnPinchBegin(
          web_contents() &&
          web_contents()->GetPrimaryPage().IsPageScaleFactorOne());
      break;
    case blink::WebInputEvent::Type::kGesturePinchUpdate:
      gesture_detector_.OnPinchUpdate(
          gesture.data.pinch_update.scale,
          state == blink::mojom::InputEventResultState::kConsumed);
      break;
    default:
      gesture_detector_.OnPinchEnd();
      break;
  }
}

void TabOverviewController::RenderWidgetHostDestroyed(
    content::RenderWidgetHost* widget_host) {
  ObserveHost(nullptr);
}

void TabOverviewController::OnWidgetActivationChanged(views::Widget* widget,
                                                      bool active) {
  if (widget == widget_.get() && !active) {
    Hide();
  }
}

void TabOverviewController::OnWidgetBoundsChanged(views::Widget* widget,
                                                  const gfx::Rect& new_bounds) {
  if (widget_ && widget != widget_.get() && !in_transition_) {
    widget_->SetBounds(
        browser_view_->contents_container()->GetBoundsInScreen());
  }
}

void TabOverviewController::ObserveContents(content::WebContents* contents) {
  gesture_detector_.Cancel();
  content::WebContentsObserver::Observe(contents);
  ObserveHost(contents ? contents->GetPrimaryMainFrame()->GetRenderWidgetHost()
                       : nullptr);
}

void TabOverviewController::ObserveHost(content::RenderWidgetHost* host) {
  if (observed_host_ == host) {
    return;
  }
  if (observed_host_) {
    observed_host_->RemoveInputEventObserver(this);
    host_observation_.Reset();
  }
  observed_host_ = host;
  if (observed_host_) {
    observed_host_->AddInputEventObserver(this);
    host_observation_.Observe(observed_host_);
  }
}

bool TabOverviewController::CreateWidget() {
  TabStripModel* model = browser_->GetTabStripModel();
  views::Widget* browser_widget = browser_view_->GetWidget();
  if (model->empty() || !browser_widget || !browser_widget->IsVisible()) {
    return false;
  }

  auto view = std::make_unique<TabOverviewView>(
      base::BindRepeating(&TabOverviewController::OnTabChosen,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&TabOverviewController::OnTabCloseRequested,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&TabOverviewController::Hide,
                          weak_factory_.GetWeakPtr()));
  view->SetTabs(model);

  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_POPUP);
  params.parent = browser_widget->GetNativeView();
  // Popups are not activatable by default; this one takes keyboard focus.
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
  params.name = "TabOverview";
  params.bounds = browser_view_->contents_container()->GetBoundsInScreen();
  widget_->Init(std::move(params));
  view_ = widget_->SetContentsView(std::move(view));
  return true;
}

void TabOverviewController::MakeInteractive() {
  views::Widget* browser_widget = browser_view_->GetWidget();
  paint_as_active_lock_ = browser_widget->LockPaintAsActive();
  browser_widget_observation_.Observe(browser_widget);
  widget_->Activate();
  overview_widget_observation_.Observe(widget_.get());
  view_->FocusActiveTile();
}

void TabOverviewController::OnGestureProgress(float progress) {
  if (widget_ && !in_transition_) {
    // Already open.
    return;
  }
  gesture_progress_ = progress;
  // Trackpad gestures normally end explicitly, but the overview appears under
  // the fingers mid-pinch; do not leave it half open if the end goes missing.
  gesture_stall_timer_.Start(FROM_HERE, base::Milliseconds(500), this,
                             &TabOverviewController::OnGestureStalled);
  if (in_transition_) {
    if (gesture_active_) {
      view_->SetTransitionProgress(progress);
    }
    return;
  }
  if (gesture_active_) {
    return;
  }
  gesture_active_ = true;
  // The overview appears once there is a picture of the page to shrink.
  content::RenderWidgetHostView* page_view =
      observed_host_ ? observed_host_->GetView() : nullptr;
  if (page_view && page_view->IsSurfaceAvailableForCopy()) {
    page_view->CopyFromSurface(
        gfx::Rect(), gfx::Size(), base::TimeDelta(),
        base::BindOnce(&TabOverviewController::OnPageSnapshot,
                       gesture_weak_factory_.GetWeakPtr(),
                       page_view->GetDeviceScaleFactor()));
  }
}

void TabOverviewController::OnGestureEnd(bool commit) {
  gesture_stall_timer_.Stop();
  if (!gesture_active_) {
    return;
  }
  gesture_active_ = false;
  gesture_weak_factory_.InvalidateWeakPtrs();
  if (in_transition_) {
    view_->AnimateTransitionTo(
        commit ? 1.0f : 0.0f,
        base::BindOnce(&TabOverviewController::OnTransitionSettled,
                       weak_factory_.GetWeakPtr(), commit));
  } else if (commit) {
    // No page snapshot arrived in time to follow the pinch. This runs inside
    // input dispatch; open the overview (which moves window activation) once
    // that has unwound.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&TabOverviewController::Show,
                                  weak_factory_.GetWeakPtr()));
  }
}

void TabOverviewController::OnGestureStalled() {
  gesture_detector_.OnPinchEnd();
}

void TabOverviewController::OnPageSnapshot(
    float device_scale_factor,
    const content::CopyFromSurfaceResult& result) {
  if (!gesture_active_ || widget_ || !result.has_value() || !CreateWidget()) {
    return;
  }
  in_transition_ = true;
  widget_->ShowInactive();
  view_->BeginTransition(
      gfx::ImageSkia::CreateFromBitmap(result->bitmap, device_scale_factor));
  view_->SetTransitionProgress(gesture_progress_);
}

void TabOverviewController::OnTransitionSettled(bool commit) {
  if (!widget_ || !in_transition_) {
    return;
  }
  if (!commit) {
    Hide();
    return;
  }
  in_transition_ = false;
  view_->EndTransition();
  MakeInteractive();
}

void TabOverviewController::OnTabChosen(tabs::TabHandle tab) {
  TabStripModel* model = browser_->GetTabStripModel();
  if (tab.Get() && model->GetIndexOfTab(tab.Get()) != TabStripModel::kNoTab) {
    model->ActivateTab(tab,
                       TabStripUserGestureDetails(
                           TabStripUserGestureDetails::GestureType::kOther));
  }
  Hide();
}

void TabOverviewController::OnTabCloseRequested(tabs::TabHandle tab) {
  browser_->GetTabStripModel()->CloseTab(
      tab, TabCloseTypes::CLOSE_USER_GESTURE |
               TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
}
