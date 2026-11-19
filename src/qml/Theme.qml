pragma Singleton
import QtQuick 2.15

QtObject {
    // Palette tokens (Section 6 + mock-up-gui)
    readonly property color bg: "#F3F4F6"
    readonly property color panel: "#FFFFFF"
    readonly property color border: "#D9DDE3"
    readonly property color divider: "#E5E7EB"
    readonly property color rowHeaderBg: "#F9FAFB"
    readonly property color text: "#1B2430"
    readonly property color muted: "#5B6675"
    readonly property color primary: "#1F5FD6"
    readonly property color primaryHover: "#1A51B8"
    readonly property color primaryPressed: "#16449C"
    readonly property color primaryTint: "#EEF4FF"
    readonly property color disabledBg: "#E9ECF1"
    readonly property color disabledText: "#6B7280"

    readonly property color success: "#17603A"
    readonly property color successBg: "#E7F4EC"
    readonly property color successBorder: "#B9DFC8"
    readonly property color successBar: "#1E7E4A"

    readonly property color warning: "#7A4A00"
    readonly property color warningBg: "#FFF4E0"
    readonly property color warningBorder: "#E8C684"

    readonly property color error: "#8A2A24"
    readonly property color errorBg: "#FBEAE8"
    readonly property color errorBorder: "#E6B6B1"
    readonly property color errorBar: "#C0392B"

    readonly property color neutralChipBg: "#EEF0F4"
    readonly property color neutralChipText: "#4B5563"

    readonly property color darkPanel: "#16212F"
    readonly property color darkCard: "#1E2B3B"
    readonly property color darkText: "#FFFFFF"
    readonly property color darkMuted: "#9BA7B8"
    readonly property color darkAccent: "#5B8FF9"

    // Typography (falls back cleanly to system sans / monospace)
    readonly property string fontSans: "Inter"
    readonly property string fontMono: "JetBrains Mono"

    // Radii
    readonly property int radiusControl: 8
    readonly property int radiusCard: 10
    readonly property int radiusChip: 999

    // Layout & accessibility
    readonly property int minWindowWidth: 1024
    readonly property int minWindowHeight: 720
    readonly property int maxContentWidth: 1100
    readonly property int headerHeight: 64
    readonly property int minHitTarget: 44
    readonly property int focusRingWidth: 2
}

