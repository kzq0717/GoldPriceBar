import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    objectName: "agentChatWindow"
    title: "AI 知识问答与延伸 - GoldPriceBar"
    width: 780
    height: 680
    minimumWidth: 640
    minimumHeight: 480
    color: "#16161A"
    visible: false

    property string contextTopic: ""
    property string currentProvider: "gemini"
    property string currentApiKey: ""
    property string currentModel: "gemini-2.5-flash"
    property string customEndpoint: ""
    property bool isGenerating: false
    property bool showConfigPanel: false
    property var chatMessages: []

    property var aiChatProviders: [
        { id: "deepseek", name: "DeepSeek (深度求索)" },
        { id: "glm",      name: "智谱 GLM (BigModel)" },
        { id: "qwen",     name: "通义千问 (Qwen)" },
        { id: "moonshot", name: "月之暗面 (Kimi)" },
        { id: "xai",      name: "xAI (Grok)" },
        { id: "gemini",   name: "Google Gemini" },
        { id: "openai",   name: "OpenAI (ChatGPT)" },
        { id: "custom",   name: "自定义 / OpenAI 兼容" }
    ]

    function getChatProvIdx(p) {
        if (!p) return 0;
        var low = p.toLowerCase().trim();
        if (low === "zhipu") low = "glm";
        for (var i = 0; i < aiChatProviders.length; ++i) {
            if (aiChatProviders[i].id === low) return i;
        }
        return 0;
    }

    function defaultAiModels(provider) {
        var p = provider ? provider.toLowerCase().trim() : "deepseek";
        if (p === "zhipu") p = "glm";
        if (p === "deepseek") {
            return [
                "deepseek-chat",
                "deepseek-reasoner"
            ];
        } else if (p === "glm") {
            return [
                "glm-4-plus",
                "glm-4-flash",
                "glm-4-long",
                "glm-4v-plus",
                "glm-3-turbo"
            ];
        } else if (p === "qwen") {
            return [
                "qwen-max",
                "qwen-plus",
                "qwen-turbo",
                "qwen-long"
            ];
        } else if (p === "moonshot") {
            return [
                "moonshot-v1-8k",
                "moonshot-v1-32k",
                "moonshot-v1-128k"
            ];
        } else if (p === "gemini") {
            return [
                "gemini-flash-latest",
                "gemini-2.5-flash-lite",
                "gemini-2.0-flash",
                "gemini-pro-latest",
                "gemini-1.5-flash"
            ];
        } else if (p === "xai") {
            return [
                "grok-2-latest",
                "grok-2",
                "grok-beta",
                "grok-vision-beta"
            ];
        } else if (p === "openai") {
            return [
                "gpt-4o",
                "gpt-4o-mini",
                "o1",
                "o1-mini",
                "gpt-4-turbo"
            ];
        } else {
            return [
                "deepseek-chat",
                "glm-4-flash",
                "gpt-4o-mini"
            ];
        }
    }

    function setAiModelSelection(modelName) {
        var mList = comboAiModel.model
        if (!mList || mList.length === 0) {
            mList = defaultAiModels(root.currentProvider)
            comboAiModel.model = mList
        }
        var foundIdx = -1
        for (var i = 0; i < mList.length; ++i) {
            if (mList[i] === modelName) {
                foundIdx = i
                break
            }
        }
        if (foundIdx >= 0) {
            comboAiModel.currentIndex = foundIdx
        } else if (modelName && modelName.length > 0) {
            var arr = Array.from(mList)
            arr.unshift(modelName)
            comboAiModel.model = arr
            comboAiModel.currentIndex = 0
        }
        root.currentModel = comboAiModel.currentText
    }

    function loadInitialSettings() {
        if (bridge) {
            currentProvider = bridge.defaultAiProvider()
            currentApiKey = bridge.defaultAiApiKey()
            var savedM = bridge.defaultAiModel()
            if (provCombo) {
                provCombo.currentIndex = getChatProvIdx(currentProvider)
            }
            if (comboAiModel) {
                comboAiModel.model = defaultAiModels(currentProvider)
                setAiModelSelection(savedM)
            }
            if (apiKeyInput) {
                apiKeyInput.text = currentApiKey
            }
        }
    }

    Component.onCompleted: {
        loadInitialSettings()
    }

    Connections {
        target: bridge
        function onRequestOpenAgentChatWindow(initialPrompt, ctx) {
            root.visible = true
            root.show()
            root.raise()
            root.requestActivate()

            loadInitialSettings()

            if (ctx && ctx.length > 0) {
                root.contextTopic = ctx
            }
            if (initialPrompt && initialPrompt.length > 0) {
                inputPrompt.text = initialPrompt
                sendQuestion(initialPrompt)
            }
        }

        function onAiAnswerStarted(q) {
            root.isGenerating = true
            var msgs = root.chatMessages.slice()
            if (msgs.length > 0 && msgs[msgs.length - 1].role === "assistant" && msgs[msgs.length - 1].loading) {
                return
            }
            msgs.push({
                "role": "assistant",
                "content": "正在深度思考与组织解答...",
                "provider": root.currentProvider,
                "model": root.currentModel,
                "time": Qt.formatTime(new Date(), "HH:mm:ss"),
                "loading": true,
                "isError": false
            })
            root.chatMessages = msgs
            Qt.callLater(scrollChatToBottom)
        }

        function onAiAnswerReceived(q, answer, prov, mdl) {
            root.isGenerating = false
            var msgs = root.chatMessages.slice()
            if (msgs.length > 0 && msgs[msgs.length - 1].role === "assistant" && msgs[msgs.length - 1].loading) {
                msgs[msgs.length - 1].content = answer
                msgs[msgs.length - 1].loading = false
                msgs[msgs.length - 1].provider = prov
                msgs[msgs.length - 1].model = mdl
            } else {
                msgs.push({
                    "role": "assistant",
                    "content": answer,
                    "provider": prov,
                    "model": mdl,
                    "time": Qt.formatTime(new Date(), "HH:mm:ss"),
                    "loading": false,
                    "isError": false
                })
            }
            root.chatMessages = msgs
            Qt.callLater(scrollChatToBottom)
        }

        function onModelsLoaded(models) {
            if (models && models.length > 0) {
                comboAiModel.model = models
                var found = -1
                for (var i = 0; i < models.length; ++i) {
                    if (models[i] === root.currentModel) {
                        found = i
                        break
                    }
                }
                comboAiModel.currentIndex = (found >= 0) ? found : 0
                root.currentModel = comboAiModel.currentText
                modelFetchStatusText.text = "✅ 成功拉取 " + models.length + " 个可用模型，请在下拉菜单点选"
                modelFetchStatusText.color = "#52C41A"
            } else {
                modelFetchStatusText.text = "⚠️ 未解析到可用模型列表"
                modelFetchStatusText.color = "#FFD04B"
            }
        }

        function onModelsLoadFailed(err) {
            modelFetchStatusText.text = "❌ " + err
            modelFetchStatusText.color = "#FF4D4F"
        }

        function onAiAnswerFailed(q, err) {
            root.isGenerating = false
            var msgs = root.chatMessages.slice()
            if (msgs.length > 0 && msgs[msgs.length - 1].role === "assistant" && msgs[msgs.length - 1].loading) {
                msgs[msgs.length - 1].content = "⚠️ 生成失败: " + err
                msgs[msgs.length - 1].loading = false
                msgs[msgs.length - 1].isError = true
            } else {
                msgs.push({
                    "role": "assistant",
                    "content": "⚠️ 生成失败: " + err,
                    "provider": root.currentProvider,
                    "model": root.currentModel,
                    "time": Qt.formatTime(new Date(), "HH:mm:ss"),
                    "loading": false,
                    "isError": true
                })
            }
            root.chatMessages = msgs
            Qt.callLater(scrollChatToBottom)
        }
    }

    function scrollChatToBottom() {
        chatFlickable.contentY = Math.max(0, chatCol.implicitHeight - chatFlickable.height)
    }

    function sendQuestion(text) {
        var q = text ? text.trim() : inputPrompt.text.trim()
        if (q.length === 0 || root.isGenerating) return

        if (!root.currentApiKey || root.currentApiKey.length === 0) {
            if (bridge) root.currentApiKey = bridge.defaultAiApiKey()
        }
        if (!root.currentModel || root.currentModel.length === 0) {
            if (bridge) root.currentModel = bridge.defaultAiModel()
        }

        var msgs = root.chatMessages.slice()
        msgs.push({
            "role": "user",
            "content": q,
            "time": Qt.formatTime(new Date(), "HH:mm:ss"),
            "loading": false,
            "isError": false
        })
        root.chatMessages = msgs

        inputPrompt.text = ""
        Qt.callLater(scrollChatToBottom)

        if (bridge) {
            bridge.askAiQuestion(q, root.contextTopic, root.currentProvider, root.currentModel, root.currentApiKey, root.customEndpoint)
        }
    }

    TextEdit {
        id: copyHelper
        visible: false
    }

    component DarkComboBox: ComboBox {
        id: dcombo
        font.pixelSize: 11
        contentItem: Text {
            leftPadding: 8
            rightPadding: dcombo.indicator ? (dcombo.indicator.width + dcombo.spacing) : 16
            text: dcombo.displayText
            font: dcombo.font
            color: "#E6E6E6"
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitHeight: 28
            color: "#11121A"
            border.color: dcombo.activeFocus ? "#40A9FF" : "#35384D"
            border.width: 1
            radius: 4
        }
        popup: Popup {
            y: dcombo.height + 2
            width: Math.max(dcombo.width, 220)
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
            width: Math.max(dcombo.width, 220) - 8
            height: 28
            contentItem: Text {
                text: modelData
                color: highlighted ? "#FFFFFF" : "#C0C0D0"
                font.pixelSize: 11
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: highlighted ? "#2A2A38" : "transparent"
                radius: 3
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ========================================================
        // 顶部工具栏
        // ========================================================
        Rectangle {
            Layout.fillWidth: true
            height: 54
            color: "#1E1F2B"
            border.color: "#2C2D3E"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Text {
                    text: "🤖"
                    font.pixelSize: 20
                    Layout.alignment: Qt.AlignVCenter
                }

                Column {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 2

                    Row {
                        spacing: 8
                        Text {
                            text: "AI 智能问答与知识延伸"
                            color: "#FFFFFF"
                            font.pixelSize: 14
                            font.bold: true
                        }
                        Rectangle {
                            height: 18
                            radius: 3
                            color: (root.currentProvider === "gemini") ? "#172A24" : "#1B2230"
                            border.color: (root.currentProvider === "gemini") ? "#29664B" : "#2E3B52"
                            border.width: 1
                            implicitWidth: provBadgeText.implicitWidth + 8
                            Text {
                                id: provBadgeText
                                anchors.centerIn: parent
                                text: (root.currentProvider === "gemini") ? ("Google Gemini (" + root.currentModel + ")") : (root.currentProvider.toUpperCase() + " (" + root.currentModel + ")")
                                color: (root.currentProvider === "gemini") ? "#6EE7B7" : "#8AB4F8"
                                font.pixelSize: 9
                                font.bold: true
                            }
                        }
                    }

                    Text {
                        text: root.isGenerating ? "⚡ 正在向模型请求思考与深度推导..." : "支持 Google Gemini / Grok / DeepSeek · 探讨 C++ 底层、实战与面试考点"
                        color: root.isGenerating ? "#FFD700" : "#8E8EA0"
                        font.pixelSize: 10
                    }
                }

                // 快捷操作
                Row {
                    spacing: 8
                    Layout.alignment: Qt.AlignVCenter

                    // 配置面板开关
                    Button {
                        text: root.showConfigPanel ? "收起配置" : "⚙️ 模型/Key配置"
                        font.pixelSize: 11
                        contentItem: Text {
                            text: parent.text
                            color: root.showConfigPanel ? "#FFFFFF" : "#C4C5D8"
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: root.showConfigPanel ? "#3B3D54" : (parent.hovered ? "#2C2D3E" : "#20212E")
                            border.color: root.showConfigPanel ? "#8AB4F8" : "#36384C"
                            border.width: 1
                        }
                        onClicked: root.showConfigPanel = !root.showConfigPanel
                    }

                    // 清屏按钮
                    Button {
                        text: "🗑️ 清屏"
                        font.pixelSize: 11
                        contentItem: Text {
                            text: parent.text
                            color: "#A2A4BA"
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? "#331E20" : "#221C20"
                            border.color: "#4A2B2D"
                            border.width: 1
                        }
                        onClicked: {
                            root.chatMessages = []
                        }
                    }
                }
            }
        }

        // ========================================================
        // 模型/Key 快速配置面板 (可折叠)
        // ========================================================
        Rectangle {
            visible: root.showConfigPanel
            Layout.fillWidth: true
            color: "#181924"
            border.color: "#2C2E42"
            border.width: 1
            implicitHeight: configCol.implicitHeight + 20

            ColumnLayout {
                id: configCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text { text: "模型平台:"; color: "#C0C1D4"; font.pixelSize: 11; width: 60 }
                    DarkComboBox {
                        id: provCombo
                        Layout.preferredWidth: 160
                        model: {
                            var arr = []
                            for (var i = 0; i < root.aiChatProviders.length; ++i) {
                                arr.push(root.aiChatProviders[i].name)
                            }
                            return arr
                        }
                        currentIndex: root.getChatProvIdx(root.currentProvider)
                        onActivated: function(idx) {
                            var item = root.aiChatProviders[idx]
                            if (item) {
                                root.currentProvider = item.id
                                var dList = defaultAiModels(item.id)
                                comboAiModel.model = dList
                                comboAiModel.currentIndex = 0
                                root.currentModel = dList[0]
                                modelFetchStatusText.text = "已切换至 " + item.name + " 推荐模型列表，支持在下拉菜单中直接选择"
                                modelFetchStatusText.color = "#8E8EA0"
                            }
                        }
                    }

                    Text { text: "选择模型:"; color: "#C0C1D4"; font.pixelSize: 11 }
                    DarkComboBox {
                        id: comboAiModel
                        Layout.preferredWidth: 180
                        model: defaultAiModels(root.currentProvider)
                        onActivated: function(idx) {
                            root.currentModel = currentText
                        }
                    }

                    Button {
                        text: "🔄 在线拉取"
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
                            color: parent.hovered ? "#3B7CD6" : "#2458A6"
                        }
                        onClicked: {
                            modelFetchStatusText.text = "正在向 " + root.currentProvider.toUpperCase() + " 在线拉取可用模型列表..."
                            modelFetchStatusText.color = "#FFD04B"
                            if (bridge) {
                                bridge.fetchModelList(root.currentProvider, root.currentApiKey)
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "💾 保存为系统默认"
                        font.pixelSize: 11
                        font.bold: true
                        contentItem: Text {
                            text: parent.text; color: "#16161A"; font.bold: true; font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? "#F5C842" : "#E5B842"
                        }
                        onClicked: {
                            root.currentModel = comboAiModel.currentText ? comboAiModel.currentText.trim() : root.currentModel
                            if (bridge) {
                                bridge.saveAiSettings(root.currentProvider, root.currentApiKey, root.currentModel)
                            }
                            root.showConfigPanel = false
                        }
                    }
                }

                Text {
                    id: modelFetchStatusText
                    text: "💡 支持直接在下拉菜单中选择平台预置推荐模型，或点击「在线拉取」获取账号下所有可用模型"
                    color: "#8E8EA0"
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    Layout.leftMargin: 72
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text { text: "API 秘钥:"; color: "#C0C1D4"; font.pixelSize: 11; width: 60 }
                    TextField {
                        id: apiKeyInput
                        Layout.fillWidth: true
                        text: root.currentApiKey
                        color: "#FFFFFF"
                        echoMode: chkShowKey.checked ? TextInput.Normal : TextInput.Password
                        placeholderText: (root.currentProvider === "gemini") ? "在此输入 Gemini API Key (以 AIzaSy 开头)" : "在此输入对应平台 API Key"
                        background: Rectangle { radius: 4; color: "#11121A"; border.color: "#35384D" }
                        onTextChanged: root.currentApiKey = text.trim()
                    }

                    Rectangle {
                        width: 26
                        height: 26
                        radius: 4
                        color: chkShowKey.checked ? "#3A3D54" : "#242533"
                        border.color: "#3F4259"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: chkShowKey.checked ? "👁️" : "🔒"
                            font.pixelSize: 11
                        }
                        MouseArea {
                            id: chkShowKey
                            property bool checked: false
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: checked = !checked
                        }
                    }
                }
            }
        }

        // ========================================================
        // 课题研讨背景横幅 (如有)
        // ========================================================
        Rectangle {
            visible: root.contextTopic.length > 0
            Layout.fillWidth: true
            height: 34
            color: "#221D12"
            border.color: "#5C4617"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                Text {
                    text: "📌 当前关联研讨主题:"
                    color: "#FFD700"
                    font.pixelSize: 11
                    font.bold: true
                }

                Text {
                    text: root.contextTopic
                    color: "#D6D7E8"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Rectangle {
                    width: 20
                    height: 20
                    radius: 10
                    color: clearCtxMouse.containsMouse ? "#E84A5F" : "#2C2518"
                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: clearCtxMouse.containsMouse ? "#FFFFFF" : "#A2A4BA"
                        font.pixelSize: 10
                    }
                    MouseArea {
                        id: clearCtxMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.contextTopic = ""
                    }
                }
            }
        }

        // ========================================================
        // 核心聊天对话流视口
        // ========================================================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 空状态欢迎引导
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                visible: root.chatMessages.length === 0

                Text {
                    text: "🤖"
                    font.pixelSize: 42
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    text: "欢迎开启 C++ 知识深度问答与延伸"
                    color: "#FFFFFF"
                    font.pixelSize: 15
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    text: "随时追问底层内存布局、智能指针生命周期、模板元编程、性能避坑与面试重点。\n点击下方推荐拓展或直接在输入框提问："
                    color: "#8E8EA0"
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    Layout.alignment: Qt.AlignHCenter
                }

                // 推荐初始问题标签组
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8

                    Repeater {
                        model: [
                            "深入剖析 shared_ptr 控制块与 make_shared 底层差异",
                            "右值引用与完美转发 (std::forward) 的本质",
                            "虚函数表 (vptr/vtbl) 的底层内存排布机制"
                        ]
                        delegate: Rectangle {
                            height: 28
                            radius: 14
                            color: chipMouse.containsMouse ? "#2E364A" : "#1D2230"
                            border.color: chipMouse.containsMouse ? "#8AB4F8" : "#2C354A"
                            border.width: 1
                            implicitWidth: chipText.implicitWidth + 20

                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text: modelData
                                color: "#A8BBE6"
                                font.pixelSize: 11
                            }
                            MouseArea {
                                id: chipMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: sendQuestion(modelData)
                            }
                        }
                    }
                }
            }

            // 滚动消息流
            Flickable {
                id: chatFlickable
                anchors.fill: parent
                anchors.margins: 14
                clip: true
                contentHeight: chatCol.implicitHeight

                ColumnLayout {
                    id: chatCol
                    width: chatFlickable.width
                    spacing: 14

                    Repeater {
                        model: root.chatMessages
                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            // 角色标志与时间栏
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: (modelData.role === "user") ? "👤 我" : ("🤖 " + (modelData.provider === "gemini" ? "Google Gemini" : modelData.provider.toUpperCase()))
                                    color: (modelData.role === "user") ? "#8AB4F8" : "#FFD700"
                                    font.pixelSize: 11
                                    font.bold: true
                                }

                                Text {
                                    text: modelData.time || ""
                                    color: "#6E7084"
                                    font.pixelSize: 10
                                }

                                Item { Layout.fillWidth: true }

                                // 复制回答
                                Text {
                                    visible: modelData.role === "assistant" && !modelData.loading
                                    id: copyAnsLbl
                                    property bool copied: false
                                    text: copied ? "✓ 已复制" : "📋 复制回答"
                                    color: copied ? "#52C41A" : "#8A8B9D"
                                    font.pixelSize: 10
                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            copyHelper.text = modelData.content
                                            copyHelper.selectAll()
                                            copyHelper.copy()
                                            copyAnsLbl.copied = true
                                            resetAnsCopyTimer.restart()
                                        }
                                    }
                                    Timer {
                                        id: resetAnsCopyTimer
                                        interval: 2000
                                        onTriggered: copyAnsLbl.copied = false
                                    }
                                }
                            }

                            // 消息气泡卡片
                            Rectangle {
                                Layout.fillWidth: true
                                radius: 8
                                color: (modelData.role === "user") ? "#1C2433" : (modelData.isError ? "#301A1C" : "#1B1C28")
                                border.color: (modelData.role === "user") ? "#2B3A54" : (modelData.isError ? "#66282B" : "#2E3044")
                                border.width: 1
                                implicitHeight: msgContentText.implicitHeight + 20

                                Text {
                                    id: msgContentText
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    text: modelData.content
                                    textFormat: (modelData.role === "assistant" && !modelData.isError) ? Text.MarkdownText : Text.PlainText
                                    color: modelData.isError ? "#FFA39E" : "#E2E2EC"
                                    font.family: (modelData.role === "assistant") ? "Consolas, 'Segoe UI', monospace" : "sans-serif"
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }
        }

        // ========================================================
        // 一键延伸快捷追问 Chips 栏
        // ========================================================
        Rectangle {
            Layout.fillWidth: true
            height: 38
            color: "#181924"
            border.color: "#27293A"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    text: "快捷延伸:"
                    color: "#8E90A4"
                    font.pixelSize: 10
                }

                Repeater {
                    model: [
                        { "label": "💡 底层内存与原理", "prompt": "请直击底层本质，深入剖析其在内存中的具体分配模型、对象生命周期及底层编译器指令。" },
                        { "label": "💻 工业级生产实战案例", "prompt": "请给出 2 个工业级高性能实战代码案例，带有详细注释说明与关键行拆解。" },
                        { "label": "⚠️ 面试高频必考与避坑", "prompt": "请总结一线大厂在考察该知识点时的高频连环追问、易犯的踩坑点以及如何优雅防御规避。" },
                        { "label": "🔄 生动通俗比喻讲解", "prompt": "请用生活化或极其生动形象的通俗比喻，深入浅出地讲解该特性的工作机理与解决的核心痛点。" }
                    ]
                    delegate: Rectangle {
                        height: 24
                        radius: 12
                        color: extChipMouse.containsMouse ? "#2E3547" : "#1F2330"
                        border.color: extChipMouse.containsMouse ? "#FFD700" : "#32374A"
                        border.width: 1
                        implicitWidth: extChipText.implicitWidth + 14

                        Text {
                            id: extChipText
                            anchors.centerIn: parent
                            text: modelData.label
                            color: extChipMouse.containsMouse ? "#FFD700" : "#BAC2D8"
                            font.pixelSize: 10
                        }
                        MouseArea {
                            id: extChipMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sendQuestion(modelData.prompt)
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // ========================================================
        // 底部提问输入栏
        // ========================================================
        Rectangle {
            Layout.fillWidth: true
            height: 80
            color: "#1B1C26"
            border.color: "#2C2D3E"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    TextArea {
                        id: inputPrompt
                        color: "#FFFFFF"
                        placeholderText: "在此向大模型输入您关于 C++ 核心知识体系的问题或追问... (按 Enter 发送，Shift+Enter 换行)"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        background: Rectangle {
                            radius: 6
                            color: "#12131C"
                            border.color: inputPrompt.activeFocus ? "#4D7CFE" : "#2E3044"
                            border.width: 1
                        }
                        Keys.onReturnPressed: function(event) {
                            if (event.modifiers & Qt.ShiftModifier) {
                                event.accepted = false
                            } else {
                                event.accepted = true
                                sendQuestion()
                            }
                        }
                    }
                }

                // 发送 / 终止按钮
                Button {
                    Layout.preferredWidth: 90
                    Layout.fillHeight: true
                    text: root.isGenerating ? "⏹ 停止" : "发送 🚀"
                    font.bold: true
                    font.pixelSize: 11
                    contentItem: Text {
                        text: parent.text
                        color: root.isGenerating ? "#FFFFFF" : "#16161A"
                        font.bold: true
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 6
                        color: root.isGenerating ? (parent.hovered ? "#FF4D4F" : "#E84A5F") : (parent.hovered ? "#F5C842" : "#E5B842")
                    }
                    onClicked: {
                        if (root.isGenerating) {
                            if (bridge) bridge.cancelAiQuestion()
                            root.isGenerating = false
                        } else {
                            sendQuestion()
                        }
                    }
                }
            }
        }
    }
}
