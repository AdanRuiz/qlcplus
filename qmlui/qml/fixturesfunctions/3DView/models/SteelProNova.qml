/*
  VertexAV Director
  SteelProNova.qml

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

import Qt3D.Core
import Qt3D.Render
import Qt3D.Extras

import org.qlcplus.classes 1.0
import "Math3DView.js" as Math3D
import "."

/*
  3D model of the SteelPro Nova, which is not a moving head with one beam out
  of its front:

    base > pan (L shaped yoke) > tilt > head spinning on its own axis

  The head is an octagon. A green laser fans out of its front, along the
  axis pan and tilt aim. Its eight SIDES alternate four RGBW beams and four
  yellow LEDs, all looking outwards at right angles to that axis. The
  yellow LEDs are the strobe of the fixture, separate from the beams, which
  have no shutter. As the head spins, beams and yellow LEDs go round with it.

  Like the moving head mesh, it is modelled hanging, base on top, with the
  head axis pointing straight down in the middle of the tilt range. The
  laser leaves through the face that looks away from the base (-Y here),
  which is the direction a position aims: QLC+ aims a head along its -Y.

  MainView3D picks this item by fixture model (see customItemResource()) and
  drives it like any other, plus setChannelValues() for what only a Nova has.
*/
Entity
{
    id: fixtureEntity
    objectName: "fixture3DItem"

    /* Tells MainView3D this item draws itself and wants the raw channels */
    readonly property bool customModel: true

    property int itemID: fixtureManager.invalidFixture()
    property bool isSelected: false
    /* Light sources: the four beams, then the four yellow strobe LEDs. Not
       the heads of the fixture definition, which has one. */
    property int headsNumber: 8
    property vector3d phySize: Qt.vector3d(0.25, 0.35, 0.2)

    /* The model is drawn 2.75 units tall and scaled to the real height */
    readonly property real k: phySize.y / 2.75

    onItemIDChanged:
    {
        isSelected = contextManager.isFixtureSelected(itemID)
        updateHeads()
    }

    /* **************** Channels of the 14 channel mode **************** */
    readonly property int chHeadRotation: 2
    readonly property int chLaser: 10
    readonly property int chStrobe: 5
    readonly property int chGold: 11

    /* **************** Pan/Tilt properties **************** */
    property real panMaxDegrees: 540
    property bool invertedPan: false
    property real panSpeed: 4000 // in milliseconds
    property real panRotation: 0

    property real tiltMaxDegrees: 270
    property bool invertedTilt: false
    property real tiltSpeed: 4000 // in milliseconds
    property real tiltRotation: 0

    /* **************** Head spin, laser and gold light **************** */
    /* Degrees, about the axis the head points along */
    property real spinAngle: 0
    /* Degrees per second. Positive is clockwise */
    property real spinSpeed: 0
    property real laserLevel: 0
    /* The two yellow LEDs: the gold channel is their level, and the strobe
       channel makes them flash on their own, from slow to fast. The strobe
       channel alone lights them too. */
    property real goldLevel: 0
    property real strobeLevel: 0
    property bool flashOn: true
    readonly property real yellowLevel:
    {
        if (goldLevel <= 0 && strobeLevel <= 0)
            return 0
        var level = goldLevel > 0 ? goldLevel : 1
        return (strobeLevel > 0 && flashOn === false) ? 0 : level
    }

    /* **************** Beams **************** */
    property real focusMinDegrees: 6
    property real focusMaxDegrees: 6
    property real distCutoff: 40.0
    property real cutoffAngle: (6 / 2) * (Math.PI / 180)
    /* Half angle of the wash of a yellow LED. Four of them this wide, a
       quarter of a turn apart, reach the whole stage: a strobe lights the room. */
    readonly property real yellowCutoffAngle: 55 * (Math.PI / 180)
    readonly property real lensRadius: 0.15 * k
    /* Distance of a lens, and of a yellow LED, from the head axis */
    readonly property real lensOffset: 0.62 * k
    readonly property real faceOffset: 0.6 * k

    property bool useScattering: View3D.renderQuality === MainView3D.LowQuality ? false : true
    property bool useShadows: View3D.renderQuality === MainView3D.LowQuality ? false : true

    property int raymarchSteps:
    {
        switch(View3D.renderQuality)
        {
            case MainView3D.LowQuality: return 0
            case MainView3D.MediumQuality: return 20
            case MainView3D.HighQuality: return 40
            case MainView3D.UltraQuality: return 80
        }
        return 0
    }

    property real coneTopRadius: lensRadius * 0.7
    property real coneBottomRadius: distCutoff * Math.tan(cutoffAngle) + coneTopRadius
    property real headLength: lensRadius * 2

    /* The beams have no shutter: the strobe channel drives the yellow LEDs */
    readonly property real shutterValue: 1
    property real dimmerValue: 0
    property color lightColor: Qt.rgba(0, 0, 0, 1)

    property var headsList: []

    /* Position of the head centre and orientation of the whole fixture, in
       world space, as MainView3D computes them */
    property var lastLightPos
    property var lastLightMatrix

    function updateHeads()
    {
        var i

        // see MultiBeams3DItem: the cones are re-parented to the scene root, so
        // they have to be destroyed explicitly
        for (i = headsList.length - 1; i >= 0; i--)
        {
            headsList[i].cleanupScattering()
            headsList[i].destroy()
        }
        headsList = []

        if (itemID < 0)
            return

        var component = Qt.createComponent("LightEntity.qml")
        if (component.status !== Component.Ready)
        {
            console.warn("SteelProNova: cannot load LightEntity.qml:", component.errorString())
            return
        }

        for (i = 0; i < headsNumber; i++)
        {
            // 0 to 3 are the beams; 4 to 7 the yellow LEDs, which are a wide
            // wash of a fixed colour that follows the strobe
            var yellow = i >= 4
            var headNode = component.createObject(fixtureEntity,
            {
                "headIndex": i,
                "enabled": Qt.binding(function() { return fixtureEntity.enabled }),
                "shutterValue": Qt.binding(function() { return fixtureEntity.shutterValue }),
                "dimmerValue": yellow ? Qt.binding(function() { return fixtureEntity.yellowLevel * 0.6 })
                                      : Qt.binding(function() { return fixtureEntity.dimmerValue }),
                "lightColor": yellow ? Qt.rgba(1.0, 0.82, 0.1, 1)
                                     : Qt.binding(function() { return fixtureEntity.lightColor }),
                // no beam in the air for the yellow LEDs: so wide a cone turns
                // into grainy haze, and what shows a strobe is the stage it lights
                "raymarchSteps": yellow ? 0
                                        : Qt.binding(function() { return fixtureEntity.raymarchSteps }),
                "cutoffAngle": yellow ? fixtureEntity.yellowCutoffAngle
                                      : Qt.binding(function() { return fixtureEntity.cutoffAngle }),
                "distCutoff": Qt.binding(function() { return fixtureEntity.distCutoff }),
                "headLength": Qt.binding(function() { return fixtureEntity.headLength }),
                "coneTopRadius": Qt.binding(function() { return fixtureEntity.coneTopRadius }),
                "goboTexture": Qt.binding(function() { return fixtureEntity.goboTexture })
            });

            if (headNode === null)
            {
                console.warn("SteelProNova: cannot create beam", i, "of item", itemID)
                break
            }
            headsList.push(headNode)
        }

        View3D.initializeFixture(itemID, fixtureEntity, null)
        updateBeams()
    }

    function setupScattering(sceneEntity)
    {
        if (sceneEntity.coneMesh.length !== distCutoff)
            sceneEntity.coneMesh.length = distCutoff

        for (var i = 0; i < headsList.length; i++)
            headsList[i].setupScattering(sceneEntity)
    }

    function cleanupScattering()
    {
        for (var i = 0; i < headsList.length; i++)
        {
            if (headsList[i] && headsList[i].cleanupScattering)
                headsList[i].cleanupScattering()
        }
    }

    function getHead(headIndex)
    {
        if (headIndex < 0 || headIndex >= headsList.length)
            return null
        return headsList[headIndex]
    }

    /* MainView3D owns this Transform for the tilt of items drawn without a
       mesh. Here the item rotates its own head, so only the range is kept. */
    function bindTiltTransform(t, maxDegrees)
    {
        tiltMaxDegrees = maxDegrees
    }

    function setHeadLightProps(headIndex, pos, matrix)
    {
        lastLightPos = pos
        lastLightMatrix = matrix
        updateBeams()
    }

    /* Aim the two beams. Each one leaves the head sideways, so its frame is
       the frame of the head (fixture > pan > tilt > spin) turned a quarter
       of a turn to one side or the other. LightEntity emits along -Y. */
    function updateBeams()
    {
        if (headsList.length < headsNumber || lastLightMatrix === undefined)
            return

        var head = Qt.matrix4x4()
        head = head.times(lastLightMatrix)
        head.rotate(panRotation, Qt.vector3d(0, 1, 0))
        head.rotate(tiltRotation, Qt.vector3d(1, 0, 0))
        head.rotate(spinAngle, Qt.vector3d(0, 1, 0))

        // Eight sides, 45 degrees apart: beams on the even ones, yellow LEDs
        // on the odd ones. Each light looks out of its side.
        for (var i = 0; i < 8; i++)
        {
            var yellow = (i % 2) === 1
            var m = Qt.matrix4x4()
            m = m.times(head)
            m.rotate(i * 45, Qt.vector3d(0, 1, 0))

            var offset = yellow ? faceOffset : lensOffset
            var pos = lastLightPos.plus(m.times(Qt.vector4d(offset, 0, 0, 0)).toVector3d())
            // A beam leaves square to its side. A yellow LED has no lens: it
            // spills everywhere in front of it, and the renderer lights a
            // surface by the axis of the cone, so that axis is leaned half way
            // towards where the head points. Aimed square to the side, four
            // LEDs light the truss around the fixture and never the floor.
            m.rotate(yellow ? 45 : 90, Qt.vector3d(0, 0, 1))

            var light = headsList[(yellow ? 4 : 0) + Math.floor(i / 2)]
            light.lightMatrix = m
            light.lightDir = m.times(Qt.vector4d(0, -1, 0, 0)).toVector3d().normalized()
            light.lightPos = pos
        }
    }

    onPanRotationChanged: updateBeams()
    onTiltRotationChanged: updateBeams()
    onSpinAngleChanged: updateBeams()

    function setHeadIntensity(headIndex, intensity)
    {
        dimmerValue = intensity
    }

    function setHeadRGBColor(headIndex, color)
    {
        lightColor = color
    }

    /* Raw value of every channel, for what the fixture types of QLC+ have no
       name for */
    function setChannelValues(values)
    {
        if (values.length <= chGold)
            return

        var rotation = values[chHeadRotation]
        if (rotation < 128)
        {
            // an angle: go there and stay
            spinSpeed = 0
            spinAngle = (rotation / 127) * 360
        }
        else
        {
            // 128 - 191 clockwise and 192 - 255 the other way, fast to slow
            var clockwise = rotation < 192
            var slowness = ((rotation - (clockwise ? 128 : 192)) / 63)
            var speed = 540 - (slowness * 510)
            spinSpeed = clockwise ? speed : -speed
        }

        laserLevel = values[chLaser] / 255
        goldLevel = values[chGold] / 255
        strobeLevel = values[chStrobe] / 255
    }

    /* Flashes of the yellow LEDs: 1 to 18 per second */
    Timer
    {
        interval: 500 / (1 + (fixtureEntity.strobeLevel * 17))
        repeat: true
        running: fixtureEntity.strobeLevel > 0 && fixtureEntity.enabled
        onTriggered: fixtureEntity.flashOn = !fixtureEntity.flashOn
        onRunningChanged: if (running === false) fixtureEntity.flashOn = true
    }

    FrameAnimation
    {
        running: fixtureEntity.spinSpeed !== 0 && fixtureEntity.enabled
        onTriggered: fixtureEntity.spinAngle = (fixtureEntity.spinAngle + (fixtureEntity.spinSpeed * frameTime)) % 360
    }

    // See Fixture3DItem: paces the animation to the rate the positions arrive at
    property real lastPositionTime: 0
    readonly property real continuousUpdateGap: 1000

    function setPosition(pan, tilt)
    {
        var now = Date.now()
        var elapsed = (lastPositionTime > 0) ? (now - lastPositionTime) : continuousUpdateGap + 1
        var oneShot = (elapsed <= 0 || elapsed >= continuousUpdateGap)
        lastPositionTime = now

        if (panMaxDegrees)
        {
            var panDeg = (panMaxDegrees / 0xFFFF) * pan
            panAnim.stop()
            panAnim.from = panRotation
            panAnim.to = invertedPan ? panMaxDegrees - panDeg : panDeg
            var panPhysical = (panSpeed / panMaxDegrees) * Math.abs(panAnim.to - panAnim.from)
            panAnim.duration = animationDuration(elapsed, panPhysical, oneShot)
            panAnim.start()
        }

        if (tiltMaxDegrees)
        {
            var baseTiltPos = tiltMaxDegrees / 2
            var tiltDeg = (tiltMaxDegrees / 0xFFFF) * tilt
            tiltAnim.stop()
            tiltAnim.from = tiltRotation
            tiltAnim.to = invertedTilt ? -baseTiltPos + tiltDeg : baseTiltPos - tiltDeg
            var tiltPhysical = (tiltSpeed / tiltMaxDegrees) * Math.abs(tiltAnim.to - tiltAnim.from)
            tiltAnim.duration = animationDuration(elapsed, tiltPhysical, oneShot)
            tiltAnim.start()
        }
    }

    function animationDuration(elapsed, physical, oneShot)
    {
        if (oneShot)
            return Math.max(physical, 300)
        return Math.max(elapsed, physical, 1)
    }

    function setPositionSpeed(panDuration, tiltDuration)
    {
        if (panDuration !== -1)
            panSpeed = panDuration
        if (tiltDuration !== -1)
            tiltSpeed = tiltDuration
    }

    function setShutter(type, low, high)
    {
    }

    /* The beams of a Nova have a fixed lens */
    function setZoom(value, degrees)
    {
    }

    NumberAnimation on panRotation
    {
        id: panAnim
        running: false
        easing.type: Easing.Linear
    }

    NumberAnimation on tiltRotation
    {
        id: tiltAnim
        running: false
        easing.type: Easing.Linear
    }

    /* Main transform of the whole fixture item, moved by MainView3D */
    property Transform transform: Transform { }

    property Layer sceneLayer
    property Effect sceneEffect

    property Texture2D goboTexture: Texture2D { }

    /* ********************* Materials ********************* */

    property Material material:
        Material
        {
            effect: sceneEffect
            parameters: [
                Parameter { name: "diffuse"; value: Qt.rgba(0.09, 0.095, 0.115, 1) },
                Parameter { name: "specular"; value: Qt.color("black") },
                Parameter { name: "shininess"; value: 1.0 },
                Parameter { name: "bloom"; value: 0 }
            ]
        }

    property Material darkMaterial:
        Material
        {
            effect: sceneEffect
            parameters: [
                Parameter { name: "diffuse"; value: Qt.rgba(0.05, 0.06, 0.07, 1) },
                Parameter { name: "specular"; value: Qt.color("black") },
                Parameter { name: "shininess"; value: 1.0 },
                Parameter { name: "bloom"; value: 0 }
            ]
        }

    /* The lenses glow in the colour of the beams */
    readonly property real beamLevel: dimmerValue * shutterValue
    property Material lensMaterial:
        Material
        {
            effect: sceneEffect
            parameters: [
                Parameter {
                    name: "diffuse"
                    value: Qt.rgba(0.08 + (fixtureEntity.lightColor.r * fixtureEntity.beamLevel),
                                   0.10 + (fixtureEntity.lightColor.g * fixtureEntity.beamLevel),
                                   0.12 + (fixtureEntity.lightColor.b * fixtureEntity.beamLevel), 1)
                },
                Parameter { name: "specular"; value: Qt.color("black") },
                Parameter { name: "shininess"; value: 1.0 },
                Parameter { name: "bloom"; value: fixtureEntity.beamLevel > 0 ? 1 : 0 }
            ]
        }

    property Material goldMaterial:
        Material
        {
            effect: sceneEffect
            parameters: [
                Parameter {
                    name: "diffuse"
                    value: Qt.rgba(0.10 + (0.90 * fixtureEntity.yellowLevel),
                                   0.08 + (0.77 * fixtureEntity.yellowLevel),
                                   0.02 + (0.08 * fixtureEntity.yellowLevel), 1)
                },
                Parameter { name: "specular"; value: Qt.color("black") },
                Parameter { name: "shininess"; value: 1.0 },
                Parameter { name: "bloom"; value: fixtureEntity.yellowLevel > 0.2 ? 1 : 0 }
            ]
        }

    /* The scene effect without its shadow pass. The laser rays are light,
       not objects: drawn with the scene effect they cast thin shadows across
       every beam they cross. */
    property Effect lightOnlyEffect:
        Effect
        {
            techniques: [
                Technique
                {
                    graphicsApiFilter { api: GraphicsApiFilter.OpenGL; profile: GraphicsApiFilter.CoreProfile; majorVersion: 3; minorVersion: 1 }
                    renderPasses: [
                        RenderPass
                        {
                            filterKeys: FilterKey { name: "pass"; value: "geometry" }
                            shaderProgram:
                                ShaderProgram
                                {
                                    vertexShaderCode: View3D.makeShader(loadSource("qrc:/geo.vert"))
                                    fragmentShaderCode: View3D.makeShader(loadSource("qrc:/geo.frag"))
                                }
                        }
                    ]
                }
            ]
        }

    /* Green of the laser at a given brightness */
    component LaserMaterial: Material
    {
        property real level: 1
        effect: fixtureEntity.lightOnlyEffect
        parameters: [
            Parameter { name: "diffuse"; value: Qt.rgba(0.13 * level, 0.85 * level, 0.37 * level, 1) },
            Parameter { name: "specular"; value: Qt.color("black") },
            Parameter { name: "shininess"; value: 1.0 },
            Parameter { name: "bloom"; value: level > 0.25 ? 1 : 0 }
        ]
    }

    property Material laserMaterial: LaserMaterial { }

    /* A ray is drawn in stretches that get thinner, and a little dimmer,
       towards its end, so that it dies away instead of stopping dead. The
       last half carries the fade.

       It is the width that does most of it. The renderer has no transparency:
       a stretch drawn dark is not a faint light but a dark rod, and shows as
       a black line across a lit floor. A stretch that stays bright and gets
       thinner than a pixel breaks up and vanishes, which is how a beam fades. */
    readonly property real rayLength: 7.0
    readonly property var rayStretch: [ 0.40, 0.14, 0.13, 0.12, 0.11, 0.10 ]
    readonly property var rayLevel:   [ 1.00, 0.90, 0.80, 0.70, 0.62, 0.55 ]
    readonly property var rayWidth:   [ 1.00, 0.72, 0.50, 0.32, 0.18, 0.08 ]
    readonly property var rayMaterials: [
        rayMaterial0, rayMaterial1, rayMaterial2, rayMaterial3, rayMaterial4, rayMaterial5
    ]
    LaserMaterial { id: rayMaterial0; level: fixtureEntity.rayLevel[0] }
    LaserMaterial { id: rayMaterial1; level: fixtureEntity.rayLevel[1] }
    LaserMaterial { id: rayMaterial2; level: fixtureEntity.rayLevel[2] }
    LaserMaterial { id: rayMaterial3; level: fixtureEntity.rayLevel[3] }
    LaserMaterial { id: rayMaterial4; level: fixtureEntity.rayLevel[4] }
    LaserMaterial { id: rayMaterial5; level: fixtureEntity.rayLevel[5] }

    /* ********************* Geometry ********************* */

    CuboidMesh
    {
        id: baseMesh
        xExtent: 2.1 * k
        yExtent: 0.66 * k
        zExtent: 1.45 * k
    }

    /* Base: hangs from the origin */
    Entity
    {
        property Transform t: Transform { translation: Qt.vector3d(0, -0.33 * k, 0) }
        components: [ baseMesh, t, fixtureEntity.material, fixtureEntity.sceneLayer ]
    }

    /* Yoke: turns with the pan */
    Entity
    {
        id: armEntity

        property Transform panTransform: Transform { rotationY: fixtureEntity.panRotation }
        components: [ panTransform ]

        Entity
        {
            CylinderMesh { id: collarMesh; radius: 0.5 * k; length: 0.16 * k; slices: 24 }
            property Transform t: Transform { translation: Qt.vector3d(0, -0.74 * k, 0) }
            components: [ collarMesh, t, fixtureEntity.darkMaterial, fixtureEntity.sceneLayer ]
        }

        Entity
        {
            CuboidMesh { id: yokeHorizMesh; xExtent: 1.45 * k; yExtent: 0.26 * k; zExtent: 0.62 * k }
            property Transform t: Transform { translation: Qt.vector3d(-0.15 * k, -0.95 * k, 0) }
            components: [ yokeHorizMesh, t, fixtureEntity.material, fixtureEntity.sceneLayer ]
        }

        Entity
        {
            CuboidMesh { id: yokeArmMesh; xExtent: 0.36 * k; yExtent: 1.38 * k; zExtent: 0.64 * k }
            property Transform t: Transform { translation: Qt.vector3d(-0.92 * k, -1.6 * k, 0) }
            components: [ yokeArmMesh, t, fixtureEntity.material, fixtureEntity.sceneLayer ]
        }

        /* Axle between the arm and the head */
        Entity
        {
            CylinderMesh { id: axleMesh; radius: 0.18 * k; length: 0.3 * k; slices: 16 }
            property Transform t:
                Transform
                {
                    translation: Qt.vector3d(-0.7 * k, -2.0 * k, 0)
                    rotationZ: 90
                }
            components: [ axleMesh, t, fixtureEntity.darkMaterial, fixtureEntity.sceneLayer ]
        }

        /* Head: tilts about the axle. MainView3D looks this entity up by name
           to know where the light comes from. */
        Entity
        {
            id: headEntity
            objectName: "headEntity"

            property Transform tiltTransform:
                Transform
                {
                    translation: Qt.vector3d(0, -2.0 * k, 0)
                    rotationX: fixtureEntity.tiltRotation
                }
            components: [ tiltTransform ]

            /* Everything below spins about the axis the head points along */
            Entity
            {
                id: spinEntity

                property Transform spinTransform: Transform { rotationY: fixtureEntity.spinAngle }
                components: [ spinTransform ]

                /* Octagonal shell */
                Entity
                {
                    CylinderMesh { id: shellMesh; radius: 0.64 * k; length: 0.72 * k; slices: 8; rings: 2 }
                    property Transform t: Transform { rotationY: 22.5 }
                    components: [ shellMesh, t, fixtureEntity.material, fixtureEntity.sceneLayer ]
                }

                /* Perforated front, where the laser comes out */
                Entity
                {
                    CylinderMesh { id: frontMesh; radius: 0.42 * k; length: 0.06 * k; slices: 8; rings: 2 }
                    property Transform t:
                        Transform
                        {
                            translation: Qt.vector3d(0, -0.39 * k, 0)
                            rotationY: 22.5
                        }
                    components: [ frontMesh, t, fixtureEntity.darkMaterial, fixtureEntity.sceneLayer ]
                }

                Entity
                {
                    enabled: fixtureEntity.laserLevel > 0
                    SphereMesh { id: laserCoreMesh; radius: 0.07 * k; rings: 8; slices: 12 }
                    property Transform t: Transform { translation: Qt.vector3d(0, -0.43 * k, 0) }
                    components: [ laserCoreMesh, t, fixtureEntity.laserMaterial, fixtureEntity.sceneLayer ]
                }

                /* Laser: 18 rays in two rings around the axis, each one in
                   six stretches (see rayStretch). The mesh is one metre long
                   and every stretch scales it to its own length and width. */
                CylinderMesh
                {
                    id: rayMesh
                    radius: 0.004
                    length: 1.0
                    slices: 5
                    rings: 2
                }

                NodeInstantiator
                {
                    model: 18 * 6

                    delegate:
                        Entity
                        {
                            enabled: fixtureEntity.laserLevel > 0

                            readonly property int ray: Math.floor(index / 6)
                            readonly property int stretch: index % 6
                            readonly property real angle: (ray / 18) * Math.PI * 2
                            readonly property real spread: (ray % 2 === 0) ? 22 : 13
                            // where this stretch starts and how long it is, in metres
                            readonly property real from:
                            {
                                var sum = 0
                                for (var s = 0; s < stretch; s++)
                                    sum += fixtureEntity.rayStretch[s]
                                return sum * fixtureEntity.rayLength
                            }
                            readonly property real len: fixtureEntity.rayStretch[stretch] * fixtureEntity.rayLength

                            property Transform t:
                                Transform
                                {
                                    matrix:
                                    {
                                        var w = fixtureEntity.rayWidth[stretch]
                                        var m = Qt.matrix4x4()
                                        m.translate(Qt.vector3d(0, -0.43 * fixtureEntity.k, 0))
                                        m.rotate(Math.sin(angle) * spread, Qt.vector3d(1, 0, 0))
                                        m.rotate(Math.cos(angle) * spread, Qt.vector3d(0, 0, 1))
                                        m.translate(Qt.vector3d(0, -(from + (len / 2)), 0))
                                        m.scale(w, len, w)
                                        return m
                                    }
                                }
                            components: [ rayMesh, t, fixtureEntity.rayMaterials[stretch], fixtureEntity.sceneLayer ]
                        }
                }

                /* The four beam lenses, on every other side */
                CylinderMesh { id: lensMesh; radius: fixtureEntity.lensRadius; length: 0.06 * k; slices: 20 }

                NodeInstantiator
                {
                    model: 4

                    delegate:
                        Entity
                        {
                            property Transform t:
                                Transform
                                {
                                    matrix:
                                    {
                                        var m = Qt.matrix4x4()
                                        m.rotate(index * 90, Qt.vector3d(0, 1, 0))
                                        m.translate(Qt.vector3d(fixtureEntity.lensOffset, 0, 0))
                                        m.rotate(90, Qt.vector3d(0, 0, 1))
                                        return m
                                    }
                                }
                            components: [ lensMesh, t, fixtureEntity.lensMaterial, fixtureEntity.sceneLayer ]
                        }
                }

                /* The four yellow strobe LEDs, on the sides in between */
                CuboidMesh { id: panelMesh; xExtent: 0.04 * k; yExtent: 0.26 * k; zExtent: 0.36 * k }

                NodeInstantiator
                {
                    model: 4

                    delegate:
                        Entity
                        {
                            property Transform t:
                                Transform
                                {
                                    matrix:
                                    {
                                        var m = Qt.matrix4x4()
                                        m.rotate(45 + (index * 90), Qt.vector3d(0, 1, 0))
                                        m.translate(Qt.vector3d(fixtureEntity.faceOffset, 0, 0))
                                        return m
                                    }
                                }
                            components: [ panelMesh, t, fixtureEntity.goldMaterial, fixtureEntity.sceneLayer ]
                        }
                }
            }
        }
    }

    components: [ transform ]
}
