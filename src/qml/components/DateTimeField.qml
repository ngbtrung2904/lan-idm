import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".."
import "FormatUtils.js" as Fmt

ColumnLayout {
    id: root

    property string label: ""
    property date value: new Date()
    property bool invalid: false
    signal valueEdited(date newValue)

    spacing: 6
    implicitWidth: 224

    Text {
        visible: root.label !== ""
        text: root.label
        font.family: Theme.fontSans
        font.pixelSize: 13
        font.weight: Font.DemiBold
        color: Theme.text
    }

    Button {
        id: triggerBtn
        Layout.fillWidth: true
        implicitHeight: Theme.minHitTarget
        padding: 0
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true

        Accessible.role: Accessible.Button
        Accessible.name: root.label + ": " + Fmt.formatDateTimeDisplay(root.value)

        onClicked: {
            var d = root.value;
            if (!d || isNaN(d.getTime())) {
                d = new Date();
            }
            yearSpin.value = d.getFullYear();
            monthSpin.value = d.getMonth() + 1;
            daySpin.value = d.getDate();
            hourSpin.value = d.getHours();
            minSpin.value = d.getMinutes();
            pickerPopup.open();
        }

        background: Item {
            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: Theme.radiusControl + 2
                color: "transparent"
                border.color: Theme.primary
                border.width: Theme.focusRingWidth
                visible: triggerBtn.activeFocus || pickerPopup.opened
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusControl
                color: Theme.panel
                border.color: root.invalid
                              ? Theme.errorBar
                              : pickerPopup.opened
                                ? Theme.primary
                                : triggerBtn.hovered
                                  ? "#9CA3AF"
                                  : Theme.border
                border.width: (root.invalid || pickerPopup.opened) ? 1.5 : 1
            }
        }

        contentItem: RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: Fmt.formatDateTimeDisplay(root.value)
                font.family: Theme.fontSans
                font.pixelSize: 14
                color: Theme.text
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            // Minimal calendar icon
            Canvas {
                width: 16
                height: 16
                Layout.alignment: Qt.AlignVCenter
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.clearRect(0, 0, width, height);
                    ctx.strokeStyle = Theme.text;
                    ctx.lineWidth = 1.4;
                    ctx.beginPath();
                    ctx.roundedRect(2, 3.5, 12, 10.5, 2, 2);
                    ctx.moveTo(2, 7);
                    ctx.lineTo(14, 7);
                    ctx.moveTo(5.5, 1.5);
                    ctx.lineTo(5.5, 4.5);
                    ctx.moveTo(10.5, 1.5);
                    ctx.lineTo(10.5, 4.5);
                    ctx.stroke();
                }
            }
        }

        Popup {
            id: pickerPopup
            y: triggerBtn.height + 6
            width: 300
            padding: 16
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
                spacing: 12

                Text {
                    text: root.label !== "" ? (root.label + " date & time") : qsTr("Select date & time")
                    font.family: Theme.fontSans
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                }

                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    Layout.fillWidth: true

                    Text { text: qsTr("Year"); font.family: Theme.fontSans; font.pixelSize: 13; color: Theme.muted }
                    SpinBox {
                        id: yearSpin
                        from: 2020; to: 2035; editable: true
                        Layout.fillWidth: true
                        implicitHeight: 36
                        Accessible.name: qsTr("Year")
                    }

                    Text { text: qsTr("Month"); font.family: Theme.fontSans; font.pixelSize: 13; color: Theme.muted }
                    SpinBox {
                        id: monthSpin
                        from: 1; to: 12; editable: true
                        Layout.fillWidth: true
                        implicitHeight: 36
                        Accessible.name: qsTr("Month")
                    }

                    Text { text: qsTr("Day"); font.family: Theme.fontSans; font.pixelSize: 13; color: Theme.muted }
                    SpinBox {
                        id: daySpin
                        from: 1; to: 31; editable: true
                        Layout.fillWidth: true
                        implicitHeight: 36
                        Accessible.name: qsTr("Day")
                    }

                    Text { text: qsTr("Hour (0-23)"); font.family: Theme.fontSans; font.pixelSize: 13; color: Theme.muted }
                    SpinBox {
                        id: hourSpin
                        from: 0; to: 23; editable: true
                        Layout.fillWidth: true
                        implicitHeight: 36
                        Accessible.name: qsTr("Hour")
                    }

                    Text { text: qsTr("Minute"); font.family: Theme.fontSans; font.pixelSize: 13; color: Theme.muted }
                    SpinBox {
                        id: minSpin
                        from: 0; to: 59; editable: true
                        Layout.fillWidth: true
                        implicitHeight: 36
                        Accessible.name: qsTr("Minute")
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Item { Layout.fillWidth: true }

                    OutlineButton {
                        text: qsTr("Cancel")
                        implicitHeight: 38
                        onClicked: pickerPopup.close()
                    }

                    PrimaryButton {
                        text: qsTr("Apply")
                        implicitHeight: 38
                        implicitWidth: 88
                        onClicked: {
                            var maxDay = new Date(yearSpin.value, monthSpin.value, 0).getDate();
                            var clampedDay = Math.min(daySpin.value, maxDay);
                            var updated = new Date(yearSpin.value, monthSpin.value - 1, clampedDay,
                                                   hourSpin.value, minSpin.value, 0, 0);
                            root.valueEdited(updated);
                            pickerPopup.close();
                        }
                    }
                }
            }
        }
    }
}
