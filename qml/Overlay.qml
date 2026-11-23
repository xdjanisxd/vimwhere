import QtQuick

Item {
    id: root
    property string prompt: ""
    property var hintItems: []
    property var gridItems: []

    component OutlinedText: Text {
        color: "white"
        style: Text.Outline
        styleColor: "#141922"
        font.bold: true
        textFormat: Text.PlainText
        padding: 2
    }

    OutlinedText {
        x: 16
        y: 16
        width: Math.max(0, root.width - 32)
        visible: text.length > 0
        z: 1
        text: root.prompt
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.Wrap
        font.pixelSize: 18
    }

    Repeater {
        model: root.hintItems
        delegate: OutlinedText {
            required property var modelData
            x: Math.max(0, Math.min(modelData.x - width / 2, root.width - width))
            y: Math.max(0, Math.min(modelData.y - height / 2, root.height - height))
            text: modelData.label
            color: "#ffe57d"
            font.pixelSize: 16
        }
    }

    Repeater {
        model: root.gridItems
        delegate: OutlinedText {
            required property var modelData
            x: modelData.x + (modelData.width - width) / 2
            y: modelData.y + (modelData.height - height) / 2
            text: modelData.label
            font.pixelSize: 26
        }
    }
}
