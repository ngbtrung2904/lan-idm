pragma Singleton
import QtQuick 2.15

QtObject {
    // Mutable theme mode — initialized from config (initialDarkTheme) and toggled live via ThemeToggleButton
    property bool dark: false

    // Core surface & border tokens (guide-theme.md Section 2)
    readonly property color bg:             dark ? "#0F151D" : "#F3F4F6"
    readonly property color panel:          dark ? "#17202B" : "#FFFFFF"
    readonly property color rowHeaderBg:    dark ? "#1D2835" : "#F8F9FB"
    readonly property color border:         dark ? "#2B3949" : "#D9DDE3"
    readonly property color divider:        dark ? "#243040" : "#E6E9EE"
    readonly property color rowOfflineBg:   dark ? "#131B25" : "#FAFAFB"
    readonly property color inputBg:        dark ? "#0F151D" : "#FFFFFF"
    readonly property color inputBorder:    dark ? "#5B6C82" : "#B8C0CC"

    // Text tokens
    readonly property color text:           dark ? "#E6EBF2" : "#1B2430"
    readonly property color muted:          dark ? "#9AA7B8" : "#5B6675"

    // Brand / interactive primary tokens
    readonly property color primary:        dark ? "#2F6FED" : "#1F5FD6"
    readonly property color primaryHover:   dark ? "#3B7BF2" : "#1A51B8"
    readonly property color primaryPressed: dark ? "#255ED1" : "#16449C"
    readonly property color primaryText:    dark ? "#7FA8FF" : "#1F5FD6"
    readonly property color primaryFill:    dark ? "#4C8DFF" : "#1F5FD6"
    readonly property color textOnPrimary:  "#FFFFFF"
    readonly property color focusRing:      dark ? "#7FA8FF" : "#1F5FD6"
    readonly property color progressTrack:  dark ? "#2A3646" : "#E3E7ED"
    readonly property color primaryTint:    dark ? "#1A2B45" : "#EEF4FF"

    // Interactive surface states
    readonly property color hoverBg:        dark ? "#1D2835" : "#F8F9FB"
    readonly property color pressedBg:      dark ? "#243040" : "#E6E9EE"
    readonly property color disabledBg:     dark ? "#243040" : "#EEF0F3"
    readonly property color disabledText:   dark ? "#8795A8" : "#5B6675"
    readonly property color disabledBorder: dark ? "#2B3949" : "#D9DDE3"

    // Status tokens
    readonly property color success:        dark ? "#6FD39A" : "#17603A"
    readonly property color successBg:      dark ? "#12301F" : "#E7F4EC"
    readonly property color successBorder:  dark ? "#1F5A3A" : "#B9DEC8"
    readonly property color successBar:     dark ? "#3DB873" : "#1E7B4A"

    readonly property color warning:        dark ? "#F2C46B" : "#7A4A00"
    readonly property color warningBg:      dark ? "#33260C" : "#FFF4E0"
    readonly property color warningBorder:  dark ? "#6B4E16" : "#E8C98A"
    readonly property color warningBar:     dark ? "#F2C46B" : "#B45309"

    readonly property color error:          dark ? "#FF9C94" : "#8A2A24"
    readonly property color errorBg:        dark ? "#3A1816" : "#FBEAE8"
    readonly property color errorBorder:    dark ? "#7A2E29" : "#EBC3BF"
    readonly property color errorBar:       dark ? "#FF6B61" : "#B3261E"

    readonly property color neutralChipText: dark ? "#B7C2D1" : "#3F4A59"
    readonly property color neutralChipBg:   dark ? "#243040" : "#EEF0F3"

    // Login left branding panel tokens
    readonly property color darkPanel:      dark ? "#0B1118" : "#16212F"
    readonly property color darkCard:       dark ? "#151E2A" : "#1F2D3F"
    readonly property color darkText:       "#F2F5F9"
    readonly property color darkMuted:      dark ? "#9FB0C4" : "#A9B6C6"
    readonly property color darkMono:       "#D6E0EC"
    readonly property color darkAccent:     dark ? "#7FA8FF" : "#5B8FF9"

    // Typography
    readonly property string fontSans: "Inter"
    readonly property string fontText: "Inter"
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
