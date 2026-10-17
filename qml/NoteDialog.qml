/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Writes a new note, or edits a note / highlight (text, colour, delete).
// Saved in the sidecar through "annotations"; the PDF itself is never touched.
Popup {
    id: control

    property string annotationId: ""
    property string kind: "note"
    property string quote: ""
    property string colorName: "orange"
    property int page: 0
    property point position

    readonly property var colorNames: ["orange", "green", "blue", "pink", "purple"]

    function colorFor(name) {
        return name === "green" ? LingmoUI.Theme.greenColor
             : name === "blue" ? LingmoUI.Theme.blueColor
             : name === "pink" ? LingmoUI.Theme.pinkColor
             : name === "purple" ? LingmoUI.Theme.purpleColor
             : LingmoUI.Theme.orangeColor
    }

    function create(pageIndex, pos) {
        annotationId = ""
        kind = "note"
        quote = ""
        colorName = "orange"
        page = pageIndex
        position = pos
        editor.text = ""
        open()
        editor.forceActiveFocus()
    }

    function edit(id) {
        const a = annotations.get(id)
        if (!a.annotationId)
            return
        annotationId = id
        kind = a.kind
        quote = a.quote || ""
        colorName = a.colorName
        page = a.page
        editor.text = a.note || ""
        open()
        editor.forceActiveFocus()
        editor.cursorPosition = editor.length
    }

    function save() {
        if (annotationId.length === 0)
            annotations.addNote(page, position, editor.text)
        else {
            annotations.setNote(annotationId, editor.text)
            if (kind === "highlight")
                annotations.setColor(annotationId, colorName)
        }
        close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(460, parent ? parent.width - LingmoUI.Units.largeSpacing * 4 : 460)
    modal: true
    focus: true
    padding: LingmoUI.Units.largeSpacing * 2

    contentItem: ColumnLayout {
        spacing: LingmoUI.Units.largeSpacing

        Label {
            text: control.annotationId.length === 0 ? qsTr("New note")
                : control.kind === "highlight" ? qsTr("Highlight on page %1").arg(control.page + 1)
                : qsTr("Note on page %1").arg(control.page + 1)
            font.pointSize: LingmoUI.Units.fontHeading
            font.weight: Font.Bold
            Layout.fillWidth: true
        }

        // The highlighted passage
        RowLayout {
            visible: control.quote.length > 0
            Layout.fillWidth: true
            spacing: LingmoUI.Units.largeSpacing * 0.75

            Rectangle {
                Layout.preferredWidth: 3
                Layout.fillHeight: true
                radius: 1.5
                color: control.colorFor(control.colorName)
            }
            Label {
                text: control.quote
                font.pointSize: LingmoUI.Units.fontBody
                font.italic: true
                color: LingmoUI.Theme.secondaryTextColor
                wrapMode: Text.Wrap
                maximumLineCount: 5
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 120

            TextArea {
                id: editor
                placeholderText: control.kind === "highlight" ? qsTr("Add a comment (optional)") : qsTr("Write your note")
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                font.pointSize: LingmoUI.Units.fontBody
                Keys.onPressed: (event) => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                        control.save()
                        event.accepted = true
                    }
                }
            }
        }

        // Highlight colour
        RowLayout {
            visible: control.kind === "highlight"
            spacing: LingmoUI.Units.smallSpacing
            Layout.fillWidth: true

            Label {
                text: qsTr("Colour")
                font.pointSize: LingmoUI.Units.fontBody
                color: LingmoUI.Theme.secondaryTextColor
                Layout.rightMargin: LingmoUI.Units.smallSpacing
            }
            Repeater {
                model: control.colorNames
                delegate: Rectangle {
                    id: swatch
                    required property string modelData
                    readonly property bool selected: control.colorName === modelData
                    width: 22
                    height: 22
                    radius: 11
                    color: control.colorFor(modelData)
                    border.width: selected ? 2 : 0
                    border.color: LingmoUI.Theme.textColor
                    scale: swatchHover.hovered ? 1.1 : 1
                    Behavior on scale { NumberAnimation { duration: LingmoUI.Units.veryShortDuration } }
                    HoverHandler { id: swatchHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: control.colorName = swatch.modelData }
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: LingmoUI.Units.smallSpacing
            spacing: LingmoUI.Units.smallSpacing

            LingmoUI.ActionButton {
                visible: control.annotationId.length > 0
                text: control.kind === "highlight" ? qsTr("Remove highlight") : qsTr("Delete note")
                destructive: true
                subtle: true
                onClicked: {
                    annotations.remove(control.annotationId)
                    control.close()
                }
            }
            Item { Layout.fillWidth: true }
            LingmoUI.ActionButton {
                text: qsTr("Cancel")
                onClicked: control.close()
            }
            LingmoUI.ActionButton {
                text: qsTr("Save")
                primary: true
                enabled: control.kind === "highlight" || editor.text.trim().length > 0
                onClicked: control.save()
            }
        }
    }
}
