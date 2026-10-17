/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Asks for the password of a protected PDF
Popup {
    id: control

    property string fileName: ""
    property bool wrongPassword: false

    signal submitted(string password)
    signal canceled()

    property bool answered: false

    // Opens the dialog, or (still open) says the last password was wrong
    function ask(wrong) {
        wrongPassword = wrong
        field.text = ""
        if (!opened) {
            answered = false
            open()
        }
        field.forceActiveFocus()
    }

    // The document opened: close without reporting a cancel
    function finish() {
        answered = true
        close()
    }

    function accept() {
        if (field.text.length > 0)
            submitted(field.text)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(420, parent ? parent.width - LingmoUI.Units.largeSpacing * 4 : 420)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: LingmoUI.Units.largeSpacing * 2
    onClosed: {
        field.text = ""
        if (!answered) {
            answered = true
            canceled()
        }
    }

    contentItem: ColumnLayout {
        spacing: LingmoUI.Units.largeSpacing

        RowLayout {
            spacing: LingmoUI.Units.largeSpacing
            Layout.fillWidth: true

            LingmoUI.IconItem {
                source: "dialog-password"
                Layout.preferredWidth: LingmoUI.Units.iconSizes.large
                Layout.preferredHeight: LingmoUI.Units.iconSizes.large
                Layout.alignment: Qt.AlignTop
            }

            ColumnLayout {
                spacing: LingmoUI.Units.smallSpacing
                Layout.fillWidth: true

                Label {
                    text: qsTr("Password required")
                    font.pointSize: LingmoUI.Units.fontHeading
                    font.weight: Font.Bold
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("“%1” is protected. Enter its password to open it.").arg(control.fileName)
                    color: LingmoUI.Theme.secondaryTextColor
                    font.pointSize: LingmoUI.Units.fontBody
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        TextField {
            id: field
            echoMode: TextInput.Password
            placeholderText: qsTr("Password")
            Layout.fillWidth: true
            onAccepted: control.accept()
        }

        Label {
            visible: control.wrongPassword
            text: qsTr("Wrong password. Try again.")
            color: LingmoUI.Theme.errorColor
            font.pointSize: LingmoUI.Units.fontCaption
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: LingmoUI.Units.smallSpacing
            spacing: LingmoUI.Units.smallSpacing

            Item { Layout.fillWidth: true }
            LingmoUI.ActionButton {
                text: qsTr("Cancel")
                onClicked: control.close()
            }
            LingmoUI.ActionButton {
                text: qsTr("Open")
                primary: true
                enabled: field.text.length > 0
                onClicked: control.accept()
            }
        }
    }

}
