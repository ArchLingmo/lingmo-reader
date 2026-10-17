/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// A link to the web: show where it goes before opening the browser
Popup {
    id: control

    property string url: ""

    function ask(link) {
        url = link
        open()
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
            text: qsTr("Open this link?")
            font.pointSize: LingmoUI.Units.fontHeading
            font.weight: Font.Bold
            Layout.fillWidth: true
        }
        Label {
            text: qsTr("The document links to an address outside it. It will open in your web browser.")
            color: LingmoUI.Theme.secondaryTextColor
            font.pointSize: LingmoUI.Units.fontBody
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        LingmoUI.Card {
            id: urlCard
            Layout.fillWidth: true
            implicitHeight: urlText.implicitHeight + LingmoUI.Units.largeSpacing * 1.5
            color: LingmoUI.Theme.alternateBackgroundColor

            TextEdit {
                id: urlText
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.largeSpacing * 0.75
                text: control.url
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                color: LingmoUI.Theme.linkColor
                selectionColor: LingmoUI.Theme.highlightColor
                selectedTextColor: LingmoUI.Theme.highlightedTextColor
                font.family: LingmoUI.Theme.fontFamily
                font.pointSize: LingmoUI.Units.fontBody
                verticalAlignment: TextEdit.AlignVCenter
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: LingmoUI.Units.smallSpacing
            spacing: LingmoUI.Units.smallSpacing

            LingmoUI.ActionButton {
                text: qsTr("Copy link")
                iconName: "edit-copy"
                onClicked: {
                    reader.copyText(control.url)
                    control.close()
                }
            }
            Item { Layout.fillWidth: true }
            LingmoUI.ActionButton {
                text: qsTr("Cancel")
                onClicked: control.close()
            }
            LingmoUI.ActionButton {
                text: qsTr("Open in browser")
                primary: true
                onClicked: {
                    reader.openExternal(control.url)
                    control.close()
                }
            }
        }
    }
}
