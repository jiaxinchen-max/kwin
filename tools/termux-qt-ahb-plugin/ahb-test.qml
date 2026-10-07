import QtQuick

Rectangle {
    width: 640
    height: 420
    color: "#101824"

    Rectangle {
        id: block
        width: 150
        height: 150
        radius: 24
        color: "#f25a1a"
        y: 135

        SequentialAnimation on x {
            loops: Animation.Infinite
            NumberAnimation { from: 24; to: 466; duration: 1400; easing.type: Easing.InOutQuad }
            NumberAnimation { from: 466; to: 24; duration: 1400; easing.type: Easing.InOutQuad }
        }
    }

    Text {
        anchors.centerIn: parent
        text: "Qt Quick → Android EGL → AHardwareBuffer"
        color: "white"
        font.pixelSize: 22
    }
}
