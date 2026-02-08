# Issue: Fix 506 `-Woverloaded-virtual` Warnings

## Summary

The build produces 506 `-Woverloaded-virtual` warnings from **29 unique source locations** across **15 unique header files**. The high count is due to headers being included in many translation units.

The warnings fire when a derived class declares a method with the same name as a virtual method in a wxWidgets base class, but with a different signature. This **hides** the base class method from overload resolution.

## Root Cause

In each case, the derived class intentionally provides its own version of a method (e.g. `Refresh()`, `Enable()`, `Popup(...)`) with a simplified or different signature than the wxWidgets base class virtual.

Example:
```
wxWindow::Refresh(bool eraseBackground = true, const wxRect* rect = nullptr)  // base
ScrolledWindow::Refresh()  // derived — hides base
```

## Why Previous Guidance Said "Do Not Fix"

The concern was that adding `using Base::Method;` would cause **ambiguous call errors** at call sites. This is true for some cases (e.g. `Enable()` vs `Enable(bool=true)` — both callable with zero args). However, many of the 29 locations have signatures different enough that a `using` declaration or signature fix would work cleanly.

## Recommended Fix Strategy

Evaluate each location individually. Three approaches available:

1. **Fix the signature** — If the derived method differs only trivially (e.g. `wxFont` vs `const wxFont&`), change to match the base and add `override`. Best fix when possible.

2. **Rename the method** — If the derived method does something semantically different from the base, rename it to avoid the collision entirely (e.g. `Update()` → `UpdateContent()`).

3. **Suppress with pragma** — For cases where neither fix works (true ambiguity), use targeted `#pragma clang diagnostic ignored` around the class definition.

## Warning Locations (29 unique, 15 files)

### `GUI/Widgets/AMSItem.hpp` — 7 warnings

| Line | Class | Method | Base class method |
|------|-------|--------|-------------------|
| 301 | `AMSrefresh` | `Update()` | `wxWindow::Update()` |
| 462 | `AMSLib` | `Update()` | `wxWindow::Update()` |
| 548 | `AMSRoad` | `Update()` | `wxWindow::Update()` |
| 582 | `AMSRoadUpPart` | `Update()` | `wxWindow::Update()` |
| 670 | `AMSPreview` | `Update()` | `wxWindow::Update()` |
| 724 | `AMSHumidity` | `Update()` | `wxWindow::Update()` |
| 758 | `AmsItem` | `Update()` | `wxWindow::Update()` |

All hide `wxWindow::Update()` (no params, repaints window). Candidate for **rename** to e.g. `UpdateState()` or `UpdateContent()`.

### `GUI/FilamentMapPanel.hpp` — 4 warnings

| Line | Class | Method | Base class method |
|------|-------|--------|-------------------|
| 21 | `FilamentMapManualPanel` | `Show(...)` | `wxWindow::Show(bool)` |
| 44 | `FilamentMapBtnPanel` | `Show(...)` | `wxWindow::Show(bool)` |
| 74 | `FilamentMapAutoPanel` | `Show(...)` | `wxWindow::Show(bool)` |
| 91 | `FilamentMapDefaultPanel` | `Show(...)` | `wxWindow::Show(bool)` |

### `GUI/Widgets/ScrolledWindow.hpp` — 2 warnings

| Line | Class | Method | Base class method |
|------|-------|--------|-------------------|
| 18 | `ScrolledWindow` | `Refresh()` | `wxWindow::Refresh(bool, const wxRect*)` |
| 19 | `ScrolledWindow` | `SetBackgroundColour(wxColour)` | `wxWindow::SetBackgroundColour(const wxColour&)` |

`SetBackgroundColour` differs only in `wxColour` vs `const wxColour&` — candidate for **signature fix**.

### `GUI/Search.hpp` — 2 warnings

| Line | Class | Method | Base class method |
|------|-------|--------|-------------------|
| 220 | `SearchDialog` | `Popup(wxPoint)` | `wxPopupTransientWindow::Popup(wxWindow*)` |
| 264 | `SearchObjectDialog` | `Popup(wxPoint)` | `wxPopupTransientWindow::Popup(wxWindow*)` |

Completely different parameter types. Candidate for **rename** (e.g. `PopupAt(wxPoint)`).

### `GUI/Widgets/RadioBox.hpp` — 2 warnings

| Line | Class | Method | Base class method |
|------|-------|--------|-------------------|
| 18 | `RadioBox` | `GetValue()` | base `GetValue()` with different return type |
| 23 | `RadioBox` | `Enable()` | `wxWindow::Enable(bool=true)` |

### `GUI/SlicingProgressNotification.hpp` — 2 warnings

| Line | Class | Method | Base class method |
|------|-------|--------|-------------------|
| 56 | `SlicingProgressNotification` | `render_text(...)` | base `render_text(...)` with different params |
| 59 | `SlicingProgressNotification` | `render_close_button(...)` | base with different params |

### `GUI/DeviceTab/uiAmsHumidityPopup.h` — 2 warnings

| Line | Class | Method | Base class method |
|------|-------|--------|-------------------|
| 41 | `uiAmsPercentHumidityDryPopup` | `Update()` | `wxWindow::Update()` |
| 48 | `uiAmsPercentHumidityDryPopup` | `Update()` | `wxWindow::Update()` |

### Single-warning files (7 files, 1 warning each)

| File | Line | Class | Method hides |
|------|------|-------|-------------|
| `GUI/Widgets/SwitchButton.hpp` | 65 | `SwitchBoard` | `Enable()` hides `wxWindow::Enable(bool)` |
| `GUI/Widgets/AnimaController.hpp` | 16 | `AnimaIcon` | `Enable()` hides `wxWindow::Enable(bool)` |
| `GUI/Widgets/LabeledStaticBox.hpp` | 45 | `LabeledStaticBox` | `SetFont(wxFont)` hides `SetFont(const wxFont&)` |
| `GUI/Widgets/ErrorMsgStaticText.hpp` | 20 | `ErrorMsgStaticText` | `SetLabel(...)` hides base `SetLabel` |
| `GUI/AMSMaterialsSetting.hpp` | 88 | `ColorPickerPopup` | `Popup(...)` hides base `Popup(wxWindow*)` |
| `GUI/AMSSetting.hpp` | 113 | `AMSSettingTypePanel` | `Update()` hides `wxWindow::Update()` |
| `GUI/GUI_ObjectTableSettings.hpp` | 74 | `ObjectTableSettings` | `UpdateAndShow(...)` hides base |
| `GUI/MediaPlayCtrl.cpp` | 511 | `DownloadProgressDialog2` | `make_job(...)` hides base |

## Approach

Fix each file incrementally, one at a time. Rebuild and test after each change. Start with the simplest cases (signature fixes) and work toward the harder ones (renames with many call sites).
