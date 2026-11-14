/*
  VertexAV Director
  graphicscard.h

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

#ifndef GRAPHICSCARD_H
#define GRAPHICSCARD_H

#include <QStringList>
#include <QString>

/**
 * Which graphics card draws the application, on computers that have two.
 *
 * A laptop with an integrated and a dedicated card gives a program the
 * integrated one unless it asks otherwise, and the 3D view is far too heavy
 * for it. The application asks for the dedicated card by default; the user
 * can choose the other one, or leave the choice to Windows.
 *
 * The choice is the per application graphics preference of Windows (the one
 * of Settings, System, Display, Graphics), which is read when a program
 * starts: a change takes effect the next time the application is opened.
 *
 * It does nothing on other systems and on computers with a single card.
 */
class GraphicsCard
{
public:
    enum Preference
    {
        HighPerformance = 0,    ///< the dedicated card (default)
        PowerSaving,            ///< the integrated card
        SystemDefault           ///< whatever Windows decides
    };

    /** The preference of the user, HighPerformance when never set */
    static int preference();
    static void setPreference(int preference);

    /** Hand the preference to Windows. To be called before anything is drawn */
    static void apply();

    /** Name of the card each Preference stands for on this computer, in the
     *  order of the enum. Empty names when it cannot be told. */
    static QStringList cardNames();
};

#endif
