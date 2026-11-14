/*
  VertexAV Director
  DirectorOverridesPanel.qml

  Copyright (c) Adan Ruiz

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

import "."

/* Floating panel with what the operator can take away from the Director:
 * the section, the intensity and the colour of the show, plus the live
 * controls. It is opened from the Director panel. */
Rectangle
{
    id: panelRoot
    width: UISettings.bigItemHeight * 4.2
    height: contentCol.implicitHeight + 2 * pad
    color: UISettings.bgStrong
    border.color: UISettings.bgLight
    border.width: 1
    radius: 6
    clip: true

    readonly property int pad: 10
    readonly property int fontPx: UISettings.textSizeDefault
    readonly property int rowH: UISettings.listItemHeight

    /* Colours the operator can force, in the order they are shown */
    readonly property var colours: [
        { "name": qsTr("Red"), "value": "#ff0000" },
        { "name": qsTr("Orange"), "value": "#ff6a00" },
        { "name": qsTr("Amber"), "value": "#ffb000" },
        { "name": qsTr("Yellow"), "value": "#ffff00" },
        { "name": qsTr("Green"), "value": "#00ff00" },
        { "name": qsTr("Cyan"), "value": "#00ffff" },
        { "name": qsTr("Blue"), "value": "#0030ff" },
        { "name": qsTr("Purple"), "value": "#8000ff" },
        { "name": qsTr("Magenta"), "value": "#ff00ff" },
        { "name": qsTr("Pink"), "value": "#ff4090" },
        { "name": qsTr("White"), "value": "#ffffff" }
    ]

    signal closeRequested()

    // Swallow clicks and wheel events so they do not reach the view below
    MouseArea
    {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: function(wheel) { wheel.accepted = true }
    }

    /* A titled box around one kind of override */
    component OverrideBox: Rectangle
    {
        id: box
        property string title: ""
        default property alias content: boxFlow.data

        Layout.fillWidth: true
        implicitHeight: boxCol.implicitHeight + 16
        color: UISettings.bgMedium
        border.color: UISettings.bgLight
        border.width: 1
        radius: 4

        Column
        {
            id: boxCol
            x: 8
            y: 8
            width: box.width - 16
            spacing: 6

            Text
            {
                text: box.title
                color: UISettings.fgLight
                font.family: UISettings.robotoFontName
                font.pixelSize: UISettings.textSizeDefault * 0.8
                font.bold: true
                font.capitalization: Font.AllUppercase
            }

            Flow
            {
                id: boxFlow
                width: parent.width
                spacing: 4
            }
        }
    }

    /* A button painted in the colour it forces, with a tick when it is the
     * one in force */
    component ColourButton: Rectangle
    {
        id: swatch
        property string value: "#ffffff"
        property string name: ""
        readonly property bool active: director ? director.forcedColor === value : false

        width: UISettings.listItemHeight * 1.5
        height: UISettings.listItemHeight * 1.2
        radius: 4
        color: value
        border.color: active ? UISettings.fgMain : (swatchMouse.containsMouse ? UISettings.fgLight : UISettings.bgLight)
        border.width: active ? 3 : 1

        Text
        {
            anchors.centerIn: parent
            visible: swatch.active
            text: "✓"
            // dark on the light colours, light on the dark ones
            color: (swatch.color.r * 0.3 + swatch.color.g * 0.59 + swatch.color.b * 0.11) > 0.55 ? "black" : "white"
            font.pixelSize: UISettings.textSizeDefault * 1.4
            font.bold: true
        }

        MouseArea
        {
            id: swatchMouse
            anchors.fill: parent
            hoverEnabled: true
            // a second click on the colour in force gives it back to the music
            onClicked: director.forceColor(swatch.active ? "" : swatch.value)
        }
    }

    ColumnLayout
    {
        id: contentCol
        x: panelRoot.pad
        y: panelRoot.pad
        width: panelRoot.width - 2 * panelRoot.pad
        spacing: 8

        // ── Header ───────────────────────────────────────────────────────────
        Rectangle
        {
            Layout.fillWidth: true
            implicitHeight: panelRoot.rowH * 1.2
            color: UISettings.sectionHeader
            radius: 4

            MouseArea
            {
                anchors.fill: parent
                drag.target: panelRoot
                drag.minimumX: 0
                drag.minimumY: 0
                cursorShape: Qt.SizeAllCursor
            }

            RowLayout
            {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 8

                DirectorPanel.PanelLabel
                {
                    Layout.fillWidth: true
                    text: qsTr("Director Overrides")
                    font.bold: true
                    font.pixelSize: panelRoot.fontPx * 1.15
                }

                DirectorPanel.PanelButton
                {
                    label: qsTr("Release all (%1)").arg(director ? director.activeOverrides : 0)
                    enabled: director ? director.activeOverrides > 0 : false
                    onClicked: director.releaseAll()
                }

                DirectorPanel.PanelButton
                {
                    label: "✕"
                    onClicked: panelRoot.closeRequested()
                }
            }
        }

        // ── Section ──────────────────────────────────────────────────────────
        OverrideBox
        {
            title: qsTr("Section overrides")

            DirectorPanel.PanelButton
            {
                label: qsTr("Auto")
                active: director ? director.forcedSection === "" : false
                onClicked: director.forceSection("")
            }

            Repeater
            {
                model: director ? director.sections : []
                DirectorPanel.PanelButton
                {
                    label: modelData
                    visible: modelData !== "unknown"
                    active: director ? director.forcedSection === modelData : false
                    activeColor: "#C07A00"
                    onClicked: director.forceSection(modelData)
                }
            }
        }

        // ── Intensity ────────────────────────────────────────────────────────
        OverrideBox
        {
            title: qsTr("Intensity overrides")

            DirectorPanel.PanelButton
            {
                label: qsTr("Auto")
                active: director ? director.forcedIntensity === "" : false
                onClicked: director.forceIntensity("")
            }

            Repeater
            {
                model: director ? director.intensities : []
                DirectorPanel.PanelButton
                {
                    label: modelData
                    active: director ? director.forcedIntensity === modelData : false
                    activeColor: "#C07A00"
                    onClicked: director.forceIntensity(modelData)
                }
            }
        }

        // ── Colour ───────────────────────────────────────────────────────────
        OverrideBox
        {
            title: qsTr("Colour overrides")

            DirectorPanel.PanelButton
            {
                label: qsTr("Auto")
                active: director ? director.forcedColor === "" : false
                onClicked: director.forceColor("")
            }

            Repeater
            {
                model: panelRoot.colours
                ColourButton
                {
                    value: modelData.value
                    name: modelData.name
                }
            }
        }

        // ── Live ─────────────────────────────────────────────────────────────
        OverrideBox
        {
            title: qsTr("Live")

            DirectorPanel.PanelButton
            {
                label: qsTr("Hold")
                active: director ? director.hold : false
                activeColor: "#C07A00"
                onClicked: director.hold = !director.hold
            }

            DirectorPanel.PanelButton
            {
                label: qsTr("Next look")
                onClicked: director.nextLook()
            }

            DirectorPanel.PanelButton
            {
                label: qsTr("Blackout")
                momentary: true
                active: director ? director.blackout : false
                activeColor: "#A03030"
                onPressed: director.blackout = true
                onReleased: director.blackout = false
            }

            DirectorPanel.PanelButton
            {
                label: qsTr("Strobe")
                momentary: true
                enabled: director ? director.strobeAllowed : false
                active: director ? director.strobeForced : false
                activeColor: "#D0D0D0"
                onPressed: director.strobeForced = true
                onReleased: director.strobeForced = false
            }

            DirectorPanel.PanelButton
            {
                label: (director && director.strobeAllowed) ? qsTr("Strobes allowed") : qsTr("Strobes blocked")
                active: director ? !director.strobeAllowed : false
                activeColor: "#A03030"
                onClicked: director.strobeAllowed = !director.strobeAllowed
            }
        }
    }
}
