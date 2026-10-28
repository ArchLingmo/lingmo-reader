/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "Exportar áudio": the document (or its chapters) as an audio file, made in
// the background by the voice engine, with progress here and in the Island.
Dialog {
    id: dialog

    property var controller: readAloud
    readonly property var job: controller.exportJob
    readonly property bool running: job && (job.state === "running" || job.state === "queued" || job.state === "starting" || job.state === "paused")
    readonly property var formats: ["mp3", "m4a", "opus", "wav", "m4b"]
    property var info: ({})

    title: qsTr("Export audio")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 48 : 560, 560)
    padding: LingmoUI.Units.largeSpacing * 1.5
    standardButtons: Dialog.NoButton

    function openFor() {
        info = controller.documentInfo()
        titleField.text = info.title || ""
        authorField.text = info.author || ""
        perChapter.checked = false
        formatSwitch.currentIndex = 0
        pathField.text = controller.suggestedExportPath(formats[formatSwitch.currentIndex], false)
        open()
    }

    function eta(seconds) {
        if (seconds === undefined || seconds < 0)
            return ""
        if (seconds < 60)
            return qsTr("less than a minute left")
        const m = Math.round(seconds / 60)
        return m < 60 ? qsTr("about %1 min left").arg(m) : qsTr("about %1 h %2 min left").arg(Math.floor(m / 60)).arg(m % 60)
    }

    contentItem: ColumnLayout {
        spacing: LingmoUI.Units.largeSpacing

        // ---- while the job runs (or after)
        LingmoUI.Card {
            Layout.fillWidth: true
            visible: dialog.job && dialog.job.state !== undefined
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.largeSpacing
                spacing: LingmoUI.Units.smallSpacing
                Label {
                    text: !dialog.job ? "" : dialog.job.state === "done" ? qsTr("The audio is ready.")
                        : dialog.job.state === "failed" ? qsTr("The export failed: %1").arg(dialog.job.message || "")
                        : dialog.job.state === "cancelled" ? qsTr("The export was cancelled.")
                        : dialog.job.state === "paused" ? qsTr("Paused — it continues where it stopped.")
                        : qsTr("Exporting… %1").arg(dialog.eta(dialog.job.etaSeconds))
                    font.pointSize: LingmoUI.Units.fontBody
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                ProgressBar {
                    Layout.fillWidth: true
                    visible: dialog.running
                    from: 0
                    to: 1
                    value: dialog.job ? (dialog.job.progress || 0) : 0
                    indeterminate: dialog.job && dialog.job.state === "starting"
                }
                Label {
                    visible: dialog.job && dialog.job.state === "done" && dialog.job.outputs !== undefined
                    text: dialog.job && dialog.job.outputs ? dialog.job.outputs.map(o => o.file.split("/").pop()
                        + "  ·  " + Math.round(o.seconds / 60) + " min"
                        + (o.lufs !== null && o.lufs !== undefined ? "  ·  " + o.lufs.toFixed(1) + " LUFS" : "")).join("\n") : ""
                    color: LingmoUI.Theme.secondaryTextColor
                    font.pointSize: LingmoUI.Units.fontCaption
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    LingmoUI.ActionButton {
                        visible: dialog.running
                        text: dialog.job && dialog.job.state === "paused" ? qsTr("Continue") : qsTr("Pause")
                        onClicked: dialog.controller.exportControl(dialog.job.state === "paused" ? "resume" : "pause")
                    }
                    LingmoUI.ActionButton {
                        visible: dialog.running
                        text: qsTr("Cancel export")
                        destructive: true
                        subtle: true
                        onClicked: dialog.controller.exportControl("cancel")
                    }
                    LingmoUI.ActionButton {
                        visible: dialog.job && dialog.job.state === "done"
                        text: qsTr("Show in folder")
                        onClicked: reader.openExternal(reader.toUrl(dialog.job.outputs[0].file.substring(0, dialog.job.outputs[0].file.lastIndexOf("/"))))
                    }
                    LingmoUI.ActionButton {
                        visible: dialog.job && (dialog.job.state === "done" || dialog.job.state === "failed" || dialog.job.state === "cancelled")
                        text: qsTr("New export")
                        onClicked: dialog.controller.exportControl("forget")
                    }
                }
            }
        }

        // ---- options
        GridLayout {
            Layout.fillWidth: true
            visible: !dialog.running
            columns: 2
            columnSpacing: LingmoUI.Units.largeSpacing
            rowSpacing: LingmoUI.Units.smallSpacing

            Label { text: qsTr("Title"); color: LingmoUI.Theme.secondaryTextColor }
            TextField { id: titleField; Layout.fillWidth: true; selectByMouse: true }

            Label { text: qsTr("Author or source"); color: LingmoUI.Theme.secondaryTextColor }
            TextField { id: authorField; Layout.fillWidth: true; selectByMouse: true }

            Label { text: qsTr("Format"); color: LingmoUI.Theme.secondaryTextColor }
            LingmoUI.SegmentedControl {
                id: formatSwitch
                model: ["MP3", "M4A", "Opus", "WAV", qsTr("M4B (audiobook)")]
                onActivated: (index) => {
                    currentIndex = index
                    if (dialog.formats[index] === "m4b")
                        perChapter.checked = false
                    pathField.text = dialog.controller.suggestedExportPath(dialog.formats[index], perChapter.checked)
                }
            }

            Label { text: qsTr("Chapters"); color: LingmoUI.Theme.secondaryTextColor }
            Switch {
                id: perChapter
                text: dialog.info.chapters > 1 ? qsTr("One file per chapter (%1 chapters)").arg(dialog.info.chapters)
                                               : qsTr("One file per chapter (the document has no chapters)")
                enabled: dialog.info.chapters > 1 && dialog.formats[formatSwitch.currentIndex] !== "m4b"
                onToggled: pathField.text = dialog.controller.suggestedExportPath(dialog.formats[formatSwitch.currentIndex], checked)
            }

            Label { text: qsTr("Speed"); color: LingmoUI.Theme.secondaryTextColor }
            RowLayout {
                Slider {
                    id: speedSlider
                    Layout.fillWidth: true
                    from: 0.5; to: 2; stepSize: 0.05
                    value: dialog.controller.speed
                }
                Label { text: speedSlider.value.toFixed(2).replace(".", Qt.locale().decimalPoint) + "×"; Layout.preferredWidth: 44 }
            }

            Label { text: qsTr("Pause between sentences"); color: LingmoUI.Theme.secondaryTextColor }
            RowLayout {
                Slider {
                    id: pauseSlider
                    Layout.fillWidth: true
                    from: 0.1; to: 3; stepSize: 0.05
                    value: dialog.controller.learning ? dialog.controller.sentencePause : 0.35
                }
                Label { text: pauseSlider.value.toFixed(2).replace(".", Qt.locale().decimalPoint) + " s"; Layout.preferredWidth: 44 }
            }

            Label { text: qsTr("Learning"); color: LingmoUI.Theme.secondaryTextColor; visible: dialog.controller.learning }
            ColumnLayout {
                visible: dialog.controller.learning
                spacing: 0
                CheckBox { id: slowRepeat; text: qsTr("Each sentence again, slowly") }
                CheckBox { id: bilingual; text: qsTr("Each sentence followed by its translation"); checked: dialog.controller.bilingual }
            }

            Label { text: qsTr("Extras"); color: LingmoUI.Theme.secondaryTextColor }
            ColumnLayout {
                spacing: 0
                CheckBox { id: lyrics; text: qsTr("Text inside the file (lyrics)"); checked: true }
                CheckBox { id: lrc; text: qsTr("Timed text .lrc (word by word, for players)"); checked: true }
                CheckBox { id: srt; text: qsTr("Subtitles .srt (sentences)") }
            }

            Label { text: qsTr("Save to"); color: LingmoUI.Theme.secondaryTextColor }
            RowLayout {
                TextField { id: pathField; Layout.fillWidth: true; selectByMouse: true }
                LingmoUI.IconButton {
                    iconName: "document-open-folder"
                    tipText: qsTr("Choose…")
                    onClicked: perChapter.checked ? folderDialog.open() : fileDialog.open()
                }
            }
        }

        Label {
            visible: !dialog.running
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("The audio is normalised to −16 LUFS for speech. You can keep reading or close the Reader: the export continues in the background and shows in the status bar.")
            color: LingmoUI.Theme.secondaryTextColor
            font.pointSize: LingmoUI.Units.fontCaption
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            LingmoUI.ActionButton {
                text: dialog.running ? qsTr("Close") : qsTr("Cancel")
                onClicked: dialog.close()
            }
            LingmoUI.ActionButton {
                id: exportButton
                visible: !dialog.running
                text: qsTr("Export")
                primary: true
                enabled: pathField.text.length > 0
                onClicked: dialog.controller.exportAudio({
                    format: dialog.formats[formatSwitch.currentIndex],
                    perChapter: perChapter.checked,
                    speed: speedSlider.value,
                    pause: pauseSlider.value,
                    title: titleField.text,
                    author: authorField.text,
                    lyrics: lyrics.checked,
                    lrc: lrc.checked,
                    srt: srt.checked,
                    slowRepeat: dialog.controller.learning && slowRepeat.checked,
                    bilingual: dialog.controller.learning && bilingual.checked,
                    out: pathField.text
                })
            }
        }
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: dialog.opened && !dialog.running && exportButton.enabled
        onActivated: exportButton.clicked()
    }

    FileDialog {
        id: fileDialog
        fileMode: FileDialog.SaveFile
        currentFile: reader.toUrl(pathField.text)
        onAccepted: pathField.text = reader.toLocalPath(selectedFile)
    }
    FolderDialog {
        id: folderDialog
        onAccepted: pathField.text = reader.toLocalPath(selectedFolder)
    }
}
