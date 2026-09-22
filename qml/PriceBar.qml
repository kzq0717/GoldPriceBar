import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    objectName: "priceBarWindow"
    title: "GoldPriceBar"
    width: Math.max(260, mainRow.implicitWidth + 22)
    height: 42
    minimumWidth: 260
    maximumHeight: 42
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"
    visible: true
    opacity: bridge ? bridge.windowOpacity : 0.95

    Behavior on width {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    property point clickPos: Qt.point(0, 0)

    onXChanged: {
        if (decisionCardWindow.visible) updateDecisionCardPos()
        if (alertCardWindow.visible) updateAlertCardPos()
        if (statusCardWindow.visible) updateStatusCardPos()
        if (secCardWindow.visible) updateSecCardPos()
    }
    onYChanged: {
        if (decisionCardWindow.visible) updateDecisionCardPos()
        if (alertCardWindow.visible) updateAlertCardPos()
        if (statusCardWindow.visible) updateStatusCardPos()
        if (secCardWindow.visible) updateSecCardPos()
    }
    onVisibleChanged: {
        if (!visible) {
            decisionCardWindow.visible = false
            alertCardWindow.visible = false
            statusCardWindow.visible = false
            secCardWindow.visible = false
        }
    }

    function updateDecisionCardPos() {
        if (!decisionTag.visible) return
        var pt = decisionTag.mapToItem(null, 0, 0)
        var gx = root.x + pt.x
        var gy = root.y + pt.y
        var cardW = decisionCardWindow.width
        var cardH = Math.max(80, decisionCardContent.implicitHeight + 24)
        decisionCardWindow.height = cardH

        var targetX = gx + (decisionTag.width - cardW) / 2
        var scrW = (root.screen ? root.screen.desktopAvailableWidth : Screen.desktopAvailableWidth)
        var scrH = (root.screen ? root.screen.desktopAvailableHeight : Screen.desktopAvailableHeight)
        if (scrW <= 0) scrW = Screen.width
        if (scrH <= 0) scrH = Screen.height

        targetX = Math.max(12, Math.min(scrW - cardW - 12, targetX))

        var targetY = gy + decisionTag.height + 6
        if (targetY + cardH > scrH - 12) {
            targetY = gy - cardH - 6
        }
        decisionCardWindow.x = targetX
        decisionCardWindow.y = targetY
    }

    function updateAlertCardPos() {
        if (!alertDot.visible) return
        var pt = alertDot.mapToItem(null, 0, 0)
        var gx = root.x + pt.x
        var gy = root.y + pt.y
        var cardW = alertCardWindow.width
        var cardH = Math.max(60, alertCardContent.implicitHeight + 24)
        alertCardWindow.height = cardH

        var targetX = gx + (alertDot.width - cardW) / 2
        var scrW = (root.screen ? root.screen.desktopAvailableWidth : Screen.desktopAvailableWidth)
        var scrH = (root.screen ? root.screen.desktopAvailableHeight : Screen.desktopAvailableHeight)
        if (scrW <= 0) scrW = Screen.width
        if (scrH <= 0) scrH = Screen.height

        targetX = Math.max(12, Math.min(scrW - cardW - 12, targetX))

        var targetY = gy + alertDot.height + 6
        if (targetY + cardH > scrH - 12) {
            targetY = gy - cardH - 6
        }
        alertCardWindow.x = targetX
        alertCardWindow.y = targetY
    }

    function updateStatusCardPos() {
        if (!statusDot.visible) return
        var pt = statusDot.mapToItem(null, 0, 0)
        var gx = root.x + pt.x
        var gy = root.y + pt.y
        var cardW = statusCardWindow.width
        var cardH = Math.max(70, statusCardContent.implicitHeight + 20)
        statusCardWindow.height = cardH

        var targetX = gx + (statusDot.width - cardW) / 2
        var scrW = (root.screen ? root.screen.desktopAvailableWidth : Screen.desktopAvailableWidth)
        var scrH = (root.screen ? root.screen.desktopAvailableHeight : Screen.desktopAvailableHeight)
        if (scrW <= 0) scrW = Screen.width
        if (scrH <= 0) scrH = Screen.height

        targetX = Math.max(12, Math.min(scrW - cardW - 12, targetX))

        var targetY = gy + statusDot.height + 6
        if (targetY + cardH > scrH - 12) {
            targetY = gy - cardH - 6
        }
        statusCardWindow.x = targetX
        statusCardWindow.y = targetY
    }

    function updateSecCardPos() {
        if (!secTag.visible) return
        var pt = secTag.mapToItem(null, 0, 0)
        var gx = root.x + pt.x
        var gy = root.y + pt.y
        var cardW = secCardWindow.width
        var cardH = Math.max(65, secCardContent.implicitHeight + 20)
        secCardWindow.height = cardH

        var targetX = gx + (secTag.width - cardW) / 2
        var scrW = (root.screen ? root.screen.desktopAvailableWidth : Screen.desktopAvailableWidth)
        var scrH = (root.screen ? root.screen.desktopAvailableHeight : Screen.desktopAvailableHeight)
        if (scrW <= 0) scrW = Screen.width
        if (scrH <= 0) scrH = Screen.height

        targetX = Math.max(12, Math.min(scrW - cardW - 12, targetX))

        var targetY = gy + secTag.height + 6
        if (targetY + cardH > scrH - 12) {
            targetY = gy - cardH - 6
        }
        secCardWindow.x = targetX
        secCardWindow.y = targetY
    }

    Timer {
        id: decisionShowTimer
        interval: 120
        repeat: false
        onTriggered: {
            if (decisionMouse.containsMouse) {
                updateDecisionCardPos()
                decisionCardWindow.visible = true
            }
        }
    }
    Timer {
        id: decisionHideTimer
        interval: 180
        repeat: false
        onTriggered: {
            if (!decisionMouse.containsMouse && !decisionCardMouse.containsMouse) {
                decisionCardWindow.visible = false
            }
        }
    }

    Timer {
        id: alertShowTimer
        interval: 150
        repeat: false
        onTriggered: {
            if (alertMouse.containsMouse) {
                updateAlertCardPos()
                alertCardWindow.visible = true
            }
        }
    }
    Timer {
        id: alertHideTimer
        interval: 180
        repeat: false
        onTriggered: {
            if (!alertMouse.containsMouse && !alertCardMouse.containsMouse) {
                alertCardWindow.visible = false
            }
        }
    }

    Timer {
        id: statusShowTimer
        interval: 120
        repeat: false
        onTriggered: {
            if (statusMouse.containsMouse) {
                updateStatusCardPos()
                statusCardWindow.visible = true
            }
        }
    }
    Timer {
        id: statusHideTimer
        interval: 180
        repeat: false
        onTriggered: {
            if (!statusMouse.containsMouse && !statusCardMouse.containsMouse) {
                statusCardWindow.visible = false
            }
        }
    }

    Timer {
        id: secShowTimer
        interval: 120
        repeat: false
        onTriggered: {
            if (secMouse.containsMouse) {
                updateSecCardPos()
                secCardWindow.visible = true
            }
        }
    }
    Timer {
        id: secHideTimer
        interval: 180
        repeat: false
        onTriggered: {
            if (!secMouse.containsMouse && !secCardMouse.containsMouse) {
                secCardWindow.visible = false
            }
        }
    }

    Connections {
        target: bridge
        function onRequestShowPriceBar() {
            root.visible = true
            root.raise()
            root.requestActivate()
        }
        function onRequestTogglePriceBar() {
            root.visible = !root.visible
            if (root.visible) {
                root.raise()
                root.requestActivate()
            }
        }
    }

    Rectangle {
        id: bgCard
        anchors.fill: parent
        radius: 8
        color: "#18181D"
        border.color: "#2C2D35"
        border.width: 1

        // 拖拽窗口移动与双击/右键交互
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: function(mouse) {
                if (mouse.button === Qt.LeftButton) {
                    clickPos = Qt.point(mouse.x, mouse.y)
                } else if (mouse.button === Qt.RightButton) {
                    contextMenu.popup()
                }
            }
            onPositionChanged: function(mouse) {
                if (mouse.buttons & Qt.LeftButton) {
                    var dx = mouse.x - clickPos.x
                    var dy = mouse.y - clickPos.y
                    root.x = root.x + dx
                    root.y = root.y + dy
                }
            }
            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton) {
                    bridge.openChartWindow()
                }
            }
        }

        RowLayout {
            id: mainRow
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            // 黄金金条图标 (品牌Logo) 与网络健康状态小圆点
            Row {
                spacing: 3
                Layout.alignment: Qt.AlignVCenter

                Text {
                    text: "🟡"
                    font.pixelSize: 13
                    Layout.alignment: Qt.AlignVCenter
                }

                // 网络状态灯（位于Logo旁，与伦敦价彻底解耦）
                Rectangle {
                    id: statusDot
                    width: 7
                    height: 7
                    radius: 3.5
                    color: {
                        if (!bridge) return "#888888"
                        if (bridge.statusLevel === 0) return "#52C41A" // 正常绿
                        if (bridge.statusLevel === 1) return "#FAAD14" // 刷新中黄
                        return "#FF4D4F" // 异常红
                    }
                    Layout.alignment: Qt.AlignVCenter

                    MouseArea {
                        id: statusMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: {
                            statusShowTimer.start()
                            statusHideTimer.stop()
                        }
                        onExited: {
                            statusShowTimer.stop()
                            statusHideTimer.start()
                        }
                        onClicked: {
                            bridge.refreshPrice()
                        }
                    }
                }
            }

            // 品种来源名称
            Text {
                text: (bridge && bridge.sourceName.length > 0) ? bridge.sourceName : "黄金行情"
                color: "#9A9AA6"
                font.pixelSize: 12
                font.bold: true
                Layout.alignment: Qt.AlignVCenter
            }

            // 实时现价
            Text {
                text: (bridge && bridge.hasValidPrice) ? bridge.price.toFixed(2) : "--.--"
                color: {
                    if (!bridge || !bridge.hasValidPrice) return "#CCCCCC"
                    if (bridge.priceChange > 0.0001) return "#FF4D4F" // 涨红
                    if (bridge.priceChange < -0.0001) return "#52C41A" // 跌绿
                    return "#E0E0E0"
                }
                font.pixelSize: 17
                font.bold: true
                Layout.alignment: Qt.AlignVCenter
            }

            // 涨跌额与涨跌幅
            Text {
                text: {
                    if (!bridge || !bridge.hasValidPrice) return "(0.00%)"
                    var sign = bridge.priceChange > 0.0001 ? "+" : ""
                    return sign + bridge.priceChange.toFixed(2) + " (" + sign + bridge.changePct.toFixed(2) + "%)"
                }
                color: {
                    if (!bridge || !bridge.hasValidPrice) return "#888888"
                    if (bridge.priceChange > 0.0001) return "#FF4D4F"
                    if (bridge.priceChange < -0.0001) return "#52C41A"
                    return "#9A9AA6"
                }
                font.pixelSize: 12
                Layout.alignment: Qt.AlignVCenter
            }

            // 对照基准行情（设置中勾选「显示国际金/基准对照价」时显示）
            Rectangle {
                id: secTag
                visible: bridge && bridge.showSecondaryPrice && bridge.secondaryPriceText.length > 0
                height: 20
                implicitWidth: secText.implicitWidth + 10
                radius: 3
                color: "#271C33"
                border.color: "#4C3368"
                border.width: 1
                Layout.alignment: Qt.AlignVCenter

                Text {
                    id: secText
                    anchors.centerIn: parent
                    text: bridge ? bridge.secondaryPriceText : ""
                    color: "#D3ADF7"
                    font.pixelSize: 11
                    font.bold: true
                }

                MouseArea {
                    id: secMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: {
                        secShowTimer.start()
                        secHideTimer.stop()
                    }
                    onExited: {
                        secShowTimer.stop()
                        secHideTimer.start()
                    }
                    onClicked: {
                        bridge.refreshPrice()
                    }
                }
            }

            // 本地持仓浮盈亏胶囊（设置中填入持仓克数与成本单价时显示）
            Rectangle {
                id: pnlTag
                visible: bridge && bridge.hasPosition && bridge.pnlText.length > 0
                height: 20
                implicitWidth: pnlTextItem.implicitWidth + 10
                radius: 3
                color: "#16201B"
                border.color: (bridge && bridge.pnlColor === "#FF4D4F") ? "#4C2224" : "#214C29"
                border.width: 1
                Layout.alignment: Qt.AlignVCenter

                Text {
                    id: pnlTextItem
                    anchors.centerIn: parent
                    text: bridge ? bridge.pnlText : ""
                    color: bridge ? bridge.pnlColor : "#52C41A"
                    font.pixelSize: 11
                    font.bold: true
                }

                ToolTip.delay: 300
                ToolTip.visible: pnlMouse.containsMouse
                ToolTip.text: "本地持仓浮动盈亏（设置中已配置克数与成本）"

                MouseArea {
                    id: pnlMouse
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }

            // 最高 / 最低
            Row {
                spacing: 6
                Layout.alignment: Qt.AlignVCenter
                visible: bridge && bridge.hasValidPrice

                Text {
                    text: "高 " + (bridge ? bridge.highPrice.toFixed(2) : "0.00")
                    color: "#A68A56"
                    font.pixelSize: 11
                }
                Text {
                    text: "低 " + (bridge ? bridge.lowPrice.toFixed(2) : "0.00")
                    color: "#568AA6"
                    font.pixelSize: 11
                }
            }

            // 价格预警指示标点（布防待触发时常态显示暗色点，触发时红/绿高频闪烁）
            Rectangle {
                id: alertDot
                width: (bridge && bridge.alertKind !== 0) ? 9 : 8
                height: width
                radius: width / 2
                visible: bridge && (bridge.alertArmed || bridge.alertKind !== 0)
                color: {
                    if (!bridge) return "transparent"
                    if (bridge.alertKind === 1) return "#FF4D4F" // 高价闪红
                    if (bridge.alertKind === 2) return "#52C41A" // 低价闪绿
                    return "#2E2E3E" // 已布防平静状态
                }
                border.color: (bridge && bridge.alertKind !== 0) ? "transparent" : "#5C5C74"
                border.width: (bridge && bridge.alertKind !== 0) ? 0 : 1
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 1
                Layout.rightMargin: 1

                SequentialAnimation on opacity {
                    running: alertDot.visible && bridge && bridge.alertKind !== 0
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.15; duration: 450; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1.0; duration: 450; easing.type: Easing.InOutQuad }
                }

                MouseArea {
                    id: alertMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: {
                        alertShowTimer.start()
                        alertHideTimer.stop()
                    }
                    onExited: {
                        alertShowTimer.stop()
                        alertHideTimer.start()
                    }
                    onClicked: {
                        alertCardWindow.visible = false
                        bridge.openSettingsWindow()
                    }
                }
            }

            // 智能趋势与买卖决策胶囊徽章
            Rectangle {
                id: decisionTag
                visible: bridge && bridge.decisionText && bridge.decisionText.length > 0
                height: 20
                implicitWidth: decisionTextItem.implicitWidth + 12
                radius: 3
                color: bridge ? bridge.decisionBg : "#22222E"
                border.color: bridge ? bridge.decisionColor : "#444455"
                border.width: 1
                Layout.alignment: Qt.AlignVCenter

                Text {
                    id: decisionTextItem
                    anchors.centerIn: parent
                    text: bridge ? bridge.decisionText : ""
                    color: bridge ? bridge.decisionColor : "#A6A6B8"
                    font.pixelSize: 10
                    font.bold: true
                }

                MouseArea {
                    id: decisionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: {
                        decisionShowTimer.start()
                        decisionHideTimer.stop()
                    }
                    onExited: {
                        decisionShowTimer.stop()
                        decisionHideTimer.start()
                    }
                    onClicked: {
                        decisionCardWindow.visible = false
                        bridge.openChartWindow()
                    }
                }
            }

            // 微小固定间距
            Item {
                width: 3
                height: 1
            }

            // 快捷按钮组
            Row {
                spacing: 4
                Layout.alignment: Qt.AlignVCenter

                // 舆情按钮
                Rectangle {
                    id: btnSent
                    width: 28
                    height: 26
                    radius: 4
                    color: btnSentMouse.pressed ? "#3D3E54" : (btnSentMouse.containsMouse ? "#2C2D3A" : "transparent")
                    border.color: btnSentMouse.containsMouse ? "#4D4E62" : "transparent"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "📰"
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: btnSentMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            console.log("Sentiment button clicked")
                            bridge.openSentimentWindow()
                        }
                    }
                    ToolTip.delay: 600
                    ToolTip.timeout: 4000
                    ToolTip.visible: btnSentMouse.containsMouse
                    ToolTip.text: "黄金实时舆情与热点分析"
                }

                // 走势图按钮
                Rectangle {
                    id: btnChart
                    width: 28
                    height: 26
                    radius: 4
                    color: btnChartMouse.pressed ? "#3D3E54" : (btnChartMouse.containsMouse ? "#2C2D3A" : "transparent")
                    border.color: btnChartMouse.containsMouse ? "#4D4E62" : "transparent"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "📈"
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: btnChartMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            console.log("Chart button clicked")
                            bridge.openChartWindow()
                        }
                    }
                    ToolTip.delay: 600
                    ToolTip.timeout: 4000
                    ToolTip.visible: btnChartMouse.containsMouse
                    ToolTip.text: "分时走势图"
                }

                // 设置按钮
                Rectangle {
                    id: btnSet
                    width: 28
                    height: 26
                    radius: 4
                    color: btnSetMouse.pressed ? "#3D3E54" : (btnSetMouse.containsMouse ? "#2C2D3A" : "transparent")
                    border.color: btnSetMouse.containsMouse ? "#4D4E62" : "transparent"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "⚙️"
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: btnSetMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            console.log("Settings button clicked")
                            bridge.openSettingsWindow()
                        }
                    }
                    ToolTip.delay: 600
                    ToolTip.timeout: 4000
                    ToolTip.visible: btnSetMouse.containsMouse
                    ToolTip.text: "软件设置"
                }

                // 关闭(隐藏到托盘)按钮
                Rectangle {
                    id: btnClose
                    width: 28
                    height: 26
                    radius: 4
                    color: btnCloseMouse.pressed ? "#662222" : (btnCloseMouse.containsMouse ? "#4D2222" : "transparent")
                    border.color: btnCloseMouse.containsMouse ? "#773333" : "transparent"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: btnCloseMouse.containsMouse ? "#FF6B6B" : "#888888"
                        font.pixelSize: 12
                        font.bold: true
                    }
                    MouseArea {
                        id: btnCloseMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.visible = false
                        }
                    }
                    ToolTip.delay: 600
                    ToolTip.timeout: 4000
                    ToolTip.visible: btnCloseMouse.containsMouse
                    ToolTip.text: "隐藏到系统托盘"
                }
            }
        }
    }

    // 悬浮全景决策卡片窗口（彻底解决小尺寸价格条 ToolTip 边界遮挡问题）
    Window {
        id: decisionCardWindow
        flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
        transientParent: root
        width: 380
        height: Math.max(90, decisionCardContent.implicitHeight + 24)
        color: "transparent"
        visible: false

        Rectangle {
            id: decisionCardBg
            anchors.fill: parent
            radius: 8
            color: "#161622"
            border.color: (bridge && bridge.decisionColor) ? bridge.decisionColor : "#44445A"
            border.width: 1

            ColumnLayout {
                id: decisionCardContent
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                // Header Row: Title & Badge
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "💡 实时趋势与交易决策"
                        color: "#FFFFFF"
                        font.pixelSize: 12
                        font.bold: true
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        radius: 3
                        color: bridge ? bridge.decisionBg : "#22222E"
                        border.color: bridge ? bridge.decisionColor : "#444455"
                        border.width: 1
                        implicitWidth: headerTagText.implicitWidth + 8
                        implicitHeight: 18

                        Text {
                            id: headerTagText
                            anchors.centerIn: parent
                            text: bridge ? bridge.decisionText : ""
                            color: bridge ? bridge.decisionColor : "#A6A6B8"
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }
                }

                // Operation Advice Block: Highlight box
                Rectangle {
                    Layout.fillWidth: true
                    radius: 5
                    color: bridge ? bridge.decisionBg : "#22222E"
                    border.color: bridge ? bridge.decisionColor : "#3A3A4C"
                    border.width: 1
                    implicitHeight: adviceCol.implicitHeight + 14

                    ColumnLayout {
                        id: adviceCol
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 3

                        Text {
                            text: "【操作建议】"
                            color: bridge ? bridge.decisionColor : "#E0E0E0"
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: bridge ? bridge.decisionAdvice : ""
                            color: "#F5F5FA"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            lineHeight: 1.28
                        }
                    }
                }

                // Key-Value Metric Rows
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    // 当日预测区间
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: "🎯 当日预测区间："
                            color: "#9A9AB0"
                            font.pixelSize: 10
                        }
                        Text {
                            text: bridge ? bridge.decisionRangeText : "--"
                            color: "#FFFFFF"
                            font.pixelSize: 10
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    // 极值时间窗口
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: "⏳ 极值时间窗口："
                            color: "#9A9AB0"
                            font.pixelSize: 10
                        }
                        Text {
                            text: bridge ? bridge.decisionTimeWindowText : "--"
                            color: "#7EB6FF"
                            font.pixelSize: 10
                            font.bold: true
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }


                    // 高点时段概率 / 已现概率 / 剩余上行 / 多日趋势
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: "📊 高点概率："
                            color: "#9A9AB0"
                            font.pixelSize: 10
                        }
                        Text {
                            text: bridge ? bridge.peakProbText : "—"
                            color: "#FFB74D"
                            font.pixelSize: 10
                            font.bold: true
                        }
                        Text {
                            text: "已现："
                            color: "#9A9AB0"
                            font.pixelSize: 10
                        }
                        Text {
                            text: bridge ? bridge.highInProbText : "—"
                            color: "#CE93D8"
                            font.pixelSize: 10
                            font.bold: true
                        }
                        Text {
                            text: "剩余上行："
                            color: "#9A9AB0"
                            font.pixelSize: 10
                        }
                        Text {
                            text: bridge ? bridge.remainingUpsideText : "—"
                            color: "#81C784"
                            font.pixelSize: 10
                            font.bold: true
                            Layout.fillWidth: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: "📈 多日趋势："
                            color: "#9A9AB0"
                            font.pixelSize: 10
                        }
                        Text {
                            text: bridge ? bridge.multiDayBiasText : "—"
                            color: "#64B5F6"
                            font.pixelSize: 10
                            font.bold: true
                        }
                        Text {
                            text: "置信："
                            color: "#9A9AB0"
                            font.pixelSize: 10
                        }
                        Text {
                            text: bridge ? bridge.forecastConfidenceText : "—"
                            color: "#FFD54F"
                            font.pixelSize: 10
                            font.bold: true
                            Layout.fillWidth: true
                        }
                    }

                    // 核心推演催化
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: bridge && (bridge.predCatalyst !== "" || bridge.predDailyPath !== "")
                        Text {
                            text: "🔥 核心推演催化："
                            color: "#9A9AB0"
                            font.pixelSize: 10
                        }
                        Text {
                            text: bridge ? (bridge.predCatalyst !== "" ? bridge.predCatalyst : bridge.predDailyPath) : "--"
                            color: "#F5D565"
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    // 均线与分位
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: "📊 均线与分位："
                            color: "#9A9AB0"
                            font.pixelSize: 10
                        }
                        Text {
                            text: bridge ? bridge.decisionMetricsText : "--"
                            color: "#D0D0E0"
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    // 时段状态
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: "⏰ 交易时段："
                            color: "#9A9AB0"
                            font.pixelSize: 10
                        }
                        Text {
                            text: bridge ? bridge.decisionSessionText : "--"
                            color: "#B0B0C4"
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                }

                // Divider line
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: "#282838"
                }

                // Footer Hint
                Text {
                    text: "👉 点击徽章可直接打开分时走势与推演图"
                    color: "#7E7E94"
                    font.pixelSize: 9
                    Layout.fillWidth: true
                }
            }

            MouseArea {
                id: decisionCardMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onExited: {
                    decisionHideTimer.start()
                }
                onClicked: {
                    decisionCardWindow.visible = false
                    bridge.openChartWindow()
                }
            }
        }
    }

    // 悬浮预警状态卡片窗口
    Window {
        id: alertCardWindow
        flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
        transientParent: root
        width: 260
        height: Math.max(60, alertCardContent.implicitHeight + 20)
        color: "transparent"
        visible: false

        Rectangle {
            id: alertCardBg
            anchors.fill: parent
            radius: 6
            color: "#161622"
            border.color: (bridge && bridge.alertKind !== 0) ? (bridge.alertKind === 1 ? "#FF4D4F" : "#52C41A") : "#4A4A62"
            border.width: 1

            ColumnLayout {
                id: alertCardContent
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                Text {
                    text: (bridge && bridge.alertKind !== 0) ? "🚨 价格预警已触发" : "🛡 价格预警布防中·待触发"
                    color: (bridge && bridge.alertKind !== 0) ? (bridge.alertKind === 1 ? "#FF4D4F" : "#52C41A") : "#B0B0C8"
                    font.pixelSize: 11
                    font.bold: true
                }

                Text {
                    Layout.fillWidth: true
                    text: bridge ? bridge.alertTooltip : ""
                    color: "#DCDCE8"
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    lineHeight: 1.22
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: "#2E2E40"
                }

                Text {
                    text: "👉 点击打开设置中心调整预警阈值"
                    color: "#7E7E94"
                    font.pixelSize: 9
                }
            }

            MouseArea {
                id: alertCardMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onExited: {
                    alertHideTimer.start()
                }
                onClicked: {
                    alertCardWindow.visible = false
                    bridge.openSettingsWindow()
                }
            }
        }
    }

    // 悬浮网络与数据源状态卡片窗口（解决小尺寸价格条 ToolTip 遮挡问题）
    Window {
        id: statusCardWindow
        flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
        transientParent: root
        width: 260
        height: Math.max(70, statusCardContent.implicitHeight + 20)
        color: "transparent"
        visible: false

        Rectangle {
            id: statusCardBg
            anchors.fill: parent
            radius: 6
            color: "#161622"
            border.color: {
                if (!bridge) return "#44445A"
                if (bridge.statusLevel === 0) return "#2C5E35"
                if (bridge.statusLevel === 1) return "#6B511A"
                return "#702324"
            }
            border.width: 1

            ColumnLayout {
                id: statusCardContent
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: "🌐 行情网络与连接状态"
                        color: "#FFFFFF"
                        font.pixelSize: 11
                        font.bold: true
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        radius: 3
                        color: {
                            if (!bridge) return "#22222E"
                            if (bridge.statusLevel === 0) return "#132B18"
                            if (bridge.statusLevel === 1) return "#2B2213"
                            return "#2B1313"
                        }
                        border.color: {
                            if (!bridge) return "#444455"
                            if (bridge.statusLevel === 0) return "#52C41A"
                            if (bridge.statusLevel === 1) return "#FAAD14"
                            return "#FF4D4F"
                        }
                        border.width: 1
                        implicitWidth: statusBadgeText.implicitWidth + 8
                        implicitHeight: 18

                        Text {
                            id: statusBadgeText
                            anchors.centerIn: parent
                            text: {
                                if (!bridge) return "未知"
                                if (bridge.statusLevel === 0) return "正常"
                                if (bridge.statusLevel === 1) return "刷新中"
                                return "异常"
                            }
                            color: {
                                if (!bridge) return "#A6A6B8"
                                if (bridge.statusLevel === 0) return "#52C41A"
                                if (bridge.statusLevel === 1) return "#FAAD14"
                                return "#FF4D4F"
                            }
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        text: "📡 数据来源："
                        color: "#9A9AB0"
                        font.pixelSize: 10
                    }
                    Text {
                        text: (bridge && bridge.sourceName.length > 0) ? bridge.sourceName : "黄金行情"
                        color: "#FFFFFF"
                        font.pixelSize: 10
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        text: "⏱️ 更新时间："
                        color: "#9A9AB0"
                        font.pixelSize: 10
                    }
                    Text {
                        text: (bridge && bridge.updateTime.length > 0) ? bridge.updateTime : "--:--:--"
                        color: "#D0D0E0"
                        font.pixelSize: 10
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    visible: bridge && bridge.showSecondaryPrice
                    Text {
                        text: "⚖️ 伦敦/对照："
                        color: "#9A9AB0"
                        font.pixelSize: 10
                    }
                    Text {
                        text: (bridge && bridge.secondaryPriceText.length > 0) ? (bridge.secondaryPriceText + " (实时)") : "等待更新..."
                        color: "#D3ADF7"
                        font.pixelSize: 10
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: "#2E2E40"
                }

                Text {
                    text: "👉 点击指示灯或卡片立即强制刷新行情"
                    color: "#7E7E94"
                    font.pixelSize: 9
                    Layout.fillWidth: true
                }
            }

            MouseArea {
                id: statusCardMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onExited: {
                    statusHideTimer.start()
                }
                onClicked: {
                    statusCardWindow.visible = false
                    bridge.refreshPrice()
                }
            }
        }
    }

    // 悬浮对照基准行情卡片窗口（解决小尺寸价格条 ToolTip 遮挡问题）
    Window {
        id: secCardWindow
        flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
        transientParent: root
        width: 270
        height: Math.max(70, secCardContent.implicitHeight + 20)
        color: "transparent"
        visible: false

        Rectangle {
            id: secCardBg
            anchors.fill: parent
            radius: 6
            color: "#161622"
            border.color: "#4C3368"
            border.width: 1

            ColumnLayout {
                id: secCardContent
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: "⚖️ 对照基准行情"
                        color: "#D3ADF7"
                        font.pixelSize: 11
                        font.bold: true
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        radius: 3
                        color: "#271C33"
                        border.color: "#6B3FA0"
                        border.width: 1
                        implicitWidth: secBadgeText.implicitWidth + 8
                        implicitHeight: 18

                        Text {
                            id: secBadgeText
                            anchors.centerIn: parent
                            text: (bridge && bridge.secondaryPriceText.length > 0) ? bridge.secondaryPriceText : "对照中"
                            color: "#E2CFFF"
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        text: "📌 境内品种现价："
                        color: "#9A9AB0"
                        font.pixelSize: 10
                    }
                    Text {
                        text: (bridge && bridge.hasValidPrice) ? (bridge.sourceName + " " + bridge.price.toFixed(2)) : "--.--"
                        color: "#FFFFFF"
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "用于实时监测国际伦敦金（XAU）与境内积存金的联动走势及内外盘价差。"
                    color: "#A0A0B4"
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                    lineHeight: 1.2
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: "#2E2E40"
                }

                Text {
                    text: "👉 点击卡片可同步刷新基准与境内行情"
                    color: "#7E7E94"
                    font.pixelSize: 9
                    Layout.fillWidth: true
                }
            }

            MouseArea {
                id: secCardMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onExited: {
                    secHideTimer.start()
                }
                onClicked: {
                    secCardWindow.visible = false
                    bridge.refreshPrice()
                }
            }
        }
    }

    // 右键上下文菜单
    Menu {
        id: contextMenu
        MenuItem {
            text: "📰 黄金实时舆情"
            onTriggered: bridge.openSentimentWindow()
        }
        MenuItem {
            text: "📈 分时走势图"
            onTriggered: bridge.openChartWindow()
        }
        MenuItem {
            text: "🔄 强制刷新行情"
            onTriggered: bridge.refreshPrice()
        }
        MenuItem {
            text: "⚙️ 软件设置"
            onTriggered: bridge.openSettingsWindow()
        }
        MenuSeparator {}
        MenuItem {
            text: "❌ 退出程序"
            onTriggered: bridge.quitApp()
        }
    }
}
