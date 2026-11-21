import QtQuick 2.15
import ".."

Rectangle {
    id: chip

    // tone: "success" | "error" | "warning" | "info" | "neutral"
    property string tone: "neutral"
    property string text: ""
    // indicator: "dot" | "check" | "none"
    property string indicator: "dot"

    implicitHeight: 26
    implicitWidth: contentRow.implicitWidth + 20
    radius: Theme.radiusChip

    color: {
        switch (tone) {
        case "success": return Theme.successBg;
        case "error":   return Theme.errorBg;
        case "warning": return Theme.warningBg;
        case "info":    return Theme.primaryTint;
        default:        return Theme.neutralChipBg;
        }
    }

    readonly property color fgColor: {
        switch (tone) {
        case "success": return Theme.success;
        case "error":   return Theme.error;
        case "warning": return Theme.warning;
        case "info":    return Theme.primaryText;
        default:        return Theme.neutralChipText;
        }
    }

    readonly property color dotColor: {
        switch (tone) {
        case "success": return Theme.successBar;
        case "error":   return Theme.errorBar;
        case "warning": return Theme.warningBar;
        case "info":    return Theme.primaryFill;
        default:        return Theme.neutralChipText;
        }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: chip.text

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            visible: chip.indicator === "dot"
            width: 7
            height: 7
            radius: 3.5
            color: chip.dotColor
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            visible: chip.indicator === "check"
            text: "\u2713"
            font.family: Theme.fontSans
            font.pixelSize: 12
            font.weight: Font.Bold
            color: chip.fgColor
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: chip.text
            font.family: Theme.fontSans
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: chip.fgColor
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
