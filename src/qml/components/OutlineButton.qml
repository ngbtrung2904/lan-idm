import QtQuick 2.15
import QtQuick.Controls 2.15
import ".."

Button {
    id: control

    // Variants: "default" | "primary" | "danger" | "active"
    property string variant: "default"
    property string iconGlyph: ""
    property bool busy: false
    property string accessibleLabel: text

    implicitHeight: Theme.minHitTarget
    implicitWidth: Math.max(84, contentRow.implicitWidth + 32)
    padding: 0
    leftPadding: 14
    rightPadding: 14
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true

    Accessible.role: Accessible.Button
    Accessible.name: accessibleLabel

    readonly property color resolvedTextColor: {
        if (!control.enabled) return Theme.disabledText;
        if (variant === "primary" || variant === "active") return Theme.primary;
        if (variant === "danger") return Theme.error;
        return Theme.text;
    }

    readonly property color resolvedBorderColor: {
        if (!control.enabled) return Theme.border;
        if (variant === "primary" || variant === "active") return Theme.primary;
        return Theme.border;
    }

    readonly property color resolvedBgColor: {
        if (!control.enabled) return Theme.disabledBg;
        if (variant === "active") return Theme.primaryTint;
        if (control.down) return "#E5E7EB";
        if (control.hovered) return variant === "primary" ? Theme.primaryTint : "#F9FAFB";
        return Theme.panel;
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: Theme.radiusControl + 2
            color: "transparent"
            border.color: Theme.primary
            border.width: Theme.focusRingWidth
            visible: control.activeFocus
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: control.resolvedBgColor
            border.color: control.resolvedBorderColor
            border.width: 1
        }
    }

    contentItem: Row {
        id: contentRow
        spacing: 6
        anchors.centerIn: parent

        BusyIndicator {
            width: 16
            height: 16
            running: control.busy
            visible: control.busy
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            visible: !control.busy && control.iconGlyph !== ""
            text: control.iconGlyph
            font.family: Theme.fontSans
            font.pixelSize: 15
            font.weight: Font.Medium
            color: control.resolvedTextColor
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: control.text
            font.family: Theme.fontSans
            font.pixelSize: 14
            font.weight: (control.variant === "primary" || control.variant === "danger" || control.variant === "active")
                         ? Font.DemiBold : Font.Medium
            color: control.resolvedTextColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
