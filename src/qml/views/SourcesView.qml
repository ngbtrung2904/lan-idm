import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".."
import "../components"
import "../components/FormatUtils.js" as Fmt

ColumnLayout {
    id: sourcesView

    spacing: 0

    property string startErrorMessage: ""

    Connections {
        target: sourcesController
        function onStartError(message) {
            sourcesView.startErrorMessage = message;
        }
        function onSelectionChanged() {
            sourcesView.startErrorMessage = "";
        }
        function onDrivesChanged() {
            sourcesView.startErrorMessage = "";
        }
    }

    function pcChipTone(pcState) {
        switch (pcState) {
        case 2: return "success"; // Online
        case 3: return "error";   // Offline
        case 1: return "info";    // Checking
        case 4: return "error";   // AuthFailed
        case 5: return "warning"; // HostKeyMismatch
        default: return "neutral";
        }
    }

    // Top bar
    AppHeader {
        Layout.fillWidth: true
        driveInteractive: true
        showFreeSpace: true
        showSignOut: true
        operatorId: authController.operatorId
        driveState: sourcesController.driveState
        currentDrive: sourcesController.currentDrive
        drivesModel: sourcesController.drives
        onDriveSelected: function(idx) { sourcesController.selectDrive(idx); }
        onRescanDrivesClicked: sourcesController.rescanDrives()
        onSignOutClicked: authController.lock()
    }

    // Scrollable body
    Flickable {
        id: flick
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentWidth: width
        contentHeight: mainColumn.implicitHeight + 56
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        ColumnLayout {
            id: mainColumn
            width: Math.min(Theme.maxContentWidth, flick.width - 56)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 28
            spacing: 18

            // Page heading
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: qsTr("Select sources")
                    font.family: Theme.fontSans
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    color: Theme.text
                }

                Text {
                    text: qsTr("Choose a time range and the PCs to copy from. Sizes are estimates for the selected range.")
                    font.family: Theme.fontSans
                    font.pixelSize: 14
                    color: Theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // Conditional alert banner: No USB
            AlertBanner {
                visible: sourcesController.driveState === 0
                Layout.fillWidth: true
                tone: "warning"
                title: qsTr("No USB drive detected")
                body: qsTr("Plug in a USB drive and it will be picked up automatically. You can still choose a time range and PCs while you wait.")
            }

            // Conditional alert banner: Not enough space
            AlertBanner {
                visible: sourcesController.driveState !== 0 && !sourcesController.fits && sourcesController.selectedCount > 0
                Layout.fillWidth: true
                tone: "error"
                title: qsTr("Not enough space on %1").arg(sourcesController.currentDrive.label || qsTr("USB drive"))
                body: qsTr("The selection needs about %1 but the drive has %2 free. Only %3 %4 fit. Deselect PCs or choose a larger drive.")
                      .arg(Fmt.formatBytes(sourcesController.selectedBytes))
                      .arg(Fmt.formatBytes(sourcesController.freeBytes))
                      .arg(sourcesController.fittingPcCount)
                      .arg(sourcesController.fittingPcCount === 1 ? qsTr("PC") : qsTr("PCs"))
            }

            // Conditional start error banner
            AlertBanner {
                visible: sourcesView.startErrorMessage !== ""
                Layout.fillWidth: true
                tone: "error"
                title: qsTr("Cannot start copy")
                body: sourcesView.startErrorMessage
            }

            // Time range card
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: timeCardCol.implicitHeight + 36
                radius: Theme.radiusCard
                color: Theme.panel
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    id: timeCardCol
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        DateTimeField {
                            label: qsTr("Start")
                            value: sourcesController.windowStart
                            invalid: !sourcesController.windowValid
                            Layout.preferredWidth: 215
                            onValueEdited: function(dt) { sourcesController.windowStart = dt; }
                        }

                        DateTimeField {
                            label: qsTr("End")
                            value: sourcesController.windowEnd
                            invalid: !sourcesController.windowValid
                            Layout.preferredWidth: 215
                            onValueEdited: function(dt) { sourcesController.windowEnd = dt; }
                        }

                        Row {
                            spacing: 8
                            Layout.alignment: Qt.AlignBottom

                            OutlineButton {
                                text: qsTr("Last 24 h")
                                variant: sourcesController.activePreset === "24h" ? "active" : "default"
                                accessibleLabel: qsTr("Last 24 hours preset")
                                onClicked: sourcesController.applyPreset("24h")
                            }

                            OutlineButton {
                                text: qsTr("Last 7 days")
                                variant: sourcesController.activePreset === "7d" ? "active" : "default"
                                accessibleLabel: qsTr("Last 7 days preset")
                                onClicked: sourcesController.applyPreset("7d")
                            }

                            OutlineButton {
                                text: qsTr("Today")
                                variant: sourcesController.activePreset === "today" ? "active" : "default"
                                accessibleLabel: qsTr("Today preset")
                                onClicked: sourcesController.applyPreset("today")
                            }
                        }

                        Item { Layout.fillWidth: true }

                        OutlineButton {
                            text: sourcesController.busy ? qsTr("Scanning...") : qsTr("Rescan PCs")
                            variant: "primary"
                            iconGlyph: "\u21BB"
                            busy: sourcesController.busy
                            enabled: !sourcesController.busy
                            Layout.alignment: Qt.AlignBottom
                            accessibleLabel: qsTr("Rescan PCs")
                            onClicked: sourcesController.rescanPcs()
                        }
                    }

                    Text {
                        visible: !sourcesController.windowValid
                        text: qsTr("Start time must be earlier than end time.")
                        font.family: Theme.fontSans
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: Theme.error
                        Accessible.role: Accessible.AlertMessage
                        Accessible.name: text
                    }

                    // Divider above optional media toggles
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 2
                        height: 1
                        color: Theme.divider
                    }

                    // Optional media toggles row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Text {
                            text: qsTr("Optional media")
                            font.family: Theme.fontSans
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            color: Theme.muted
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // Include pictures checkbox (44px min height)
                        AbstractButton {
                            id: includePicturesBox
                            implicitWidth: picturesCheckRow.implicitWidth + 8
                            implicitHeight: Theme.minHitTarget
                            checkable: true
                            checked: sourcesController.includePictures
                            focusPolicy: Qt.StrongFocus
                            hoverEnabled: true

                            Accessible.role: Accessible.CheckBox
                            Accessible.name: qsTr("Include pictures")
                            Accessible.checked: checked

                            onClicked: {
                                var next = !sourcesController.includePictures;
                                sourcesController.includePictures = next;
                                checked = Qt.binding(function() { return sourcesController.includePictures; });
                            }

                            contentItem: Item {
                                Row {
                                    id: picturesCheckRow
                                    anchors.left: parent.left
                                    anchors.leftMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 8

                                    Item {
                                        width: 22
                                        height: 22
                                        anchors.verticalCenter: parent.verticalCenter

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 22
                                            height: 22
                                            radius: 6
                                            color: "transparent"
                                            border.color: Theme.focusRing
                                            border.width: Theme.focusRingWidth
                                            visible: includePicturesBox.activeFocus
                                        }

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 18
                                            height: 18
                                            radius: 4
                                            color: includePicturesBox.checked ? Theme.primaryFill : Theme.inputBg
                                            border.color: includePicturesBox.checked ? Theme.primaryFill : Theme.inputBorder
                                            border.width: 1.5

                                            Text {
                                                anchors.centerIn: parent
                                                visible: includePicturesBox.checked
                                                text: "\u2713"
                                                font.family: Theme.fontSans
                                                font.pixelSize: 12
                                                font.weight: Font.Bold
                                                color: Theme.textOnPrimary
                                            }
                                        }
                                    }

                                    Text {
                                        text: qsTr("Include pictures")
                                        font.family: Theme.fontSans
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        color: Theme.text
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Text {
                                        text: "~/Pictures/lightshots"
                                        font.family: Theme.fontMono
                                        font.pixelSize: 12
                                        color: Theme.muted
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                        }

                        // Include screen recordings checkbox (44px min height)
                        AbstractButton {
                            id: includeRecordingsBox
                            implicitWidth: recordingsCheckRow.implicitWidth + 8
                            implicitHeight: Theme.minHitTarget
                            checkable: true
                            checked: sourcesController.includeRecordings
                            focusPolicy: Qt.StrongFocus
                            hoverEnabled: true

                            Accessible.role: Accessible.CheckBox
                            Accessible.name: qsTr("Include screen recordings")
                            Accessible.checked: checked

                            onClicked: {
                                var next = !sourcesController.includeRecordings;
                                sourcesController.includeRecordings = next;
                                checked = Qt.binding(function() { return sourcesController.includeRecordings; });
                            }

                            contentItem: Item {
                                Row {
                                    id: recordingsCheckRow
                                    anchors.left: parent.left
                                    anchors.leftMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 8

                                    Item {
                                        width: 22
                                        height: 22
                                        anchors.verticalCenter: parent.verticalCenter

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 22
                                            height: 22
                                            radius: 6
                                            color: "transparent"
                                            border.color: Theme.focusRing
                                            border.width: Theme.focusRingWidth
                                            visible: includeRecordingsBox.activeFocus
                                        }

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 18
                                            height: 18
                                            radius: 4
                                            color: includeRecordingsBox.checked ? Theme.primaryFill : Theme.inputBg
                                            border.color: includeRecordingsBox.checked ? Theme.primaryFill : Theme.inputBorder
                                            border.width: 1.5

                                            Text {
                                                anchors.centerIn: parent
                                                visible: includeRecordingsBox.checked
                                                text: "\u2713"
                                                font.family: Theme.fontSans
                                                font.pixelSize: 12
                                                font.weight: Font.Bold
                                                color: Theme.textOnPrimary
                                            }
                                        }
                                    }

                                    Text {
                                        text: qsTr("Include screen recordings")
                                        font.family: Theme.fontSans
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        color: Theme.text
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Text {
                                        text: "~/recordings/"
                                        font.family: Theme.fontMono
                                        font.pixelSize: 12
                                        color: Theme.muted
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: qsTr("Applies to supported PCs only")
                            font.family: Theme.fontSans
                            font.pixelSize: 12
                            color: Theme.muted
                            elide: Text.ElideRight
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                }
            }

            // PCs Table Card
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: tableCol.implicitHeight
                radius: Theme.radiusCard
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                clip: true

                ColumnLayout {
                    id: tableCol
                    width: parent.width
                    spacing: 0

                    // Table header row
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 44
                        color: Theme.rowHeaderBg

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 20
                            spacing: 12

                            // Select-all checkbox (44x44 hit target)
                            AbstractButton {
                                id: selectAllBox
                                implicitWidth: Theme.minHitTarget
                                implicitHeight: Theme.minHitTarget
                                checkable: true
                                checked: sourcesController.allOnlineSelected && sourcesController.onlinePcCount > 0
                                enabled: sourcesController.onlinePcCount > 0
                                focusPolicy: Qt.StrongFocus
                                hoverEnabled: true

                                Accessible.role: Accessible.CheckBox
                                Accessible.name: qsTr("Select all online PCs")
                                Accessible.checked: checked

                                onClicked: sourcesController.selectAll(!sourcesController.allOnlineSelected)

                                contentItem: Item {
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 22
                                        height: 22
                                        radius: 6
                                        color: "transparent"
                                        border.color: Theme.focusRing
                                        border.width: Theme.focusRingWidth
                                        visible: selectAllBox.activeFocus
                                    }

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18
                                        radius: 4
                                        color: selectAllBox.checked ? Theme.primaryFill : Theme.inputBg
                                        border.color: selectAllBox.checked ? Theme.primaryFill : Theme.inputBorder
                                        border.width: 1.5

                                        Text {
                                            anchors.centerIn: parent
                                            visible: selectAllBox.checked
                                            text: "\u2713"
                                            font.family: Theme.fontSans
                                            font.pixelSize: 12
                                            font.weight: Font.Bold
                                            color: Theme.textOnPrimary
                                        }
                                    }
                                }
                            }

                            Text {
                                text: qsTr("PC")
                                Layout.fillWidth: true
                                Layout.minimumWidth: 140
                                Layout.preferredWidth: 180
                                font.family: Theme.fontSans
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                                color: Theme.muted
                            }

                            Text {
                                text: qsTr("STATUS")
                                Layout.minimumWidth: 135
                                Layout.preferredWidth: 135
                                font.family: Theme.fontSans
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                                color: Theme.muted
                            }

                            Text {
                                text: qsTr("LOGS")
                                Layout.minimumWidth: 115
                                Layout.preferredWidth: 115
                                font.family: Theme.fontSans
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                                color: Theme.muted
                            }

                            Text {
                                text: qsTr("PICTURES")
                                Layout.minimumWidth: 125
                                Layout.preferredWidth: 125
                                font.family: Theme.fontSans
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                                color: Theme.muted
                            }

                            Text {
                                text: qsTr("RECORDINGS")
                                Layout.minimumWidth: 125
                                Layout.preferredWidth: 125
                                font.family: Theme.fontSans
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                                color: Theme.muted
                            }

                            Text {
                                text: qsTr("TOTAL")
                                Layout.minimumWidth: 95
                                Layout.preferredWidth: 95
                                font.family: Theme.fontSans
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                                color: Theme.muted
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.divider
                        }
                    }

                    // PC rows
                    Repeater {
                        model: sourcesController.pcModel

                        delegate: Rectangle {
                            id: pcRow
                            Layout.fillWidth: true
                            implicitHeight: 66
                            color: model.selectable ? Theme.panel : Theme.rowOfflineBg

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 20
                                spacing: 12

                                // Row checkbox (44x44 hit target)
                                AbstractButton {
                                    id: rowCheck
                                    implicitWidth: Theme.minHitTarget
                                    implicitHeight: Theme.minHitTarget
                                    checkable: true
                                    checked: model.selected
                                    enabled: model.selectable
                                    focusPolicy: model.selectable ? Qt.StrongFocus : Qt.NoFocus
                                    hoverEnabled: model.selectable

                                    Accessible.role: Accessible.CheckBox
                                    Accessible.name: qsTr("Select %1 (%2)").arg(model.id).arg(model.stateText)
                                    Accessible.checked: model.selected

                                    onClicked: sourcesController.setPcSelected(index, !model.selected)

                                    contentItem: Item {
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 22
                                            height: 22
                                            radius: 6
                                            color: "transparent"
                                            border.color: Theme.focusRing
                                            border.width: Theme.focusRingWidth
                                            visible: rowCheck.activeFocus
                                        }

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 18
                                            height: 18
                                            radius: 4
                                            color: !model.selectable
                                                   ? Theme.disabledBg
                                                   : model.selected
                                                     ? Theme.primaryFill
                                                     : Theme.inputBg
                                            border.color: !model.selectable
                                                          ? Theme.disabledBorder
                                                          : model.selected
                                                            ? Theme.primaryFill
                                                            : Theme.inputBorder
                                            border.width: 1.5

                                            Text {
                                                anchors.centerIn: parent
                                                visible: model.selected && model.selectable
                                                text: "\u2713"
                                                font.family: Theme.fontSans
                                                font.pixelSize: 12
                                                font.weight: Font.Bold
                                                color: Theme.textOnPrimary
                                            }
                                        }
                                    }
                                }

                                // PC id + host
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 140
                                    Layout.preferredWidth: 180
                                    spacing: 2

                                    Text {
                                        text: model.id
                                        font.family: Theme.fontSans
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: model.selectable ? Theme.text : Theme.muted
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: model.host
                                        font.family: Theme.fontMono
                                        font.pixelSize: 12
                                        color: Theme.muted
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }

                                // Status chip
                                Item {
                                    Layout.minimumWidth: 135
                                    Layout.preferredWidth: 135
                                    implicitHeight: statusChip.implicitHeight

                                    StatusChip {
                                        id: statusChip
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        tone: sourcesView.pcChipTone(model.state)
                                        text: model.stateText
                                        indicator: "dot"
                                    }
                                }

                                // Online data columns (LOGS, PICTURES, RECORDINGS, TOTAL)
                                ColumnLayout {
                                    visible: model.state === 2
                                    Layout.minimumWidth: 115
                                    Layout.preferredWidth: 115
                                    spacing: 2

                                    Text {
                                        text: Fmt.formatBytes(model.logsBytes)
                                        font.family: Theme.fontSans
                                        font.pixelSize: 14
                                        color: Theme.text
                                    }

                                    Text {
                                        text: Fmt.formatFileCount(model.logsCount)
                                        font.family: Theme.fontSans
                                        font.pixelSize: 12
                                        color: Theme.muted
                                    }
                                }

                                ColumnLayout {
                                    visible: model.state === 2
                                    Layout.minimumWidth: 125
                                    Layout.preferredWidth: 125
                                    spacing: 2

                                    Text {
                                        visible: !model.supportsPictures || !sourcesController.includePictures
                                        text: !model.supportsPictures ? qsTr("N/A") : qsTr("Off")
                                        font.family: Theme.fontSans
                                        font.pixelSize: 13
                                        color: Theme.muted
                                    }

                                    Text {
                                        visible: model.supportsPictures && sourcesController.includePictures
                                        text: Fmt.formatBytes(model.shotsBytes)
                                        font.family: Theme.fontSans
                                        font.pixelSize: 14
                                        color: Theme.text
                                    }

                                    Text {
                                        visible: model.supportsPictures && sourcesController.includePictures
                                        text: Fmt.formatFileCount(model.shotsCount)
                                        font.family: Theme.fontSans
                                        font.pixelSize: 12
                                        color: Theme.muted
                                    }
                                }

                                ColumnLayout {
                                    visible: model.state === 2
                                    Layout.minimumWidth: 125
                                    Layout.preferredWidth: 125
                                    spacing: 2

                                    Text {
                                        visible: !model.supportsRecordings || !sourcesController.includeRecordings
                                        text: !model.supportsRecordings ? qsTr("N/A") : qsTr("Off")
                                        font.family: Theme.fontSans
                                        font.pixelSize: 13
                                        color: Theme.muted
                                    }

                                    Text {
                                        visible: model.supportsRecordings && sourcesController.includeRecordings
                                        text: Fmt.formatBytes(model.recordingsBytes)
                                        font.family: Theme.fontSans
                                        font.pixelSize: 14
                                        color: Theme.text
                                    }

                                    Text {
                                        visible: model.supportsRecordings && sourcesController.includeRecordings
                                        text: Fmt.formatFileCount(model.recordingsCount)
                                        font.family: Theme.fontSans
                                        font.pixelSize: 12
                                        color: Theme.muted
                                    }
                                }

                                Text {
                                    visible: model.state === 2
                                    Layout.minimumWidth: 95
                                    Layout.preferredWidth: 95
                                    text: Fmt.formatBytes(model.totalBytes)
                                    font.family: Theme.fontSans
                                    font.pixelSize: 14
                                    font.weight: Font.Bold
                                    color: Theme.text
                                }

                                // Non-online spanning status message
                                Text {
                                    visible: model.state !== 2
                                    Layout.minimumWidth: 115 + 125 + 125 + 95 + 36
                                    Layout.preferredWidth: 115 + 125 + 125 + 95 + 36
                                    text: model.statusMessage !== "" ? model.statusMessage : model.stateText
                                    font.family: Theme.fontSans
                                    font.pixelSize: 13
                                    color: model.state === 5 ? Theme.warning : Theme.muted
                                    elide: Text.ElideRight
                                }
                            }

                            // Bottom row divider
                            Rectangle {
                                visible: index < sourcesController.totalPcCount - 1
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: Theme.divider
                            }
                        }
                    }
                }
            }

            // Footer summary card
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: footerRow.implicitHeight + 36
                radius: Theme.radiusCard
                color: Theme.panel
                border.color: Theme.border
                border.width: 1

                RowLayout {
                    id: footerRow
                    anchors.fill: parent
                    anchors.margins: 18
                    anchors.leftMargin: 22
                    anchors.rightMargin: 22
                    spacing: 28

                    // Left: selection count + destination
                    ColumnLayout {
                        Layout.preferredWidth: 230
                        spacing: 3

                        Text {
                            text: qsTr("%1 of %2 PCs selected")
                                  .arg(sourcesController.selectedCount)
                                  .arg(sourcesController.totalPcCount)
                            font.family: Theme.fontSans
                            font.pixelSize: 15
                            font.weight: Font.Bold
                            color: Theme.text
                        }

                        Text {
                            text: sourcesController.driveState === 0
                                  ? qsTr("Destination: not set")
                                  : qsTr("Destination %1").arg(sourcesController.destinationPreview)
                            font.family: Theme.fontSans
                            font.pixelSize: 12
                            color: Theme.muted
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }

                    // Center: size estimate + capacity bar
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        readonly property bool overSpace: sourcesController.driveState !== 0
                                                          && !sourcesController.fits
                                                          && sourcesController.selectedCount > 0

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: parent.parent.overSpace
                                      ? qsTr("Needs about %1").arg(Fmt.formatBytes(sourcesController.selectedBytes))
                                      : qsTr("About %1").arg(Fmt.formatBytes(sourcesController.selectedBytes))
                                font.family: Theme.fontSans
                                font.pixelSize: 13
                                font.weight: Font.Bold
                                color: parent.parent.overSpace ? Theme.error : Theme.text
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: sourcesController.driveState === 0
                                      ? qsTr("No drive to compare against")
                                      : parent.parent.overSpace
                                        ? qsTr("%1 free").arg(Fmt.formatBytes(sourcesController.freeBytes))
                                        : qsTr("of %1 free").arg(Fmt.formatBytes(sourcesController.freeBytes))
                                font.family: Theme.fontSans
                                font.pixelSize: 13
                                font.weight: parent.parent.overSpace ? Font.Bold : Font.Normal
                                color: parent.parent.overSpace ? Theme.error : Theme.muted
                            }
                        }

                        Rectangle {
                            id: barTrack
                            Layout.fillWidth: true
                            height: 8
                            radius: 4
                            color: Theme.progressTrack
                            clip: true

                            Rectangle {
                                height: parent.height
                                radius: 4
                                width: {
                                    if (sourcesController.driveState === 0 || sourcesController.selectedCount === 0) {
                                        return 0;
                                    }
                                    if (!sourcesController.fits) {
                                        return parent.width;
                                    }
                                    var free = Math.max(1, sourcesController.freeBytes);
                                    return Math.min(parent.width, parent.width * (sourcesController.selectedBytes / free));
                                }
                                color: (!sourcesController.fits && sourcesController.selectedCount > 0)
                                       ? Theme.errorBar
                                       : Theme.primaryFill

                                Behavior on width {
                                    NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                                }
                            }
                        }
                    }

                    // Right: Start copy CTA
                    PrimaryButton {
                        text: qsTr("Start copy")
                        implicitWidth: 138
                        enabled: sourcesController.canStart
                        accessibleLabel: qsTr("Start copy")
                        onClicked: sourcesController.startCopy()
                    }
                }
            }
        }
    }
}
