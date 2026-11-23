import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".."
import "../components"
import "../components/FormatUtils.js" as Fmt

ColumnLayout {
    id: progressView

    spacing: 0

    property bool showWarningDetails: false

    // Phase constants matching TransferController::Phase
    readonly property bool isRunning: transferController.phase === 1
    readonly property bool isFinished: transferController.phase === 2
    readonly property bool isFinishedWithWarnings: transferController.phase === 3
    readonly property bool isUsbRemoved: transferController.phase === 4
    readonly property bool isCancelling: transferController.phase === 5
    readonly property bool isCancelled: transferController.phase === 6

    function isActiveStage(stage) {
        return stage === 1 || stage === 2 || stage === 3 || stage === 9; // CopyingLogs, CopyingScreenshots, Verifying, CopyingRecordings
    }

    function jobChipTone(stage) {
        switch (stage) {
        case 4: return "success"; // Done
        case 5: return "warning"; // DoneWithWarnings
        case 6: return "error";   // Failed
        case 7: return "neutral"; // Cancelled
        case 8: return "neutral"; // Skipped
        default: return "neutral"; // Queued
        }
    }

    function jobChipIndicator(stage) {
        if (stage === 4) return "check"; // Done
        if (stage === 5 || stage === 6) return "dot";
        return "none"; // Queued, Skipped, Cancelled
    }

    function jobBarColor(stage) {
        if (stage === 4) return Theme.successBar;
        if (stage === 5) return Theme.warningBar;
        if (stage === 6 || stage === 7) return Theme.errorBar;
        return Theme.primaryFill;
    }

    function overallBarColor() {
        if (isFinished) return Theme.successBar;
        if (isFinishedWithWarnings) return Theme.warningBar;
        if (isCancelled || isUsbRemoved) return Theme.errorBar;
        return Theme.primaryFill;
    }

    function headingTitle() {
        if (isFinished) return qsTr("Copy finished");
        if (isFinishedWithWarnings) return qsTr("Copy finished with warnings");
        if (isUsbRemoved) return qsTr("Copy paused — USB removed");
        if (isCancelling) return qsTr("Cancelling copy...");
        if (isCancelled) return qsTr("Copy cancelled");
        return transferController.isLocalDestination ? qsTr("Copying to local disk") : qsTr("Copying to USB");
    }

    function headingSubtitle() {
        var prefix = transferController.windowSummary + " \u00B7 "
                     + transferController.selectedPcCount
                     + (transferController.selectedPcCount === 1 ? qsTr(" PC") : qsTr(" PCs"));
        if (isFinished || isFinishedWithWarnings) {
            return prefix + " \u00B7 " + qsTr("Saved to %1").arg(transferController.sessionPath);
        }
        if (isCancelled) {
            return prefix + " \u00B7 " + qsTr("Copy was cancelled by the operator.");
        }
        if (isUsbRemoved) {
            return prefix + " \u00B7 " + qsTr("Reconnect the USB drive to continue.");
        }
        return prefix + " \u00B7 " + (transferController.isLocalDestination
                                      ? qsTr("Saving to local disk.")
                                      : qsTr("Keep the USB drive plugged in until all copies finish."));
    }

    // Header (non-interactive USB pill, no Sign out while copying)
    AppHeader {
        Layout.fillWidth: true
        driveInteractive: false
        showFreeSpace: false
        showSignOut: false
        operatorId: authController.operatorId
        driveState: isUsbRemoved ? 0 : sourcesController.driveState
        currentDrive: sourcesController.currentDrive
        drivesModel: sourcesController.drives
        useLocalDisk: transferController.isLocalDestination
        localDiskPath: sourcesController.localDiskPath
    }

    // Scrollable main area
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

            // Blocking USB removed alert
            AlertBanner {
                visible: progressView.isUsbRemoved
                Layout.fillWidth: true
                tone: "error"
                title: qsTr("USB drive removed")
                body: qsTr("The USB drive was disconnected during the copy. Reconnect the drive and click Retry, or cancel the copy.")

                PrimaryButton {
                    text: qsTr("Retry")
                    implicitWidth: 96
                    accessibleLabel: qsTr("Retry copy after reconnecting USB")
                    onClicked: transferController.retry()
                }

                OutlineButton {
                    text: qsTr("Cancel copy")
                    variant: "danger"
                    accessibleLabel: qsTr("Cancel copy")
                    onClicked: cancelDialog.open()
                }
            }

            // Title & subtitle
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: progressView.headingTitle()
                    font.family: Theme.fontSans
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    color: Theme.text
                }

                Text {
                    text: progressView.headingSubtitle()
                    font.family: Theme.fontSans
                    font.pixelSize: 14
                    color: Theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // Overall progress card
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: overallCol.implicitHeight + 44
                radius: Theme.radiusCard
                color: Theme.panel
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    id: overallCol
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        RowLayout {
                            spacing: 10
                            Layout.alignment: Qt.AlignBottom

                            Text {
                                text: transferController.overallPercent + "%"
                                font.family: Theme.fontSans
                                font.pixelSize: 36
                                font.weight: Font.Bold
                                color: Theme.text
                                Layout.alignment: Qt.AlignBaseline
                            }

                            Text {
                                text: qsTr("overall")
                                font.family: Theme.fontSans
                                font.pixelSize: 14
                                color: Theme.muted
                                Layout.alignment: Qt.AlignBaseline
                            }
                        }

                        Item { Layout.fillWidth: true }

                        RowLayout {
                            spacing: 22
                            Layout.alignment: Qt.AlignBottom

                            Text {
                                textFormat: Text.StyledText
                                text: "<b>" + Fmt.formatBytes(transferController.copiedBytes) + "</b>"
                                      + " <font color=\"" + Theme.muted + "\">" + qsTr("of") + " "
                                      + Fmt.formatBytes(transferController.totalBytes) + "</font>"
                                font.family: Theme.fontSans
                                font.pixelSize: 14
                                color: Theme.text
                                Layout.alignment: Qt.AlignBaseline
                            }

                            Text {
                                visible: progressView.isRunning && transferController.rate > 0
                                textFormat: Text.StyledText
                                text: "<b>" + Fmt.formatRate(transferController.rate) + "</b>"
                                      + " <font color=\"" + Theme.muted + "\">" + qsTr("combined") + "</font>"
                                font.family: Theme.fontSans
                                font.pixelSize: 14
                                color: Theme.text
                                Layout.alignment: Qt.AlignBaseline
                            }

                            Text {
                                visible: progressView.isRunning && transferController.etaSeconds >= 0
                                textFormat: Text.StyledText
                                text: "<b>" + Fmt.formatEta(transferController.etaSeconds) + "</b>"
                                      + " <font color=\"" + Theme.muted + "\">" + qsTr("left") + "</font>"
                                font.family: Theme.fontSans
                                font.pixelSize: 14
                                color: Theme.text
                                Layout.alignment: Qt.AlignBaseline
                            }
                        }
                    }

                    // 12px overall progress bar
                    Rectangle {
                        Layout.fillWidth: true
                        height: 12
                        radius: 6
                        color: Theme.progressTrack
                        clip: true

                        Rectangle {
                            height: parent.height
                            radius: 6
                            width: parent.width * Math.max(0, Math.min(1, transferController.overallPercent / 100.0))
                            color: progressView.overallBarColor()

                            Behavior on width {
                                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                            }
                        }
                    }
                }
            }

            // Warnings expander (shown when warnings exist)
            Rectangle {
                visible: transferController.warnings.length > 0
                Layout.fillWidth: true
                implicitHeight: warnCol.implicitHeight + 28
                radius: Theme.radiusCard
                color: Theme.warningBg
                border.color: Theme.warningBorder
                border.width: 1

                ColumnLayout {
                    id: warnCol
                    anchors.fill: parent
                    anchors.margins: 14
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("%1 %2 recorded during copy")
                                  .arg(transferController.warnings.length)
                                  .arg(transferController.warnings.length === 1 ? qsTr("warning") : qsTr("warnings"))
                            font.family: Theme.fontSans
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: Theme.warning
                            Layout.fillWidth: true
                        }

                        OutlineButton {
                            text: progressView.showWarningDetails ? qsTr("Hide details") : qsTr("Show details")
                            implicitHeight: 36
                            accessibleLabel: text
                            onClicked: progressView.showWarningDetails = !progressView.showWarningDetails
                        }
                    }

                    ColumnLayout {
                        visible: progressView.showWarningDetails
                        Layout.fillWidth: true
                        spacing: 4

                        Repeater {
                            model: transferController.warnings
                            delegate: Text {
                                Layout.fillWidth: true
                                text: "\u2022  " + modelData
                                font.family: Theme.fontMono
                                font.pixelSize: 12
                                color: Theme.warning
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            // Per-PC Jobs Table Card
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: jobTableCol.implicitHeight
                radius: Theme.radiusCard
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                clip: true

                ColumnLayout {
                    id: jobTableCol
                    width: parent.width
                    spacing: 0

                    // Header row
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 44
                        color: Theme.rowHeaderBg

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 22
                            anchors.rightMargin: 22
                            spacing: 16

                            Text {
                                text: qsTr("PC")
                                Layout.minimumWidth: 150
                                Layout.preferredWidth: 150
                                Layout.maximumWidth: 150
                                font.family: Theme.fontSans
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                                color: Theme.muted
                            }

                            Text {
                                text: qsTr("STAGE")
                                Layout.minimumWidth: 240
                                Layout.preferredWidth: 240
                                Layout.maximumWidth: 240
                                font.family: Theme.fontSans
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                                color: Theme.muted
                            }

                            Text {
                                text: qsTr("PROGRESS")
                                Layout.fillWidth: true
                                Layout.minimumWidth: 180
                                font.family: Theme.fontSans
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                                color: Theme.muted
                            }

                            Text {
                                text: qsTr("COPIED")
                                Layout.minimumWidth: 150
                                Layout.preferredWidth: 150
                                Layout.maximumWidth: 150
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

                    // Job rows
                    Repeater {
                        id: jobRepeater
                        model: transferController.jobModel

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 68
                            color: model.stage === 8 ? Theme.rowOfflineBg : Theme.panel

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 22
                                anchors.rightMargin: 22
                                spacing: 16

                                // PC column
                                ColumnLayout {
                                    Layout.minimumWidth: 150
                                    Layout.preferredWidth: 150
                                    Layout.maximumWidth: 150
                                    spacing: 2

                                    Text {
                                        text: model.id
                                        font.family: Theme.fontSans
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: model.stage === 8 ? Theme.muted : Theme.text
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

                                // STAGE column
                                Item {
                                    Layout.minimumWidth: 240
                                    Layout.preferredWidth: 240
                                    Layout.maximumWidth: 240
                                    implicitHeight: 44
                                    clip: true

                                    // Active stage: blue stageText + rate + mono currentFile
                                    ColumnLayout {
                                        visible: progressView.isActiveStage(model.stage)
                                        anchors.fill: parent
                                        spacing: 2

                                        Text {
                                            Layout.fillWidth: true
                                            text: model.rate > 0
                                                  ? (model.stageText + " \u00B7 " + Fmt.formatRate(model.rate))
                                                  : model.stageText
                                            font.family: Theme.fontSans
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            color: Theme.primaryText
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: model.currentFile
                                            font.family: Theme.fontMono
                                            font.pixelSize: 12
                                            color: Theme.muted
                                            elide: Text.ElideMiddle
                                        }
                                    }

                                    // Inactive stage: StatusChip (+ Retry button on Failed)
                                    Row {
                                        visible: !progressView.isActiveStage(model.stage)
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 10

                                        StatusChip {
                                            tone: progressView.jobChipTone(model.stage)
                                            text: model.stageText
                                            indicator: progressView.jobChipIndicator(model.stage)
                                            anchors.verticalCenter: parent.verticalCenter
                                        }

                                        Button {
                                            visible: model.stage === 6
                                            implicitHeight: 32
                                            implicitWidth: retryLbl.implicitWidth + 12
                                            padding: 0
                                            focusPolicy: Qt.StrongFocus
                                            Accessible.role: Accessible.Button
                                            Accessible.name: qsTr("Retry %1").arg(model.id)
                                            onClicked: transferController.retryPc(model.id)
                                            background: Rectangle {
                                                radius: 6
                                                color: parent.hovered ? Theme.primaryTint : "transparent"
                                                border.color: parent.activeFocus ? Theme.focusRing : "transparent"
                                                border.width: parent.activeFocus ? Theme.focusRingWidth : 0
                                            }
                                            contentItem: Text {
                                                id: retryLbl
                                                text: qsTr("Retry")
                                                font.family: Theme.fontSans
                                                font.pixelSize: 13
                                                font.weight: Font.DemiBold
                                                color: Theme.primaryText
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }
                                    }
                                }

                                // PROGRESS column
                                Item {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 180
                                    implicitHeight: 36
                                    clip: true

                                    // Skipped text
                                    Text {
                                        visible: model.stage === 8
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Offline, not selected for this copy.")
                                        font.family: Theme.fontSans
                                        font.pixelSize: 13
                                        color: Theme.muted
                                        elide: Text.ElideRight
                                    }

                                    // Failed error text
                                    Text {
                                        visible: model.stage === 6 && model.errorMessage !== ""
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: model.errorMessage
                                        font.family: Theme.fontSans
                                        font.pixelSize: 13
                                        color: Theme.error
                                        elide: Text.ElideRight
                                    }

                                    // Progress bar + percent
                                    RowLayout {
                                        visible: model.stage !== 8 && !(model.stage === 6 && model.errorMessage !== "")
                                        anchors.fill: parent
                                        spacing: 14

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 80
                                            height: 8
                                            radius: 4
                                            color: Theme.progressTrack
                                            clip: true

                                            Rectangle {
                                                height: parent.height
                                                radius: 4
                                                width: parent.width * Math.max(0, Math.min(1, model.percent / 100.0))
                                                color: progressView.jobBarColor(model.stage)

                                                Behavior on width {
                                                    NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                                                }
                                            }
                                        }

                                        Text {
                                            Layout.minimumWidth: 48
                                            Layout.preferredWidth: 48
                                            Layout.maximumWidth: 48
                                            text: model.percent + "%"
                                            font.family: Theme.fontSans
                                            font.pixelSize: 13
                                            font.weight: Font.Bold
                                            color: Theme.text
                                            horizontalAlignment: Text.AlignRight
                                        }
                                    }
                                }

                                // COPIED column
                                Text {
                                    Layout.minimumWidth: 150
                                    Layout.preferredWidth: 150
                                    Layout.maximumWidth: 150
                                    text: {
                                        if (model.stage === 8) return qsTr("None");
                                        if (model.stage === 4) return Fmt.formatBytes(model.copiedBytes);
                                        var copiedStr = model.copiedBytes > 0 ? Fmt.formatBytes(model.copiedBytes) : "0";
                                        return copiedStr + " / " + Fmt.formatBytes(model.totalBytes);
                                    }
                                    font.family: Theme.fontSans
                                    font.pixelSize: 13
                                    color: model.stage === 4 ? Theme.text : Theme.muted
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                visible: index < jobRepeater.count - 1
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

            // Bottom action bar
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 16

                OutlineButton {
                    text: qsTr("Cancel copy")
                    variant: "danger"
                    enabled: progressView.isRunning || progressView.isUsbRemoved
                    accessibleLabel: qsTr("Cancel copy")
                    onClicked: cancelDialog.open()
                }

                Button {
                    id: backLink
                    implicitHeight: Theme.minHitTarget
                    implicitWidth: backLabel.implicitWidth + 16
                    padding: 0
                    enabled: !progressView.isRunning && !progressView.isCancelling
                    focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                    hoverEnabled: enabled

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Back to sources")

                    onClicked: transferController.resetToSources()

                    background: Rectangle {
                        radius: Theme.radiusControl
                        color: backLink.hovered ? Theme.primaryTint : "transparent"
                        border.color: backLink.activeFocus ? Theme.focusRing : "transparent"
                        border.width: backLink.activeFocus ? Theme.focusRingWidth : 0
                    }

                    contentItem: Text {
                        id: backLabel
                        text: qsTr("Back to sources")
                        font.family: Theme.fontSans
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: backLink.enabled ? Theme.primaryText : Theme.disabledText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: transferController.ejectStatus !== ""
                          ? transferController.ejectStatus
                          : qsTr("Files are written with a temporary name and renamed when complete.")
                    font.family: Theme.fontSans
                    font.pixelSize: 13
                    font.weight: transferController.ejectStatus !== "" ? Font.DemiBold : Font.Normal
                    color: transferController.ejectStatus !== ""
                           ? (transferController.ejectError ? Theme.error : Theme.success)
                           : Theme.muted
                    wrapMode: Text.WordWrap
                }

                PrimaryButton {
                    visible: !transferController.isLocalDestination
                    text: qsTr("Eject USB safely")
                    implicitWidth: 160
                    enabled: transferController.canEject
                    accessibleLabel: qsTr("Eject USB safely")
                    onClicked: transferController.eject()
                }
            }
        }
    }

    // Cancel confirmation dialog
    Dialog {
        id: cancelDialog
        anchors.centerIn: Overlay.overlay
        width: 420
        modal: true
        title: qsTr("Cancel copy?")
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.panel
            radius: Theme.radiusCard
            border.color: Theme.border
            border.width: 1
        }

        header: Item {
            implicitHeight: 52
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Cancel copy?")
                font.family: Theme.fontSans
                font.pixelSize: 18
                font.weight: Font.Bold
                color: Theme.text
            }
        }

        contentItem: Text {
            leftPadding: 24
            rightPadding: 24
            text: transferController.isLocalDestination
                  ? qsTr("Active file transfers will be stopped. Already completed files will remain in the local folder.")
                  : qsTr("Active file transfers will be stopped. Already completed files will remain on the USB drive.")
            font.family: Theme.fontSans
            font.pixelSize: 14
            color: Theme.muted
            wrapMode: Text.WordWrap
        }

        footer: Item {
            implicitHeight: 68
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 10

                Item { Layout.fillWidth: true }

                OutlineButton {
                    text: qsTr("Keep copying")
                    accessibleLabel: qsTr("Keep copying")
                    onClicked: cancelDialog.close()
                }

                OutlineButton {
                    text: qsTr("Stop copy")
                    variant: "danger"
                    accessibleLabel: qsTr("Confirm stop copy")
                    onClicked: {
                        cancelDialog.close();
                        transferController.cancel();
                    }
                }
            }
        }
    }
}
