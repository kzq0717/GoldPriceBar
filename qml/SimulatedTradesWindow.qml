import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: simTradesWindow
    objectName: "simulatedTradesWindow"
    title: "决策模拟交易与持仓明细 - GoldPriceBar"
    width: 860
    height: 560
    minimumWidth: 720
    minimumHeight: 460
    visible: false
    color: "#12121A"

    property var tradeModel: []

    function reloadTrades() {
        if (bridge) {
            tradeModel = bridge.getSimulatedTrades(200)
        }
    }

    onVisibleChanged: {
        if (visible) {
            reloadTrades()
        }
    }

    Connections {
        target: bridge
        function onSimulatedTradesUpdated() {
            reloadTrades()
        }
        function onPriceChanged() {
            // keep live prices updated
        }
        function onPositionChanged() {
            reloadTrades()
        }
        function onRequestShowSimulatedTrades() {
            simTradesWindow.visible = true
            simTradesWindow.raise()
            simTradesWindow.requestActivate()
            reloadTrades()
        }
        function onRequestCloseSimulatedTrades() {
            simTradesWindow.visible = false
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // Top Header
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "💼 决策算法模拟交易与持仓明细"
                color: "#FFFFFF"
                font.pixelSize: 16
                font.bold: true
            }

            Rectangle {
                radius: 4
                color: (bridge && bridge.autoSimulateTrading) ? "#163219" : "#2A2A38"
                border.color: (bridge && bridge.autoSimulateTrading) ? "#52C41A" : "#44445A"
                border.width: 1
                implicitHeight: 20
                implicitWidth: statusTextItem.implicitWidth + 10

                Text {
                    id: statusTextItem
                    anchors.centerIn: parent
                    text: (bridge && bridge.autoSimulateTrading) ? "🤖 自动联动中" : "⏸️ 手动模式"
                    color: (bridge && bridge.autoSimulateTrading) ? "#52C41A" : "#A6A6B8"
                    font.pixelSize: 10
                    font.bold: true
                }
            }

            Item { Layout.fillWidth: true }

            // Close button
            Rectangle {
                width: 28
                height: 28
                radius: 14
                color: closeMouse.containsMouse ? "#3A3A4C" : "#22222E"

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    color: "#D0D0E0"
                    font.pixelSize: 12
                }

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: simTradesWindow.close()
                }
            }
        }

        // Summary Metric Cards (5 Cards)
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            // 1. 持仓克数
            Rectangle {
                Layout.fillWidth: true
                height: 58
                radius: 6
                color: "#181826"
                border.color: "#2C2C40"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    Text { text: "当前持仓"; color: "#8E8EA6"; font.pixelSize: 10 }
                    Text {
                        text: (bridge ? bridge.positionGrams.toFixed(2) : "0.00") + " g"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
            }

            // 2. 成本均价
            Rectangle {
                Layout.fillWidth: true
                height: 58
                radius: 6
                color: "#181826"
                border.color: "#2C2C40"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    Text { text: "成本均价"; color: "#8E8EA6"; font.pixelSize: 10 }
                    Text {
                        text: (bridge ? bridge.positionCost.toFixed(2) : "0.00") + " 元/g"
                        color: "#E2E2F0"
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
            }

            // 3. 当前现价
            Rectangle {
                Layout.fillWidth: true
                height: 58
                radius: 6
                color: "#181826"
                border.color: "#2C2C40"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    Text { text: "当前现价"; color: "#8E8EA6"; font.pixelSize: 10 }
                    Text {
                        text: (bridge && bridge.price > 0 ? bridge.price.toFixed(2) : "--") + " 元/g"
                        color: "#FFD591"
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
            }

            // 4. 浮动盈亏
            Rectangle {
                Layout.fillWidth: true
                height: 58
                radius: 6
                color: "#181826"
                border.color: "#2C2C40"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    Text { text: "持仓浮动盈亏"; color: "#8E8EA6"; font.pixelSize: 10 }
                    Text {
                        text: (bridge && bridge.hasPosition && bridge.pnlText.length > 0) ? bridge.pnlText : "无持仓"
                        color: (bridge && bridge.hasPosition) ? bridge.pnlColor : "#8E8EA6"
                        font.pixelSize: 13
                        font.bold: true
                    }
                }
            }

            // 5. 累计平仓盈亏
            Rectangle {
                Layout.fillWidth: true
                height: 58
                radius: 6
                color: "#181826"
                border.color: "#2C2C40"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    Text { text: "累计已平仓盈亏"; color: "#8E8EA6"; font.pixelSize: 10 }
                    Text {
                        property double rPnl: bridge ? bridge.totalRealizedPnl : 0.0
                        text: (rPnl >= 0 ? "+" : "") + rPnl.toFixed(2) + " 元"
                        color: rPnl >= 0 ? "#FF4D4F" : "#52C41A"
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
            }
        }

        // Action Toolbar
        Rectangle {
            Layout.fillWidth: true
            height: 44
            radius: 6
            color: "#161622"
            border.color: "#2C2C3D"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 12

                // Auto trade checkbox (自定义暗黑精美复选框，解决原生 CheckBox 布局偏移与文字遮挡问题)
                Item {
                    implicitWidth: chkRow.implicitWidth
                    implicitHeight: 26
                    Layout.alignment: Qt.AlignVCenter

                    Row {
                        id: chkRow
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7

                        Rectangle {
                            id: chkBoxSquare
                            width: 16
                            height: 16
                            radius: 3
                            color: (bridge && bridge.autoSimulateTrading) ? "#16381B" : (chkMouse.containsMouse ? "#2A2A3C" : "#1F1F2B")
                            border.color: (bridge && bridge.autoSimulateTrading) ? "#52C41A" : (chkMouse.containsMouse ? "#6E6E88" : "#4A4A60")
                            border.width: 1
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                visible: bridge && bridge.autoSimulateTrading
                                text: "✓"
                                color: "#52C41A"
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }

                        Text {
                            text: "🤖 决策算法联动自动买卖"
                            color: (bridge && bridge.autoSimulateTrading) ? "#52C41A" : (chkMouse.containsMouse ? "#FFFFFF" : "#C0C0D4")
                            font.pixelSize: 11
                            font.bold: bridge && bridge.autoSimulateTrading
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: chkMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (bridge) bridge.autoSimulateTrading = !bridge.autoSimulateTrading
                        }
                    }
                }

                // Batch grams input
                RowLayout {
                    spacing: 4
                    Text { text: "单笔克数:"; color: "#9A9AB0"; font.pixelSize: 11 }
                    Rectangle {
                        width: 55
                        height: 26
                        radius: 4
                        color: "#222230"
                        border.color: "#3E3E52"
                        border.width: 1

                        TextInput {
                            id: txtBatchGrams
                            anchors.fill: parent
                            anchors.margins: 4
                            color: "#FFFFFF"
                            font.pixelSize: 11
                            text: (bridge && bridge.simulateTradeBatchGrams > 0) ? bridge.simulateTradeBatchGrams.toFixed(1) : "10.0"
                            selectByMouse: true
                            onEditingFinished: {
                                var val = parseFloat(text)
                                if (val > 0 && bridge) {
                                    bridge.simulateTradeBatchGrams = val
                                }
                            }
                        }
                    }
                    Text { text: "g"; color: "#9A9AB0"; font.pixelSize: 11 }
                }

                Item { Layout.fillWidth: true }

                // Buy button
                Rectangle {
                    width: 90
                    height: 28
                    radius: 4
                    color: buyBtnMouse.containsMouse ? "#237804" : "#163219"
                    border.color: "#52C41A"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "🟢 模拟买入"
                        color: "#52C41A"
                        font.pixelSize: 11
                        font.bold: true
                    }

                    MouseArea {
                        id: buyBtnMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            var g = parseFloat(txtBatchGrams.text) || 10.0
                            if (bridge) bridge.executeSimulatedTrade("BUY", g, "手动执行模拟买入")
                        }
                    }
                }

                // Sell button
                Rectangle {
                    width: 90
                    height: 28
                    radius: 4
                    color: (bridge && bridge.positionGrams > 0) ? (sellBtnMouse.containsMouse ? "#A8071A" : "#3E1C1F") : "#22222E"
                    border.color: (bridge && bridge.positionGrams > 0) ? "#FF7875" : "#444455"
                    border.width: 1
                    opacity: (bridge && bridge.positionGrams > 0) ? 1.0 : 0.45

                    Text {
                        anchors.centerIn: parent
                        text: "🔴 模拟卖出"
                        color: (bridge && bridge.positionGrams > 0) ? "#FF7875" : "#888899"
                        font.pixelSize: 11
                        font.bold: true
                    }

                    MouseArea {
                        id: sellBtnMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: (bridge && bridge.positionGrams > 0) ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            if (bridge && bridge.positionGrams > 0) {
                                var g = parseFloat(txtBatchGrams.text) || 10.0
                                bridge.executeSimulatedTrade("SELL", g, "手动执行模拟卖出")
                            }
                        }
                    }
                }

                // Refresh button
                Rectangle {
                    width: 60
                    height: 28
                    radius: 4
                    color: refMouse.containsMouse ? "#2A2A3D" : "#1B1B26"
                    border.color: "#4A4A62"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "🔄 刷新"
                        color: "#C0C0D4"
                        font.pixelSize: 11
                    }

                    MouseArea {
                        id: refMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: reloadTrades()
                    }
                }

                // Clear button
                Rectangle {
                    width: 70
                    height: 28
                    radius: 4
                    color: clearMouse.containsMouse ? "#4D2224" : "#24181B"
                    border.color: "#662A2E"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "🗑️ 清空"
                        color: "#FF9999"
                        font.pixelSize: 11
                    }

                    MouseArea {
                        id: clearMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (bridge) {
                                bridge.clearSimulatedTrades()
                                reloadTrades()
                            }
                        }
                    }
                }
            }
        }

        // Table Header
        Rectangle {
            Layout.fillWidth: true
            height: 28
            radius: 4
            color: "#181825"
            border.color: "#282838"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 4

                Text { text: "交易时间"; color: "#8E8EA4"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 130 }
                Text { text: "方向"; color: "#8E8EA4"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 60; horizontalAlignment: Text.AlignHCenter }
                Text { text: "决策依据"; color: "#8E8EA4"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 150 }
                Text { text: "买入/成交价"; color: "#8E8EA4"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 85; horizontalAlignment: Text.AlignRight }
                Text { text: "克数(g)"; color: "#8E8EA4"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 65; horizontalAlignment: Text.AlignRight }
                Text { text: "成交额"; color: "#8E8EA4"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 85; horizontalAlignment: Text.AlignRight }
                Text { text: "手续费"; color: "#8E8EA4"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 65; horizontalAlignment: Text.AlignRight }
                Text { text: "本次盈亏"; color: "#8E8EA4"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 85; horizontalAlignment: Text.AlignRight }
                Text { text: "剩余持仓与成本"; color: "#8E8EA4"; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight }
            }
        }

        // Table Rows List
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 6
            color: "#14141E"
            border.color: "#252535"
            border.width: 1
            clip: true

            ListView {
                id: tradeListView
                anchors.fill: parent
                anchors.margins: 4
                model: simTradesWindow.tradeModel
                spacing: 3
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar {
                    active: true
                    policy: ScrollBar.AsNeeded
                }

                delegate: Rectangle {
                    width: tradeListView.width - 8
                    height: 32
                    radius: 4
                    color: index % 2 === 0 ? "#1A1A28" : "#1E1E2F"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 4

                        // 时间
                        Text {
                            text: modelData.tradeTime || ""
                            color: "#C0C0D4"
                            font.pixelSize: 10
                            Layout.preferredWidth: 130
                        }

                        // 方向
                        Rectangle {
                            Layout.preferredWidth: 55
                            height: 20
                            radius: 3
                            color: modelData.action === "BUY" ? "#163219" : "#3E1C1F"
                            border.color: modelData.action === "BUY" ? "#52C41A" : "#FF7875"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: modelData.action === "BUY" ? "🟢 买入" : "🔴 卖出"
                                color: modelData.action === "BUY" ? "#52C41A" : "#FF7875"
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }

                        // 决策依据
                        Text {
                            text: modelData.decisionTag || "--"
                            color: "#E0E0FF"
                            font.pixelSize: 10
                            font.bold: true
                            Layout.preferredWidth: 150
                            elide: Text.ElideRight
                        }

                        // 成交价
                        Text {
                            text: Number(modelData.price).toFixed(2)
                            color: "#FFFFFF"
                            font.pixelSize: 10
                            Layout.preferredWidth: 85
                            horizontalAlignment: Text.AlignRight
                        }

                        // 成交克数
                        Text {
                            text: Number(modelData.grams).toFixed(2) + "g"
                            color: "#FFFFFF"
                            font.pixelSize: 10
                            font.bold: true
                            Layout.preferredWidth: 65
                            horizontalAlignment: Text.AlignRight
                        }

                        // 成交金额
                        Text {
                            text: "¥" + Number(modelData.amount).toFixed(2)
                            color: "#D0D0E2"
                            font.pixelSize: 10
                            Layout.preferredWidth: 85
                            horizontalAlignment: Text.AlignRight
                        }

                        // 手续费
                        Text {
                            text: "¥" + Number(modelData.fee).toFixed(2)
                            color: "#A0A0B2"
                            font.pixelSize: 10
                            Layout.preferredWidth: 65
                            horizontalAlignment: Text.AlignRight
                        }

                        // 实现盈亏
                        Text {
                            property double pnl: modelData.realizedPnl || 0.0
                            text: modelData.action === "SELL" ? ((pnl >= 0 ? "+" : "") + pnl.toFixed(2)) : "--"
                            color: modelData.action === "SELL" ? (pnl >= 0 ? "#FF4D4F" : "#52C41A") : "#777788"
                            font.pixelSize: 10
                            font.bold: modelData.action === "SELL"
                            Layout.preferredWidth: 85
                            horizontalAlignment: Text.AlignRight
                        }

                        // 剩余持仓/成本
                        Text {
                            text: Number(modelData.remainingGrams).toFixed(1) + "g @" + Number(modelData.remainingCost).toFixed(2)
                            color: "#B4B4C8"
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }

            // Empty state
            ColumnLayout {
                anchors.centerIn: parent
                visible: !simTradesWindow.tradeModel || simTradesWindow.tradeModel.length === 0
                spacing: 8

                Text {
                    text: "📊 暂无模拟交易记录"
                    color: "#8E8EA4"
                    font.pixelSize: 14
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: "当开启【决策联动自动买卖】或点击上方【模拟买入/卖出】时，系统将自动记录并实时更新悬浮条持仓盈亏。"
                    color: "#66667A"
                    font.pixelSize: 11
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }
    }
}
