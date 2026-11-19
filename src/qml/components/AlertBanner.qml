import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".."

Rectangle {
    id: banner

    // tone: "warning" | "error"
    property string tone: "warning"
    property string title: ""
    property string body: ""
    default property alias actions: actionsSlot.data

    readonly property color fgColor: tone === "error" ? Theme.error : Theme.warning

    color: tone === "error" ? Theme.errorBg : Theme.warningBg
    border.color: tone === "error" ? Theme.errorBorder : Theme.warningBorder
    border.width: 1
    radius: Theme.radiusCard
    implicitHeight: contentLayout.implicitHeight + 28

    Accessible.role: Accessible.AlertMessage
    Accessible.name: title + ". " + body

    RowLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: 14
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        // Circle-! icon
        Rectangle {
            width: 20
            height: 20
            radius: 10
            color: "transparent"
            border.color: banner.fgColor
            border.width: 1.6
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1

            Text {
                anchors.centerIn: parent
                text: "!"
                font.family: Theme.fontSans
                font.pixelSize: 12
                font.weight: Font.Bold
                color: banner.fgColor
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Text {
                text: banner.title
                font.family: Theme.fontSans
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: banner.fgColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Text {
                visible: banner.body !== ""
                text: banner.body
                font.family: Theme.fontSans
                font.pixelSize: 13
                color: banner.fgColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        RowLayout {
            id: actionsSlot
            spacing: 8
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
