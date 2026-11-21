import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".."
import "../components"

Item {
    id: loginView

    readonly property bool stacked: width < 820

    function submit() {
        if (authController.state === 1 || authController.throttleSeconds > 0) {
            return;
        }
        var pass = passphraseField.text;
        passphraseField.text = "";
        authController.unlock(operatorField.text, pass);
    }

    Component.onCompleted: {
        if (authController.operatorId && authController.operatorId.length > 0) {
            operatorField.text = authController.operatorId;
        }
        passphraseField.forceActiveFocus();
    }

    Connections {
        target: authController
        function onStateChanged() {
            if (authController.state === 0) {
                passphraseField.text = "";
            }
        }
    }

    GridLayout {
        anchors.fill: parent
        columns: loginView.stacked ? 1 : 2
        columnSpacing: 0
        rowSpacing: 0

        // Left dark branding panel
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: !loginView.stacked
            Layout.preferredWidth: loginView.stacked ? parent.width : parent.width * 0.47
            Layout.preferredHeight: loginView.stacked ? leftContent.implicitHeight + 64 : parent.height
            color: Theme.darkPanel

            ColumnLayout {
                id: leftContent
                anchors.fill: parent
                anchors.margins: 48
                spacing: 0

                // Brand row
                Row {
                    spacing: 10

                    Canvas {
                        width: 22
                        height: 22
                        anchors.verticalCenter: parent.verticalCenter
                        property color strokeColor: Theme.darkAccent
                        onStrokeColorChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            ctx.strokeStyle = strokeColor;
                            ctx.lineWidth = 2.2;
                            ctx.lineCap = "round";
                            ctx.lineJoin = "round";
                            ctx.beginPath();
                            ctx.moveTo(11, 3);
                            ctx.lineTo(11, 14);
                            ctx.moveTo(6.5, 10);
                            ctx.lineTo(11, 14.5);
                            ctx.lineTo(15.5, 10);
                            ctx.moveTo(4, 18.5);
                            ctx.lineTo(18, 18.5);
                            ctx.stroke();
                        }
                    }

                    Text {
                        text: qsTr("Data Collector")
                        font.family: Theme.fontSans
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        color: Theme.darkText
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Item { height: 40; width: 1 }

                // Headline
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Collect logs and screenshots from every PC onto one USB drive.")
                    font.family: Theme.fontSans
                    font.pixelSize: 36
                    font.weight: Font.Bold
                    color: Theme.darkText
                    lineHeight: 1.16
                    wrapMode: Text.WordWrap
                }

                Item { height: 20; width: 1 }

                // Subtitle
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Pick a time range, choose the PCs, and copy. Files are pulled over the local network.")
                    font.family: Theme.fontSans
                    font.pixelSize: 15
                    color: Theme.darkMuted
                    lineHeight: 1.48
                    wrapMode: Text.WordWrap
                }

                Item {
                    Layout.fillHeight: true
                    implicitHeight: 32
                }

                // Bottom section: COLLECTED FROM EACH PC
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        text: qsTr("COLLECTED FROM EACH PC")
                        font.family: Theme.fontSans
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.9
                        color: Theme.darkMuted
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 46
                        radius: Theme.radiusControl
                        color: Theme.darkCard

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 12

                            Text {
                                text: qsTr("Logs")
                                font.family: Theme.fontSans
                                font.pixelSize: 14
                                color: Theme.darkText
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: authController.logsDisplayPath
                                font.family: Theme.fontMono
                                font.pixelSize: 13
                                color: Theme.darkMono
                                elide: Text.ElideMiddle
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 46
                        radius: Theme.radiusControl
                        color: Theme.darkCard

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 12

                            Text {
                                text: qsTr("Screenshots")
                                font.family: Theme.fontSans
                                font.pixelSize: 14
                                color: Theme.darkText
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: authController.screenshotsDisplayPath
                                font.family: Theme.fontMono
                                font.pixelSize: 13
                                color: Theme.darkMono
                                elide: Text.ElideMiddle
                            }
                        }
                    }
                }
            }
        }

        // Right panel with theme toggle and centered Sign in card
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: loginView.stacked ? parent.width : parent.width * 0.53
            color: Theme.bg

            ThemeToggleButton {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 20
                anchors.rightMargin: 24
            }

            Rectangle {
                id: signInCard
                anchors.centerIn: parent
                width: Math.min(400, parent.width - 48)
                implicitHeight: cardContent.implicitHeight + 64
                radius: Theme.radiusCard
                color: Theme.panel
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    id: cardContent
                    anchors.fill: parent
                    anchors.margins: 32
                    spacing: 0

                    Text {
                        text: qsTr("Sign in")
                        font.family: Theme.fontSans
                        font.pixelSize: 24
                        font.weight: Font.Bold
                        color: Theme.text
                    }

                    Item { height: 8; width: 1 }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Enter your operator credentials to unlock access to the PCs.")
                        font.family: Theme.fontSans
                        font.pixelSize: 14
                        color: Theme.muted
                        lineHeight: 1.4
                        wrapMode: Text.WordWrap
                    }

                    Item { height: 22; width: 1 }

                    // Operator ID
                    Text {
                        text: qsTr("Operator ID")
                        font.family: Theme.fontSans
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }

                    Item { height: 6; width: 1 }

                    TextField {
                        id: operatorField
                        Layout.fillWidth: true
                        implicitHeight: Theme.minHitTarget
                        placeholderText: qsTr("operator01")
                        font.family: Theme.fontSans
                        font.pixelSize: 14
                        color: Theme.text
                        placeholderTextColor: Theme.muted
                        leftPadding: 14
                        rightPadding: 14
                        selectByMouse: true
                        enabled: authController.state !== 1

                        Accessible.role: Accessible.EditableText
                        Accessible.name: qsTr("Operator ID")

                        KeyNavigation.tab: passphraseField
                        onAccepted: passphraseField.forceActiveFocus()

                        background: Item {
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -3
                                radius: Theme.radiusControl + 2
                                color: "transparent"
                                border.color: Theme.focusRing
                                border.width: Theme.focusRingWidth
                                visible: operatorField.activeFocus
                            }
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusControl
                                color: Theme.inputBg
                                border.color: operatorField.activeFocus ? Theme.focusRing : Theme.inputBorder
                                border.width: 1
                            }
                        }
                    }

                    Item { height: 16; width: 1 }

                    // Passphrase
                    Text {
                        text: qsTr("Passphrase")
                        font.family: Theme.fontSans
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }

                    Item { height: 6; width: 1 }

                    TextField {
                        id: passphraseField
                        Layout.fillWidth: true
                        implicitHeight: Theme.minHitTarget
                        echoMode: TextInput.Password
                        font.family: Theme.fontSans
                        font.pixelSize: 14
                        color: Theme.text
                        leftPadding: 14
                        rightPadding: 14
                        selectByMouse: true
                        enabled: authController.state !== 1 && authController.throttleSeconds === 0

                        Accessible.role: Accessible.EditableText
                        Accessible.name: qsTr("Passphrase")

                        KeyNavigation.tab: signInBtn
                        onAccepted: loginView.submit()

                        background: Item {
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -3
                                radius: Theme.radiusControl + 2
                                color: "transparent"
                                border.color: Theme.focusRing
                                border.width: Theme.focusRingWidth
                                visible: passphraseField.activeFocus
                            }
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusControl
                                color: Theme.inputBg
                                border.color: passphraseField.activeFocus ? Theme.focusRing : Theme.inputBorder
                                border.width: 1
                            }
                        }
                    }

                    Item { height: 10; width: 1 }

                    // Lock note
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Canvas {
                            width: 14
                            height: 14
                            Layout.alignment: Qt.AlignVCenter
                            property color strokeColor: Theme.muted
                            onStrokeColorChanged: requestPaint()
                            onPaint: {
                                var ctx = getContext("2d");
                                ctx.clearRect(0, 0, width, height);
                                ctx.strokeStyle = strokeColor;
                                ctx.lineWidth = 1.3;
                                ctx.beginPath();
                                ctx.roundedRect(2.5, 6.5, 9, 6, 1.5, 1.5);
                                ctx.moveTo(4.5, 6.5);
                                ctx.arc(7, 5, 2.5, Math.PI, 0, false);
                                ctx.lineTo(9.5, 6.5);
                                ctx.stroke();
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Unlocks the collection key for this session only.")
                            font.family: Theme.fontSans
                            font.pixelSize: 12
                            color: Theme.muted
                            wrapMode: Text.WordWrap
                        }
                    }

                    // Inline error
                    Rectangle {
                        visible: authController.error !== ""
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        implicitHeight: errText.implicitHeight + 16
                        radius: Theme.radiusControl
                        color: Theme.errorBg
                        border.color: Theme.errorBorder
                        border.width: 1

                        Text {
                            id: errText
                            anchors.fill: parent
                            anchors.margins: 8
                            text: authController.error
                            font.family: Theme.fontSans
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            color: Theme.error
                            wrapMode: Text.WordWrap
                            Accessible.role: Accessible.AlertMessage
                            Accessible.name: text
                        }
                    }

                    Item { height: 20; width: 1 }

                    // Sign in button
                    PrimaryButton {
                        id: signInBtn
                        Layout.fillWidth: true
                        text: authController.state === 1
                              ? qsTr("Unlocking...")
                              : authController.throttleSeconds > 0
                                ? qsTr("Wait %1 s").arg(authController.throttleSeconds)
                                : qsTr("Sign in")
                        busy: authController.state === 1
                        enabled: authController.state !== 1 && authController.throttleSeconds === 0
                        accessibleLabel: qsTr("Sign in")
                        onClicked: loginView.submit()
                    }

                    Item { height: 18; width: 1 }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Need access? Contact your system administrator.")
                        font.family: Theme.fontSans
                        font.pixelSize: 12
                        color: Theme.muted
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
