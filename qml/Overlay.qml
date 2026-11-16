import QtQuick

Rectangle {
    id: root
    color: "#36091420"
    property string mode: ""
    property string prompt: ""
    property var hintItems: []
    property var gridItems: []

    Rectangle {
        x: 16; y: 16
        width: Math.min(root.width - 32, promptText.implicitWidth + 28)
        height: 38
        radius: 7
        color: "#ec172332"
        border.color: "#a1c8f4"
        Text {
            id: promptText
            anchors.centerIn: parent
            text: root.prompt
            color: "white"
            font.pixelSize: 16
            font.bold: true
        }
    }

    Repeater {
        model: root.hintItems
        delegate: Rectangle {
            id: hintBubble
            required property var modelData
            x: modelData.x - width / 2
            y: modelData.y - height / 2
            width: hintText.implicitWidth + 12
            height: 25
            radius: 5
            color: "#f9d976"
            border.color: "#1e2530"
            border.width: 2
            Text {
                id: hintText
                anchors.centerIn: parent
                text: hintBubble.modelData.label
                color: "#171b25"
                font.pixelSize: 15
                font.bold: true
            }
        }
    }

    Repeater {
        model: root.gridItems
        delegate: Item {
            id: gridCell
            required property var modelData
            x: modelData.x; y: modelData.y
            width: modelData.width; height: modelData.height
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: "#a4e6fc"
                border.width: 2
            }
            Rectangle {
                anchors.centerIn: parent
                width: 32; height: 32; radius: 16
                color: "#de172332"
                border.color: "white"
                Text {
                    anchors.centerIn: parent
                    text: gridCell.modelData.label
                    color: "white"
                    font.pixelSize: 19
                    font.bold: true
                }
            }
        }
    }
}
