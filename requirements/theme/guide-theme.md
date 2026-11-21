# Data Collector: Fonts and Theme Tokens

For the orchestrator and the frontend-developer. This file defines the fonts and the light and dark palettes used by the GUI mock-up, and how to ship them in the Qt app. It supplements section 8 of `data-collector-orchestrator-guide-v2.md`, and where this file is more specific, this file wins.

Repo rules still apply: no silent fallbacks, structured logging, files only where reused by 2+ files (so the theme lives in the single `Theme.qml`).

---

## 1. Fonts

| Role | Family | Where it is used |
|---|---|---|
| Interface text | **Inter** | Everything: headings, labels, buttons, table text, chips, banners |
| Code and data text | **JetBrains Mono** | File paths, IP addresses, host names, mount points, current file name in progress rows |

Both are free, open source fonts under the **SIL Open Font License 1.1**, so they can be bundled and redistributed with the app. Keep each font's `OFL.txt` license file next to the font files.

Official sources (download once, do not substitute other fonts):
- Inter: https://github.com/rsms/inter (releases), also https://fonts.google.com/specimen/Inter
- JetBrains Mono: https://github.com/JetBrains/JetBrainsMono (releases), also https://fonts.google.com/specimen/JetBrains+Mono

If the agent cannot download them (restricted network), stop and ask the user to place the files in `resources/fonts/`. Do not switch to a system font.

### Files to bundle (static TTF, not variable)
Static files give predictable weights in Qt. Bundle only what the design uses:

| File | Weight | Use |
|---|---|---|
| `Inter-Regular.ttf` | 400 | Body text, descriptions |
| `Inter-Medium.ttf` | 500 | Preset buttons |
| `Inter-SemiBold.ttf` | 600 | Headings, labels, chips, primary buttons, table header |
| `JetBrainsMono-Regular.ttf` | 400 | All mono text |

Optional later: `Inter-Bold.ttf` (700), `JetBrainsMono-Medium.ttf` (500). The mock-up does not use them.

### Type scale (px, as in the mock-up; in QML use `font.pixelSize`)

| Style | Size / weight | Used for |
|---|---|---|
| Login headline | 36 / 600, line height 1.15, letter spacing -0.01em | Login left panel |
| Big percent | 40 / 600, letter spacing -0.02em | Overall progress number |
| Page title | 24 / 600 | "Select sources", "Copying to USB", "Sign in" |
| Section value | 16 / 600 | "5 of 6 PCs selected" |
| Body | 14 / 400 | Descriptions, table text |
| Body strong | 14 / 600 | PC names |
| Button primary | 15 / 600 | Sign in, Start copy, Cancel copy, Eject |
| Label | 13 / 600 | Field labels, stage text, banner titles |
| Small | 13 / 400 or 12 / 400 | Help text, secondary lines, counts |
| Chip | 12 / 600 | Status chips |
| Table header | 12 / 600, uppercase, letter spacing 0.04em | Column titles |
| Mono | 12 to 13 / 400 | Paths, IPs, mount points; 11 for the current file name |

### Bundling in Qt 6.7 (CMake)
1. Put the TTFs and their `OFL.txt` files in `resources/fonts/`.
2. Add them to the resources with an alias so QML and C++ use short paths:
   ```cmake
   qt_add_resources(AesaIdm "fonts"
       PREFIX "/fonts"
       BASE "${CMAKE_SOURCE_DIR}/resources/fonts"
       FILES
           resources/fonts/Inter-Regular.ttf
           resources/fonts/Inter-Medium.ttf
           resources/fonts/Inter-SemiBold.ttf
           resources/fonts/JetBrainsMono-Regular.ttf)
   ```
3. Register them once at startup in `main.cpp`, before the QML engine loads. Check every return value (`-1` means failure) and log a structured error. Do not continue silently with a substitute font:
   ```cpp
   static const char *const kFontPaths[] = {
       ":/fonts/Inter-Regular.ttf", ":/fonts/Inter-Medium.ttf",
       ":/fonts/Inter-SemiBold.ttf", ":/fonts/JetBrainsMono-Regular.ttf"};

   for (const char *fontPath : kFontPaths) {
       if (QFontDatabase::addApplicationFont(QString::fromLatin1(fontPath)) < 0) {
           qCritical().nospace() << "op=\"load_font\" path=\"" << fontPath << "\" err=\"addApplicationFont failed\"";
           return 1;
       }
   }
   ```
4. Use the family names `"Inter"` and `"JetBrains Mono"` in `Theme.qml`.
5. Add an assertion to the end-to-end smoke test that both families are present in `QFontDatabase::families()` after startup.

The Google Fonts link inside the mock-up artboards is only for the mock-up. The app is used on an offline LAN, so the fonts must be bundled. Do not rely on `fonts-inter` or `fonts-jetbrains-mono` Ubuntu packages being installed.

---

## 2. Theme tokens

The app supports two palettes, **light** and **dark**, selected by the config key `ui.theme` (`"light"` or `"dark"`, default `"light"`). Add this key to `config/config.example.json` and validate it in `ConfigLoader`. `Theme.qml` is a singleton that exposes the values below. It chooses the palette once at startup. A live toggle is not required.

### Core colors

| Token | Light | Dark |
|---|---|---|
| bg (window) | #F3F4F6 | #0F151D |
| panel (cards, header) | #FFFFFF | #17202B |
| panel header row (table head) | #F8F9FB | #1D2835 |
| border | #D9DDE3 | #2B3949 |
| row divider | #E6E9EE | #243040 |
| row offline background | #FAFAFB | #131B25 |
| input background | #FFFFFF | #0F151D |
| input / outlined button border | #B8C0CC | #5B6C82 |
| text | #1B2430 | #E6EBF2 |
| muted text | #5B6675 | #9AA7B8 |
| primary (button background) | #1F5FD6 | #2F6FED |
| primary text and links | #1F5FD6 | #7FA8FF |
| primary fill (bars, checkbox accent) | #1F5FD6 | #4C8DFF |
| on-primary (text on primary button) | #FFFFFF | #FFFFFF |
| focus ring | #1F5FD6 | #7FA8FF |
| progress track | #E3E7ED | #2A3646 |
| login left panel background | #16212F | #0B1118 |
| login left panel row background | #1F2D3F | #151E2A |
| login left panel text / muted | #F2F5F9 / #A9B6C6 | #F2F5F9 / #9FB0C4 |
| login left panel mono text | #D6E0EC | #D6E0EC |

### Status colors (text, background, border)

| State | Light | Dark |
|---|---|---|
| Success (Online, Done, USB ready) | text #17603A, bg #E7F4EC, border #B9DEC8, bar fill #1E7B4A | text #6FD39A, bg #12301F, border #1F5A3A, bar fill #3DB873 |
| Warning (No USB, hints) | text #7A4A00, bg #FFF4E0, border #E8C98A | text #F2C46B, bg #33260C, border #6B4E16 |
| Error (Offline, Failed, not enough space) | text #8A2A24, bg #FBEAE8, border #EBC3BF, bar fill #B3261E | text #FF9C94, bg #3A1816, border #7A2E29, bar fill #FF6B61 |
| Neutral chip (Queued, Skipped) | text #3F4A59, bg #EEF0F3 | text #B7C2D1, bg #243040 |
| Disabled button | text #5B6675, bg #EEF0F3, border #D9DDE3 | text #8795A8, bg #243040, border #2B3949 |

Rules:
- Status never relies on color alone. Every chip carries text, and offline or failed rows carry a message.
- Text and its background keep at least 4.5:1 contrast (3:1 for large text and UI borders). Do not tint these colors by hand, copy the values.
- The dark palette is not an inversion of the light one. Surfaces step up in lightness (bg, panel, header row) to show depth, and the primary button background is slightly darker than the link color so white text keeps its contrast.
- Dark input fields use the window background color so they read as recessed.

### Shape and spacing (same for both themes)

| Token | Value |
|---|---|
| Control radius | 8 px |
| Card radius | 10 px (login card 12 px) |
| Chip radius | 999 px (full pill) |
| Header height | 64 px |
| Minimum hit target | 44 px (field and secondary button height 44, Sign in 46, Start copy and footer buttons 48) |
| Card padding | 20 px (progress overall card 24 px) |
| Page padding | 32 px, content max width 1100 px, centered |
| Gaps | 24 px between cards, 20 px inside cards, 12 px between related items |
| Table row height | 64 px (68 px on progress) |
| Progress bar height | 12 px overall, 8 px per row, and 8 px estimate bar |
| Focus ring | 2 px, 2 px offset |

---

## 3. Theme.qml shape

```qml
pragma Singleton
import QtQuick

QtObject {
    id: root

    // Set once from the config before the main window loads.
    property bool dark: false

    readonly property string fontText: "Inter"
    readonly property string fontMono: "JetBrains Mono"

    readonly property color bg:        dark ? "#0F151D" : "#F3F4F6"
    readonly property color panel:     dark ? "#17202B" : "#FFFFFF"
    readonly property color border:    dark ? "#2B3949" : "#D9DDE3"
    readonly property color text:      dark ? "#E6EBF2" : "#1B2430"
    readonly property color muted:     dark ? "#9AA7B8" : "#5B6675"
    readonly property color primary:   dark ? "#2F6FED" : "#1F5FD6"
    readonly property color primaryText: dark ? "#7FA8FF" : "#1F5FD6"
    readonly property color primaryFill: dark ? "#4C8DFF" : "#1F5FD6"
    // ...continue with every token in section 2, same names in both branches.

    readonly property int radiusControl: 8
    readonly property int radiusCard: 10
    readonly property int radiusChip: 999
}
```

Rules for the frontend-developer:
- No hex values outside `Theme.qml`. Every color in a view comes from `Theme`.
- Both palettes must be complete. A missing dark token is a defect.
- Cover the same screens and states in both themes: Login, Sources (ready, no USB, low space, drive menu open), Progress (running, finished, USB removed). Acceptance includes a screenshot of each state in each theme.

---

## 4. Mock-up reference

The Design canvas contains the light screens (Login, Sources, Progress, No USB, Low space with the drive menu open) and the dark versions of Login, Sources and Progress. The dark No USB and Low space states are not drawn, use the status color table above for them. The mock-up uses the Google Fonts link only to render the two families, the app bundles them as described in section 1.