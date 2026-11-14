/*
  Q Light Controller Plus
  RenderShadowMapFilter.qml

  Copyright (c) Eric Arnebäck

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
import Qt3D.Core
import QtQuick
import Qt3D.Core
import Qt3D.Render

/* Draws the shadow map of one light: the scene seen from it.
 *
 * It is the most expensive thing a light asks for, since the whole scene is
 * drawn again, so it is only done when it can change something: while the
 * light is lit, and for a moment after the light or the scene has moved. A
 * light that stands still keeps the map it has. A fixture body that moves in
 * front of another light does not count as the scene moving: its shadow would
 * be redrawn late, which nobody looks at in a show.
 *
 * SubtreeEnabler is what really skips the work: a node that is merely
 * disabled still gets its render view, clear included. */
SubtreeEnabler
{
    id: shadowRoot

    property Entity fixtureItem
    property alias layers: shadowLayerFilter.layers

    readonly property bool lit: fixtureItem && fixtureItem.lightIntensity ? true : false
    property bool dirty: true

    enabled: lit && dirty

    property matrix4x4 lightView: fixtureItem ? fixtureItem.lightViewProjectionMatrix : Qt.matrix4x4()
    onLightViewChanged: markDirty()
    onLitChanged: if (lit) settleTimer.restart()

    function markDirty()
    {
        dirty = true
        settleTimer.restart()
    }

    // a map drawn while dark does not exist: stay dirty until lit
    property Timer settleTimer:
        Timer
        {
            interval: 300
            onTriggered: if (shadowRoot.lit) shadowRoot.dirty = false
        }

    property Connections sceneWatch:
        Connections
        {
            target: View3D
            function onShadowsInvalidated() { shadowRoot.markDirty() }
        }

    LayerFilter
    {
        id: shadowLayerFilter

        CameraSelector
        {
            RenderTargetSelector
            {
                id: targetSelector
                target: fixtureItem ? fixtureItem.shadowMap : null

                ClearBuffers
                {
                    buffers: ClearBuffers.DepthBuffer

                    RenderPassFilter
                    {
                        id: geometryPass
                        matchAny: FilterKey { name: "pass"; value: "shadows" }

                        parameters: [
                            Parameter { name: "lightViewMatrix"; value: fixtureItem ? fixtureItem.lightViewMatrix : Qt.matrix4x4() },
                            Parameter { name: "lightProjectionMatrix"; value: fixtureItem ? fixtureItem.lightProjectionMatrix : Qt.matrix4x4() },
                            Parameter { name: "lightViewProjectionMatrix"; value: fixtureItem ? fixtureItem.lightViewProjectionMatrix : Qt.matrix4x4() }
                        ]
                    }
                }
            }
        }
    }
}
