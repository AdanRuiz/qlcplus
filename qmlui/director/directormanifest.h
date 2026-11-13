/*
  Q Light Controller Plus
  directormanifest.h

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

#ifndef DIRECTORMANIFEST_H
#define DIRECTORMANIFEST_H

#include <QString>
#include <QList>

/**
 * What the Stage Wizard generated, described in a way that does not depend
 * on the (translated) names and folders of the Functions. The Director
 * builds its default rules from this.
 */
struct DirectorManifest
{
    struct Group
    {
        quint32 id = 0;
        QString name;
        int role = 0;           ///< StageWizard::FixtureRole
        bool hasMovement = false;
        bool hasColor = false;  ///< RGB or CMY mixing
        bool hasShutter = false;
        bool hasDimmer = false;
    };

    struct Item
    {
        quint32 function = 0;   ///< Top level Function of the effect
        quint32 group = 0;
        int effect = 0;         ///< StageWizard::EffectFlag
    };

    QList<Group> groups;
    QList<Item> items;

    void clear()
    {
        groups.clear();
        items.clear();
    }
};

#endif
