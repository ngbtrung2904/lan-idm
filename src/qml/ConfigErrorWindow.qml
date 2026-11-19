import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."
import "components"

ApplicationWindow {
    id: root

    visible: true
    width: 640
    height: 400
    minimumWidth: 520
    minimumHeight: 340
    title: qsTr("Configuration Error — Data Collector")
    color: Theme.bg

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 560)
        implicitHeight: contentCol.implicitHeight + 56
        radius: Theme.radiusCard
        color: Theme.panel
        border.color: Theme.border
        border.width: 1

        ColumnLayout {
            id: contentCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 28
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    width: 36
                    height: 36
                    radius: 18
                    color: Theme.errorBg
                    border.color: Theme.errorBorder
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "!"
                        font.family: Theme.fontSans
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: Theme.error
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: qsTr("Configuration file problem")
                        font.family: Theme.fontSans
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        color: Theme.text
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTr("Data Collector could not start because its configuration is missing or invalid.")
                        font.family: Theme.fontSans
                        font.pixelSize: 13
                        color: Theme.muted
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: errorText.implicitHeight + 24
                radius: Theme.radiusControl
                color: Theme.errorBg
                border.color: Theme.errorBorder
                border.width: 1

                Text {
                    id: errorText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 14
                    text: typeof configErrorText !== "undefined" && configErrorText.length > 0
                          ? configErrorText
                          : qsTr("Unknown configuration error.")
                    font.family: Theme.fontSans
                    font.pixelSize: 14
                    color: Theme.error
                    wrapMode: Text.WordWrap
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: qsTr("Config file checked:")
                    font.family: Theme.fontSans
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: Theme.muted
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: pathText.implicitHeight + 16
                    radius: Theme.radiusControl
                    color: Theme.rowHeaderBg
                    border.color: Theme.divider
                    border.width: 1

                    Text {
                        id: pathText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 10
                        text: typeof configPathText !== "undefined" && configPathText.length > 0
                              ? configPathText
                              : qsTr("(not specified)")
                        font.family: Theme.fontMono
                        font.pixelSize: 13
                        color: Theme.text
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Fix the configuration file or contact your system administrator, then restart Data Collector.")
                font.family: Theme.fontSans
                font.pixelSize: 13
                color: Theme.muted
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6

                Item { Layout.fillWidth: true }

                PrimaryButton {
                    id: exitBtn
                    text: qsTr("Exit")
                    onClicked: Qt.quit()
                    Component.onCompleted: forceActiveFocus()
                }
            }
        }
    }
}
