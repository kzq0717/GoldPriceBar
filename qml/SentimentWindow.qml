import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    objectName: "sentimentWindow"
    title: "黄金实时舆情监测与热点分析"
    width: 680
    height: 540
    minimumWidth: 540
    minimumHeight: 400
    color: "#16161A"
    visible: false

    Connections {
        target: bridge
        function onRequestShowSentiment() {
            root.visible = true
            root.show()
            root.raise()
            root.requestActivate()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 顶部标题栏区域
        Rectangle {
            Layout.fillWidth: true
            height: 52
            color: "#1E1E24"
            border.color: "#2B2B36"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Text {
                    text: "📰"
                    font.pixelSize: 18
                    Layout.alignment: Qt.AlignVCenter
                }

                Column {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 2

                    Text {
                        text: "黄金实时舆情监测"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.bold: true
                    }
                    Text {
                        text: "聚合主流财经与全球热点，自动多空偏向分类"
                        color: "#8C8C9A"
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillWidth: true }

                // 刷新按钮
                Button {
                    Layout.alignment: Qt.AlignVCenter
                    text: (bridge && bridge.sentimentLoading) ? "正在拉取..." : "🔄 刷新资讯"
                    enabled: bridge ? !bridge.sentimentLoading : true
                    onClicked: bridge.refreshSentiment()

                    contentItem: Text {
                        text: parent.text
                        color: "#E0E0E0"
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.hovered ? "#363645" : "#282834"
                        radius: 4
                        border.color: "#3E3E50"
                    }
                }
            }
        }

        // 舆情多空比例统计条（仅在有数据且非网络异常时展示）
        Rectangle {
            Layout.fillWidth: true
            height: 40
            color: "#1A1A20"
            visible: bridge && !bridge.sentimentNetworkError && bridge.sentimentItems.length > 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                // 多空进度比例条
                Rectangle {
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: 8
                    radius: 4
                    color: "#2A2A35"
                    clip: true

                    Row {
                        anchors.fill: parent
                        Rectangle {
                            height: parent.height
                            width: parent.width * (bridge ? (bridge.sentimentBullCount / Math.max(1, bridge.sentimentBullCount + bridge.sentimentBearCount + bridge.sentimentNeutralCount)) : 0.33)
                            color: "#52C41A" // 利多绿
                        }
                        Rectangle {
                            height: parent.height
                            width: parent.width * (bridge ? (bridge.sentimentNeutralCount / Math.max(1, bridge.sentimentBullCount + bridge.sentimentBearCount + bridge.sentimentNeutralCount)) : 0.33)
                            color: "#8C8C9A" // 中性灰
                        }
                        Rectangle {
                            height: parent.height
                            width: parent.width * (bridge ? (bridge.sentimentBearCount / Math.max(1, bridge.sentimentBullCount + bridge.sentimentBearCount + bridge.sentimentNeutralCount)) : 0.33)
                            color: "#FF4D4F" // 利空红
                        }
                    }
                }

                // 统计文本
                Text {
                    text: {
                        if (!bridge) return ""
                        return "利多 " + bridge.sentimentBullCount + " 条  |  中性 " + bridge.sentimentNeutralCount + " 条  |  利空 " + bridge.sentimentBearCount + " 条"
                    }
                    color: "#B0B0C0"
                    font.pixelSize: 12
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "共 " + (bridge ? bridge.sentimentItems.length : 0) + " 条热点"
                    color: "#7E7E8E"
                    font.pixelSize: 11
                }
            }
        }

        // 核心内容展示区
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 1. 网络异常状态提示卡片（重点：当网络故障时优雅展示，避免软件崩溃）
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 32)
                height: 240
                radius: 8
                color: "#1E1E26"
                border.color: "#3A2A2A"
                border.width: 1
                visible: bridge ? bridge.sentimentNetworkError : false

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8
                        Text {
                            text: "⚠️"
                            font.pixelSize: 24
                        }
                        Text {
                            text: "网络连接异常"
                            color: "#FF4D4F"
                            font.pixelSize: 16
                            font.bold: true
                        }
                    }

                    Text {
                        text: "未能获取黄金实时舆情与热点资讯。"
                        color: "#E0E0E0"
                        font.pixelSize: 13
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        radius: 4
                        color: "#16161D"
                        border.color: "#2C2C38"

                        Text {
                            anchors.fill: parent
                            anchors.margins: 8
                            text: (bridge && bridge.sentimentError.length > 0) ? bridge.sentimentError : "网络请求超时或目标服务不可达，请检查本地网络或代理设置。"
                            color: "#9A9AA8"
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 12

                        Button {
                            text: "🔄 立即重试"
                            onClicked: bridge.refreshSentiment()
                            contentItem: Text {
                                text: parent.text
                                color: "#FFFFFF"
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#2D68C4" : "#1E52A8"
                                radius: 4
                            }
                        }

                        Button {
                            text: "⚙️ 检查代理设置"
                            onClicked: bridge.openSettingsWindow()
                            contentItem: Text {
                                text: parent.text
                                color: "#CCCCCC"
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#363645" : "#282834"
                                radius: 4
                                border.color: "#3E3E50"
                            }
                        }
                    }
                }
            }

            // 2. 加载中状态提示
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                visible: bridge ? (bridge.sentimentLoading && bridge.sentimentItems.length === 0 && !bridge.sentimentNetworkError) : false

                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    running: true
                }
                Text {
                    text: "正在检索全网最新黄金舆情与市场热点..."
                    color: "#8C8C9A"
                    font.pixelSize: 13
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            // 3. 正常舆情列表展示
            ListView {
                id: newsList
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                clip: true
                visible: bridge ? (!bridge.sentimentNetworkError && bridge.sentimentItems.length > 0) : false
                model: bridge ? bridge.sentimentItems : []

                ScrollBar.vertical: ScrollBar {
                    active: true
                }

                delegate: Rectangle {
                    width: newsList.width
                    height: itemLayout.implicitHeight + 20
                    radius: 6
                    color: itemMouse.containsMouse ? "#242430" : "#1C1C24"
                    border.color: itemMouse.containsMouse ? "#3D3D52" : "#262633"
                    border.width: 1

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.link && modelData.link.length > 0) {
                                bridge.openUrl(modelData.link)
                            }
                        }
                    }

                    ColumnLayout {
                        id: itemLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 10
                        spacing: 6

                        // 偏向标签 + 标题
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                width: 36
                                height: 20
                                radius: 3
                                color: {
                                    if (modelData.bias === "bullish") return "#1B3B24"
                                    if (modelData.bias === "bearish") return "#3E1E22"
                                    return "#282833"
                                }
                                border.color: {
                                    if (modelData.bias === "bullish") return "#2E7D32"
                                    if (modelData.bias === "bearish") return "#C62828"
                                    return "#555566"
                                }

                                Text {
                                    anchors.centerIn: parent
                                    text: {
                                        if (modelData.bias === "bullish") return "利多"
                                        if (modelData.bias === "bearish") return "利空"
                                        return "中性"
                                    }
                                    color: {
                                        if (modelData.bias === "bullish") return "#66BB6A"
                                        if (modelData.bias === "bearish") return "#EF5350"
                                        return "#9E9E9E"
                                    }
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.title
                                color: itemMouse.containsMouse ? "#40A9FF" : "#F0F0F0"
                                font.pixelSize: 13
                                font.bold: true
                                elide: Text.ElideRight
                            }
                        }

                        // 摘要内容
                        Text {
                            Layout.fillWidth: true
                            text: modelData.summary
                            color: "#9A9AA6"
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            visible: modelData.summary && modelData.summary.length > 0
                        }

                        // 来源与时间
                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: modelData.source + " · " + modelData.published
                                color: "#666677"
                                font.pixelSize: 11
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: "查看原文 ↗"
                                color: itemMouse.containsMouse ? "#40A9FF" : "#555566"
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }
    }
}
