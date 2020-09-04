import QtQuick 2.0
import QtQuick.Controls 2.4
import QtQuick.Controls.Material 2.12


Item {
    id: batteryBlock
    property var gauge

    width: gauge.diameter / 2.6
    height: gauge.diameter / 11.5

    //anchors.topMargin: gauge.batteryTopMargin
    anchors.horizontalCenter: gauge.horizontalCenter
    anchors.bottom: gauge.bottom

    property real battFontSize: Math.max(10, battery.height * 0.51)

    property real margin: 5

    property bool isCharging: false
    property bool isDischarging: false

    Rectangle {
        id: battery
        width: parent.width
        height: parent.height

        border.color: gauge.borderColor
        border.width: 2
        x: gauge.paddingLeft

        radius: 3
        color: gauge.innerColor

        Rectangle {
            opacity: gauge.baseOpacity
            radius: 2

            anchors.left: battery.left
            anchors.leftMargin: parent.border.width
            anchors.topMargin: parent.border.width
            anchors.verticalCenter: battery.verticalCenter

            property bool battD: gauge.isBatteryBlinking
            property string battDColor: gauge.battBlinkingColor

            onBattDChanged: {
                battDAnimation.loops = battD ? Animation.Infinite : 1;
                if (!battD) battDColor = gauge.battBlinkingColor;
            }

            ColorAnimation on battDColor {
                id: battDAnimation
                running: gauge.isBatteryBlinking
                from: gauge.battBlinkingColor
                to: gauge.battWarningColor
                duration: gauge.gaugesColorAnimation
                loops: Animation.Infinite
            }

            height: battery.height - parent.border.width * 2
            color: gauge.isBatteryWarning || gauge.isBatteryBlinking ? battDColor : gauge.battColor
            width: (battery.width - parent.border.width * 2) * gauge.batteryPercents / 100
        }

        Item {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 6

                font.pixelSize: batteryBlock.battFontSize
                id: tBat
                text: qsTr("%1 x %2").arg(gauge.batteryCellVolts.toFixed(2)).arg(gauge.batteryCells)
                font.family: gauge.ff
            }
        }

        Item {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 6
                font.pixelSize: batteryBlock.battFontSize
                font.family: gauge.ff
                id: tBatPercent
                text: gauge.batteryPercents.toFixed(0) + '%'
            }
        }

        Rectangle {
            anchors.left: battery.right
            anchors.verticalCenter: battery.verticalCenter
            color: gauge.borderColor
            height: battery.height * 0.5
            width: 3
            border.color: gauge.borderColor
        }
    }
}
