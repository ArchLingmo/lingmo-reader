/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "Ouvir": the floating player over the pages (Vidro Lingmo)
LingmoUI.GlassSurface {
    id: bar

    property var controller: readAloud
    signal exportRequested()
    signal closeRequested()

    radius: LingmoUI.Theme.hugeRadius
    blurMode: LingmoUI.GlassSurface.Never     // inside the window, which already asks KWin for blur
    elevated: true
    implicitHeight: column.implicitHeight + LingmoUI.Units.largeSpacing * 1.5
    implicitWidth: 760

    readonly property var speeds: [0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0]
    readonly property var languages: [
        { code: "pt", name: qsTr("Portuguese") }, { code: "en", name: qsTr("English") },
        { code: "es", name: qsTr("Spanish") }, { code: "fr", name: qsTr("French") },
        { code: "de", name: qsTr("German") }, { code: "it", name: qsTr("Italian") }
    ]

    function speedText(s) {
        const r = Math.round(s * 100) / 100
        return (Number.isInteger(Math.round(r * 100) / 10) ? r.toFixed(1) : r.toFixed(2)).replace(".", Qt.locale().decimalPoint) + "×"
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: LingmoUI.Units.largeSpacing * 0.75
        anchors.leftMargin: LingmoUI.Units.largeSpacing
        anchors.rightMargin: LingmoUI.Units.largeSpacing
        spacing: LingmoUI.Units.smallSpacing

        // ---- transport and what is being read
        RowLayout {
            Layout.fillWidth: true
            spacing: LingmoUI.Units.smallSpacing

            LingmoUI.IconButton {
                iconName: "media-skip-backward"
                tipText: qsTr("Previous paragraph")
                onClicked: bar.controller.previousParagraph()
            }
            LingmoUI.IconButton {
                iconName: "media-seek-backward"
                tipText: qsTr("Previous sentence")
                onClicked: bar.controller.previousSentence()
            }
            LingmoUI.IconButton {
                id: playButton
                size: 40
                iconSize: 20
                round: true
                checkable: true
                checked: true
                iconName: bar.controller.playing ? "media-playback-pause" : "media-playback-start"
                tipText: bar.controller.playing ? qsTr("Pause (Space)") : qsTr("Play (Space)")
                onClicked: bar.controller.togglePlay()
                LingmoUI.Spinner {
                    anchors.centerIn: parent
                    size: parent.width + 6
                    running: bar.controller.buffering
                    visible: running
                    lineWidth: 2
                }
            }
            LingmoUI.IconButton {
                iconName: "media-seek-forward"
                tipText: qsTr("Next sentence")
                onClicked: bar.controller.nextSentence()
            }
            LingmoUI.IconButton {
                iconName: "media-skip-forward"
                tipText: qsTr("Next paragraph")
                onClicked: bar.controller.nextParagraph()
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 120     // shrinks first: the controls keep their size
                Layout.minimumWidth: 60
                Layout.leftMargin: LingmoUI.Units.smallSpacing
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: bar.controller.currentText.length > 0 ? bar.controller.currentText
                        : bar.controller.buffering ? qsTr("Preparing the voice…") : qsTr("Click a sentence on the page to read from there.")
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.pointSize: LingmoUI.Units.fontBody
                    color: LingmoUI.Theme.textColor
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: bar.controller.learning && bar.controller.bilingual ? bar.controller.currentTranslation
                        : qsTr("Sentence %1 · page %2").arg(bar.controller.sentenceIndex + 1).arg(bar.controller.currentPage + 1)
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.pointSize: LingmoUI.Units.fontCaption
                    color: LingmoUI.Theme.secondaryTextColor
                }
            }

            LingmoUI.ActionButton {
                id: speedButton
                text: bar.speedText(bar.controller.speed)
                tipText: qsTr("Speed (the pitch stays the same)")
                Layout.preferredWidth: Math.max(implicitWidth, 62)
                onClicked: speedMenu.popup(speedButton, 0, -speedMenu.implicitHeight - 4)
            }
            ComboBox {
                id: voiceBox
                Layout.preferredWidth: Math.min(200, Math.max(130, bar.width * 0.2))
                // voices for the book's language, plus every cloned or designed voice: Chatterbox and
                // OmniVoice speak any language with them
                model: bar.controller.voices.filter(v => v.language === bar.controller.language || v.language === ""
                                                    || ((v.kind === "cloned" || v.kind === "designed") && v.usable !== false))
                textRole: "name"
                valueRole: "id"
                displayText: {
                    const v = bar.controller.voices.find(x => x.id === bar.controller.voiceId)
                    return v ? v.name : qsTr("Voice")
                }
                onActivated: (index) => bar.controller.voiceId = currentValue
                ToolTip.text: qsTr("Voice")
                ToolTip.visible: hovered
                ToolTip.delay: 600
            }
            LingmoUI.IconButton {
                iconName: "document-save-as"
                tipText: qsTr("Export audio…")
                onClicked: bar.exportRequested()
            }
            LingmoUI.IconButton {
                iconName: "window-close"
                tipText: qsTr("Stop reading (Esc)")
                onClicked: bar.closeRequested()
            }
        }

        // ---- options
        Flow {
            Layout.fillWidth: true
            spacing: LingmoUI.Units.largeSpacing

            Switch {
                text: qsTr("Highlight while reading")
                checked: bar.controller.highlightEnabled
                onToggled: bar.controller.highlightEnabled = checked
                font.pointSize: LingmoUI.Units.fontCaption
            }
            Switch {
                text: qsTr("Follow the reading")
                checked: bar.controller.followReading
                onToggled: bar.controller.followReading = checked
                font.pointSize: LingmoUI.Units.fontCaption
            }
            Switch {
                text: qsTr("Learning mode")
                checked: bar.controller.learning
                onToggled: bar.controller.learning = checked
                font.pointSize: LingmoUI.Units.fontCaption
            }
            Label {
                verticalAlignment: Text.AlignVCenter
                visible: bar.controller.timing === "estimate"
                width: Math.min(implicitWidth, bar.width - LingmoUI.Units.largeSpacing * 3)
                wrapMode: Text.Wrap
                text: qsTr("Word timing approximate (install the highlight model in Voice Studio)")
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
            }
        }

        // ---- learning tools (wrap on narrow windows)
        Flow {
            Layout.fillWidth: true
            visible: bar.controller.learning
            spacing: LingmoUI.Units.smallSpacing

            LingmoUI.ActionButton {
                text: qsTr("Repeat sentence")
                iconName: "media-playlist-repeat"
                onClicked: bar.controller.repeatSentence()
            }
            LingmoUI.ActionButton {
                text: qsTr("Again, slowly")
                iconName: "media-seek-backward"
                onClicked: bar.controller.repeatSlow()
            }
            Label {
                height: LingmoUI.Units.controlHeight
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Pause")
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
            }
            Slider {
                width: 110
                from: 0
                to: 5
                stepSize: 0.25
                value: bar.controller.sentencePause
                onMoved: bar.controller.sentencePause = value
                ToolTip.text: qsTr("%1 s between sentences").arg(value.toLocaleString(Qt.locale(), "f", 2))
                ToolTip.visible: pressed || hovered
            }
            Switch {
                text: qsTr("Bilingual")
                checked: bar.controller.bilingual
                onToggled: bar.controller.bilingual = checked
                font.pointSize: LingmoUI.Units.fontCaption
            }
            ComboBox {
                width: 130
                model: bar.languages
                textRole: "name"
                valueRole: "code"
                Component.onCompleted: currentIndex = Math.max(0, indexOfValue(bar.controller.language))
                onActivated: bar.controller.language = currentValue
                ToolTip.text: qsTr("Language of the text")
                ToolTip.visible: hovered
            }
            Label {
                height: LingmoUI.Units.controlHeight
                verticalAlignment: Text.AlignVCenter
                text: "→"
                color: LingmoUI.Theme.secondaryTextColor
            }
            ComboBox {
                width: 130
                model: bar.languages
                textRole: "name"
                valueRole: "code"
                Component.onCompleted: currentIndex = Math.max(0, indexOfValue(bar.controller.nativeLanguage))
                onActivated: bar.controller.nativeLanguage = currentValue
                ToolTip.text: qsTr("Your language (translations)")
                ToolTip.visible: hovered
            }
            Label {
                height: LingmoUI.Units.controlHeight
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Click a word to translate it")
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
            }
        }
    }

    Menu {
        id: speedMenu
        Repeater {
            model: bar.speeds
            delegate: MenuItem {
                required property real modelData
                text: bar.speedText(modelData)
                checkable: true
                checked: Math.abs(bar.controller.speed - modelData) < 0.01
                onTriggered: bar.controller.speed = modelData
            }
        }
    }
}
