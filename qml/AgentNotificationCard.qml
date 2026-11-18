import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    objectName: "agentCardWindow"
    title: "AgentCard"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"
    width: 440
    height: Math.min(540, cardBg.implicitHeight)
    visible: false

    property string agentId: ""
    property string agentName: ""
    property string agentType: "curriculum"
    property string cardTitle: "C++ 核心知识点推送"
    property string stage: "第一阶段：底层内存基石"
    property string summary: ""
    property int day: 1
    property int totalDays: 30
    property string stars: "⭐"
    property string codeSnippet: ""
    property string pitfall: ""
    property string notePath: ""
    property var keyPoints: []
    property bool isCompleted: false

    onVisibleChanged: {
        if (visible) {
            updatePosition()
        }
    }

    function updatePosition() {
        var scrW = (root.screen ? root.screen.desktopAvailableWidth : Screen.desktopAvailableWidth)
        var scrH = (root.screen ? root.screen.desktopAvailableHeight : Screen.desktopAvailableHeight)
        if (scrW <= 0) scrW = Screen.width
        if (scrH <= 0) scrH = Screen.height

        root.x = scrW - root.width - 24
        root.y = scrH - root.height - 24
    }

    Connections {
        target: bridge
        function onAgentNotificationTriggered(data) {
            root.agentId = data.agentId || ""
            root.agentName = data.agentName || ""
            root.agentType = data.agentType || "curriculum"
            root.cardTitle = data.title || "任务提醒"
            root.stage = data.stage || ""
            root.summary = data.summary || ""
            root.day = data.day || 1
            root.totalDays = data.totalDays || 30
            root.stars = data.stars || "⭐"
            root.codeSnippet = data.codeSnippet || ""
            root.pitfall = data.pitfall || ""
            root.notePath = data.notePath || ""
            root.keyPoints = data.keyPoints || []
            root.isCompleted = !!data.isCompleted

            root.visible = true
            root.updatePosition()
            root.raise()
            root.requestActivate()
        }
    }

    TextEdit {
        id: hiddenCopyBuffer
        visible: false
    }

    Rectangle {
        id: cardBg
        anchors.fill: parent
        radius: 10
        color: "#181824"
        border.color: (root.agentType === "curriculum") ? "#E5B842" : "#4D7CFE"
        border.width: 1
        implicitHeight: contentCol.implicitHeight + 28

        ColumnLayout {
            id: contentCol
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            // 顶部标题栏（支持整行拖拽窗口）
            Item {
                Layout.fillWidth: true
                height: 38

                MouseArea {
                    id: headerDragArea
                    anchors.fill: parent
                    property point clickPos: "0,0"
                    cursorShape: Qt.SizeAllCursor
                    onPressed: function(mouse) {
                        clickPos = Qt.point(mouse.x, mouse.y)
                    }
                    onPositionChanged: function(mouse) {
                        var delta = Qt.point(mouse.x - clickPos.x, mouse.y - clickPos.y)
                        root.x += delta.x
                        root.y += delta.y
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    spacing: 8

                    Text {
                        text: (root.agentType === "curriculum") ? "📚" : ((root.agentType === "command") ? "💻" : "⏰")
                        font.pixelSize: 18
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Column {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 2

                        Row {
                            spacing: 6
                            Text {
                                text: (root.agentId === "cpp_daily_master") ? "C++ 核心内功进阶" : root.agentName
                                color: (root.agentId === "cpp_daily_master") ? "#F5C842" : "#8AB4F8"
                                font.pixelSize: 11
                                font.bold: true
                            }
                            Rectangle {
                                visible: root.agentType === "curriculum" && !root.isCompleted
                                height: 16
                                radius: 3
                                color: "#2C2517"
                                border.color: "#80661A"
                                border.width: 1
                                implicitWidth: badgeText.implicitWidth + 8
                                Text {
                                    id: badgeText
                                    anchors.centerIn: parent
                                    text: (root.agentId === "cpp_daily_master") ? ("Day " + root.day + "/" + root.totalDays + " " + root.stars) : ("Day " + root.day + "/" + root.totalDays)
                                    color: "#FFD700"
                                    font.pixelSize: 9
                                    font.bold: true
                                }
                            }
                        }

                        Text {
                            text: root.cardTitle
                            color: "#FFFFFF"
                            font.pixelSize: 13
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    // 拖拽手柄提示
                    Text {
                        text: "⋮⋮"
                        color: "#52546B"
                        font.pixelSize: 14
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // 优雅圆形主题关闭按钮
                    Rectangle {
                        id: closeBtn
                        width: 24
                        height: 24
                        radius: 12
                        color: closeMouse.containsMouse ? "#E84A5F" : "#252636"
                        border.color: closeMouse.containsMouse ? "#FF6B6B" : "#3E4158"
                        border.width: 1
                        Layout.alignment: Qt.AlignVCenter

                        Behavior on color { ColorAnimation { duration: 150 } }
                        Behavior on border.color { ColorAnimation { duration: 150 } }

                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            color: closeMouse.containsMouse ? "#FFFFFF" : "#A2A4BA"
                            font.pixelSize: 11
                            font.bold: true
                        }

                        MouseArea {
                            id: closeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.visible = false
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#2B2C3D"
            }

            // 中间可滚动内容区域
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.maximumHeight: 380
                contentHeight: detailCol.implicitHeight
                clip: true

                ColumnLayout {
                    id: detailCol
                    width: parent.width
                    spacing: 8

                    // 核心概述
                    Rectangle {
                        Layout.fillWidth: true
                        radius: 6
                        color: "#202130"
                        border.color: "#303244"
                        border.width: 1
                        implicitHeight: summaryText.implicitHeight + 14

                        Text {
                            id: summaryText
                            anchors.fill: parent
                            anchors.margins: 7
                            text: root.summary
                            color: "#D8D9E6"
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }
                    }

                    // 知识要点 (如有)
                    Repeater {
                        model: root.keyPoints
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: "•"
                                color: "#E5B842"
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignTop
                            }
                            Text {
                                text: modelData
                                color: "#B8B9CC"
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // 典型代码示例 (如有)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: root.codeSnippet.length > 0

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "💻 经典精辟代码"
                                color: "#A6A7B8"
                                font.pixelSize: 10
                                font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                id: copyTextLbl
                                property bool copied: false
                                text: copied ? "✓ 已复制代码" : "📋 复制代码"
                                color: copied ? "#52C41A" : "#8AB4F8"
                                font.pixelSize: 10
                                MouseArea {
                                    id: copyMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        hiddenCopyBuffer.text = root.codeSnippet
                                        hiddenCopyBuffer.selectAll()
                                        hiddenCopyBuffer.copy()
                                        copyTextLbl.copied = true
                                        resetCopyTimer.restart()
                                    }
                                }
                                Timer {
                                    id: resetCopyTimer
                                    interval: 2000
                                    onTriggered: copyTextLbl.copied = false
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 4
                            color: "#0F1017"
                            border.color: "#252636"
                            border.width: 1
                            implicitHeight: codeText.implicitHeight + 14

                            Text {
                                id: codeText
                                anchors.fill: parent
                                anchors.margins: 7
                                text: root.codeSnippet
                                color: "#7EE787"
                                font.family: "Consolas, 'Courier New', monospace"
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    // 避坑与面试题 (如有)
                    Rectangle {
                        Layout.fillWidth: true
                        radius: 6
                        color: "#2A1F16"
                        border.color: "#593D22"
                        border.width: 1
                        visible: root.pitfall.length > 0
                        implicitHeight: pitfallText.implicitHeight + 14

                        Text {
                            id: pitfallText
                            anchors.fill: parent
                            anchors.margins: 7
                            text: "⚠️ 避坑/面试重点：\n" + root.pitfall
                            color: "#FFB86C"
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#2B2C3D"
            }

            // 底部操作按钮栏
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                // 内部查阅笔记 (不打开外部文件管理器)
                Button {
                    visible: (root.notePath && root.notePath.length > 0)
                    text: "📖 内部查阅笔记"
                    font.pixelSize: 11
                    contentItem: Text {
                        text: parent.text
                        color: "#FFFFFF"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? "#3B3D54" : "#2A2B3D"
                        border.color: "#4A4D6B"
                        border.width: 1
                    }
                    onClicked: {
                        var fileName = ""
                        if (root.notePath && root.notePath.length > 0) {
                            fileName = root.notePath.replace(/^.*[\\\/]/, "")
                        }
                        if (bridge) {
                            bridge.openAgentWindowWithNote(fileName)
                            root.visible = false
                        }
                    }
                }

                // AI 智能问答与延伸
                Button {
                    visible: (root.agentId === "cpp_daily_master") || (root.summary && root.summary.length > 0)
                    text: (root.agentId === "cpp_daily_master") ? "🤖 AI 延伸问答" : "🤖 调用 AI 深度解答"
                    font.pixelSize: 11
                    contentItem: Text {
                        text: parent.text
                        color: "#93C5FD"
                        font.pixelSize: 11
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? "#1E2B47" : "#141C30"
                        border.color: "#355088"
                        border.width: 1
                    }
                    onClicked: {
                        if (bridge) {
                            if (root.agentId === "cpp_daily_master") {
                                var ctx = "【课题】" + root.cardTitle + " (阶段: " + root.stage + "，难度: " + root.stars + ")\n【核心概述】" + root.summary
                                bridge.openAgentChatWindow("请针对该核心考点，深入解析其底层内存模型与编译器实现机制，并给出 2 个工业级经典踩坑避坑案例。", ctx)
                            } else {
                                var askPrompt = root.summary
                                if (root.agentType === "curriculum") {
                                    askPrompt = "【" + root.agentName + " · 第 " + root.day + " 天任务规划】\n" + root.summary + "\n\n请针对今日学习目标与任务要求，给出深入浅出的知识解析、关键落地步骤、核心代码示例与拓展实践。"
                                }
                                bridge.openAgentChatWindow(askPrompt, "【任务】" + root.agentName)
                            }
                        }
                    }
                }

                // 推进下一天 (针对课程类)
                Button {
                    visible: root.agentType === "curriculum" && !root.isCompleted
                    text: "✓ 推进下一天"
                    font.pixelSize: 11
                    contentItem: Text {
                        text: parent.text
                        color: "#FFD700"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? "#4D3B18" : "#332710"
                        border.color: "#80661A"
                        border.width: 1
                    }
                    onClicked: {
                        if (bridge && root.agentId.length > 0) {
                            bridge.advanceCurriculumDay(root.agentId)
                            root.visible = false
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // 打开任务管理中心
                Button {
                    text: "⚙️ 任务中心"
                    font.pixelSize: 11
                    contentItem: Text {
                        text: parent.text
                        color: "#8AB4F8"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? "#2C3447" : "#1E2433"
                        border.color: "#3D4B66"
                        border.width: 1
                    }
                    onClicked: {
                        root.visible = false
                        if (bridge) bridge.openAgentWindow()
                    }
                }

                // 知道了 (关闭提醒)
                Button {
                    text: "知道了"
                    font.pixelSize: 11
                    font.bold: true
                    contentItem: Text {
                        text: parent.text
                        color: "#16161A"
                        font.pixelSize: 11
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? "#F5C842" : "#E5B842"
                    }
                    onClicked: {
                        root.visible = false
                    }
                }
            }
        }
    }
}
