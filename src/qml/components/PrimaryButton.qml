import QtQuick 2.15
import QtQuick.Controls 2.15
import ".."

Button {
    id: control

    property bool busy: false
    property string accessibleLabel: text

    implicitHeight: Theme.minHitTarget
    implicitWidth: Math.max(120, contentItem.implicitWidth + 36)
    padding: 0
    leftPadding: 18
    rightPadding: 18
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true

    Accessible.role: Accessible.Button
    Accessible.name: accessibleLabel

    background: Item {
        // Outer 2px focus ring
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: Theme.radiusControl + 2
            color: "transparent"
            border.color: Theme.focusRing
            border.width: Theme.focusRingWidth
            visible: control.activeFocus
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: !control.enabled
                   ? Theme.disabledBg
                   : control.down
                     ? Theme.primaryPressed
                     : control.hovered
                       ? Theme.primaryHover
                       : Theme.primary
            border.color: !control.enabled ? Theme.disabledBorder : "transparent"
            border.width: !control.enabled ? 1 : 0
        }
    }

    contentItem: Row {
        spacing: 8
        anchors.centerIn: parent

        BusyIndicator {
            width: 18
            height: 18
            running: control.busy
            visible: control.busy
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: control.text
            font.family: Theme.fontSans
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: control.enabled ? Theme.textOnPrimary : Theme.disabledText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
