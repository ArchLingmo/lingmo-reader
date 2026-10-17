/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// No document open: "Open document" and the recent files
Item {
    id: welcome

    signal openRequested()
    signal fileChosen(string path)

    function relativeTime(date) {
        if (!date || isNaN(date.getTime()))
            return ""
        const now = new Date()
        const today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        const day = new Date(date.getFullYear(), date.getMonth(), date.getDate())
        const days = Math.round((today - day) / 86400000)
        if (days <= 0)
            return qsTr("Today, %1").arg(Qt.formatTime(date, Locale.ShortFormat))
        if (days === 1)
            return qsTr("Yesterday")
        if (days < 7)
            return qsTr("%n day(s) ago", "", days)
        return Qt.formatDate(date, Locale.ShortFormat)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        LingmoUI.PageHeader {
            Layout.fillWidth: true
            Layout.rightMargin: LingmoUI.Units.largeSpacing
            title: qsTr("Documents")
            reserveWindowControls: true
        }

        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: content.implicitHeight + LingmoUI.Units.largeSpacing * 4
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            LingmoUI.WheelHandler { target: scroller }

            ColumnLayout {
                id: content
                width: Math.min(scroller.width - LingmoUI.Units.largeSpacing * 4, 640)
                x: (scroller.width - width) / 2
                y: recentList.count > 0 ? LingmoUI.Units.largeSpacing * 3
                                        : Math.max(LingmoUI.Units.largeSpacing * 3, (scroller.height - implicitHeight) / 2 - LingmoUI.Units.windowHeaderHeight)
                spacing: LingmoUI.Units.largeSpacing

                LingmoUI.EmptyState {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Math.min(380, content.width)
                    iconName: "application-pdf"
                    title: qsTr("No document open")
                    text: qsTr("Open a PDF or drop it on this window. Documents reopen at the page where you stopped.")
                    actionText: qsTr("Open document")
                    onActionTriggered: welcome.openRequested()
                }

                RowLayout {
                    visible: recentList.count > 0
                    Layout.fillWidth: true
                    Layout.topMargin: LingmoUI.Units.largeSpacing * 2

                    Label {
                        text: qsTr("Recent")
                        font.pointSize: LingmoUI.Units.fontHeading
                        font.weight: Font.Bold
                        Layout.fillWidth: true
                    }
                    LingmoUI.ActionButton {
                        text: qsTr("Clear list")
                        onClicked: recentFiles.clear()
                    }
                }

                Repeater {
                    id: recentList
                    model: recentFiles

                    delegate: LingmoUI.Card {
                        id: card
                        required property string path
                        required property string name
                        required property string folder
                        required property int page
                        required property int pageCount
                        required property var opened
                        required property bool exists

                        Layout.fillWidth: true
                        implicitHeight: row.implicitHeight + LingmoUI.Units.largeSpacing * 1.5
                        clickable: exists
                        hoverEnabled: true
                        onClicked: welcome.fileChosen(card.path)

                        RowLayout {
                            id: row
                            anchors.fill: parent
                            anchors.leftMargin: card.padding
                            anchors.rightMargin: LingmoUI.Units.largeSpacing
                            spacing: LingmoUI.Units.largeSpacing

                            LingmoUI.IconItem {
                                source: "application-pdf"
                                opacity: card.exists ? 1 : 0.5
                                Layout.preferredWidth: LingmoUI.Units.iconSizes.medium
                                Layout.preferredHeight: LingmoUI.Units.iconSizes.medium
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Label {
                                    text: card.name
                                    font.pointSize: LingmoUI.Units.fontBody
                                    font.weight: Font.DemiBold
                                    color: card.exists ? LingmoUI.Theme.textColor : LingmoUI.Theme.disabledTextColor
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: card.exists ? card.folder : qsTr("File not found · %1").arg(card.folder)
                                    font.pointSize: LingmoUI.Units.fontCaption
                                    color: LingmoUI.Theme.secondaryTextColor
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }

                            ColumnLayout {
                                spacing: 2
                                Layout.alignment: Qt.AlignVCenter

                                Label {
                                    text: card.pageCount > 0 ? qsTr("Page %1 of %2").arg(card.page + 1).arg(card.pageCount)
                                                             : qsTr("Page %1").arg(card.page + 1)
                                    font.pointSize: LingmoUI.Units.fontCaption
                                    color: LingmoUI.Theme.secondaryTextColor
                                    Layout.alignment: Qt.AlignRight
                                }
                                Label {
                                    text: welcome.relativeTime(card.opened)
                                    font.pointSize: LingmoUI.Units.fontCaption
                                    color: LingmoUI.Theme.secondaryTextColor
                                    Layout.alignment: Qt.AlignRight
                                }
                            }

                            // Reading progress
                            Rectangle {
                                visible: card.pageCount > 1
                                Layout.preferredWidth: 4
                                Layout.preferredHeight: LingmoUI.Units.iconSizes.medium
                                radius: 2
                                color: LingmoUI.Theme.alternateBackgroundColor
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    radius: 2
                                    height: parent.height * Math.min(1, (card.page + 1) / Math.max(1, card.pageCount))
                                    color: LingmoUI.Theme.highlightColor
                                }
                            }

                            LingmoUI.IconButton {
                                iconName: "edit-delete"
                                tipText: qsTr("Remove from list")
                                opacity: card.hovered ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: LingmoUI.Units.shortDuration } }
                                onClicked: recentFiles.remove(card.path)
                            }
                        }
                    }
                }
            }
        }
    }
}
