/*
  Q Light Controller Plus
  DirectorPanel.qml

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

/* Floating panel of the Director: live state, overrides and demo mode.
 * It floats over the current view so the 2D/3D preview stays visible. */
Rectangle
{
    id: panelRoot
    width: UISettings.bigItemHeight * 4.6
    height: contentCol.implicitHeight + 2 * pad
    color: UISettings.bgStrong
    border.color: UISettings.bgLight
    border.width: 1
    radius: 6
    clip: true

    readonly property int pad: 10
    readonly property int fontPx: UISettings.textSizeDefault
    readonly property int rowH: UISettings.listItemHeight
    readonly property bool ready: director ? director.hasRules : false

    signal closeRequested()
    /** The operator asked for the overrides panel */
    signal overridesRequested()

    // Swallow clicks and wheel events so they do not reach the view below
    MouseArea
    {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: function(wheel) { wheel.accepted = true }
    }

    /* A small text button. "active" lights it up, "momentary" reports
     * press and release instead of clicks. */
    component PanelButton: Rectangle
    {
        id: btn
        property string label: ""
        property bool active: false
        property bool momentary: false
        property color activeColor: UISettings.highlight
        signal clicked()
        signal pressed()
        signal released()

        implicitWidth: btnText.implicitWidth + 16
        implicitHeight: UISettings.listItemHeight
        radius: 4
        color: active ? activeColor : (btnMouse.containsMouse ? UISettings.bgLight : UISettings.bgControl)
        opacity: enabled ? 1.0 : 0.4

        Text
        {
            id: btnText
            anchors.centerIn: parent
            text: btn.label
            color: UISettings.fgMain
            font.family: UISettings.robotoFontName
            font.pixelSize: UISettings.textSizeDefault
        }

        MouseArea
        {
            id: btnMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: if (!btn.momentary) btn.clicked()
            onPressed: if (btn.momentary) btn.pressed()
            onReleased: if (btn.momentary) btn.released()
            onCanceled: if (btn.momentary) btn.released()
        }
    }

    component PanelLabel: Text
    {
        color: UISettings.fgMain
        font.family: UISettings.robotoFontName
        font.pixelSize: UISettings.textSizeDefault
        elide: Text.ElideRight
    }

    component SectionTitle: Text
    {
        color: UISettings.fgLight
        font.family: UISettings.robotoFontName
        font.pixelSize: UISettings.textSizeDefault * 0.8
        font.bold: true
        font.capitalization: Font.AllUppercase
    }

    ColumnLayout
    {
        id: contentCol
        x: panelRoot.pad
        y: panelRoot.pad
        width: panelRoot.width - 2 * panelRoot.pad
        spacing: 6

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

                PanelLabel
                {
                    text: qsTr("Director")
                    font.bold: true
                    font.pixelSize: panelRoot.fontPx * 1.15
                }

                Rectangle
                {
                    implicitWidth: panelRoot.fontPx * 0.7
                    implicitHeight: implicitWidth
                    radius: implicitWidth / 2
                    color: (director && director.connected) ? "#3CCB5A" : UISettings.bgLight
                }

                PanelLabel
                {
                    Layout.fillWidth: true
                    text: director ? director.source : ""
                    color: UISettings.fgLight
                }

                PanelButton
                {
                    label: (director && director.enabled) ? qsTr("AUTO") : qsTr("OFF")
                    active: director ? director.enabled : false
                    activeColor: "#2E9E48"
                    enabled: panelRoot.ready
                    onClicked: director.enabled = !director.enabled
                }

                // Opens the overrides panel. Amber while something is overridden
                IconButton
                {
                    implicitWidth: panelRoot.rowH
                    implicitHeight: panelRoot.rowH
                    width: panelRoot.rowH
                    height: panelRoot.rowH
                    faSource: FontAwesome.fa_sliders
                    faColor: (director && director.activeOverrides > 0) ? "#FFB020" : UISettings.fgMain
                    enabled: panelRoot.ready
                    tooltip: qsTr("Overrides: section, intensity, colour and live controls")
                    onClicked: panelRoot.overridesRequested()
                }

                PanelButton
                {
                    label: "✕"
                    onClicked: panelRoot.closeRequested()
                }
            }
        }

        // ── No rules yet ─────────────────────────────────────────────────────
        PanelLabel
        {
            Layout.fillWidth: true
            visible: !panelRoot.ready
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            color: UISettings.fgLight
            text: qsTr("The Director has no rules yet. Run the Show Wizard and leave \"Let the Director run the show\" checked: it builds the looks and the rules from your fixture groups.")
        }

        // ── Music ────────────────────────────────────────────────────────────
        SectionTitle { text: qsTr("Music"); visible: panelRoot.ready }

        RowLayout
        {
            Layout.fillWidth: true
            visible: panelRoot.ready
            spacing: 10

            PanelLabel
            {
                text: (director && director.bpm > 0) ? director.bpm + " BPM" : "--- BPM"
                font.bold: true
            }

            Row
            {
                spacing: 3
                Repeater
                {
                    model: 4
                    Rectangle
                    {
                        width: panelRoot.fontPx * 0.8
                        height: width
                        radius: 2
                        color: (director && director.beatInBar === index + 1)
                               ? (index === 0 ? "#FFB020" : UISettings.highlight) : UISettings.bgControl
                    }
                }
            }

            PanelLabel
            {
                Layout.fillWidth: true
                text: director ? (director.section ? director.section.toUpperCase() : "---") : ""
                font.bold: true
                color: (director && director.forcedSection !== "") ? "#FFB020" : UISettings.fgMain
            }

            PanelLabel
            {
                text: director ? (director.intensity ? director.intensity.toUpperCase() : "---") : ""
                font.bold: true
                color: (director && director.forcedIntensity !== "") ? "#FFB020" : UISettings.fgMain
            }
        }

        RowLayout
        {
            Layout.fillWidth: true
            visible: panelRoot.ready
            spacing: 10

            // Energy
            Rectangle
            {
                Layout.fillWidth: true
                implicitHeight: panelRoot.fontPx * 0.6
                radius: 2
                color: UISettings.bgControl

                Rectangle
                {
                    width: parent.width * (director ? director.energy / 255 : 0)
                    height: parent.height
                    radius: 2
                    color: UISettings.highlight
                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }

            // Palette received from the music
            Row
            {
                spacing: 3
                Repeater
                {
                    model: director ? director.paletteColors : []
                    Rectangle
                    {
                        width: panelRoot.fontPx
                        height: width
                        radius: 2
                        color: modelData
                        border.color: UISettings.bgLight
                        border.width: 1
                    }
                }
            }

            PanelLabel
            {
                text: (director && director.strobeOk) ? qsTr("strobe ok") : qsTr("no strobe")
                color: (director && director.strobeOk) ? "#3CCB5A" : UISettings.fgMedium
            }
        }

        // ── Layers ───────────────────────────────────────────────────────────
        RowLayout
        {
            Layout.fillWidth: true
            visible: panelRoot.ready

            SectionTitle { text: qsTr("Layers") }
            PanelLabel
            {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                color: UISettings.fgLight
                text: director ? (director.ruleName !== "" ? qsTr("Rule: %1").arg(director.ruleName) : qsTr("No rule active")) : ""
            }
        }

        Repeater
        {
            model: (panelRoot.ready && director) ? director.layers : []

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 6

                PanelLabel
                {
                    Layout.preferredWidth: panelRoot.fontPx * 5.5
                    text: modelData.name
                    color: UISettings.fgLight
                }

                PanelLabel
                {
                    Layout.fillWidth: true
                    text: modelData.off ? qsTr("off") : (modelData.count > 0 ? modelData.functions : "—")
                    color: modelData.off ? UISettings.fgMedium : UISettings.fgMain
                }

                PanelButton
                {
                    label: qsTr("Pin")
                    active: modelData.pinned
                    activeColor: "#C07A00"
                    onClicked: director.pinLayer(modelData.index, !modelData.pinned)
                }

                PanelButton
                {
                    label: qsTr("Off")
                    active: modelData.off
                    activeColor: "#A03030"
                    onClicked: director.setLayerOff(modelData.index, !modelData.off)
                }
            }
        }

        // ── Demo ─────────────────────────────────────────────────────────────
        SectionTitle { text: qsTr("Demo (no music needed)"); visible: panelRoot.ready }

        RowLayout
        {
            Layout.fillWidth: true
            visible: panelRoot.ready
            spacing: 6

            PanelButton
            {
                label: (director && director.demoRunning) ? qsTr("■ Stop demo") : qsTr("▶ Start demo")
                active: director ? director.demoRunning : false
                activeColor: "#2E9E48"
                onClicked:
                {
                    if (director.demoRunning)
                    {
                        director.stopDemo()
                    }
                    else
                    {
                        if (!director.enabled)
                            director.enabled = true
                        director.startDemo()
                    }
                }
            }

            Item { Layout.fillWidth: true }

            PanelButton { label: "−"; onClicked: director.demoBpm = director.demoBpm - 4 }
            PanelLabel { text: (director ? director.demoBpm : 0) + " BPM" }
            PanelButton { label: "+"; onClicked: director.demoBpm = director.demoBpm + 4 }
        }

        Flow
        {
            Layout.fillWidth: true
            visible: panelRoot.ready
            spacing: 4

            Repeater
            {
                model: director ? director.demoSong : []
                PanelButton
                {
                    label: modelData.section
                    enabled: director ? director.demoRunning : false
                    active: director ? (director.demoRunning && director.demoStep === modelData.index) : false
                    activeColor: "#2E9E48"
                    onClicked: director.demoJumpTo(modelData.index)
                }
            }
        }

        PanelLabel
        {
            Layout.fillWidth: true
            visible: panelRoot.ready
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            color: UISettings.fgMedium
            font.pixelSize: panelRoot.fontPx * 0.8
            text: qsTr("Open the 2D or 3D view to watch the show. Click a section to jump to it on the next bar.")
        }
    }
}
