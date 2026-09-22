# Architecture

## Immediate-mode UI

There is no widget tree. Every screen is one function
(`DrawHomeScreen`, `DrawSettingsScreen`, `DrawSpeedTestScreen`, …) that runs
top-to-bottom on every single frame, painting directly onto a Cairo context
handed to us by a single `GtkDrawingArea`. `PaintApp()` clears the canvas,
switches on `AppState::screen`, and calls the matching draw function; the
draw function returns the total content height so the caller can clamp
scrolling.

This mirrors the original Windows build's GDI+ `Paint()` function almost
exactly — the whole point of the port was to keep the same "just repaint
everything, every frame, from application state" model rather than adopt
GTK's native retained-widget approach, since state (scroll position, which
proxy is focused, live ping results arriving from a background thread) is
already a plain C++ struct (`AppState`) that's trivial to reason about
without a parallel widget hierarchy to keep in sync.

### Why this is fine performance-wise

The canvas is small (a phone-shaped ~480×800 window), Cairo is fast at flat
fills/strokes, and redraws only happen on `gtk_widget_queue_draw()` — i.e. on
input or a timer tick, not continuously. There's no measurable difference
between this and a widget-based layout at this scale.

## Hit-testing

Since there's no widget tree, there's no widget to receive click events
either. Every draw function that places something clickable also calls
`AddContentHit(app, x, y, w, h, "action:name")`, which appends a rectangle
and an action string to `app->hitRects`. After painting, a single
`GtkGestureClick` on the drawing area walks `hitRects` **in reverse** (most
recently drawn = drawn on top = checked first) and dispatches the first
rectangle that contains the click point to `HandleAction()`.

Two helper functions matter here:

- **`AddHit`** — registers a hit rectangle unconditionally. Used *only* by
  the top bar's own back/gear buttons, which must stay clickable at a fixed
  screen position no matter how the content below has scrolled.
- **`AddContentHit`** — registers a hit rectangle for scrollable content,
  but **rejects it outright if it's entirely above the visible content area**
  (`y + h <= app->currentBarH`). Without this, a card's hit rectangle —
  computed from its scrolled Y position — could end up geometrically
  overlapping the fixed top bar's screen coordinates once scrolled far
  enough, even though it's invisible (clipped away). Since content hits are
  registered *after* the top bar's and get checked *first* (reverse
  iteration), that invisible rectangle would silently swallow clicks meant
  for the back button. This was a real, previously-shipped bug; `AddHit` vs
  `AddContentHit` is the fix, and the rule of thumb going forward is: **any
  new clickable element inside scrolled content uses `AddContentHit`, never
  `AddHit`.**

Every screen also wraps its scrollable content in a Cairo clip
(`cairo_rectangle(0, barH, width, height-barH); cairo_clip()`) so painted
content can't visually bleed above the fixed top bar during a scroll — the
visual half of the same problem `AddContentHit` solves for input.

## Navigation state

`AppState::screen` is the current screen; `AppState::settingsReturnTo` is
where "back" goes *from Settings*. Every sub-screen reachable from Settings
(Saved, Speed Test, Support) sets `settingsReturnTo = Screen::Settings`
before navigating there, so their own "back" returns to Settings.

The subtlety: when "back" lands you **on** Settings, `settingsReturnTo` must
be reset to `Screen::Home` right then — not later. If it isn't, the *next*
press of Settings' own back button reuses the stale value (still pointing
at whatever sub-screen you came from) and sends you right back into it
instead of Home, which looks exactly like "the back button doesn't work"
even though the click is registering correctly the whole time. This was a
real, previously-shipped bug — see `CHANGELOG.md`. The fix lives in the
`"nav:back"` branch of `HandleAction()`:

```cpp
if (action == "nav:back") {
    app->screen = app->settingsReturnTo;
    app->scrollY = 0;
    if (app->screen == Screen::Settings) app->settingsReturnTo = Screen::Home;
}
```

## The Speed Test input field is hand-drawn, not a GTK widget

An earlier revision overlaid a real `GtkText` widget on top of the drawn
input pill (via `GtkOverlay`), to get free caret/IME handling. That worked
functionally but was replaced with a fully hand-drawn field (text + a
blinking caret, both painted directly; keyboard input read from a
`GtkEventControllerKey` and mapped through `gdk_keyval_to_unicode`) for two
reasons:

1. Proxy links/configs are always plain ASCII (`tg://…`, `server:port:secret`),
   so there is no real IME/Unicode-input need to justify a whole extra widget.
2. Compositing a real widget over a Cairo canvas at a position that changes
   with scroll, on top of a screen that itself only exists conditionally,
   turned out to be a source of subtle, hard-to-reproduce visual glitches on
   some systems (and, worse, its off-screen "hidden" position when not on
   the Speed Test screen may have still been hit-testable, which was one
   contributing theory for the back-button bug above). Hand-drawing the
   field removes that whole class of problem outright — everything is
   Cairo, all the time, no second rendering/event pipeline to keep in sync
   with the first.

The caret blinks via a 500ms `g_timeout_add` that flips `caretVisible` and
requests a redraw only while `speedInputFocused` is true.

## SVG icon rendering

Most icons in the app (gear, bookmark, arrows, the theme-mode glyphs, …) are
small hand-drawn Cairo paths — a handful of `cairo_arc`/`cairo_line_to`
calls each, living in the `Icon::` namespace in `main.cpp`.

The GitHub and Telegram marks on the Support screen are different: they're
rendered from the **actual official SVG path data** (sourced from the
[simple-icons](https://github.com/simple-icons/simple-icons) project, 24×24
viewBox), because a hand-approximated silhouette wasn't accurate enough for
a real trademarked logo. Reproducing them exactly meant writing a small,
generic SVG path parser (`Icon::SvgPathParser`) rather than hand-transcribing
each Bézier curve — it supports the commands that appear in real icon paths
(`M L H V C S Q A Z`, upper or lower case, including the elliptical-arc-to-
Bézier conversion `A`/`a` needs) and feeds them straight into an equivalent
sequence of `cairo_move_to` / `cairo_curve_to` / `cairo_line_to` calls,
scaled to fit whatever pixel size the caller asks for:

```cpp
Icon::DrawSvgIcon24(cr, kGitHubPathData, cx, cy, /*size=*/24, color);
```

If you need to add another brand mark later, grab its path `d` attribute
from simple-icons (or any accurate 24×24-viewBox SVG) and pass it to
`DrawSvgIcon24` the same way — no manual path transcription needed.

## Networking

All network I/O (`ProxySource::Fetch`, `PingService::PingAll`,
`SpeedTester::Run`, `BannerSlideshow::FetchBannerItems`) runs on background
`std::thread`s, never on the GTK main thread. Results are handed back via
`g_idle_add`, matching the same "do work off the UI thread, marshal the
result back" pattern the Windows build uses with `PostMessage`.

`ProxySource::Fetch()` is intentionally sequential, not concurrent: primary
URL, then (only if that fails) the fallback URL, then (only if *that* fails
too) the on-disk cache. This is a product requirement, not an
implementation detail — see the comment in `ProxySource.h`.
