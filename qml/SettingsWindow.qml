import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    objectName: "settingsWindow"
    title: "设置中心 - GoldPriceBar"
    width: 780
    height: 600
    minimumWidth: 700
    minimumHeight: 520
    color: "#141418"
    visible: false

    property int currentCategory: 0
    property var sourceKeys: ["zs", "ms", "cib", "icbc", "cmb", "gj", "ccb", "abc", "boc", "cgb", "jd"]
    property var sourceLabels: [
        "浙商银行 · 积存金 (zs)",
        "民生银行 · 积存金 (ms)",
        "兴业银行 · 积存金 (cib)",
        "工商银行 · 积存金 (icbc)",
        "招商银行 · Au99.99 (cmb)",
        "伦敦金现 · 国际金 (gj)",
        "建设银行 · 积存金 (ccb)",
        "农业银行 · 存金通 (abc)",
        "中国银行 · 积存金 (boc)",
        "广发银行 · 积存金 (cgb)",
        "京东金融 · 黄金 (jd)"
    ]

    property var cachedAmpData: null

    // Inline Reusable Components
    component SettingCard: Rectangle {
        property string title: ""
        property string icon: ""
        default property alias cardContent: cardLayout.data
        Layout.fillWidth: true
        implicitHeight: cardLayout.implicitHeight + 32
        radius: 8
        color: "#1B1B22"
        border.color: "#282834"
        border.width: 1

        ColumnLayout {
            id: cardLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: title.length > 0

                Text {
                    text: icon
                    font.pixelSize: 14
                    visible: icon.length > 0
                }
                Text {
                    text: title
                    color: "#FFFFFF"
                    font.pixelSize: 13
                    font.bold: true
                }
            }
        }
    }

    component DarkTextField: TextField {
        id: dtf
        color: "#E6E6E6"
        font.pixelSize: 12
        padding: 8
        leftPadding: 10
        rightPadding: 10
        selectionColor: "#2D68C4"
        selectedTextColor: "#FFFFFF"
        background: Rectangle {
            implicitHeight: 32
            color: "#121216"
            border.color: dtf.activeFocus ? "#40A9FF" : "#30303D"
            border.width: 1
            radius: 4
        }
    }

    component DarkSpinBox: SpinBox {
        id: dsb
        editable: true
        font.pixelSize: 12
        contentItem: TextInput {
            z: 2
            text: dsb.textFromValue(dsb.value, dsb.locale)
            font: dsb.font
            color: "#E6E6E6"
            selectionColor: "#2D68C4"
            selectedTextColor: "#FFFFFF"
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            readOnly: !dsb.editable
            validator: dsb.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }
        up.indicator: Rectangle {
            x: dsb.mirrored ? 0 : dsb.width - width
            height: dsb.height
            implicitWidth: 26
            color: dsb.up.pressed ? "#3C3C4C" : (dsb.up.hovered ? "#2E2E3C" : "#1E1E26")
            border.color: "#30303D"
            border.width: 1
            radius: 2
            Text {
                text: "+"
                font.pixelSize: 13
                color: dsb.up.enabled ? "#D0D0E0" : "#555566"
                anchors.centerIn: parent
            }
        }
        down.indicator: Rectangle {
            x: dsb.mirrored ? dsb.width - width : 0
            height: dsb.height
            implicitWidth: 26
            color: dsb.down.pressed ? "#3C3C4C" : (dsb.down.hovered ? "#2E2E3C" : "#1E1E26")
            border.color: "#30303D"
            border.width: 1
            radius: 2
            Text {
                text: "-"
                font.pixelSize: 13
                color: dsb.down.enabled ? "#D0D0E0" : "#555566"
                anchors.centerIn: parent
            }
        }
        background: Rectangle {
            implicitWidth: 120
            implicitHeight: 32
            color: "#121216"
            border.color: dsb.activeFocus ? "#40A9FF" : "#30303D"
            border.width: 1
            radius: 4
        }
    }

    component DarkCheckBox: CheckBox {
        id: dcb
        font.pixelSize: 12
        contentItem: Text {
            text: dcb.text
            font: dcb.font
            color: dcb.checked ? "#FFFFFF" : "#A6A6B4"
            verticalAlignment: Text.AlignVCenter
            leftPadding: dcb.indicator.width + dcb.spacing
        }
    }

    component DarkComboBox: ComboBox {
        id: dcombo
        font.pixelSize: 12
        contentItem: Text {
            leftPadding: 10
            rightPadding: dcombo.indicator.width + dcombo.spacing
            text: dcombo.displayText
            font: dcombo.font
            color: "#E6E6E6"
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitWidth: 180
            implicitHeight: 32
            color: "#121216"
            border.color: dcombo.activeFocus ? "#40A9FF" : "#30303D"
            border.width: 1
            radius: 4
        }
        popup: Popup {
            y: dcombo.height + 2
            width: dcombo.width
            implicitHeight: Math.min(260, contentItem.implicitHeight + 10)
            padding: 4
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: dcombo.popup.visible ? dcombo.delegateModel : null
                currentIndex: dcombo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator { }
            }
            background: Rectangle {
                color: "#1C1C24"
                border.color: "#383848"
                border.width: 1
                radius: 4
            }
        }
        delegate: ItemDelegate {
            width: dcombo.width - 8
            height: 30
            contentItem: Text {
                text: modelData
                color: highlighted ? "#FFFFFF" : "#C0C0D0"
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: highlighted ? "#2D68C4" : "transparent"
                radius: 3
            }
        }
    }

    Connections {
        target: bridge
        function onRequestShowSettings() {
            syncFromBridge()
            root.visible = true
            root.show()
            root.raise()
            root.requestActivate()
        }

        function onModelsLoaded(models) {
            modelComboModel.clear()
            for (var i = 0; i < models.length; ++i) {
                modelComboModel.append({ text: models[i] })
            }
            lblModelStatus.text = "✅ 成功拉取 " + models.length + " 个可用模型"
            lblModelStatus.color = "#52C41A"
            if (models.length > 0) {
                txtModelName.text = models[0]
            }
        }

        function onModelsLoadFailed(err) {
            lblModelStatus.text = "❌ " + err
            lblModelStatus.color = "#FF4D4F"
        }

        function onPriceChanged() {
            updatePositionProfit()
        }
    }

    Component.onCompleted: {
        syncFromBridge()
    }

    function syncFromBridge() {
        if (!bridge) return
        var s = bridge.getAllSettings()

        // 1. 行情与显示
        chkSentiment.checked = !!s.sentimentEnabled
        spinInterval.value = s.refreshIntervalSec || 5
        sliderOpacity.value = Math.round((s.windowOpacity !== undefined ? s.windowOpacity : 0.95) * 100)
        chkSecondaryPrice.checked = !!s.showSecondaryPrice
        chkDarkTheme.checked = !!s.darkTheme
        chkMovingAvg.checked = !!s.showMovingAverage
        chkHotkey.checked = (s.hotkeyEnabled !== undefined) ? s.hotkeyEnabled : true
        chkAutoStart.checked = !!s.autoStart

        var curSrc = (s.dataSource || "zs").toLowerCase()
        var idx = sourceKeys.indexOf(curSrc)
        comboSource.currentIndex = (idx >= 0) ? idx : 0

        // 2. 预警与计划
        txtAlertHigh.text = (s.alertHigh && s.alertHigh > 0) ? Number(s.alertHigh).toFixed(2) : "0"
        txtAlertLow.text = (s.alertLow && s.alertLow > 0) ? Number(s.alertLow).toFixed(2) : "0"
        spinAlertCooldown.value = s.alertCooldownSec || 60
        chkTrayNotify.checked = (s.trayNotifyOnAlert !== undefined) ? s.trayNotifyOnAlert : true
        chkAlertSound.checked = !!s.alertSound
        chkQuietHours.checked = !!s.quietHoursEnabled
        txtQuietStart.text = s.quietStart || "23:00"
        txtQuietEnd.text = s.quietEnd || "07:00"

        chkSmartMa.checked = !!s.smartAlertMa
        chkSmartPercentile.checked = !!s.smartAlertPercentile
        spinPercentileLow.value = s.percentileLow || 10
        spinPercentileHigh.value = s.percentileHigh || 90
        chkPremiumAlert.checked = !!s.premiumAlertEnabled
        spinPremiumPct.value = Math.round((s.premiumThresholdPct || 1.5) * 10)

        chkPlan.checked = !!s.planEnabled
        txtPlanBuy.text = (s.planBuyPrice && s.planBuyPrice > 0) ? Number(s.planBuyPrice).toFixed(2) : "0"
        txtPlanSell.text = (s.planSellPrice && s.planSellPrice > 0) ? Number(s.planSellPrice).toFixed(2) : "0"
        txtPlanInvalid.text = (s.planInvalidPrice && s.planInvalidPrice > 0) ? Number(s.planInvalidPrice).toFixed(2) : "0"

        // 3. AI价格预测
        comboForecastEngine.currentIndex = s.forecastOnline ? 1 : 0
        spinForecastInterval.value = s.forecastIntervalSec || 300
        comboProvider.currentIndex = (s.llmProvider === "gemini") ? 1 : 0
        txtApiKey.text = s.xaiApiKey || ""
        txtModelName.text = s.xaiModel || (comboProvider.currentIndex === 1 ? "gemini-2.0-flash" : "grok-2-latest")

        // 4. 持仓与报告
        txtPositionGrams.text = (s.positionGrams && s.positionGrams > 0) ? Number(s.positionGrams).toFixed(2) : ""
        txtPositionCost.text = (s.positionCost && s.positionCost > 0) ? Number(s.positionCost).toFixed(2) : ""
        chkDailyReport.checked = !!s.dailyReportEnabled
        txtDailyReportTime.text = s.dailyReportTime || "17:30"
        spinDcaDay.value = (s.dcaDayOfMonth >= 1 && s.dcaDayOfMonth <= 31) ? s.dcaDayOfMonth : 15
        txtDcaNote.text = s.dcaNote || ""
        chkEventAlert.checked = !!s.eventAlertEnabled

        // 5. 高级与数据
        txtPrimaryUrl.text = s.primaryPriceUrl || ""
        txtChartUrl.text = s.chartUrl || ""
        txtDatabaseDir.text = s.databaseDir || ""
        chkProxy.checked = !!s.proxyEnabled
        txtProxyHost.text = s.proxyHost || "127.0.0.1"
        spinProxyPort.value = s.proxyPort || 7890

        // 动态数据计算
        updatePositionProfit()
        refreshEventCalendar()
        refresh10DayAmp()
    }

    function saveAll() {
        if (!bridge) return
        var m = {}

        // 1. 行情与显示
        m["sentimentEnabled"] = chkSentiment.checked
        m["refreshIntervalSec"] = spinInterval.value
        m["windowOpacity"] = sliderOpacity.value / 100.0
        m["showSecondaryPrice"] = chkSecondaryPrice.checked
        m["darkTheme"] = chkDarkTheme.checked
        m["showMovingAverage"] = chkMovingAvg.checked
        m["hotkeyEnabled"] = chkHotkey.checked
        m["autoStart"] = chkAutoStart.checked
        m["dataSource"] = sourceKeys[comboSource.currentIndex]

        // 2. 预警与计划
        m["alertHigh"] = parseFloat(txtAlertHigh.text) || 0.0
        m["alertLow"] = parseFloat(txtAlertLow.text) || 0.0
        m["alertCooldownSec"] = spinAlertCooldown.value
        m["trayNotifyOnAlert"] = chkTrayNotify.checked
        m["alertSound"] = chkAlertSound.checked
        m["quietHoursEnabled"] = chkQuietHours.checked
        m["quietStart"] = txtQuietStart.text.trim()
        m["quietEnd"] = txtQuietEnd.text.trim()
        m["smartAlertMa"] = chkSmartMa.checked
        m["smartAlertPercentile"] = chkSmartPercentile.checked
        m["percentileLow"] = spinPercentileLow.value
        m["percentileHigh"] = spinPercentileHigh.value
        m["premiumAlertEnabled"] = chkPremiumAlert.checked
        m["premiumThresholdPct"] = spinPremiumPct.value / 10.0
        m["planEnabled"] = chkPlan.checked
        m["planBuyPrice"] = parseFloat(txtPlanBuy.text) || 0.0
        m["planSellPrice"] = parseFloat(txtPlanSell.text) || 0.0
        m["planInvalidPrice"] = parseFloat(txtPlanInvalid.text) || 0.0

        // 3. AI价格预测
        m["forecastOnline"] = (comboForecastEngine.currentIndex === 1)
        m["forecastIntervalSec"] = spinForecastInterval.value
        m["llmProvider"] = (comboProvider.currentIndex === 1) ? "gemini" : "xai"
        m["xaiApiKey"] = txtApiKey.text.trim()
        m["xaiModel"] = txtModelName.text.trim()

        // 4. 持仓与报告
        m["positionGrams"] = parseFloat(txtPositionGrams.text) || 0.0
        m["positionCost"] = parseFloat(txtPositionCost.text) || 0.0
        m["dailyReportEnabled"] = chkDailyReport.checked
        m["dailyReportTime"] = txtDailyReportTime.text.trim()
        m["dcaDayOfMonth"] = spinDcaDay.value
        m["dcaNote"] = txtDcaNote.text.trim()
        m["eventAlertEnabled"] = chkEventAlert.checked

        // 5. 高级与数据
        m["primaryPriceUrl"] = txtPrimaryUrl.text.trim()
        m["chartUrl"] = txtChartUrl.text.trim()
        m["databaseDir"] = txtDatabaseDir.text.trim()
        m["proxyEnabled"] = chkProxy.checked
        m["proxyHost"] = txtProxyHost.text.trim()
        m["proxyPort"] = spinProxyPort.value

        bridge.saveAllSettings(m)
        root.visible = false
    }

    function updatePositionProfit() {
        var grams = parseFloat(txtPositionGrams.text) || 0.0
        var cost = parseFloat(txtPositionCost.text) || 0.0
        var curPrice = bridge ? bridge.price : 0.0
        if (grams > 0 && cost > 0 && curPrice > 0) {
            var totalCost = grams * cost
            var curVal = grams * curPrice
            var profit = curVal - totalCost
            var pct = (profit / totalCost) * 100.0
            lblProfitCalc.text = "持仓市值: ¥" + curVal.toFixed(2) + " | 浮动盈亏: " + (profit >= 0 ? "+" : "") + profit.toFixed(2) + " (" + (profit >= 0 ? "+" : "") + pct.toFixed(2) + "%)"
            lblProfitCalc.color = profit >= 0 ? "#FF4D4F" : "#52C41A"
        } else {
            lblProfitCalc.text = "💡 输入持仓克数与成本单价后自动核算市值与盈亏情况"
            lblProfitCalc.color = "#8E8E9F"
        }
    }

    function refresh10DayAmp() {
        if (!bridge) return
        var res = bridge.get10DayAmplitude()
        cachedAmpData = res
        lbl10DayAmp.text = res.text || "正在计算近10日振幅..."
    }

    function refreshEventCalendar() {
        if (!bridge) return
        var s = bridge.getEventCalendarSummary()
        lblEventSummary.text = (s && s.length > 0) ? s : "近7日内无特大宏观经济事件"
    }

    ListModel {
        id: modelComboModel
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 顶部标题栏
        Rectangle {
            Layout.fillWidth: true
            height: 50
            color: "#181820"
            border.color: "#242430"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 10

                Text {
                    text: "⚙️"
                    font.pixelSize: 18
                }

                Column {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 2
                    Text {
                        text: "GoldPriceBar 参数配置中心"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.bold: true
                    }
                    Text {
                        text: "集中配置行情、预警监控、AI 预测、个人资产与系统参数"
                        color: "#8E8E9F"
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 28
                    height: 28
                    radius: 4
                    color: closeArea.containsMouse ? "#E81123" : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: closeArea.containsMouse ? "#FFFFFF" : "#8E8E9F"
                        font.pixelSize: 13
                    }

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.visible = false
                    }
                }
            }
        }

        // 中间主体：左侧分类侧边栏 + 右侧内容页
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 左侧分类导航
            Rectangle {
                Layout.fillHeight: true
                implicitWidth: 168
                color: "#111115"
                border.color: "#22222C"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    Repeater {
                        model: [
                            { icon: "📊", name: "行情与显示" },
                            { icon: "🔔", name: "预警与计划" },
                            { icon: "🤖", name: "AI 价格预测" },
                            { icon: "💼", name: "持仓与报告" },
                            { icon: "⚙️", name: "高级与数据" }
                        ]

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: 38
                            radius: 6
                            color: (currentCategory === index) ? "#232635" : (tabMouse.containsMouse ? "#1A1A22" : "transparent")
                            border.color: (currentCategory === index) ? "#3A4B75" : "transparent"
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10

                                Text {
                                    text: modelData.icon
                                    font.pixelSize: 14
                                }

                                Text {
                                    text: modelData.name
                                    color: (currentCategory === index) ? "#40A9FF" : (tabMouse.containsMouse ? "#FFFFFF" : "#A6A6B4")
                                    font.pixelSize: 12
                                    font.bold: (currentCategory === index)
                                    Layout.fillWidth: true
                                }
                            }

                            MouseArea {
                                id: tabMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: currentCategory = index
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 48
                        radius: 6
                        color: "#16161D"
                        border.color: "#242430"

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 2
                            Text {
                                text: "当前跟踪金价"
                                color: "#8E8E9F"
                                font.pixelSize: 10
                                Layout.alignment: Qt.AlignHCenter
                            }
                            Text {
                                text: (bridge && bridge.hasValidPrice) ? ("¥ " + bridge.price.toFixed(2)) : "--"
                                color: (bridge && bridge.priceChange >= 0) ? "#FF4D4F" : "#52C41A"
                                font.pixelSize: 13
                                font.bold: true
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }
                }
            }

            // 右侧内容滚动区域
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: parent.width - 24
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 14

                    Item { height: 6 } // 顶部内边距

                    // ==========================================
                    // 分类 0: 📊 行情与显示
                    // ==========================================
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        visible: currentCategory === 0

                        // 舆情卡片
                        SettingCard {
                            title: "黄金实时舆情监测"
                            icon: "📰"

                            DarkCheckBox {
                                id: chkSentiment
                                text: "开启黄金实时多空舆情监测"
                                font.bold: true
                                onToggled: {
                                    if (bridge) {
                                        bridge.setSentimentEnabled(checked)
                                        if (checked) {
                                            bridge.openSentimentWindow()
                                        }
                                    }
                                }
                            }

                            Text {
                                text: "💡 勾选时将直接弹出舆情分析窗口，实时聚合财经快讯与热点多空偏向；若遇网络异常界面将柔性提示，确保软件主体运行稳定。"
                                color: "#8E8E9F"
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                Layout.leftMargin: 26
                            }
                        }

                        // 行情品种与刷新
                        SettingCard {
                            title: "行情源与抓取频率"
                            icon: "📊"

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    text: "跟踪黄金品种:"
                                    color: "#A6A6B4"
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 100
                                }

                                DarkComboBox {
                                    id: comboSource
                                    Layout.fillWidth: true
                                    model: sourceLabels
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    text: "刷新间隔 (秒):"
                                    color: "#A6A6B4"
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 100
                                }

                                DarkSpinBox {
                                    id: spinInterval
                                    from: 1
                                    to: 60
                                    value: 5
                                }

                                Text {
                                    text: "建议 3~10 秒，高频刷新可获得极致实时性"
                                    color: "#777788"
                                    font.pixelSize: 11
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        // 界面与悬浮控制
                        SettingCard {
                            title: "悬浮看板外观与交互"
                            icon: "🎨"

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    text: "悬浮条不透明度 (" + sliderOpacity.value + "%):"
                                    color: "#A6A6B4"
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 140
                                }

                                Slider {
                                    id: sliderOpacity
                                    Layout.fillWidth: true
                                    from: 30
                                    to: 100
                                    stepSize: 5
                                    value: 95
                                    onMoved: {
                                        if (bridge) {
                                            bridge.setWindowOpacity(value / 100.0)
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 20

                                DarkCheckBox {
                                    id: chkSecondaryPrice
                                    text: "显示国际金/基准对照价"
                                }

                                DarkCheckBox {
                                    id: chkDarkTheme
                                    text: "深色主题风格"
                                    checked: true
                                }

                                DarkCheckBox {
                                    id: chkMovingAvg
                                    text: "分时图显示动态均线"
                                    checked: true
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 20

                                DarkCheckBox {
                                    id: chkHotkey
                                    text: "启用全局快捷键 (Ctrl+Shift+G 切换显隐)"
                                }

                                DarkCheckBox {
                                    id: chkAutoStart
                                    text: "开机自动启动"
                                }
                            }
                        }
                    }

                    // ==========================================
                    // 分类 1: 🔔 预警与计划
                    // ==========================================
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        visible: currentCategory === 1

                        // 价格突破高低预警
                        SettingCard {
                            title: "高低价触发预警"
                            icon: "🔔"

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 14

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Text {
                                        text: "目标止盈 / 上限预警价 (元/克, 0 关闭):"
                                        color: "#A6A6B4"
                                        font.pixelSize: 11
                                    }
                                    DarkTextField {
                                        id: txtAlertHigh
                                        Layout.fillWidth: true
                                        placeholderText: "例如 685.00"
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Text {
                                        text: "支撑防守 / 下限预警价 (元/克, 0 关闭):"
                                        color: "#A6A6B4"
                                        font.pixelSize: 11
                                    }
                                    DarkTextField {
                                        id: txtAlertLow
                                        Layout.fillWidth: true
                                        placeholderText: "例如 655.00"
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 14

                                RowLayout {
                                    spacing: 8
                                    Text {
                                        text: "预警冷却时间:"
                                        color: "#A6A6B4"
                                        font.pixelSize: 11
                                    }
                                    DarkSpinBox {
                                        id: spinAlertCooldown
                                        from: 10
                                        to: 3600
                                        value: 60
                                    }
                                    Text { text: "秒"; color: "#8E8E9F"; font.pixelSize: 11 }
                                }

                                DarkCheckBox {
                                    id: chkTrayNotify
                                    text: "系统托盘气泡通知"
                                    checked: true
                                }

                                DarkCheckBox {
                                    id: chkAlertSound
                                    text: "播放提示音"
                                }
                            }

                            // 静默时段
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                DarkCheckBox {
                                    id: chkQuietHours
                                    text: "启用夜间免打扰静默时段"
                                }

                                Text {
                                    text: "时间段:"
                                    color: "#8E8E9F"
                                    font.pixelSize: 11
                                    visible: chkQuietHours.checked
                                }

                                DarkTextField {
                                    id: txtQuietStart
                                    visible: chkQuietHours.checked
                                    implicitWidth: 70
                                    text: "23:00"
                                }

                                Text {
                                    text: "至"
                                    color: "#8E8E9F"
                                    font.pixelSize: 11
                                    visible: chkQuietHours.checked
                                }

                                DarkTextField {
                                    id: txtQuietEnd
                                    visible: chkQuietHours.checked
                                    implicitWidth: 70
                                    text: "07:00"
                                }
                            }
                        }

                        // 近 10 日振幅分析推荐卡片
                        SettingCard {
                            title: "智能极值辅助 (近10日历史振幅)"
                            icon: "📈"

                            Text {
                                id: lbl10DayAmp
                                text: "正在读取历史日线数据..."
                                color: "#FFD04B"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Button {
                                    text: "🔄 重新计算振幅"
                                    onClicked: refresh10DayAmp()
                                    contentItem: Text {
                                        text: parent.text
                                        color: "#D0D0E0"
                                        font.pixelSize: 11
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        color: parent.hovered ? "#323242" : "#22222C"
                                        radius: 4
                                        border.color: "#383848"
                                    }
                                }

                                Button {
                                    text: "⚡ 采用为高低预警"
                                    enabled: cachedAmpData && cachedAmpData.min > 0 && cachedAmpData.max > 0
                                    onClicked: {
                                        if (cachedAmpData) {
                                            txtAlertLow.text = cachedAmpData.min.toFixed(2)
                                            txtAlertHigh.text = cachedAmpData.max.toFixed(2)
                                        }
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: parent.enabled ? "#FFFFFF" : "#777788"
                                        font.pixelSize: 11
                                        font.bold: true
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        color: parent.enabled ? (parent.hovered ? "#2D68C4" : "#1E52A8") : "#1A1A22"
                                        radius: 4
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 14

                                DarkCheckBox {
                                    id: chkSmartMa
                                    text: "均线偏离度预警 (乖离率过大)"
                                }

                                DarkCheckBox {
                                    id: chkSmartPercentile
                                    text: "历史分位数极值预警"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                visible: chkSmartPercentile.checked

                                Text { text: "极值区间分位: 低分位"; color: "#8E8E9F"; font.pixelSize: 11 }
                                DarkSpinBox {
                                    id: spinPercentileLow
                                    from: 1
                                    to: 30
                                    value: 10
                                }
                                Text { text: "% 到 高分位"; color: "#8E8E9F"; font.pixelSize: 11 }
                                DarkSpinBox {
                                    id: spinPercentileHigh
                                    from: 70
                                    to: 99
                                    value: 90
                                }
                                Text { text: "%"; color: "#8E8E9F"; font.pixelSize: 11 }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                DarkCheckBox {
                                    id: chkPremiumAlert
                                    text: "国内外溢价率异常监控"
                                }

                                Text {
                                    text: "阈值:"
                                    color: "#8E8E9F"
                                    font.pixelSize: 11
                                    visible: chkPremiumAlert.checked
                                }

                                DarkSpinBox {
                                    id: spinPremiumPct
                                    visible: chkPremiumAlert.checked
                                    from: 1
                                    to: 100
                                    value: 15
                                    textFromValue: function(value, locale) { return (value / 10.0).toFixed(1) + "%"; }
                                }
                            }
                        }

                        // 交易计划盯盘
                        SettingCard {
                            title: "交易计划盯盘 (挂单与执行预设)"
                            icon: "📝"

                            DarkCheckBox {
                                id: chkPlan
                                text: "启用交易计划目标监控"
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                enabled: chkPlan.checked

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Text { text: "计划买入触发价 (元):"; color: "#A6A6B4"; font.pixelSize: 11 }
                                    DarkTextField {
                                        id: txtPlanBuy
                                        Layout.fillWidth: true
                                        placeholderText: "0 为不设"
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Text { text: "计划卖出止盈价 (元):"; color: "#A6A6B4"; font.pixelSize: 11 }
                                    DarkTextField {
                                        id: txtPlanSell
                                        Layout.fillWidth: true
                                        placeholderText: "0 为不设"
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Text { text: "逻辑失效止损价 (元):"; color: "#A6A6B4"; font.pixelSize: 11 }
                                    DarkTextField {
                                        id: txtPlanInvalid
                                        Layout.fillWidth: true
                                        placeholderText: "0 为不设"
                                    }
                                }
                            }
                        }
                    }

                    // ==========================================
                    // 分类 2: 🤖 AI 价格预测
                    // ==========================================
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        visible: currentCategory === 2

                        SettingCard {
                            title: "价格预测引擎与更新频率"
                            icon: "🤖"

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    text: "预测模式:"
                                    color: "#A6A6B4"
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 90
                                }

                                DarkComboBox {
                                    id: comboForecastEngine
                                    Layout.fillWidth: true
                                    model: ["本地轻量统计回归引擎 (零延时/免Key)", "云端大语言模型深度研判 (xAI / Gemini)"]
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    text: "预测更新间隔:"
                                    color: "#A6A6B4"
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 90
                                }

                                DarkSpinBox {
                                    id: spinForecastInterval
                                    from: 60
                                    to: 3600
                                    stepSize: 60
                                    value: 300
                                }

                                Text { text: "秒 (默认 300 秒，云端大模型调用建议设置 300~600 秒以防超额)"; color: "#777788"; font.pixelSize: 11; Layout.fillWidth: true }
                            }
                        }

                        SettingCard {
                            title: "云端大模型 API 配置 (仅在大模型预测模式下生效)"
                            icon: "🔑"

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    text: "AI 服务商:"
                                    color: "#A6A6B4"
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 90
                                }

                                DarkComboBox {
                                    id: comboProvider
                                    Layout.fillWidth: true
                                    model: ["xAI (Grok)", "Google Gemini"]
                                    onCurrentIndexChanged: {
                                        if (txtModelName.text.indexOf("grok") >= 0 && currentIndex === 1) {
                                            txtModelName.text = "gemini-2.0-flash"
                                        } else if (txtModelName.text.indexOf("gemini") >= 0 && currentIndex === 0) {
                                            txtModelName.text = "grok-2-latest"
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    text: "API Key:"
                                    color: "#A6A6B4"
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 90
                                }

                                DarkTextField {
                                    id: txtApiKey
                                    Layout.fillWidth: true
                                    echoMode: chkShowKey.checked ? TextInput.Normal : TextInput.Password
                                    placeholderText: comboProvider.currentIndex === 0 ? "xai-..." : "AIzaSy..."
                                }

                                DarkCheckBox {
                                    id: chkShowKey
                                    text: "显示"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    text: "模型名称:"
                                    color: "#A6A6B4"
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 90
                                }

                                DarkTextField {
                                    id: txtModelName
                                    Layout.fillWidth: true
                                    placeholderText: "例如 grok-2-latest 或 gemini-2.0-flash"
                                }

                                Button {
                                    text: "🔄 在线拉取模型"
                                    onClicked: {
                                        lblModelStatus.text = "正在拉取最新模型列表..."
                                        lblModelStatus.color = "#FFD04B"
                                        var p = (comboProvider.currentIndex === 1) ? "gemini" : "xai"
                                        bridge.fetchModelList(p, txtApiKey.text)
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: "#FFFFFF"
                                        font.pixelSize: 11
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        color: parent.hovered ? "#3B7CD6" : "#2458A6"
                                        radius: 4
                                    }
                                }
                            }

                            Text {
                                id: lblModelStatus
                                text: "支持输入自定义模型，或点击右侧按钮根据 API Key 自动拉取全部可用模型"
                                color: "#8E8E9F"
                                font.pixelSize: 11
                                Layout.fillWidth: true
                                Layout.leftMargin: 102
                            }
                        }
                    }

                    // ==========================================
                    // 分类 3: 💼 持仓与报告
                    // ==========================================
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        visible: currentCategory === 3

                        // 个人持仓核算
                        SettingCard {
                            title: "个人黄金持仓核算"
                            icon: "💰"

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 14

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Text { text: "当前持仓总克数 (克):"; color: "#A6A6B4"; font.pixelSize: 11 }
                                    DarkTextField {
                                        id: txtPositionGrams
                                        Layout.fillWidth: true
                                        placeholderText: "例如 20.0"
                                        onTextChanged: updatePositionProfit()
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Text { text: "持仓成本均价 (元/克):"; color: "#A6A6B4"; font.pixelSize: 11 }
                                    DarkTextField {
                                        id: txtPositionCost
                                        Layout.fillWidth: true
                                        placeholderText: "例如 620.50"
                                        onTextChanged: updatePositionProfit()
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 36
                                radius: 4
                                color: "#141419"
                                border.color: "#252530"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    Text {
                                        id: lblProfitCalc
                                        text: "💡 输入持仓克数与成本单价后自动核算市值与盈亏情况"
                                        color: "#8E8E9F"
                                        font.pixelSize: 11
                                        font.bold: true
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }

                        // 每日复盘与定投助手
                        SettingCard {
                            title: "复盘简报与定投计划"
                            icon: "📅"

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                DarkCheckBox {
                                    id: chkDailyReport
                                    text: "每日定时推送收盘收益简报"
                                }

                                Text { text: "推送时间:"; color: "#8E8E9F"; font.pixelSize: 11; visible: chkDailyReport.checked }

                                DarkTextField {
                                    id: txtDailyReportTime
                                    visible: chkDailyReport.checked
                                    implicitWidth: 80
                                    text: "17:30"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text { text: "每月固定定投日:"; color: "#A6A6B4"; font.pixelSize: 11 }
                                DarkSpinBox {
                                    id: spinDcaDay
                                    from: 1
                                    to: 31
                                    value: 15
                                }
                                Text { text: "号"; color: "#8E8E9F"; font.pixelSize: 11 }

                                Text { text: "定投备忘:"; color: "#A6A6B4"; font.pixelSize: 11; Layout.leftMargin: 10 }
                                DarkTextField {
                                    id: txtDcaNote
                                    Layout.fillWidth: true
                                    placeholderText: "如：每月 15 号发薪定投 5 克积存金"
                                }
                            }
                        }

                        // 宏观日程速览
                        SettingCard {
                            title: "全球宏观大事件日程速览"
                            icon: "🌍"

                            DarkCheckBox {
                                id: chkEventAlert
                                text: "开启美联储议息、非农与 CPI 重磅日程提示"
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: lblEventSummary.implicitHeight + 16
                                radius: 4
                                color: "#141419"
                                border.color: "#252530"

                                Text {
                                    id: lblEventSummary
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 8
                                    text: "读取宏观日程中..."
                                    color: "#B4B4C4"
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    // ==========================================
                    // 分类 4: ⚙️ 高级与数据
                    // ==========================================
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        visible: currentCategory === 4

                        // 自定义 API 接口
                        SettingCard {
                            title: "自定义行情数据接口 (高级开发者专用)"
                            icon: "🔗"

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "主行情抓取 URL (留空使用内置聚合源):"; color: "#A6A6B4"; font.pixelSize: 11 }
                                DarkTextField {
                                    id: txtPrimaryUrl
                                    Layout.fillWidth: true
                                    placeholderText: "默认内置源 (如 https://jin.20021002.xyz/api/realtime?type=)"
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "分时/历史走势图 URL (留空使用内置源):"; color: "#A6A6B4"; font.pixelSize: 11 }
                                DarkTextField {
                                    id: txtChartUrl
                                    Layout.fillWidth: true
                                    placeholderText: "默认内置源 (如 https://jin.20021002.xyz/api/history?type=)"
                                }
                            }
                        }

                        // 本地数据库
                        SettingCard {
                            title: "SQLite 极值与分时数据库存储"
                            icon: "🗄️"

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Text { text: "数据库目录:"; color: "#A6A6B4"; font.pixelSize: 11 }

                                DarkTextField {
                                    id: txtDatabaseDir
                                    Layout.fillWidth: true
                                    placeholderText: "默认存放在 AppData/GoldPriceBar 目录"
                                }

                                Button {
                                    text: "📁 浏览..."
                                    onClicked: {
                                        var d = bridge.selectDatabaseDir()
                                        if (d && d.length > 0) {
                                            txtDatabaseDir.text = d
                                        }
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: "#E0E0E0"
                                        font.pixelSize: 11
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        color: parent.hovered ? "#323242" : "#22222C"
                                        radius: 4
                                        border.color: "#383848"
                                    }
                                }
                            }
                        }

                        // 网络与代理
                        SettingCard {
                            title: "网络与代理配置 (主要用于外网行情与舆情)"
                            icon: "🌐"

                            DarkCheckBox {
                                id: chkProxy
                                text: "启用 HTTP / SOCKS 代理"
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                enabled: chkProxy.checked

                                Text { text: "代理服务器:"; color: "#A6A6B4"; font.pixelSize: 11 }

                                DarkTextField {
                                    id: txtProxyHost
                                    Layout.fillWidth: true
                                    text: "127.0.0.1"
                                }

                                Text { text: "端口:"; color: "#A6A6B4"; font.pixelSize: 11 }

                                DarkSpinBox {
                                    id: spinProxyPort
                                    from: 1
                                    to: 65535
                                    value: 7890
                                }
                            }

                            Text {
                                text: "💡 实时金价获取已内置直连容灾，即使代理客户端未开启也不会导致基础价格断连。"
                                color: "#8E8E9F"
                                font.pixelSize: 11
                                Layout.fillWidth: true
                            }
                        }
                    }

                    Item { height: 12 } // 底部留白
                }
            }
        }

        // 底部操作控制栏
        Rectangle {
            Layout.fillWidth: true
            height: 52
            color: "#181820"
            border.color: "#242430"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 10

                // 退出软件按钮
                Button {
                    text: "🚪 退出软件"
                    onClicked: bridge.quitApp()
                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? "#FF7875" : "#D08080"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.hovered ? "#361B1E" : "#241618"
                        radius: 4
                        border.color: parent.hovered ? "#5C2223" : "#3B1E20"
                    }
                }

                // 打开日志目录
                Button {
                    text: "📂 打开日志目录"
                    onClicked: bridge.openLogDir()
                    contentItem: Text {
                        text: parent.text
                        color: "#A0A0B0"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.hovered ? "#2D2D38" : "#202028"
                        radius: 4
                        border.color: "#343442"
                    }
                }

                // 检查更新
                Button {
                    text: "🔄 检查更新"
                    onClicked: bridge.checkUpdate()
                    contentItem: Text {
                        text: parent.text
                        color: "#A0A0B0"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.hovered ? "#2D2D38" : "#202028"
                        radius: 4
                        border.color: "#343442"
                    }
                }

                Item { Layout.fillWidth: true }

                // 取消按钮
                Button {
                    text: "取消"
                    implicitWidth: 80
                    onClicked: root.visible = false
                    contentItem: Text {
                        text: parent.text
                        color: "#B0B0C0"
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.hovered ? "#323240" : "#242430"
                        radius: 4
                        border.color: "#383848"
                    }
                }

                // 保存并应用
                Button {
                    text: "💾 保存并应用"
                    implicitWidth: 110
                    onClicked: saveAll()
                    contentItem: Text {
                        text: parent.text
                        color: "#FFFFFF"
                        font.pixelSize: 12
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.hovered ? "#2D68C4" : "#1E52A8"
                        radius: 4
                    }
                }
            }
        }
    }
}
