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
import Qt3D.Core
import Qt3D.Render

/* Holds the render passes of one light, and takes them out of the frame
 * while that light is off: each of them is a render view of its own, and an
 * empty one still costs its share of every frame. */
SubtreeEnabler
{
    property Entity fixtureItem
    enabled: fixtureItem && fixtureItem.lightIntensity ? true : false
}
