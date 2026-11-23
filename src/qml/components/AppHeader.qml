import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".."
import "FormatUtils.js" as Fmt

Rectangle {
    id: header

    property bool driveInteractive: true
    property bool showFreeSpace: true
    property bool showSignOut: true
    property string operatorId: ""
    property int driveState: 0
    property var currentDrive: ({})
    property var drivesModel: null
    property bool useLocalDisk: false
    property string localDiskPath: ""
    property real localDiskFreeBytes: 0

    signal driveSelected(int index)
    signal rescanDrivesClicked()
    signal signOutClicked()
    signal selectLocalDiskClicked()
    signal selectUsbModeClicked()
    signal changeLocalFolderClicked()

    height: Theme.headerHeight
    color: Theme.panel

    // Bottom 1px border
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.divider
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        spacing: 16

        // Left: Download icon + "Data Collector"
        Row {
            spacing: 10
            Layout.alignment: Qt.AlignVCenter

            Canvas {
                width: 20
                height: 20
                anchors.verticalCenter: parent.verticalCenter
                property color strokeColor: Theme.primaryFill
                onStrokeColorChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.clearRect(0, 0, width, height);
                    ctx.strokeStyle = strokeColor;
                    ctx.lineWidth = 2;
                    ctx.lineCap = "round";
                    ctx.lineJoin = "round";
                    ctx.beginPath();
                    ctx.moveTo(10, 3);
                    ctx.lineTo(10, 13);
                    ctx.moveTo(6, 9.5);
                    ctx.lineTo(10, 13.5);
                    ctx.lineTo(14, 9.5);
                    ctx.moveTo(4, 17);
                    ctx.lineTo(16, 17);
                    ctx.stroke();
                }
            }

            Text {
                text: qsTr("Data Collector")
                font.family: Theme.fontSans
                font.pixelSize: 16
                font.weight: Font.Bold
                color: Theme.text
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Item { Layout.fillWidth: true }

        // Right: USB pill chip
        Button {
            id: usbPill
            implicitHeight: Theme.minHitTarget
            implicitWidth: pillRow.implicitWidth + 32
            padding: 0
            enabled: header.driveInteractive
            focusPolicy: header.driveInteractive ? Qt.StrongFocus : Qt.NoFocus
            hoverEnabled: header.driveInteractive

            readonly property bool menuOpen: drivePopup.opened
            readonly property bool hasDrive: header.useLocalDisk || Boolean(header.driveState !== 0 && header.currentDrive && header.currentDrive.mountPoint)
            readonly property color pillBg: {
                if (menuOpen) return Theme.panel;
                if (!hasDrive) return Theme.warningBg;
                return Theme.successBg;
            }
            readonly property color pillBorder: {
                if (menuOpen) return Theme.focusRing;
                if (!hasDrive) return Theme.warningBorder;
                return Theme.successBorder;
            }
            readonly property color pillTextColor: {
                if (menuOpen) return Theme.text;
                if (!hasDrive) return Theme.warning;
                return Theme.success;
            }

            Accessible.role: header.driveInteractive ? Accessible.Button : Accessible.StaticText
            Accessible.name: header.useLocalDisk
                             ? (qsTr("Local disk: ") + header.localDiskPath)
                             : hasDrive
                               ? (qsTr("USB drive: ") + (header.currentDrive.label || "") + " " + header.currentDrive.mountPoint)
                               : qsTr("No USB drive detected")

            onClicked: {
                if (header.driveInteractive) {
                    if (drivePopup.opened) {
                        drivePopup.close();
                    } else {
                        drivePopup.open();
                    }
                }
            }

            background: Item {
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    radius: Theme.radiusChip
                    color: "transparent"
                    border.color: Theme.focusRing
                    border.width: Theme.focusRingWidth
                    visible: usbPill.activeFocus && !usbPill.menuOpen
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: 2
                    anchors.bottomMargin: 2
                    radius: Theme.radiusChip
                    color: usbPill.pillBg
                    border.color: usbPill.pillBorder
                    border.width: usbPill.menuOpen ? 2 : 1
                }
            }

            contentItem: Row {
                id: pillRow
                anchors.centerIn: parent
                spacing: 8

                // Minimal USB drive icon
                Canvas {
                    id: driveIcon
                    width: 14
                    height: 16
                    anchors.verticalCenter: parent.verticalCenter
                    property color strokeColor: usbPill.menuOpen ? Theme.primaryText : usbPill.pillTextColor
                    onStrokeColorChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.clearRect(0, 0, width, height);
                        ctx.strokeStyle = strokeColor;
                        ctx.lineWidth = 1.4;
                        ctx.beginPath();
                        ctx.roundedRect(3, 5, 8, 9, 1.5, 1.5);
                        ctx.rect(4.5, 1.5, 5, 3.5);
                        ctx.stroke();
                    }
                }

                Text {
                    text: {
                        if (header.useLocalDisk) {
                            return (usbPill.menuOpen && header.currentDrive && header.currentDrive.label)
                                   ? header.currentDrive.label
                                   : qsTr("Local disk");
                        }
                        if (!usbPill.hasDrive) return qsTr("No USB drive detected");
                        if (usbPill.menuOpen && header.currentDrive.label) return header.currentDrive.label;
                        return qsTr("USB ready");
                    }
                    font.family: Theme.fontSans
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: usbPill.pillTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    visible: usbPill.hasDrive
                    text: (header.currentDrive && header.currentDrive.mountPoint)
                          || (header.useLocalDisk ? header.localDiskPath : "")
                    font.family: Theme.fontMono
                    font.pixelSize: 12
                    color: usbPill.pillTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    visible: usbPill.hasDrive && header.showFreeSpace
                    text: Fmt.formatBytes(header.useLocalDisk ? header.localDiskFreeBytes : header.currentDrive.freeBytes) + qsTr(" free")
                    font.family: Theme.fontSans
                    font.pixelSize: 13
                    color: usbPill.pillTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    visible: header.driveInteractive
                    text: usbPill.menuOpen ? "\u25B4" : "\u25BE"
                    font.family: Theme.fontSans
                    font.pixelSize: 12
                    color: usbPill.pillTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Popup {
                id: drivePopup
                x: usbPill.width - width
                y: usbPill.height + 6
                width: 340
                padding: 0
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

                background: Rectangle {
                    color: Theme.panel
                    radius: Theme.radiusCard
                    border.color: Theme.border
                    border.width: 1
                }

                contentItem: ColumnLayout {
                    spacing: 0

                    Repeater {
                        id: driveRepeater
                        model: header.drivesModel

                        delegate: ItemDelegate {
                            id: driveRow
                            Layout.fillWidth: true
                            implicitHeight: 58
                            padding: 0
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus

                            Accessible.role: Accessible.MenuItem
                            Accessible.name: model.label + " " + model.mountPoint + " " + Fmt.formatBytes(model.freeBytes) + " free"

                            onClicked: {
                                header.driveSelected(index);
                                drivePopup.close();
                            }

                            background: Rectangle {
                                color: (!header.useLocalDisk && model.selected)
                                       ? Theme.primaryTint
                                       : driveRow.hovered
                                         ? Theme.hoverBg
                                         : "transparent"
                                radius: index === 0 ? Theme.radiusCard : 0
                                border.color: driveRow.activeFocus ? Theme.focusRing : "transparent"
                                border.width: driveRow.activeFocus ? Theme.focusRingWidth : 0
                            }

                            contentItem: RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: model.label
                                        font.family: Theme.fontSans
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: Theme.text
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: model.mountPoint
                                        font.family: Theme.fontMono
                                        font.pixelSize: 12
                                        color: Theme.muted
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }

                                Text {
                                    text: Fmt.formatBytes(model.freeBytes) + qsTr(" free")
                                    font.family: Theme.fontSans
                                    font.pixelSize: 13
                                    color: Theme.muted
                                }

                                Text {
                                    visible: !header.useLocalDisk && model.selected
                                    text: "\u2713"
                                    font.family: Theme.fontSans
                                    font.pixelSize: 15
                                    font.weight: Font.Bold
                                    color: Theme.primaryText
                                }
                            }
                        }
                    }

                    // Empty state when no drives (clickable to return to USB mode if currently on Local disk)
                    ItemDelegate {
                        id: noUsbRow
                        visible: !header.drivesModel || driveRepeater.count === 0
                        Layout.fillWidth: true
                        implicitHeight: header.useLocalDisk ? 56 : 52
                        padding: 0
                        enabled: header.useLocalDisk
                        hoverEnabled: header.useLocalDisk
                        focusPolicy: header.useLocalDisk ? Qt.StrongFocus : Qt.NoFocus

                        Accessible.role: header.useLocalDisk ? Accessible.MenuItem : Accessible.StaticText
                        Accessible.name: header.useLocalDisk
                                         ? qsTr("Switch to USB drive (no USB drives detected yet)")
                                         : qsTr("No USB drives detected")

                        onClicked: {
                            if (header.useLocalDisk) {
                                header.selectUsbModeClicked();
                                drivePopup.close();
                            }
                        }

                        background: Rectangle {
                            color: (header.useLocalDisk && noUsbRow.hovered) ? Theme.hoverBg : "transparent"
                            radius: Theme.radiusCard
                            border.color: noUsbRow.activeFocus ? Theme.focusRing : "transparent"
                            border.width: noUsbRow.activeFocus ? Theme.focusRingWidth : 0
                        }

                        contentItem: ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 2

                            Item { Layout.fillHeight: true }

                            Text {
                                visible: header.useLocalDisk
                                text: qsTr("Use USB drive")
                                font.family: Theme.fontSans
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: Theme.primaryText
                            }

                            Text {
                                text: header.useLocalDisk
                                      ? qsTr("No USB drives detected yet")
                                      : qsTr("No USB drives detected")
                                font.family: Theme.fontSans
                                font.pixelSize: header.useLocalDisk ? 12 : 13
                                color: Theme.muted
                                horizontalAlignment: header.useLocalDisk ? Text.AlignLeft : Text.AlignHCenter
                                Layout.fillWidth: true
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.divider
                    }

                    // Local disk destination row
                    ItemDelegate {
                        id: localDiskRow
                        Layout.fillWidth: true
                        implicitHeight: 58
                        padding: 0
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus

                        Accessible.role: Accessible.MenuItem
                        Accessible.name: qsTr("Local disk") + " " + header.localDiskPath + " "
                                         + Fmt.formatBytes(header.localDiskFreeBytes) + " " + qsTr("free")

                        onClicked: {
                            header.selectLocalDiskClicked();
                            drivePopup.close();
                        }

                        background: Rectangle {
                            color: header.useLocalDisk
                                   ? Theme.primaryTint
                                   : localDiskRow.hovered
                                     ? Theme.hoverBg
                                     : "transparent"
                            border.color: localDiskRow.activeFocus ? Theme.focusRing : "transparent"
                            border.width: localDiskRow.activeFocus ? Theme.focusRingWidth : 0
                        }

                        contentItem: RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: qsTr("Local disk")
                                    font.family: Theme.fontSans
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: header.localDiskPath
                                    font.family: Theme.fontMono
                                    font.pixelSize: 12
                                    color: Theme.muted
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }

                            Text {
                                text: Fmt.formatBytes(header.localDiskFreeBytes) + qsTr(" free")
                                font.family: Theme.fontSans
                                font.pixelSize: 13
                                color: Theme.muted
                            }

                            Text {
                                visible: header.useLocalDisk
                                text: "\u2713"
                                font.family: Theme.fontSans
                                font.pixelSize: 15
                                font.weight: Font.Bold
                                color: Theme.primaryText
                            }
                        }
                    }

                    // Change local folder row
                    ItemDelegate {
                        id: changeLocalFolderItem
                        Layout.fillWidth: true
                        implicitHeight: Theme.minHitTarget
                        padding: 0
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus

                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Change local folder...")

                        onClicked: {
                            drivePopup.close();
                            header.changeLocalFolderClicked();
                        }

                        background: Rectangle {
                            color: changeLocalFolderItem.hovered ? Theme.hoverBg : "transparent"
                            border.color: changeLocalFolderItem.activeFocus ? Theme.focusRing : "transparent"
                            border.width: changeLocalFolderItem.activeFocus ? Theme.focusRingWidth : 0
                        }

                        contentItem: Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 16
                            spacing: 8

                            Text {
                                text: qsTr("Change local folder...")
                                font.family: Theme.fontSans
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: Theme.primaryText
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.divider
                    }

                    ItemDelegate {
                        id: rescanItem
                        Layout.fillWidth: true
                        implicitHeight: Theme.minHitTarget
                        padding: 0
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus

                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Rescan drives")

                        onClicked: {
                            header.rescanDrivesClicked();
                            drivePopup.close();
                        }

                        background: Rectangle {
                            color: rescanItem.hovered ? Theme.hoverBg : "transparent"
                            radius: Theme.radiusCard
                        }

                        contentItem: Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 16
                            spacing: 8

                            Text {
                                text: "\u21BB"
                                font.family: Theme.fontSans
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                color: Theme.primaryText
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: qsTr("Rescan drives")
                                font.family: Theme.fontSans
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: Theme.primaryText
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }
        }

        // Operator ID
        Text {
            text: header.operatorId
            font.family: Theme.fontSans
            font.pixelSize: 14
            color: Theme.muted
            Layout.alignment: Qt.AlignVCenter
        }

        // Sign out button
        Button {
            id: signOutBtn
            visible: header.showSignOut
            implicitHeight: Theme.minHitTarget
            implicitWidth: signOutLabel.implicitWidth + 16
            padding: 0
            focusPolicy: Qt.StrongFocus
            hoverEnabled: true

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Sign out")

            onClicked: header.signOutClicked()

            background: Rectangle {
                radius: Theme.radiusControl
                color: signOutBtn.hovered ? Theme.primaryTint : "transparent"
                border.color: signOutBtn.activeFocus ? Theme.focusRing : "transparent"
                border.width: signOutBtn.activeFocus ? Theme.focusRingWidth : 0
            }

            contentItem: Text {
                id: signOutLabel
                text: qsTr("Sign out")
                font.family: Theme.fontSans
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.primaryText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        // Light / Dark theme toggle
        ThemeToggleButton {
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
