import QtQuick

import QtQuick.Controls

import QtQuick.Layouts



Window {

    id: root

    objectName: "agentWindow"

    title: "自动化 Agent 任务中心 - GoldPriceBar"

    width: sidePanelOpen ? 1220 : 780

    height: 600

    minimumWidth: 780

    minimumHeight: 480

    color: "#16161A"

    visible: false



    Behavior on width {

        NumberAnimation { duration: 250; easing.type: Easing.OutCubic }

    }



    onSidePanelOpenChanged: {

        if (!sidePanelOpen) {
            leftListCollapsed = false
        } else {

            var scrW = (root.screen ? root.screen.desktopAvailableWidth : Screen.desktopAvailableWidth)

            if (scrW > 0 && root.x + 1220 > scrW) {

                root.x = Math.max(10, scrW - 1230)

            }

            if (sidePanelTab === 0) {

                loadPreviewDay(previewDay)

            } else {

                loadNotesList()

            }

        }

    }



    property var taskList: []

    property bool isEditing: false

    property var previewTopic: null

    property int previewDay: 1
    property string currentCurriculumAgentId: "cpp_daily_master"
    property string currentCurriculumName: "C++ 30天知识体系阶梯通关"



    // 右侧扩展面板状态

    property bool sidePanelOpen: false
    property bool leftListCollapsed: false

    onLeftListCollapsedChanged: {
        if (leftListCollapsed) {
            if (!sidePanelOpen) {
                sidePanelOpen = true
                if (sidePanelTab === 0) {
                    loadPreviewDay(previewDay)
                } else {
                    loadNotesList()
                }
            }
        }
    }

    property int sidePanelTab: 0 // 0: 课程大纲, 1: 学习笔记

    property var noteFiles: []

    property string selectedNoteFile: ""

    property string noteContent: ""



    function reloadTasks() {

        if (bridge) {

            taskList = bridge.getAgentTasks()

        }

    }



    function loadPreviewDay(day) {
        var total = 30
        if (bridge && bridge.getCurriculumTotalDaysForAgent) {
            total = bridge.getCurriculumTotalDaysForAgent(root.currentCurriculumAgentId)
            if (total <= 0) total = 30
        }
        previewDay = Math.max(1, Math.min(total, day))
        if (bridge) {
            if (bridge.getCurriculumTopicForAgent) {
                previewTopic = bridge.getCurriculumTopicForAgent(root.currentCurriculumAgentId, previewDay)
            } else {
                previewTopic = bridge.getCurriculumTopic(previewDay)
            }
        }
    }



    function loadNotesList() {

        if (bridge) {

            noteFiles = bridge.getNoteFiles()

            if (noteFiles.length > 0) {

                if (selectedNoteFile === "" || noteFiles.indexOf(selectedNoteFile) === -1) {

                    selectedNoteFile = noteFiles[0]

                }

                loadSelectedNote()

            } else {

                selectedNoteFile = ""

                noteContent = ""

            }

        }

    }



    function loadSelectedNote() {

        if (bridge && selectedNoteFile.length > 0) {

            noteContent = bridge.getNoteContent(selectedNoteFile)

        }

    }



    Component.onCompleted: {

        reloadTasks()

        loadPreviewDay(1)

    }



    Connections {

        target: bridge

        function onRequestShowAgentWindow() {

            root.visible = true

            root.show()

            root.raise()

            root.requestActivate()

            root.reloadTasks()

        }

        function onAgentTasksUpdated() {

            root.reloadTasks()

            if (sidePanelOpen && sidePanelTab === 1) {

                loadNotesList()

            }

        }

        function onRequestOpenNoteInAgentWindow(fileName) {

            root.visible = true

            root.show()

            root.raise()

            root.requestActivate()

            root.sidePanelOpen = true

            root.sidePanelTab = 1

            root.loadNotesList()

            if (fileName && fileName.length > 0) {

                root.selectedNoteFile = fileName

                root.loadSelectedNote()

            }

        }

    }



    // 隐藏的剪贴板辅助对象

    TextEdit {

        id: hiddenClipboard

        visible: false

    }



    // 主视口布局：左侧主任务管理 + (可选) 右侧扩展详情抽屉

    RowLayout {

        anchors.fill: parent

        spacing: 0



        // ========================================================

        // 左侧栏：主任务管理与控制台 (展开时 780，收起时 48)

        // ========================================================

        Item {
            id: leftPanelContainer
            Layout.preferredWidth: root.leftListCollapsed ? 48 : (root.sidePanelOpen ? 780 : root.width)
            Layout.fillWidth: !root.sidePanelOpen && !root.leftListCollapsed
            Layout.fillHeight: true
            clip: true

            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
            }

            ColumnLayout {
                anchors.fill: parent
                visible: !root.leftListCollapsed
                spacing: 0



            // 顶部导航与功能栏

            Rectangle {

                Layout.fillWidth: true

                height: 60

                color: "#1E1E24"

                border.color: "#2B2B36"

                border.width: 1



                RowLayout {

                    anchors.fill: parent

                    anchors.leftMargin: 16

                    anchors.rightMargin: 16

                    spacing: 12



                    Text {

                        text: "🤖"

                        font.pixelSize: 22

                        Layout.alignment: Qt.AlignVCenter

                    }



                    Column {

                        Layout.alignment: Qt.AlignVCenter

                        spacing: 2

                        Layout.fillWidth: true



                        Text {

                            text: "自动化 Agent 任务管理中心"

                            color: "#FFFFFF"

                            font.pixelSize: 14

                            font.bold: true

                        }

                        Text {

                            text: "定点推送 · 阶梯课程 · 系统监控 · 支持工作日排程与本地 SQLite 归档"

                            color: "#8E8EA0"

                            font.pixelSize: 11

                        }

                    }



                    // 快捷操作按钮组

                    Row {

                        spacing: 8

                        Layout.alignment: Qt.AlignVCenter



                        // 课程大纲按钮 (展开右侧面板)

                        Button {

                            text: (root.sidePanelOpen && root.sidePanelTab === 0) ? "📖 收起大纲" : "📖 课程大纲"

                            font.pixelSize: 11

                            contentItem: Text {

                                text: parent.text

                                color: (root.sidePanelOpen && root.sidePanelTab === 0) ? "#FFFFFF" : "#FFD700"

                                font.pixelSize: 11

                                font.bold: (root.sidePanelOpen && root.sidePanelTab === 0)

                                horizontalAlignment: Text.AlignHCenter

                                verticalAlignment: Text.AlignVCenter

                            }

                            background: Rectangle {

                                radius: 4

                                color: (root.sidePanelOpen && root.sidePanelTab === 0) ? "#5E4612" : (parent.hovered ? "#3B3219" : "#2A2412")

                                border.color: (root.sidePanelOpen && root.sidePanelTab === 0) ? "#FFD700" : "#66521A"

                                border.width: 1

                            }

                            onClicked: {

                                if (root.sidePanelOpen && root.sidePanelTab === 0) {

                                    root.sidePanelOpen = false

                                } else {

                                    root.sidePanelTab = 0

                                    root.sidePanelOpen = true

                                    root.loadPreviewDay(root.previewDay)

                                }

                            }

                        }



                        // 学习笔记按钮 (展开右侧面板)

                        Button {

                            text: (root.sidePanelOpen && root.sidePanelTab === 1) ? "📝 收起笔记" : "📝 学习笔记"

                            font.pixelSize: 11

                            contentItem: Text {

                                text: parent.text

                                color: (root.sidePanelOpen && root.sidePanelTab === 1) ? "#FFFFFF" : "#D0D0E0"

                                font.pixelSize: 11

                                font.bold: (root.sidePanelOpen && root.sidePanelTab === 1)

                                horizontalAlignment: Text.AlignHCenter

                                verticalAlignment: Text.AlignVCenter

                            }

                            background: Rectangle {

                                radius: 4

                                color: (root.sidePanelOpen && root.sidePanelTab === 1) ? "#3C405C" : (parent.hovered ? "#353647" : "#242533")

                                border.color: (root.sidePanelOpen && root.sidePanelTab === 1) ? "#8AB4F8" : "#404259"

                                border.width: 1

                            }

                            onClicked: {

                                if (root.sidePanelOpen && root.sidePanelTab === 1) {

                                    root.sidePanelOpen = false

                                } else {

                                    root.sidePanelTab = 1

                                    root.sidePanelOpen = true

                                    root.loadNotesList()

                                }

                            }

                        }



                        // AI 问答按钮

                        Button {

                            text: "🤖 AI 问答"

                            font.pixelSize: 11

                            contentItem: Text {

                                text: parent.text

                                color: "#93C5FD"

                                font.pixelSize: 11

                                horizontalAlignment: Text.AlignHCenter

                                verticalAlignment: Text.AlignVCenter

                            }

                            background: Rectangle {

                                radius: 4

                                color: parent.hovered ? "#212E47" : "#172033"

                                border.color: "#304D78"

                                border.width: 1

                            }

                            onClicked: {

                                if (bridge) bridge.openAgentChatWindow()

                            }

                        }



                        // 新增 Agent 按钮

                        Button {

                            text: "+ 新增 Agent"

                            font.pixelSize: 11

                            font.bold: true

                            contentItem: Text {

                                text: parent.text

                                color: "#FFFFFF"

                                font.pixelSize: 11

                                font.bold: true

                                horizontalAlignment: Text.AlignHCenter

                                verticalAlignment: Text.AlignVCenter

                            }

                            background: Rectangle {

                                radius: 4

                                color: parent.hovered ? "#4D7CFE" : "#386BF6"

                            }

                            onClicked: {

                                openAddDialog()

                            }

                        }

                        // 向左收起任务列表按钮
                        Button {
                            text: "◀ 收起列表"
                            font.pixelSize: 11
                            contentItem: Text {
                                text: parent.text
                                color: parent.hovered ? "#FFFFFF" : "#C5C7D8"
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? "#36384C" : "#242533"
                                border.color: parent.hovered ? "#565978" : "#383A52"
                                border.width: 1
                            }
                            onClicked: {
                                root.leftListCollapsed = true
                                if (!root.sidePanelOpen) {
                                    root.sidePanelOpen = true
                                    root.sidePanelTab = 0
                                    root.loadPreviewDay(root.previewDay)
                                }
                            }
                        }

                    }

                }

            }



            // 主体任务列表展示

            ScrollView {

                Layout.fillWidth: true

                Layout.fillHeight: true

                clip: true



                ListView {

                    id: agentListView

                    width: parent.width

                    model: root.taskList

                    spacing: 12

                    topMargin: 12

                    bottomMargin: 12



                    delegate: Rectangle {

                        width: agentListView.width - 24

                        anchors.horizontalCenter: parent.horizontalCenter

                        radius: 8

                        color: "#1E1F2B"

                        border.color: modelData.enabled ? "#383B4F" : "#2A2B38"

                        border.width: 1

                        implicitHeight: cardCol.implicitHeight + 24



                        ColumnLayout {

                            id: cardCol

                            anchors.fill: parent

                            anchors.margins: 14

                            spacing: 10



                            // 头部：类型标签、名称、状态

                            RowLayout {

                                Layout.fillWidth: true

                                spacing: 8



                                Rectangle {

                                    height: 20

                                    radius: 3

                                    color: (modelData.type === "curriculum") ? "#2B2414" : ((modelData.type === "command") ? "#17261B" : "#192233")

                                    border.color: (modelData.type === "curriculum") ? "#80661A" : ((modelData.type === "command") ? "#296638" : "#2A4B80")

                                    border.width: 1

                                    implicitWidth: typeBadgeText.implicitWidth + 10



                                    Text {

                                        id: typeBadgeText

                                        anchors.centerIn: parent

                                        text: (modelData.id === "cpp_daily_master") ? "📚 C++ 通关课程" : ((modelData.type === "curriculum") ? "📚 阶梯式课程" : ((modelData.type === "command") ? "💻 脚本监控" : "⏰ 智能任务"))

                                        color: (modelData.type === "curriculum") ? "#FFD700" : ((modelData.type === "command") ? "#6EE7B7" : "#93C5FD")

                                        font.pixelSize: 10

                                        font.bold: true

                                    }

                                }



                                Text {

                                    text: modelData.name

                                    color: modelData.enabled ? "#FFFFFF" : "#8A8B9C"

                                    font.pixelSize: 13

                                    font.bold: true

                                    Layout.fillWidth: true

                                }



                                // 工作日标签

                                Rectangle {

                                    property bool isWorkdayOnly: (modelData.content && modelData.content.skip_weekends) || (modelData.scheduleRule && modelData.scheduleRule.indexOf("workday@") !== -1)

                                    height: 18

                                    radius: 3

                                    color: isWorkdayOnly ? "#282314" : "#1B2230"

                                    border.color: isWorkdayOnly ? "#6B5518" : "#2E3B52"

                                    border.width: 1

                                    implicitWidth: schedTagText.implicitWidth + 8



                                    Text {

                                        id: schedTagText

                                        anchors.centerIn: parent

                                        text: parent.isWorkdayOnly ? "📅 仅工作日" : "📅 每天执行"

                                        color: parent.isWorkdayOnly ? "#FFD700" : "#8AB4F8"

                                        font.pixelSize: 9

                                    }

                                }



                                Rectangle {

                                    height: 18

                                    radius: 9

                                    color: modelData.enabled ? "#13261B" : "#26262B"

                                    border.color: modelData.enabled ? "#2E7D46" : "#4A4A52"

                                    border.width: 1

                                    implicitWidth: statusText.implicitWidth + 12



                                    Text {

                                        id: statusText

                                        anchors.centerIn: parent

                                        text: modelData.enabled ? "🟢 调度中" : "⏸ 已暂停"

                                        color: modelData.enabled ? "#52C41A" : "#888888"

                                        font.pixelSize: 10

                                    }

                                }

                            }



                            // 调度时间与执行信息

                            RowLayout {

                                Layout.fillWidth: true

                                spacing: 16



                                Text {

                                    text: "⏰ 调度规则: " + modelData.scheduleRule

                                    color: "#A5A6B8"

                                    font.pixelSize: 11

                                }

                                Text {

                                    text: "🕒 下次执行: " + (modelData.nextRun ? modelData.nextRun : "计算中")

                                    color: "#8AB4F8"

                                    font.pixelSize: 11

                                }

                                Text {

                                    text: "✓ 上次执行: " + (modelData.lastRun ? modelData.lastRun : "尚未执行")

                                    color: "#78798A"

                                    font.pixelSize: 11

                                }

                                // 当左侧列表收起时，在右侧面板顶部展示快速展开按钮
                        Button {
                            visible: root.leftListCollapsed
                            text: "▶ 展开任务列表"
                            font.pixelSize: 11
                            contentItem: Text {
                                text: parent.text
                                color: "#FFD700"
                                font.pixelSize: 11
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? "#42381A" : "#282312"
                                border.color: "#80661A"
                                border.width: 1
                            }
                            onClicked: {
                                root.leftListCollapsed = false
                            }
                        }

                        Item { Layout.fillWidth: true }

                            }



                            // 课程类专属进度条

                            ColumnLayout {

                                Layout.fillWidth: true

                                spacing: 4

                                visible: modelData.type === "curriculum"



                                RowLayout {

                                    Layout.fillWidth: true

                                    Text {

                                        text: "学习进度：第 " + (modelData.content && modelData.content.current_day ? modelData.content.current_day : 1) + " / " + (modelData.content && modelData.content.total_days ? modelData.content.total_days : 30) + " 天"

                                        color: "#E2E2EC"

                                        font.pixelSize: 11

                                        font.bold: true

                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {

                                        property int curDay: modelData.content && modelData.content.current_day ? modelData.content.current_day : 1

                                        property int totDay: modelData.content && modelData.content.total_days ? modelData.content.total_days : 30

                                        text: Math.min(100, Math.round(curDay * 100 / totDay)) + "%"

                                        color: "#FFD700"

                                        font.pixelSize: 11

                                        font.bold: true

                                    }

                                }



                                // 进度条槽

                                Rectangle {

                                    Layout.fillWidth: true

                                    height: 6

                                    radius: 3

                                    color: "#14141E"

                                    border.color: "#2B2C3D"

                                    border.width: 1



                                    Rectangle {

                                        height: parent.height

                                        radius: 3

                                        property int curDay: modelData.content && modelData.content.current_day ? modelData.content.current_day : 1

                                        property int totDay: modelData.content && modelData.content.total_days ? modelData.content.total_days : 30

                                        width: Math.min(parent.width, Math.max(8, parent.width * (curDay / totDay)))

                                        color: "#E5B842"

                                    }

                                }

                            }



                            // 任务摘要 / 今日课题 (对课程类直接展示今日题目与重点)
                            Text {
                                visible: true
                                text: (modelData.type === "curriculum")
                                    ? (function() {
                                        var cDay = (modelData.content && modelData.content.current_day) ? modelData.content.current_day : 1;
                                        var top = bridge ? bridge.getCurriculumTopicForAgent(modelData.id, cDay) : null;
                                        var titleStr = (top && top.title) ? top.title : (modelData.content && modelData.content.prompt ? modelData.content.prompt : "");
                                        return "今日课题 (Day " + (cDay < 10 ? "0" : "") + cDay + "): " + titleStr;
                                    })()
                                    : ((modelData.type === "command")
                                        ? ("执行指令: " + (modelData.content && modelData.content.command ? modelData.content.command : ""))
                                        : ("内容: " + (modelData.content && (modelData.content.message || modelData.content.prompt) ? (modelData.content.message || modelData.content.prompt) : (modelData.name || ""))))
                                color: (modelData.type === "curriculum") ? "#FFD700" : "#8E8EA0"
                                font.pixelSize: 11
                                font.bold: (modelData.type === "curriculum")
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }



                            // 分割线

                            Rectangle {

                                Layout.fillWidth: true

                                height: 1

                                color: "#282938"

                            }



                            // 底部操作按钮组

                            RowLayout {

                                Layout.fillWidth: true

                                spacing: 8



                                // 立即执行/测试

                                Button {

                                    text: "▶ 立即测试执行"

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

                                        color: parent.hovered ? "#4D7CFE" : "#386BF6"

                                    }

                                    onClicked: {

                                        if (bridge) bridge.runAgentTaskNow(modelData.id)

                                    }

                                }



                                // 暂停 / 启用

                                Button {

                                    text: modelData.enabled ? "⏸ 暂停" : "▶ 启用"

                                    font.pixelSize: 11

                                    contentItem: Text {

                                        text: parent.text

                                        color: modelData.enabled ? "#E2E2EC" : "#52C41A"

                                        font.pixelSize: 11

                                        horizontalAlignment: Text.AlignHCenter

                                        verticalAlignment: Text.AlignVCenter

                                    }

                                    background: Rectangle {

                                        radius: 4

                                        color: parent.hovered ? "#383A4D" : "#282938"

                                        border.color: "#3F4157"

                                        border.width: 1

                                    }

                                    onClicked: {

                                        if (bridge) bridge.toggleAgentTask(modelData.id, !modelData.enabled)

                                    }

                                }



                                // 编辑

                                Button {

                                    text: "✏️ 编辑"

                                    font.pixelSize: 11

                                    contentItem: Text {

                                        text: parent.text

                                        color: "#D0D0E0"

                                        font.pixelSize: 11

                                        horizontalAlignment: Text.AlignHCenter

                                        verticalAlignment: Text.AlignVCenter

                                    }

                                    background: Rectangle {

                                        radius: 4

                                        color: parent.hovered ? "#383A4D" : "#282938"

                                        border.color: "#3F4157"

                                        border.width: 1

                                    }

                                    onClicked: {

                                        openEditDialog(modelData)

                                    }

                                }



                                // 课程类：查看课程大纲全景
                                Button {
                                    visible: modelData.type === "curriculum"
                                    text: "📖 课程全景"
                                    font.pixelSize: 11
                                    contentItem: Text {
                                        text: parent.text
                                        color: "#A7F3D0"
                                        font.pixelSize: 11
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        radius: 4
                                        color: parent.hovered ? "#1B3B2B" : "#13281E"
                                        border.color: "#2E6948"
                                        border.width: 1
                                    }
                                    onClicked: {
                                        root.currentCurriculumAgentId = modelData.id
                                        root.currentCurriculumName = modelData.name
                                        var curDay = modelData.content && modelData.content.current_day ? modelData.content.current_day : 1
                                        root.sidePanelTab = 0
                                        root.sidePanelOpen = true
                                        root.loadPreviewDay(curDay)
                                    }
                                }

                                // 课程类：学习笔记直达
                                Button {
                                    visible: modelData.type === "curriculum"
                                    text: "📝 学习笔记"
                                    font.pixelSize: 11
                                    contentItem: Text {
                                        text: parent.text
                                        color: "#93C5FD"
                                        font.pixelSize: 11
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        radius: 4
                                        color: parent.hovered ? "#1E293B" : "#141D2B"
                                        border.color: "#2563EB"
                                        border.width: 1
                                    }
                                    onClicked: {
                                        root.currentCurriculumAgentId = modelData.id
                                        root.currentCurriculumName = modelData.name
                                        root.sidePanelTab = 1
                                        root.sidePanelOpen = true
                                        root.loadNotesList()
                                        var curDay = modelData.content && modelData.content.current_day ? modelData.content.current_day : 1
                                        var dayPrefix = "Day_" + (curDay < 10 ? "0" : "") + curDay
                                        for (var ni = 0; ni < root.noteFiles.length; ++ni) {
                                            var nf = root.noteFiles[ni]
                                            if (nf.indexOf(dayPrefix) !== -1 && (modelData.name.indexOf("Rust") !== -1 ? nf.indexOf("Rust") !== -1 : nf.indexOf("Rust") === -1)) {
                                                root.selectedNoteFile = nf
                                                root.loadSelectedNote()
                                                break
                                            }
                                        }
                                    }
                                }

                                // 课程类：重置进度
                                Button {
                                    visible: modelData.type === "curriculum"
                                    text: "🔄 重置天数"

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

                                        color: parent.hovered ? "#383219" : "#242010"

                                        border.color: "#5C4A17"

                                        border.width: 1

                                    }

                                    onClicked: {

                                        resetModalAgentId = modelData.id

                                        resetDayValue = modelData.content && modelData.content.current_day ? modelData.content.current_day : 1

                                        resetModal.visible = true

                                    }

                                }



                                Item { Layout.fillWidth: true }



                                // 删除

                                Button {

                                    text: "🗑️ 删除"

                                    font.pixelSize: 11

                                    contentItem: Text {

                                        text: parent.text

                                        color: "#FF6B6B"

                                        font.pixelSize: 11

                                        horizontalAlignment: Text.AlignHCenter

                                        verticalAlignment: Text.AlignVCenter

                                    }

                                    background: Rectangle {

                                        radius: 4

                                        color: parent.hovered ? "#4D2222" : "#331616"

                                        border.color: "#662626"

                                        border.width: 1

                                    }

                                    onClicked: {

                                        if (bridge) bridge.deleteAgentTask(modelData.id)

                                    }

                                }

                            }

                        }

                    }

                }

            }



            // 底部数据库状态栏

            Rectangle {

                Layout.fillWidth: true

                height: 32

                color: "#18181F"

                border.color: "#282833"

                border.width: 1



                RowLayout {

                    anchors.fill: parent

                    anchors.leftMargin: 16

                    anchors.rightMargin: 16



                    Text {

                        text: "💾 本地数据库: " + (bridge ? bridge.getAgentDatabasePath() : "automation_agents.db")

                        color: "#7E8094"

                        font.pixelSize: 10

                    }

                    Item { Layout.fillWidth: true }

                    Text {

                        text: "共 " + root.taskList.length + " 个已注册 Agent"

                        color: "#A2A4BA"

                        font.pixelSize: 10

                    }

                }

            }

            } // 结束展开状态 ColumnLayout

            // 收起模式下的极简竖条 (48px)
            Rectangle {
                id: collapsedRail
                anchors.fill: parent
                visible: root.leftListCollapsed
                color: "#181820"
                border.color: "#282937"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: 12
                    anchors.bottomMargin: 12
                    spacing: 12

                    // 顶部展开大按钮
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 34
                        height: 34
                        radius: 6
                        color: btnExpandRailMouse.containsMouse ? "#3A3522" : "#222330"
                        border.color: btnExpandRailMouse.containsMouse ? "#FFD700" : "#3C3F57"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "▶"
                            color: btnExpandRailMouse.containsMouse ? "#FFD700" : "#A6A6BC"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        MouseArea {
                            id: btnExpandRailMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.text: "向右展开任务列表"
                            onClicked: root.leftListCollapsed = false
                        }
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 24
                        height: 1
                        color: "#2C2D3D"
                    }

                    // 图标 1: 🤖 Agent (点击展开列表)
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 32
                        height: 32
                        radius: 6
                        color: iconTaskMouse.containsMouse ? "#2A2D40" : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "🤖"
                            font.pixelSize: 16
                        }

                        MouseArea {
                            id: iconTaskMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.text: "Agent 任务管理 (点击展开)"
                            onClicked: root.leftListCollapsed = false
                        }
                    }

                    // 图标 2: 📖 课程大纲 (点击切换至 Tab 0)
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 32
                        height: 32
                        radius: 6
                        color: (root.sidePanelOpen && root.sidePanelTab === 0) ? "#3A3522" : (iconCurMouse.containsMouse ? "#2A2D40" : "transparent")
                        border.color: (root.sidePanelOpen && root.sidePanelTab === 0) ? "#FFD700" : "transparent"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "📖"
                            font.pixelSize: 15
                        }

                        MouseArea {
                            id: iconCurMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.text: "30天课程全景"
                            onClicked: {
                                root.sidePanelTab = 0
                                root.sidePanelOpen = true
                                root.loadPreviewDay(root.previewDay)
                            }
                        }
                    }

                    // 图标 3: 📝 学习笔记 (点击切换至 Tab 1)
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 32
                        height: 32
                        radius: 6
                        color: (root.sidePanelOpen && root.sidePanelTab === 1) ? "#27334D" : (iconNoteMouse.containsMouse ? "#2A2D40" : "transparent")
                        border.color: (root.sidePanelOpen && root.sidePanelTab === 1) ? "#8AB4F8" : "transparent"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "📝"
                            font.pixelSize: 15
                        }

                        MouseArea {
                            id: iconNoteMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.text: "学习笔记速览"
                            onClicked: {
                                root.sidePanelTab = 1
                                root.sidePanelOpen = true
                                root.loadNotesList()
                            }
                        }
                    }

                    // 图标 4: ➕ 新增 Agent
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 32
                        height: 32
                        radius: 6
                        color: iconAddMouse.containsMouse ? "#2450B0" : "#1B3B82"

                        Text {
                            anchors.centerIn: parent
                            text: "➕"
                            font.pixelSize: 13
                            color: "#FFFFFF"
                        }

                        MouseArea {
                            id: iconAddMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.text: "新增 Agent"
                            onClicked: openAddDialog()
                        }
                    }

                    Item { Layout.fillHeight: true }

                    // 底部展开指示
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 30
                        height: 30
                        radius: 15
                        color: btnBottomExpandMouse.containsMouse ? "#3A3522" : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "▶"
                            color: btnBottomExpandMouse.containsMouse ? "#FFD700" : "#72748C"
                            font.pixelSize: 11
                        }

                        MouseArea {
                            id: btnBottomExpandMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.text: "向右展开任务列表"
                            onClicked: root.leftListCollapsed = false
                        }
                    }
                }
            }
        } // 结束 leftPanelContainer



        // ========================================================

        // 中间分割线与折叠把手 (在侧栏打开或左侧收起时显示)

        // ========================================================

        Rectangle {
            id: splitDivider
            visible: root.sidePanelOpen || root.leftListCollapsed
            width: 14
            Layout.fillHeight: true
            color: "transparent"

            // 纵向分割细线
            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: parent.height
                color: splitDividerMouse.containsMouse ? "#FFD700" : "#2B2C3D"
            }

            // 中间悬浮小把手
            Rectangle {
                anchors.centerIn: parent
                width: 16
                height: 38
                radius: 8
                color: splitDividerMouse.containsMouse ? "#3D3720" : "#20212E"
                border.color: splitDividerMouse.containsMouse ? "#FFD700" : "#3D4058"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: root.leftListCollapsed ? "▶" : "◀"
                    font.pixelSize: 9
                    color: splitDividerMouse.containsMouse ? "#FFD700" : "#9E9EAF"
                }
            }

            MouseArea {
                id: splitDividerMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                ToolTip.visible: containsMouse
                ToolTip.text: root.leftListCollapsed ? "向右展开任务列表" : "向左收起任务列表"
                onClicked: {
                    root.leftListCollapsed = !root.leftListCollapsed
                    if (root.leftListCollapsed && !root.sidePanelOpen) {
                        root.sidePanelOpen = true
                        root.sidePanelTab = 0
                        root.loadPreviewDay(root.previewDay)
                    }
                }
            }
        }



        // ========================================================

        // 右侧扩展面板：课程大纲 & 学习笔记速览 (宽度 440)

        // ========================================================

        Rectangle {

            id: sidePanel

            visible: root.sidePanelOpen

            Layout.preferredWidth: root.leftListCollapsed ? (root.width - 62) : 440

            Layout.fillWidth: true

            Layout.fillHeight: true

            color: "#171822"



            ColumnLayout {

                anchors.fill: parent

                spacing: 0



                // 右侧面板顶部导航栏

                Rectangle {

                    Layout.fillWidth: true

                    height: 52

                    color: "#1E1F2B"

                    border.color: "#2B2D3F"

                    border.width: 1



                    RowLayout {

                        anchors.fill: parent

                        anchors.leftMargin: 14

                        anchors.rightMargin: 14

                        spacing: 8



                        // 选项卡 1：课程大纲 Tab

                        Rectangle {

                            height: 30

                            radius: 5

                            color: (root.sidePanelTab === 0) ? "#2F2714" : (tabCurMouse.containsMouse ? "#262738" : "transparent")

                            border.color: (root.sidePanelTab === 0) ? "#8A6D1C" : "transparent"

                            border.width: 1

                            implicitWidth: tabCurText.implicitWidth + 18



                            RowLayout {

                                anchors.centerIn: parent

                                spacing: 4

                                Text {

                                    id: tabCurText

                                    text: "📖 30天课程全景"

                                    color: (root.sidePanelTab === 0) ? "#FFD700" : "#A2A4BA"

                                    font.pixelSize: 11

                                    font.bold: (root.sidePanelTab === 0)

                                }

                            }

                            MouseArea {

                                id: tabCurMouse

                                anchors.fill: parent

                                hoverEnabled: true

                                cursorShape: Qt.PointingHandCursor

                                onClicked: {

                                    root.sidePanelTab = 0

                                    root.loadPreviewDay(root.previewDay)

                                }

                            }

                        }



                        // 选项卡 2：学习笔记 Tab

                        Rectangle {

                            height: 30

                            radius: 5

                            color: (root.sidePanelTab === 1) ? "#20283A" : (tabNoteMouse.containsMouse ? "#262738" : "transparent")

                            border.color: (root.sidePanelTab === 1) ? "#3A5580" : "transparent"

                            border.width: 1

                            implicitWidth: tabNoteText.implicitWidth + 18



                            RowLayout {

                                anchors.centerIn: parent

                                spacing: 4

                                Text {

                                    id: tabNoteText

                                    text: "📝 学习笔记速览"

                                    color: (root.sidePanelTab === 1) ? "#8AB4F8" : "#A2A4BA"

                                    font.pixelSize: 11

                                    font.bold: (root.sidePanelTab === 1)

                                }

                            }

                            MouseArea {

                                id: tabNoteMouse

                                anchors.fill: parent

                                hoverEnabled: true

                                cursorShape: Qt.PointingHandCursor

                                onClicked: {

                                    root.sidePanelTab = 1

                                    root.loadNotesList()

                                }

                            }

                        }



                        Item { Layout.fillWidth: true }



                        // 收起侧栏按钮

                        Rectangle {

                            width: 26

                            height: 26

                            radius: 13

                            color: collapseBtnMouse.containsMouse ? "#E84A5F" : "#282938"

                            border.color: collapseBtnMouse.containsMouse ? "#FF6B6B" : "#3D3E54"

                            border.width: 1



                            Text {

                                anchors.centerIn: parent

                                text: "✕"

                                color: collapseBtnMouse.containsMouse ? "#FFFFFF" : "#A2A4BA"

                                font.pixelSize: 11

                                font.bold: true

                            }



                            MouseArea {

                                id: collapseBtnMouse

                                anchors.fill: parent

                                hoverEnabled: true

                                cursorShape: Qt.PointingHandCursor

                                onClicked: {
                                    root.sidePanelOpen = false
                                    root.leftListCollapsed = false
                                }

                            }

                        }

                    }

                }



                // ====================================================

                // 视图 A：30天课程大纲体系 (Tab 0)

                // ====================================================

                ColumnLayout {
                    visible: root.sidePanelTab === 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 10
                    Layout.margins: 14

                    // 课程体系选择切换器 (C++ / Rust / 自定义课程)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: "当前课程:"
                            color: "#8E8EA0"
                            font.pixelSize: 11
                            font.bold: true
                        }
                        ComboBox {
                            id: curriculumSelectorCombo
                            Layout.fillWidth: true
                            model: {
                                var list = [{ name: "📘 C++ 30天核心知识阶梯通关", id: "cpp_daily_master" }]
                                for (var i = 0; i < root.taskList.length; ++i) {
                                    var t = root.taskList[i]
                                    if (t.id !== "cpp_daily_master" && (t.type === "curriculum" || (t.name && t.name.toLowerCase().indexOf("rust") !== -1))) {
                                        var icon = (t.name && t.name.toLowerCase().indexOf("rust") !== -1) ? "🦀 " : "📚 "
                                        list.push({ name: icon + t.name, id: t.id })
                                    }
                                }
                                var hasRust = false
                                for (var j = 0; j < list.length; ++j) {
                                    if (list[j].id.toLowerCase().indexOf("rust") !== -1 || list[j].name.toLowerCase().indexOf("rust") !== -1) {
                                        hasRust = true
                                        break
                                    }
                                }
                                if (!hasRust) {
                                    list.push({ name: "🦀 Rust 30天从入门到精通 (大纲)", id: "rust_30days" })
                                }
                                return list
                            }
                            textRole: "name"
                            valueRole: "id"
                            currentIndex: {
                                for (var i = 0; i < count; ++i) {
                                    var item = model[i]
                                    if (item && item.id === root.currentCurriculumAgentId) return i
                                }
                                return 0
                            }
                            onActivated: function(index) {
                                var item = model[index]
                                if (item) {
                                    root.currentCurriculumAgentId = item.id
                                    root.currentCurriculumName = item.name
                                    root.loadPreviewDay(1)
                                }
                            }
                        }
                    }

                    // 天数切换控制器与阶段标签
                    RowLayout {

                        Layout.fillWidth: true

                        spacing: 8



                        // 自定义主题风天数 Stepper: ◀ Day 01 / 30 ▶

                        RowLayout {

                            spacing: 4



                            Rectangle {

                                width: 28

                                height: 28

                                radius: 4

                                color: prevDayMouse.containsMouse ? "#3A3522" : "#242533"

                                border.color: prevDayMouse.containsMouse ? "#E5B842" : "#383A4E"

                                border.width: 1



                                Text {

                                    anchors.centerIn: parent

                                    text: "◀"

                                    color: (root.previewDay > 1) ? (prevDayMouse.containsMouse ? "#FFD700" : "#E0E0ED") : "#555566"

                                    font.pixelSize: 11

                                }



                                MouseArea {

                                    id: prevDayMouse

                                    anchors.fill: parent

                                    hoverEnabled: true

                                    cursorShape: (root.previewDay > 1) ? Qt.PointingHandCursor : Qt.ArrowCursor

                                    onClicked: {

                                        if (root.previewDay > 1) {

                                            root.loadPreviewDay(root.previewDay - 1)

                                        }

                                    }

                                }

                            }



                            Rectangle {

                                height: 28

                                width: 105

                                radius: 4

                                color: "#141520"

                                border.color: "#3F4259"

                                border.width: 1



                                RowLayout {

                                    anchors.centerIn: parent

                                    spacing: 4

                                    Text { text: "Day"; color: "#8E8EA0"; font.pixelSize: 11 }

                                    Text { text: (root.previewDay < 10 ? "0" : "") + root.previewDay; color: "#FFD700"; font.pixelSize: 12; font.bold: true }

                                    Text { text: "/ " + ((bridge && bridge.getCurriculumTotalDaysForAgent) ? Math.max(1, bridge.getCurriculumTotalDaysForAgent(root.currentCurriculumAgentId)) : 30); color: "#8E8EA0"; font.pixelSize: 11 }

                                }

                            }



                            Rectangle {

                                width: 28

                                height: 28

                                radius: 4

                                color: nextDayMouse.containsMouse ? "#3A3522" : "#242533"

                                border.color: nextDayMouse.containsMouse ? "#E5B842" : "#383A4E"

                                border.width: 1



                                Text {

                                    anchors.centerIn: parent

                                    text: "▶"

                                    color: (root.previewDay < 30) ? (nextDayMouse.containsMouse ? "#FFD700" : "#E0E0ED") : "#555566"

                                    font.pixelSize: 11

                                }



                                MouseArea {

                                    id: nextDayMouse

                                    anchors.fill: parent

                                    hoverEnabled: true

                                    cursorShape: (root.previewDay < 30) ? Qt.PointingHandCursor : Qt.ArrowCursor

                                    onClicked: {

                                        if (root.previewDay < 30) {

                                            root.loadPreviewDay(root.previewDay + 1)

                                        }

                                    }

                                }

                            }

                        }



                        Item { Layout.fillWidth: true }



                        // 所属阶段徽章

                        Rectangle {

                            height: 22

                            radius: 3

                            color: "#242116"

                            border.color: "#6B5518"

                            border.width: 1

                            implicitWidth: curStageText.implicitWidth + 10



                            Text {

                                id: curStageText

                                anchors.centerIn: parent

                                text: root.previewTopic ? root.previewTopic.stage : "阶段规划"

                                color: "#FFD700"

                                font.pixelSize: 10

                            }

                        }

                    }



                    // 微信公众号一键排版导出卡片

                    Rectangle {

                        Layout.fillWidth: true

                        radius: 6

                        color: "#14231A"

                        border.color: "#245437"

                        border.width: 1

                        implicitHeight: wechatCol.implicitHeight + 16



                        ColumnLayout {

                            id: wechatCol

                            anchors.fill: parent

                            anchors.margins: 8

                            spacing: 6



                            RowLayout {

                                Layout.fillWidth: true

                                spacing: 8



                                Row {

                                    spacing: 4

                                    Layout.alignment: Qt.AlignVCenter

                                    Text {

                                        text: "📢"

                                        font.pixelSize: 12

                                    }

                                    Text {

                                        text: "微信公众号一键排版导出:"

                                        color: "#A7F3D0"

                                        font.pixelSize: 11

                                        font.bold: true

                                    }

                                }



                                Item { Layout.fillWidth: true }



                                // 复制30天全景大纲 (全景路线图推文)

                                Button {

                                    id: btnCopySummary

                                    property bool isCopied: false

                                    text: isCopied ? "✓ 30天全景大纲已复制" : "📋 复制30天全景大纲"

                                    font.pixelSize: 11

                                    font.bold: true

                                    contentItem: Text {

                                        text: parent.text

                                        color: "#FFFFFF"

                                        font.bold: true

                                        font.pixelSize: 11

                                        horizontalAlignment: Text.AlignHCenter

                                        verticalAlignment: Text.AlignVCenter

                                    }

                                    background: Rectangle {

                                        radius: 4

                                        color: parent.isCopied ? "#047857" : (parent.hovered ? "#059669" : "#07C160")

                                    }

                                    onClicked: {

                                        if (bridge) {

                                            var md = (bridge.getCurriculumWechatSummaryMarkdownForAgent ? bridge.getCurriculumWechatSummaryMarkdownForAgent(root.currentCurriculumAgentId) : bridge.getCurriculumWechatSummaryMarkdown())

                                            bridge.copyToClipboard(md)

                                            hiddenClipboard.text = md

                                            hiddenClipboard.selectAll()

                                            hiddenClipboard.copy()

                                            btnCopySummary.isCopied = true

                                            timerResetSummaryCopy.restart()

                                            wechatNoticeText.text = "✅ 30天全景大纲已成功复制到剪贴板！采用精美公众号 Markdown 排版，可直接粘贴发布！"

                                            wechatNoticeText.visible = true

                                            timerHideWechatNotice.restart()

                                        }

                                    }

                                    Timer {

                                        id: timerResetSummaryCopy

                                        interval: 2500

                                        onTriggered: btnCopySummary.isCopied = false

                                    }

                                }



                                // 复制单日精讲推文

                                Button {

                                    id: btnCopyDay

                                    property bool isCopied: false

                                    text: isCopied ? ("✓ Day " + (root.previewDay < 10 ? "0" : "") + root.previewDay + " 推文已复制") : ("📰 复制今日推文")

                                    font.pixelSize: 11

                                    font.bold: true

                                    contentItem: Text {

                                        text: parent.text

                                        color: "#FFFFFF"

                                        font.bold: true

                                        font.pixelSize: 11

                                        horizontalAlignment: Text.AlignHCenter

                                        verticalAlignment: Text.AlignVCenter

                                    }

                                    background: Rectangle {

                                        radius: 4

                                        color: parent.isCopied ? "#7C2D12" : (parent.hovered ? "#C2410C" : "#EA580C")

                                    }

                                    onClicked: {

                                        if (bridge) {

                                            var md = (bridge.getCurriculumDayWechatMarkdownForAgent ? bridge.getCurriculumDayWechatMarkdownForAgent(root.currentCurriculumAgentId, root.previewDay) : bridge.getCurriculumDayWechatMarkdown(root.previewDay))

                                            bridge.copyToClipboard(md)

                                            hiddenClipboard.text = md

                                            hiddenClipboard.selectAll()

                                            hiddenClipboard.copy()

                                            btnCopyDay.isCopied = true

                                            timerResetDayCopy.restart()

                                            wechatNoticeText.text = "✅ Day " + (root.previewDay < 10 ? "0" : "") + root.previewDay + " 单篇推文已成功复制到剪贴板！包含要点、实战代码与避坑分析！"

                                            wechatNoticeText.visible = true

                                            timerHideWechatNotice.restart()

                                        }

                                    }

                                    Timer {

                                        id: timerResetDayCopy

                                        interval: 2500

                                        onTriggered: btnCopyDay.isCopied = false

                                    }

                                }

                            }



                            Text {

                                id: wechatNoticeText

                                visible: false

                                text: ""

                                color: "#34D399"

                                font.pixelSize: 10

                                Layout.fillWidth: true

                                wrapMode: Text.Wrap



                                Timer {

                                    id: timerHideWechatNotice

                                    interval: 4000

                                    onTriggered: wechatNoticeText.visible = false

                                }

                            }

                        }

                    }



                    // 课程主题与难度星级

                    RowLayout {

                        Layout.fillWidth: true

                        spacing: 8



                        Text {

                            text: root.previewTopic ? ("Day " + (root.previewDay < 10 ? "0" : "") + root.previewDay + " · " + root.previewTopic.title) : ""

                            color: "#FFFFFF"

                            font.pixelSize: 13

                            font.bold: true

                            Layout.fillWidth: true

                            elide: Text.ElideRight

                        }



                        Text {

                            text: root.previewTopic ? ("难度: " + ("⭐".repeat(root.previewTopic.difficulty || 1))) : ""

                            color: "#FFD700"

                            font.pixelSize: 10

                        }

                    }



                    // 课程详情滚动区

                    ScrollView {

                        Layout.fillWidth: true

                        Layout.fillHeight: true

                        clip: true



                        ColumnLayout {

                            width: parent.width

                            spacing: 10



                            // 核心概述卡片

                            Rectangle {

                                Layout.fillWidth: true

                                radius: 6

                                color: "#1F202E"

                                border.color: "#313348"

                                border.width: 1

                                implicitHeight: prevSummaryText.implicitHeight + 16



                                Text {

                                    id: prevSummaryText

                                    anchors.fill: parent

                                    anchors.margins: 8

                                    text: root.previewTopic ? root.previewTopic.summary : ""

                                    color: "#D0D2E5"

                                    font.pixelSize: 11

                                    wrapMode: Text.Wrap

                                }

                            }



                            // 深度要点

                            Text {

                                text: "💡 核心解析要点:"

                                color: "#E0E1F0"

                                font.pixelSize: 11

                                font.bold: true

                            }



                            Repeater {

                                model: root.previewTopic ? root.previewTopic.key_points : []

                                delegate: RowLayout {

                                    Layout.fillWidth: true

                                    spacing: 6

                                    Text {

                                        text: "•"

                                        color: "#FFD700"

                                        font.pixelSize: 12

                                        Layout.alignment: Qt.AlignTop

                                    }

                                    Text {

                                        text: modelData

                                        color: "#B2B4C8"

                                        font.pixelSize: 11

                                        wrapMode: Text.Wrap

                                        Layout.fillWidth: true

                                    }

                                }

                            }



                            // 代码示例

                            ColumnLayout {

                                Layout.fillWidth: true

                                spacing: 4

                                visible: root.previewTopic && root.previewTopic.code_example



                                RowLayout {

                                    Layout.fillWidth: true

                                    Text {

                                        text: "💻 经典精辟代码示例:"

                                        color: "#E0E1F0"

                                        font.pixelSize: 11

                                        font.bold: true

                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {

                                        id: codeCopyPrompt

                                        property bool isCopied: false

                                        text: isCopied ? "✓ 已复制" : "📋 复制代码"

                                        color: isCopied ? "#52C41A" : "#8AB4F8"

                                        font.pixelSize: 10



                                        MouseArea {

                                            anchors.fill: parent

                                            hoverEnabled: true

                                            cursorShape: Qt.PointingHandCursor

                                            onClicked: {

                                                if (root.previewTopic) {

                                                    hiddenClipboard.text = root.previewTopic.code_example

                                                    hiddenClipboard.selectAll()

                                                    hiddenClipboard.copy()

                                                    codeCopyPrompt.isCopied = true

                                                    resetCodeCopyTimer.restart()

                                                }

                                            }

                                        }

                                        Timer {

                                            id: resetCodeCopyTimer

                                            interval: 2000

                                            onTriggered: codeCopyPrompt.isCopied = false

                                        }

                                    }

                                }



                                Rectangle {

                                    Layout.fillWidth: true

                                    radius: 5

                                    color: "#0F1017"

                                    border.color: "#252738"

                                    border.width: 1

                                    implicitHeight: prevCodeText.implicitHeight + 14



                                    Text {

                                        id: prevCodeText

                                        anchors.fill: parent

                                        anchors.margins: 8

                                        text: root.previewTopic ? root.previewTopic.code_example : ""

                                        color: "#7EE787"

                                        font.family: "Consolas, 'Courier New', monospace"

                                        font.pixelSize: 10

                                        wrapMode: Text.Wrap

                                    }

                                }

                            }



                            // 避坑与面试题

                            Rectangle {

                                Layout.fillWidth: true

                                radius: 6

                                color: "#2B1E14"

                                border.color: "#5C3D21"

                                border.width: 1

                                visible: root.previewTopic && root.previewTopic.pitfall_or_interview

                                implicitHeight: prevPitfallText.implicitHeight + 16



                                Text {

                                    id: prevPitfallText

                                    anchors.fill: parent

                                    anchors.margins: 8

                                    text: root.previewTopic ? ("⚠️ 避坑与面试重点：\n" + root.previewTopic.pitfall_or_interview) : ""

                                    color: "#FFB86C"

                                    font.pixelSize: 10

                                    wrapMode: Text.Wrap

                                }

                            }

                        }

                    }



                    // 课程操作底部栏

                    RowLayout {

                        Layout.fillWidth: true

                        spacing: 8



                        // 设为当前进度

                        Button {

                            text: "🚀 设为此天进度"

                            font.pixelSize: 11

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

                                if (bridge) {

                                    bridge.setCurriculumDay(root.currentCurriculumAgentId, root.previewDay); root.reloadTasks()

                                }

                            }

                        }



                        // 针对今日课题延伸追问 AI

                        Button {

                            text: "🤖 延伸追问 AI"

                            font.pixelSize: 11

                            contentItem: Text {

                                text: parent.text

                                color: "#93C5FD"

                                font.pixelSize: 11

                                horizontalAlignment: Text.AlignHCenter

                                verticalAlignment: Text.AlignVCenter

                            }

                            background: Rectangle {

                                radius: 4

                                color: parent.hovered ? "#1E2B47" : "#141C30"

                                border.color: "#2E4675"

                                border.width: 1

                            }

                            onClicked: {

                                if (bridge && root.previewTopic) {

                                    var ctx = "【课题】Day " + (root.previewDay < 10 ? "0" : "") + root.previewDay + ": " + root.previewTopic.title + " (阶段: " + root.previewTopic.stage + ")\n\n【核心概述】" + root.previewTopic.summary

                                    bridge.openAgentChatWindow("请直击底层机理，深入剖析其内存布局与运行机制，并指出生产环境常见避坑要点与高频面试考点。", ctx)

                                }

                            }

                        }



                        // 立即测试推送此天

                        Button {

                            text: "🧪 立即测试推送此天"

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

                                color: parent.hovered ? "#4D7CFE" : "#386BF6"

                            }

                            onClicked: {

                                if (bridge && root.previewTopic) {

                                    var fakeNotif = {

                                        "agentId": root.currentCurriculumAgentId,
                                        "agentName": root.currentCurriculumName || "30天阶梯通关体系",

                                        "agentType": "curriculum",

                                        "title": "Day " + (root.previewDay < 10 ? "0" : "") + root.previewDay + ": " + root.previewTopic.title,

                                        "topicTitle": root.previewTopic.title,

                                        "stage": root.previewTopic.stage,

                                        "summary": root.previewTopic.summary,

                                        "day": root.previewDay,

                                        "totalDays": 30,

                                        "difficulty": root.previewTopic.difficulty || 1,

                                        "stars": "⭐".repeat(root.previewTopic.difficulty || 1),

                                        "codeSnippet": root.previewTopic.code_example,

                                        "pitfall": root.previewTopic.pitfall_or_interview,

                                        "keyPoints": root.previewTopic.key_points || []

                                    }

                                    bridge.agentNotificationTriggered(fakeNotif)

                                    bridge.openAgentCardWindow()

                                }

                            }

                        }



                        // 复制今日公众号推文快捷按钮

                        Button {

                            id: btnBottomCopyDay

                            property bool isCopied: false

                            text: isCopied ? "✓ 推文已复制" : "📢 复制公众号推文"

                            font.pixelSize: 11

                            contentItem: Text {

                                text: parent.text

                                color: parent.isCopied ? "#A7F3D0" : "#6EE7B7"

                                font.pixelSize: 11

                                font.bold: true

                                horizontalAlignment: Text.AlignHCenter

                                verticalAlignment: Text.AlignVCenter

                            }

                            background: Rectangle {

                                radius: 4

                                color: parent.isCopied ? "#163826" : (parent.hovered ? "#163826" : "#0F261A")

                                border.color: parent.isCopied ? "#34D399" : "#2C6E44"

                                border.width: 1

                            }

                            onClicked: {

                                if (bridge) {

                                    var md = (bridge.getCurriculumDayWechatMarkdownForAgent ? bridge.getCurriculumDayWechatMarkdownForAgent(root.currentCurriculumAgentId, root.previewDay) : bridge.getCurriculumDayWechatMarkdown(root.previewDay))

                                    bridge.copyToClipboard(md)

                                    hiddenClipboard.text = md

                                    hiddenClipboard.selectAll()

                                    hiddenClipboard.copy()

                                    btnBottomCopyDay.isCopied = true

                                    timerResetBottomCopy.restart()

                                    wechatNoticeText.text = "✅ Day " + (root.previewDay < 10 ? "0" : "") + root.previewDay + " 单篇推文已复制到剪贴板！"

                                    wechatNoticeText.visible = true

                                    timerHideWechatNotice.restart()

                                }

                            }

                            Timer {

                                id: timerResetBottomCopy

                                interval: 2500

                                onTriggered: btnBottomCopyDay.isCopied = false

                            }

                        }



                        Item { Layout.fillWidth: true }

                    }

                }



                // ====================================================

                // 视图 B：学习笔记速览 (Tab 1)

                // ====================================================

                ColumnLayout {

                    visible: root.sidePanelTab === 1

                    Layout.fillWidth: true

                    Layout.fillHeight: true

                    spacing: 10

                    Layout.margins: 14



                    // 笔记文件选择工具条

                    RowLayout {

                        Layout.fillWidth: true

                        spacing: 8



                        Text {

                            text: "当前笔记:"

                            color: "#A2A4BA"

                            font.pixelSize: 11

                        }



                        // 文件选择下拉列表

                        ComboBox {

                            id: noteCombo

                            Layout.fillWidth: true

                            model: root.noteFiles

                            currentIndex: (root.noteFiles.length > 0 && root.selectedNoteFile.length > 0) ? Math.max(0, root.noteFiles.indexOf(root.selectedNoteFile)) : 0

                            onActivated: function(index) {

                                if (index >= 0 && index < root.noteFiles.length) {

                                    root.selectedNoteFile = root.noteFiles[index]

                                    root.loadSelectedNote()

                                }

                            }

                        }



                        // 刷新按钮

                        Rectangle {

                            width: 28

                            height: 28

                            radius: 4

                            color: refreshNotesMouse.containsMouse ? "#35374C" : "#242533"

                            border.color: "#3F4259"

                            border.width: 1



                            Text {

                                anchors.centerIn: parent

                                text: "🔄"

                                font.pixelSize: 11

                            }



                            MouseArea {

                                id: refreshNotesMouse

                                anchors.fill: parent

                                hoverEnabled: true

                                cursorShape: Qt.PointingHandCursor

                                onClicked: root.loadNotesList()

                            }

                        }

                    }



                    // 笔记内容渲染或空状态

                    Item {

                        Layout.fillWidth: true

                        Layout.fillHeight: true



                        // 空状态提示

                        ColumnLayout {

                            anchors.centerIn: parent

                            spacing: 8

                            visible: root.noteFiles.length === 0



                            Text {

                                text: "📝"

                                font.pixelSize: 36

                                Layout.alignment: Qt.AlignHCenter

                            }

                            Text {

                                text: "尚未生成本地学习笔记"

                                color: "#FFFFFF"

                                font.pixelSize: 13

                                font.bold: true

                                Layout.alignment: Qt.AlignHCenter

                            }

                            Text {

                                text: "当任务在定点触发（或点击「立即测试执行」）后，\n系统会自动生成 Markdown 笔记并归档在本地。"

                                color: "#808298"

                                font.pixelSize: 11

                                horizontalAlignment: Text.AlignHCenter

                                Layout.alignment: Qt.AlignHCenter

                            }

                        }



                        // 笔记内容展示

                        ScrollView {

                            anchors.fill: parent

                            clip: true

                            visible: root.noteFiles.length > 0



                            Rectangle {

                                width: parent.width

                                implicitHeight: noteText.implicitHeight + 24

                                radius: 6

                                color: "#11121A"

                                border.color: "#282A3A"

                                border.width: 1



                                Text {

                                    id: noteText

                                    anchors.fill: parent

                                    anchors.margins: 12

                                    text: root.noteContent

                                    color: "#D8D9E8"

                                    font.family: "Consolas, 'Segoe UI', monospace"

                                    font.pixelSize: 11

                                    wrapMode: Text.Wrap

                                }

                            }

                        }

                    }



                    // 底部笔记目录操作栏

                    RowLayout {

                        Layout.fillWidth: true

                        spacing: 8



                        Button {

                            text: "📂 打开笔记目录"

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

                                if (bridge) bridge.openLearningNotesDir()

                            }

                        }



                        Item { Layout.fillWidth: true }



                        Text {

                            text: "共 " + root.noteFiles.length + " 篇归档笔记"

                            color: "#808298"

                            font.pixelSize: 10

                        }

                    }

                }

            }

        }

    }



    // ========================================================

    // 弹窗 1：新增 / 编辑 Agent 任务 Modal

    // ========================================================

    property string editTaskId: ""
    property var editingTaskData: null

    function openAddDialog() {
        isEditing = false
        editingTaskData = null
        editTaskId = "agent_" + Date.now().toString(36)
        inputName.text = "新自动化任务"
        typeCombo.currentIndex = 0
        inputSchedule.text = "10:00"
        chkWorkday.checked = true
        inputContent.text = ""
        taskModal.visible = true
    }

    function openEditDialog(data) {
        isEditing = true
        editingTaskData = data
        editTaskId = data.id
        inputName.text = data.name
        if (data.type === "curriculum") typeCombo.currentIndex = 1
        else if (data.type === "command") typeCombo.currentIndex = 2
        else typeCombo.currentIndex = 0
        
        var rule = data.scheduleRule || "10:00"
        chkWorkday.checked = (data.content && !!data.content.skip_weekends) || (rule.indexOf("workday@") !== -1)
        if (rule.indexOf("workday@") !== -1) {
            rule = rule.replace(/^workday@/i, "")
        }
        inputSchedule.text = rule

        if (data.content) {
            if (data.type === "command") {
                inputContent.text = data.content.command ? data.content.command : (data.content.message || "")
            } else {
                inputContent.text = data.content.message ? data.content.message : (data.content.prompt ? data.content.prompt : "")
            }
        } else {
            inputContent.text = ""
        }
        taskModal.visible = true
    }



    Rectangle {

        id: taskModal

        anchors.fill: parent

        color: "#B3000000"

        visible: false



        MouseArea { anchors.fill: parent } // 阻挡下层点击



        Rectangle {

            width: 520

            height: 480

            anchors.centerIn: parent

            radius: 10

            color: "#1D1E29"

            border.color: "#3F4259"

            border.width: 1



            ColumnLayout {

                anchors.fill: parent

                anchors.margins: 20

                spacing: 12



                // 标题与右上角关闭按钮

                RowLayout {

                    Layout.fillWidth: true



                    Text {

                        text: root.isEditing ? "✏️ 编辑 Agent 任务" : "➕ 新建自动化 Agent 任务"

                        color: "#FFFFFF"

                        font.pixelSize: 14

                        font.bold: true

                        Layout.fillWidth: true

                    }



                    Rectangle {

                        width: 24

                        height: 24

                        radius: 12

                        color: modalCloseMouse.containsMouse ? "#E84A5F" : "#282938"

                        border.color: modalCloseMouse.containsMouse ? "#FF6B6B" : "#3D3E54"

                        border.width: 1



                        Text {

                            anchors.centerIn: parent

                            text: "✕"

                            color: modalCloseMouse.containsMouse ? "#FFFFFF" : "#A2A4BA"

                            font.pixelSize: 11

                            font.bold: true

                        }



                        MouseArea {

                            id: modalCloseMouse

                            anchors.fill: parent

                            hoverEnabled: true

                            cursorShape: Qt.PointingHandCursor

                            onClicked: taskModal.visible = false

                        }

                    }

                }



                Rectangle { Layout.fillWidth: true; height: 1; color: "#2E3042" }



                // 任务名称

                RowLayout {

                    Layout.fillWidth: true

                    Text { text: "任务名称:"; color: "#C0C1D4"; font.pixelSize: 12; width: 70 }

                    TextField {

                        id: inputName

                        Layout.fillWidth: true

                        color: "#FFFFFF"

                        placeholderText: "例如：C++ 进阶推送 / 喝水提醒"

                        background: Rectangle { radius: 4; color: "#14151E"; border.color: "#333547" }

                    }

                }



                // 任务类型

                RowLayout {

                    Layout.fillWidth: true

                    Text { text: "任务类型:"; color: "#C0C1D4"; font.pixelSize: 12; width: 70 }

                    ComboBox {

                        id: typeCombo

                        Layout.fillWidth: true

                        model: ["⏰ 智能任务 / 定时提醒 (支持 AI 深度解答)", "📚 阶梯式通关课程 (每日自动推进进度)", "💻 本地系统命令 / 脚本执行"]

                    }

                }



                // 调度时间

                RowLayout {

                    Layout.fillWidth: true

                    Text { text: "触发时间:"; color: "#C0C1D4"; font.pixelSize: 12; width: 70 }

                    TextField {

                        id: inputSchedule

                        Layout.fillWidth: true

                        color: "#FFFFFF"

                        placeholderText: "例如：10:00 (每天十点) 或 interval:30 (每30分钟)"

                        background: Rectangle { radius: 4; color: "#14151E"; border.color: "#333547" }

                    }

                }



                // 工作日仅排程选项 (跳过周末)

                RowLayout {

                    Layout.fillWidth: true

                    spacing: 10



                    Rectangle {

                        id: workdayBox

                        width: 18

                        height: 18

                        radius: 4

                        color: chkWorkday.checked ? "#E5B842" : "#14151E"

                        border.color: chkWorkday.checked ? "#FFD700" : "#404258"

                        border.width: 1



                        Text {

                            anchors.centerIn: parent

                            text: "✓"

                            color: "#16161A"

                            font.pixelSize: 12

                            font.bold: true

                            visible: chkWorkday.checked

                        }



                        MouseArea {

                            id: chkWorkday

                            anchors.fill: parent

                            property bool checked: true

                            hoverEnabled: true

                            cursorShape: Qt.PointingHandCursor

                            onClicked: checked = !checked

                        }

                    }



                    Column {

                        Layout.fillWidth: true

                        spacing: 2

                        Text {

                            text: "仅工作日执行 (周一至周五，周末/休息日自动跳过)"

                            color: "#E2E2EC"

                            font.pixelSize: 11

                            font.bold: true

                            MouseArea {

                                anchors.fill: parent

                                cursorShape: Qt.PointingHandCursor

                                onClicked: chkWorkday.checked = !chkWorkday.checked

                            }

                        }

                        Text {

                            text: "适用于每日课程推送或工作提醒，周五执行后自动顺延至下周一"

                            color: "#808298"

                            font.pixelSize: 10

                        }

                    }

                }



                // 内容输入

                ColumnLayout {

                    Layout.fillWidth: true

                    Layout.fillHeight: true

                    spacing: 4



                    Text {

                        text: (typeCombo.currentIndex === 2) ? "执行指令 (Shell/CMD):" : ((typeCombo.currentIndex === 1) ? "课程计划 / 核心提示 (支持 AI 解读):" : "任务内容 / 提醒文本 (支持 AI 解读):")

                        color: "#C0C1D4"

                        font.pixelSize: 12

                    }

                    ScrollView {

                        Layout.fillWidth: true

                        Layout.fillHeight: true

                        TextArea {

                            id: inputContent

                            color: "#FFFFFF"

                            wrapMode: Text.Wrap

                            background: Rectangle { radius: 4; color: "#14151E"; border.color: "#333547" }

                        }

                    }

                }



                // 底部按钮

                RowLayout {

                    Layout.fillWidth: true

                    Item { Layout.fillWidth: true }



                    Button {

                        text: "取消"

                        contentItem: Text {

                            text: parent.text

                            color: "#B2B3C5"

                            font.pixelSize: 11

                            horizontalAlignment: Text.AlignHCenter

                            verticalAlignment: Text.AlignVCenter

                        }

                        background: Rectangle {

                            radius: 4

                            color: parent.hovered ? "#35374A" : "#242533"

                            border.color: "#3F4259"

                            border.width: 1

                        }

                        onClicked: taskModal.visible = false

                    }



                    Button {

                        text: "保存并写入数据库"

                        contentItem: Text {

                            text: parent.text

                            color: "#FFFFFF"

                            font.bold: true

                            font.pixelSize: 11

                            horizontalAlignment: Text.AlignHCenter

                            verticalAlignment: Text.AlignVCenter

                        }

                        background: Rectangle {

                            radius: 4

                            color: parent.hovered ? "#4D7CFE" : "#386BF6"

                        }

                        onClicked: {

                            var tType = "reminder"

                            if (typeCombo.currentIndex === 1) tType = "curriculum"

                            else if (typeCombo.currentIndex === 2) tType = "command"



                            var sRule = inputSchedule.text.trim()

                            var isWk = chkWorkday.checked

                            if (isWk && !sRule.startsWith("workday@") && !sRule.startsWith("interval:")) {

                                sRule = "workday@" + sRule.replace(/^daily@/i, "")

                            } else if (!isWk && sRule.startsWith("workday@")) {

                                sRule = sRule.replace(/^workday@/i, "")

                            }



                            var payload = {

                                "id": root.editTaskId,

                                "name": inputName.text.trim(),

                                "type": tType,

                                "scheduleRule": sRule,

                                "enabled": true,

                                "content": {

                                    "skip_weekends": isWk

                                }

                            }



                            var contentText = inputContent.text.trim()



                            if (tType === "curriculum") {

                                var curDay = 1

                                var totDay = 30

                                if (root.isEditing && root.editingTaskData && root.editingTaskData.content) {

                                    if (root.editingTaskData.content.current_day !== undefined) curDay = root.editingTaskData.content.current_day

                                    if (root.editingTaskData.content.total_days !== undefined) totDay = root.editingTaskData.content.total_days

                                }

                                payload.content["current_day"] = curDay

                                payload.content["total_days"] = totDay

                                payload.content["message"] = contentText

                                payload.content["prompt"] = contentText

                            } else if (tType === "command") {

                                payload.content["command"] = contentText

                                payload.content["message"] = contentText

                            } else {

                                payload.content["message"] = contentText

                                payload.content["prompt"] = contentText

                            }



                            if (bridge) {

                                bridge.addOrUpdateAgentTask(payload)

                            }

                            taskModal.visible = false

                        }

                    }

                }

            }

        }

    }



    // ========================================================

    // 弹窗 2：重置天数进度 Modal (优化为暗黑金色调 Stepper)

    // ========================================================

    property string resetModalAgentId: ""

    property int resetDayValue: 1



    Rectangle {

        id: resetModal

        anchors.fill: parent

        color: "#B3000000"

        visible: false

        MouseArea { anchors.fill: parent }



        Rectangle {

            width: 360

            height: 210

            anchors.centerIn: parent

            radius: 10

            color: "#1D1E29"

            border.color: "#3F4259"

            border.width: 1



            ColumnLayout {

                anchors.fill: parent

                anchors.margins: 18

                spacing: 12



                RowLayout {

                    Layout.fillWidth: true

                    Text {

                        text: "🔄 重置学习天数"

                        color: "#FFFFFF"

                        font.pixelSize: 13

                        font.bold: true

                        Layout.fillWidth: true

                    }

                    Rectangle {

                        width: 22

                        height: 22

                        radius: 11

                        color: rstCloseMouse.containsMouse ? "#E84A5F" : "#282938"

                        border.color: rstCloseMouse.containsMouse ? "#FF6B6B" : "#3D3E54"

                        border.width: 1

                        Text {

                            anchors.centerIn: parent

                            text: "✕"

                            color: rstCloseMouse.containsMouse ? "#FFFFFF" : "#A2A4BA"

                            font.pixelSize: 10

                            font.bold: true

                        }

                        MouseArea {

                            id: rstCloseMouse

                            anchors.fill: parent

                            hoverEnabled: true

                            cursorShape: Qt.PointingHandCursor

                            onClicked: resetModal.visible = false

                        }

                    }

                }



                Rectangle { Layout.fillWidth: true; height: 1; color: "#2E3042" }



                Text { text: "设置要学习的进度天数 (1 - 30):"; color: "#B8B9CC"; font.pixelSize: 11 }



                // 自定义主题 Stepper

                RowLayout {

                    Layout.alignment: Qt.AlignHCenter

                    spacing: 10



                    Rectangle {

                        width: 32

                        height: 32

                        radius: 6

                        color: rstPrevMouse.containsMouse ? "#3A3522" : "#242533"

                        border.color: rstPrevMouse.containsMouse ? "#E5B842" : "#383A4E"

                        border.width: 1

                        Text {

                            anchors.centerIn: parent

                            text: "◀"

                            color: (root.resetDayValue > 1) ? "#E5B842" : "#555566"

                            font.pixelSize: 12

                        }

                        MouseArea {

                            id: rstPrevMouse

                            anchors.fill: parent

                            hoverEnabled: true

                            cursorShape: (root.resetDayValue > 1) ? Qt.PointingHandCursor : Qt.ArrowCursor

                            onClicked: if (root.resetDayValue > 1) root.resetDayValue--

                        }

                    }



                    Rectangle {

                        width: 140

                        height: 32

                        radius: 6

                        color: "#14151E"

                        border.color: "#3F4259"

                        border.width: 1



                        RowLayout {

                            anchors.centerIn: parent

                            spacing: 4

                            Text { text: "第"; color: "#8E8EA0"; font.pixelSize: 12 }

                            Text { text: (root.resetDayValue < 10 ? "0" : "") + root.resetDayValue; color: "#FFD700"; font.pixelSize: 14; font.bold: true }

                            Text { text: "/ 30 天"; color: "#8E8EA0"; font.pixelSize: 12 }

                        }

                    }



                    Rectangle {

                        width: 32

                        height: 32

                        radius: 6

                        color: rstNextMouse.containsMouse ? "#3A3522" : "#242533"

                        border.color: rstNextMouse.containsMouse ? "#E5B842" : "#383A4E"

                        border.width: 1

                        Text {

                            anchors.centerIn: parent

                            text: "▶"

                            color: (root.resetDayValue < 30) ? "#E5B842" : "#555566"

                            font.pixelSize: 12

                        }

                        MouseArea {

                            id: rstNextMouse

                            anchors.fill: parent

                            hoverEnabled: true

                            cursorShape: (root.resetDayValue < 30) ? Qt.PointingHandCursor : Qt.ArrowCursor

                            onClicked: if (root.resetDayValue < 30) root.resetDayValue++

                        }

                    }

                }



                Item { Layout.fillHeight: true }



                RowLayout {

                    Layout.fillWidth: true

                    Item { Layout.fillWidth: true }

                    Button {

                        text: "取消"

                        contentItem: Text { text: parent.text; color: "#B8B9CC"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }

                        background: Rectangle { radius: 4; color: parent.hovered ? "#35374A" : "#242533"; border.color: "#3F4259"; border.width: 1 }

                        onClicked: resetModal.visible = false

                    }

                    Button {

                        text: "确定设置"

                        contentItem: Text { text: parent.text; color: "#16161A"; font.bold: true; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }

                        background: Rectangle { radius: 4; color: parent.hovered ? "#F5C842" : "#E5B842" }

                        onClicked: {

                            if (bridge && root.resetModalAgentId) {

                                bridge.setCurriculumDay(root.resetModalAgentId, root.resetDayValue)

                            }

                            resetModal.visible = false

                        }

                    }

                }

            }

        }

    }

}

