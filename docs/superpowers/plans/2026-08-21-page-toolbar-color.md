# Page Toolbar Color Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On macOS, tint normal Chromium browser chrome from the active page once per primary document load while leaving Incognito and app windows unchanged.

**Architecture:** `BrowserView` samples the active `WebContents` after its first visually non-empty paint and stores the result in `content::PageUserData`, making the value immutable for that document and naturally scoped across tab switches and BFCache. `BrowserWidget` converts that latched color into the existing `BrowserThemePack` used by web apps and refreshes the window theme.

**Tech Stack:** Chromium C++, Views, Content `WebContentsObserver`/`PageUserData`, `BrowserThemePack`, in-process browser tests.

**Spec:** Conversation-approved bounded design from 2026-08-21; this plan is its durable implementation record.

## Global Constraints

- Implement only on macOS with `BUILDFLAG(IS_MAC)`.
- Apply only to `BrowserWindowInterface::Type::TYPE_NORMAL` windows.
- Incognito must retain its existing theme and color-provider behavior.
- Existing app/PWA theme behavior must remain unchanged.
- Sample exactly once per primary `content::Page`; ignore later DOM theme/background changes.
- Prefer `WebContents::GetThemeColor()`, fall back to `GetBackgroundColor()`, and force a selected color opaque.
- Do not inspect rendered pixels, sticky headers, or scroll state.
- Add no preference, feature flag, controller, dependency, or renderer change.
- Use focused real browser tests and follow RED → GREEN TDD.

---

### Task 1: Latch and apply the active page color

**Files:**
- Modify: `chrome/browser/ui/views/frame/browser_view.h`
- Modify: `chrome/browser/ui/views/frame/browser_view.cc`
- Modify: `chrome/browser/ui/views/frame/browser_widget.h`
- Modify: `chrome/browser/ui/views/frame/browser_widget.cc`
- Test: `chrome/browser/ui/views/frame/browser_widget_browsertest.cc`

**Interfaces:**
- Consumes: `WebContents::CompletedFirstVisuallyNonEmptyPaint()`, `GetThemeColor()`, `GetBackgroundColor()`, `content::PageUserData`, and `BrowserThemePack::BuildFromWebAppColors()`.
- Produces: `BrowserWidget::SetPageThemeColor(std::optional<SkColor> color)` and `BrowserView::UpdatePageToolbarThemeColor()`.

- [x] **Step 1: Write the failing macOS browser test**

Add a local first-paint waiter and these tests:

```cpp
IN_PROC_BROWSER_TEST_F(
    BrowserWidgetColorProviderTest,
    PageThemeColorIsLatchedPerPage) {
  // Page 1: red theme-color over a blue body; toolbar becomes red.
  // Mutate theme-color to yellow; toolbar remains red.
  // Page 2: no theme-color and a green body; toolbar becomes green.
  // Switch back to Page 1; toolbar is still the latched red.
  // Cross-document navigate Page 1 to blue; toolbar becomes blue.
}

IN_PROC_BROWSER_TEST_F(
    BrowserWidgetColorProviderTest,
    PageThemeColorDoesNotAffectIncognito) {
  // Capture Incognito's baseline toolbar color, load colored content, and
  // verify that the baseline color is unchanged.
}
```

Use literal `SkColor` expectations and read the real result through:

```cpp
GetBrowserWidget(target_browser)
    ->GetColorProvider()
    ->GetColor(kColorToolbar)
```

Use real `data:text/html` documents, `content::ThemeChangeWaiter` for the
post-load meta mutation, and a local `WebContentsObserver`/`base::RunLoop`
first-paint waiter.

- [x] **Step 2: Run RED and record the expected failure**

```sh
/Users/xiguoduan/go/src/github.com/alextuan1024/depot_tools/autoninja \
  -C out/Release browser_tests

out/Release/browser_tests \
  --gtest_filter='BrowserWidgetColorProviderTest.PageThemeColor*'
```

Expected RED: the normal window's `kColorToolbar` remains the default/profile
color instead of literal red. Compilation errors are not RED; fix them and
rerun until the behavioral assertion fails.

- [x] **Step 3: Add the per-document latch to `BrowserView`**

In `browser_view.cc`, define a file-local macOS-only page datum:

```cpp
class PageToolbarThemeColor
    : public content::PageUserData<PageToolbarThemeColor> {
 public:
  const std::optional<SkColor>& color() const { return color_; }

 private:
  friend content::PageUserData<PageToolbarThemeColor>;
  PageToolbarThemeColor(content::Page& page,
                        std::optional<SkColor> color);

  std::optional<SkColor> color_;
  PAGE_USER_DATA_KEY_DECL();
};
```

Implement `BrowserView::UpdatePageToolbarThemeColor()` with this behavior:

```cpp
if (!web_contents() ||
    !web_contents()->CompletedFirstVisuallyNonEmptyPaint()) {
  browser_widget()->SetPageThemeColor(std::nullopt);
  return;
}

content::Page& page = web_contents()->GetPrimaryPage();
auto* data = PageToolbarThemeColor::GetForPage(page);
if (!data) {
  std::optional<SkColor> color = web_contents()->GetThemeColor();
  if (!color) {
    color = web_contents()->GetBackgroundColor();
  }
  if (color) {
    color = SkColorSetA(*color, SK_AlphaOPAQUE);
  }
  PageToolbarThemeColor::CreateForPage(page, color);
  data = PageToolbarThemeColor::GetForPage(page);
}
browser_widget()->SetPageThemeColor(data->color());
```

Call the helper from:

```cpp
void BrowserView::DidFirstVisuallyNonEmptyPaint();
void BrowserView::PrimaryPageChanged(content::Page& page);
void BrowserView::OnActiveTabChanged(...);  // after Observe(new_contents)
```

Do not add `DidChangeThemeColor()` or `OnBackgroundColorChanged()` observers.
A same-document navigation keeps the same page; a new document gets a new
datum; BFCache restoration reuses the stored datum.

- [x] **Step 4: Apply the latch through `BrowserWidget`**

Add this macOS-only public method and member:

```cpp
void SetPageThemeColor(std::optional<SkColor> color);
scoped_refptr<BrowserThemePack> page_theme_pack_;
```

The setter returns without changing state unless the browser is normal and
non-Incognito. For a color, create an autogenerated pack and call:

```cpp
BrowserThemePack::BuildFromWebAppColors(*color, *color,
                                        page_theme_pack_.get());
```

For `std::nullopt`, reset the pack. Then call:

```cpp
UserChangedTheme(BrowserThemeChangeType::kWebAppTheme);
```

When the pack exists, `GetCustomTheme()` returns it after the existing
Incognito/user-color guard and before app/profile lookup. `GetThemeProvider()`
returns the profile's default provider so extension theme images cannot cover
the page-colored toolbar. Do not change `BrowserThemePack` or the web-app
controller.

- [x] **Step 5: Run GREEN and inspect the diff**

Run the targeted test command from Step 2. Expected: both tests pass with zero
failures. Then run:

```sh
git diff --check
git diff --stat
git diff -- chrome/browser/ui/views/frame/browser_view.h \
  chrome/browser/ui/views/frame/browser_view.cc \
  chrome/browser/ui/views/frame/browser_widget.h \
  chrome/browser/ui/views/frame/browser_widget.cc \
  chrome/browser/ui/views/frame/browser_widget_browsertest.cc
```

Confirm there is no dynamic theme observer, controller, preference, flag,
renderer change, or non-macOS behavior.

- [x] **Step 6: Commit the implementation**

```sh
git add docs/superpowers/plans/2026-08-21-page-toolbar-color.md \
  chrome/browser/ui/views/frame/browser_view.h \
  chrome/browser/ui/views/frame/browser_view.cc \
  chrome/browser/ui/views/frame/browser_widget.h \
  chrome/browser/ui/views/frame/browser_widget.cc \
  chrome/browser/ui/views/frame/browser_widget_browsertest.cc
git commit -m "Add per-page toolbar colors on macOS"
```
