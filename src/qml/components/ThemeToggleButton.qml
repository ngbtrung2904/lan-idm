import QtQuick 2.15
import QtQuick.Controls 2.15
import ".."

Button {
    id: control

    implicitWidth: Theme.minHitTarget
    implicitHeight: Theme.minHitTarget
    padding: 0
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true

    Accessible.role: Accessible.Button
    Accessible.name: Theme.dark ? qsTr("Switch to light theme") : qsTr("Switch to dark theme")

    ToolTip.visible: control.hovered
    ToolTip.delay: 300
    ToolTip.text: control.Accessible.name

    onClicked: Theme.dark = !Theme.dark

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
            color: control.down
                   ? Theme.pressedBg
                   : control.hovered
                     ? Theme.hoverBg
                     : Theme.panel
            border.color: Theme.inputBorder
            border.width: 1
        }
    }

    contentItem: Item {
        Canvas {
            id: iconCanvas
            width: 20
            height: 20
            anchors.centerIn: parent

            property bool darkMode: Theme.dark
            property color strokeColor: Theme.text

            onDarkModeChanged: requestPaint()
            onStrokeColorChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.strokeStyle = strokeColor;
                ctx.lineWidth = 1.7;
                ctx.lineCap = "round";
                ctx.lineJoin = "round";

                if (darkMode) {
                    // Sun icon with 8 rays (click to switch to light)
                    var cx = width / 2.0;
                    var cy = height / 2.0;
                    ctx.beginPath();
                    ctx.arc(cx, cy, 3.8, 0, Math.PI * 2, false);
                    ctx.stroke();

                    ctx.beginPath();
                    for (var i = 0; i < 8; i++) {
                        var angle = i * Math.PI / 4.0;
                        var cosA = Math.cos(angle);
                        var sinA = Math.sin(angle);
                        ctx.moveTo(cx + cosA * 6.2, cy + sinA * 6.2);
                        ctx.lineTo(cx + cosA * 8.4, cy + sinA * 8.4);
                    }
                    ctx.stroke();
                } else {
                    // Crescent moon icon (click to switch to dark)
                    var s = width / 24.0;
                    ctx.beginPath();
                    ctx.arc(12.0 * s, 12.0 * s, 8.6 * s, 0.09, -1.66, false);
                    ctx.arc(16.6 * s, 7.3 * s, 6.7 * s, -2.50, 0.93, true);
                    ctx.closePath();
                    ctx.stroke();
                }
            }
        }
    }
}
