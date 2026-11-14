/*
  VertexAV Director
  PopupGraphicsCard.qml

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

import org.qlcplus.classes 1.0
import "."

/** Which graphics card draws the application, on a computer with two */
CustomPopupDialog
{
    id: popupRoot

    width: Math.min(mainView.width * 0.6, UISettings.bigItemHeight * 9)
    title: qsTr("Graphics card")
    standardButtons: Dialog.Ok

    /** Card names in the order of GraphicsCard::Preference */
    property var cardNames: qlcplus.graphicsCardNames
    /** The preference the application was started with */
    property int startedWith: { startedWith = qlcplus.graphicsCard }

    function cardLabel(text, index)
    {
        var name = (cardNames && cardNames.length > index) ? cardNames[index] : ""
        return name !== "" ? text + ": " + name : text
    }

    ButtonGroup { id: cardGroup }

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
                height: UISettings.listItemHeight
                label: qsTr("In use now: %1").arg(qlcplus.graphicsRenderer !== "" ? qlcplus.graphicsRenderer : qsTr("unknown"))
            }

            RobotoText
            {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                fontBold: true
                label: qsTr("Draw the application with:")
            }

            CustomCheckBox
            {
                implicitHeight: UISettings.listItemHeight
                implicitWidth: height
                ButtonGroup.group: cardGroup
                checked: qlcplus.graphicsCard === 0
                onClicked: qlcplus.graphicsCard = 0
            }
            RobotoText
            {
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                label: popupRoot.cardLabel(qsTr("The dedicated card (default)"), 0)
            }

            CustomCheckBox
            {
                implicitHeight: UISettings.listItemHeight
                implicitWidth: height
                ButtonGroup.group: cardGroup
                checked: qlcplus.graphicsCard === 1
                onClicked: qlcplus.graphicsCard = 1
            }
            RobotoText
            {
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                label: popupRoot.cardLabel(qsTr("The power saving card"), 1)
            }

            CustomCheckBox
            {
                implicitHeight: UISettings.listItemHeight
                implicitWidth: height
                ButtonGroup.group: cardGroup
                checked: qlcplus.graphicsCard === 2
                onClicked: qlcplus.graphicsCard = 2
            }
            RobotoText
            {
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                label: qsTr("Whatever Windows decides")
            }

            RobotoText
            {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                height: UISettings.listItemHeight * 2
                wrapText: true
                labelColor: qlcplus.graphicsCard !== popupRoot.startedWith ? "#FFB020" : UISettings.fgMedium
                label: qlcplus.graphicsCard !== popupRoot.startedWith
                       ? qsTr("Close the application and open it again to use the card you chose.")
                       : qsTr("The 3D view needs the dedicated card. A change takes effect the next time the application is opened.")
            }
        }
}
