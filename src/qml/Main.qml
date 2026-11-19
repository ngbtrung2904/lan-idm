import QtQuick 2.15
import QtQuick.Controls 2.15
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

    Loader {
        id: viewLoader
        anchors.fill: parent
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
