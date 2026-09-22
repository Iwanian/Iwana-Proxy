# Localization

Three languages: `en`, `fa` (Persian), `ru`. Strings live in `src/Loc.h` as
a `Key -> string` table per language, looked up via `L(app, Loc::Key::X)`.
Adding a string means adding it to the `enum class Key` and to **all three**
language tables in `Loc.h` — the code doesn't fall back gracefully if a key
is missing from one language's table, on purpose, so a missing translation
fails loudly (a build error, or an obviously-wrong string on screen) rather
than silently.

## RTL is not "mirror everything"

Persian is the only RTL language here, and the rule that fell out of a lot
of back-and-forth is: **text direction and alignment follow the language;
control/element *position* only mirrors where it traditionally should, and
stays fixed where mirroring would actually hurt usability.** Concretely:

### Mirrors properly (traditional RTL) in `fa`

- **Top bar**: gear icon moves to the *left*; back button moves to the
  *right* (and its chevron points right); title/subtitle become
  right-aligned, growing from the right edge.
- **Toggle rows** (banner slider, auto-scan enable): label right-aligned on
  the right, the switch itself on the *left* — the mirror image of the
  English row, not a repositioned copy of it.
- **Nav rows** (Saved Proxies / Speed Test / Support cards in Settings):
  icon + label move together as one group to the right side, staying
  adjacent to each other — never split across opposite edges of the row.
- **Support screen's GitHub/Telegram rows**: same "icon + label move
  together" rule, grouped on the right.
- **Auto-scan interval label** ("Scan interval: N seconds"): right-aligned.
  (The slider track itself stays left-to-right/min-to-max in both
  languages — only the label above it mirrors.)
- **Body paragraphs** (the "all proxies are collected from third parties…"
  disclaimer, and similar full-width text blocks): right-aligned.

### Deliberately does **not** mirror, in any language

- **Proxy cards** (Home/Saved lists): name + status/download/Russian badges
  + host:port + copy/bookmark icons always on the **left**; latency number
  + "LATENCY" caption + Connect button always on the **right** — in `en`,
  `fa`, and `ru` alike. This was an explicit, repeated product decision,
  not an oversight: the card's layout is treated as a fixed data
  visualization (like a table row) rather than prose, so it doesn't
  reflow with reading direction.
- The **"LATENCY"** caption specifically is never translated — it stays the
  English word in every language, again by explicit direction.
- **Language selection rows** (فارسی / English / Русский in Settings) sit
  in the same fixed column regardless of which language is currently
  active, so switching languages doesn't reshuffle the list you're looking
  at.

## Why this split, mechanically

Every draw function that needs it takes `bool rtl = IsRtl(app)` and either:

1. **Branches on `rtl` for position** (top bar, toggles, nav rows, Support
   rows) — genuinely different `x` coordinates per direction, mirrored
   around the row/screen's horizontal center.
2. **Branches on `rtl` only for `PangoAlignment`** (`PANGO_ALIGN_RIGHT` vs.
   `PANGO_ALIGN_LEFT`) while keeping the same box position — used for body
   text.
3. **Ignores `rtl` entirely** — proxy cards. `DrawProxyCard` has no `rtl`
   branches in its layout math at all; the only Persian-specific thing that
   happens is that the *pill text* it draws (Online/Offline/Scanning,
   For Download, Russian) comes out as Persian words, because that text
   came from `Loc::T()` — the geometry around it never changes.

If you're adding a new UI element and aren't sure which bucket it falls
into: default to bucket 3 (fixed position, translated text only) unless the
element is a standalone row with its own label+control pair, in which case
it's almost certainly bucket 1.

## Why Pango instead of hand-rolled bidi

The Windows build (GDI+) had to hand-roll a fair amount of RTL-adjacent
logic — GDI+ doesn't implement the Unicode Bidi Algorithm (UAX #9) for you.
Pango does, natively, so mixed Persian/English strings (a proxy hostname
inside an otherwise-Persian sentence, for instance) shape and order
correctly with zero special-casing in this codebase — `pango_layout_set_
alignment` plus feeding it well-formed UTF-8 is the entire RTL text story.
This is the one area where the Linux port is *more* correct by construction
than a literal line-for-line port of the Windows code would have been.

## Fonts

`Theme::FontFamily` is a comma-separated Pango fallback chain (`Cantarell,
Noto Sans, Noto Sans Arabic, Ubuntu, DejaVu Sans, sans-serif`) rather than a
single font, specifically so Persian glyphs still render correctly (falling
through to *Noto Sans Arabic*) on a system that has the Latin-only members
of that list but not all of them. If you add a font-family constant
anywhere, always give it a similarly broad fallback chain — a single named
font that happens to lack Arabic-script glyphs on the build machine will
render Persian text as invisible/tofu boxes on someone else's system, and
it's an easy thing to not notice until it's reported by a Persian-locale
user.
