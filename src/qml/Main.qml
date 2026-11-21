import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."
import "views"

ApplicationWindow {
    id: root

    visible: true
    width: 1200
    height: 820
    minimumWidth: Theme.minWindowWidth
    minimumHeight: Theme.minWindowHeight
    title: qsTr("Data Collector")
    color: Theme.bg

    // "sources" | "progress" (used when authController.state === 2)
    property string activeScreen: "sources"

    Component.onCompleted: {
        if (typeof initialDarkTheme !== "undefined") {
            Theme.dark = Boolean(initialDarkTheme);
        }
    }

    Connections {
        target: authController
        function onStateChanged() {
            if (authController.state !== 2) {
                root.activeScreen = "sources";
            }
        }
    }

    Connections {
        target: sourcesController
        function onCopyRequested() {
            root.activeScreen = "progress";
        }
    }

    Connections {
        target: transferController
        function onBackToSourcesRequested() {
            root.activeScreen = "sources";
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Loader {
            id: viewLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: {
                if (authController.state !== 2) {
                    return loginComp;
                }
                if (root.activeScreen === "progress") {
                    return progressComp;
                }
                return sourcesComp;
            }
        }

        // Bottom version & copyright footer
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: Theme.panel

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                spacing: 12

                Text {
                    text: "Data Collector v" + (typeof appVersion !== "undefined" ? appVersion : "")
                    font.family: Theme.fontSans
                    font.pixelSize: 12
                    color: Theme.muted
                    Layout.alignment: Qt.AlignVCenter
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Copyright \u00A9 2026 " + (typeof appAuthor !== "undefined" ? appAuthor : "noname")
                    font.family: Theme.fontSans
                    font.pixelSize: 12
                    color: Theme.muted
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
    }

    Component {
        id: loginComp
        LoginView {}
    }

    Component {
        id: sourcesComp
        SourcesView {}
    }

    Component {
        id: progressComp
        ProgressView {}
    }
}
