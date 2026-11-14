/*
  VertexAV Director
  PopupStartupProject.qml

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs

import org.qlcplus.classes 1.0
import "."

/** Which project the application opens when it starts.
 *
 *  It is shown on its own after a project has been opened at startup, to say
 *  so (until the user asks not to be told again), and from the Actions menu
 *  to change the preference. */
CustomPopupDialog
{
    id: popupRoot

    width: Math.min(mainView.width * 0.6, UISettings.bigItemHeight * 9)
    title: qsTr("Project at startup")
    standardButtons: Dialog.Ok

    /** true when it opens by itself, to tell what has just been opened */
    property bool asNotice: false

    onOpened:
    {
        fileEdit.text = qlcplus.startupFile !== "" ? qlcplus.startupFile : qlcplus.fileName()
        noticeCheck.checked = !qlcplus.startupNotice
    }

    onClicked: (role) =>
    {
        if (fileRadio.checked)
            qlcplus.startupFile = fileEdit.text
        qlcplus.startupNotice = !noticeCheck.checked
        asNotice = false
    }

    ButtonGroup { id: startupGroup }

    FileDialog
    {
        id: startupFileDialog
        title: qsTr("Project to open at startup")
        fileMode: FileDialog.OpenFile
        nameFilters: [ qsTr("Project files") + " (*.qxw *.qxw.gz)", qsTr("All files") + " (*)" ]
        onAccepted:
        {
            var path = selectedFile.toString()
            path = path.replace(/^file:\/\/\//, Qt.platform.os === "windows" ? "" : "/")
            fileEdit.text = decodeURIComponent(path)
            fileRadio.checked = true
            qlcplus.startupMode = App.StartupFile
        }
    }

    contentItem:
        GridLayout
        {
            columns: 2
            columnSpacing: UISettings.listItemHeight / 2
            rowSpacing: UISettings.listItemHeight / 4

            RobotoText
            {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                visible: popupRoot.asNotice
                height: visible ? UISettings.listItemHeight * 2 : 0
                wrapText: true
                label: qsTr("The project you used last has been opened:") + "\n" + qlcplus.startupLoadedFile
            }

            RobotoText
            {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                fontBold: true
                label: qsTr("When the application starts:")
            }

            CustomCheckBox
            {
                id: lastRadio
                implicitHeight: UISettings.listItemHeight
                implicitWidth: height
                ButtonGroup.group: startupGroup
                checked: qlcplus.startupMode === App.StartupLast
                onClicked: qlcplus.startupMode = App.StartupLast
            }
            RobotoText
            {
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                label: qsTr("Open the project used last")
            }

            CustomCheckBox
            {
                id: fileRadio
                implicitHeight: UISettings.listItemHeight
                implicitWidth: height
                ButtonGroup.group: startupGroup
                checked: qlcplus.startupMode === App.StartupFile
                onClicked: qlcplus.startupMode = App.StartupFile
            }
            RobotoText
            {
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                label: qsTr("Always open this project:")
            }

            Item { implicitWidth: 1; implicitHeight: 1 }
            RowLayout
            {
                Layout.fillWidth: true
                spacing: UISettings.listItemHeight / 4

                CustomTextEdit
                {
                    id: fileEdit
                    Layout.fillWidth: true
                    implicitHeight: UISettings.listItemHeight
                    enabled: fileRadio.checked
                    onTextChanged: if (fileRadio.checked) qlcplus.startupFile = text
                }
                GenericButton
                {
                    implicitHeight: UISettings.listItemHeight
                    implicitWidth: UISettings.bigItemHeight
                    label: qsTr("Browse...")
                    onClicked: startupFileDialog.open()
                }
            }

            CustomCheckBox
            {
                id: noneRadio
                implicitHeight: UISettings.listItemHeight
                implicitWidth: height
                ButtonGroup.group: startupGroup
                checked: qlcplus.startupMode === App.StartupNone
                onClicked: qlcplus.startupMode = App.StartupNone
            }
            RobotoText
            {
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                label: qsTr("Start with an empty project")
            }

            Rectangle
            {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                implicitHeight: 1
                color: UISettings.bgLight
            }

            CustomCheckBox
            {
                id: noticeCheck
                implicitHeight: UISettings.listItemHeight
                implicitWidth: height
            }
            RobotoText
            {
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                label: qsTr("Do not show this notice again")
            }

            RobotoText
            {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                fontSize: UISettings.textSizeDefault * 0.8
                labelColor: UISettings.fgMedium
                label: qsTr("This can be changed at any time from Actions, \"Project at startup\".")
            }
        }
}
