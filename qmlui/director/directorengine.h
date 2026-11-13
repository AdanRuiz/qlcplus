/*
  Q Light Controller Plus
  directorengine.h

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

#ifndef DIRECTORENGINE_H
#define DIRECTORENGINE_H

#include <QStringList>
#include <QString>
#include <QList>

/**
 * DirectorEngine is the decision core of the Director: given the musical
 * state (section, intensity, strobe permission) and a set of rules, it
 * decides which Functions should be running on each layer.
 *
 * It knows nothing about Doc, the UI or the input plugins. It only deals
 * with Function IDs and returns the list of start/stop actions the caller
 * has to perform. This keeps it testable and movable to another process.
 */
class DirectorEngine final
{
public:
    enum Layer
    {
        Base = 0,   ///< Colour wash, always on
        Movement,   ///< Positions and movement effects
        Rhythm,     ///< Matrices and chasers
        Accent,     ///< Strobes and blinders, one bar at a time
        LayerCount
    };

    /** A set of Functions started and stopped together */
    typedef QList<quint32> Bundle;

    struct Rule
    {
        QString name;
        QStringList sections;       ///< Empty = any section
        QStringList intensities;    ///< Empty = any intensity
        int bars = 8;               ///< Rotate candidates every N bars (0 = never)
        QList<Bundle> layers[LayerCount];

        bool matches(const QString &section, const QString &intensity) const;
        int specificity() const;
        bool isEmpty() const;
    };

    struct Action
    {
        enum Type { Stop, Start };
        Type type;
        int layer;
        quint32 function;
    };

    DirectorEngine();

    /** Stable, non translated layer names used in the project file */
    static QString layerName(int layer);
    static int layerFromName(const QString &name);

    /*********************************************************************
     * Rules
     *********************************************************************/
public:
    void setRules(const QList<Rule> &rules);
    const QList<Rule> &rules() const;
    void clearRules();

    /** Drop every reference to $function (it has been deleted) */
    void removeFunction(quint32 function);

    /*********************************************************************
     * Musical state
     *********************************************************************/
public:
    void setSection(const QString &section);
    void setIntensity(const QString &intensity);
    void setStrobeOk(bool ok);

    QString detectedSection() const;
    QString detectedIntensity() const;

    /** What the engine actually uses: the forced value when there is one */
    QString section() const;
    QString intensity() const;
    bool strobeOk() const;

    /*********************************************************************
     * Overrides
     *********************************************************************/
public:
    /** Force a section/intensity. An empty string gives control back */
    void forceSection(const QString &section);
    void forceIntensity(const QString &intensity);
    QString forcedSection() const;
    QString forcedIntensity() const;

    /** Keep whatever is running on $layer until unpinned */
    void pinLayer(int layer, bool pin);
    bool isLayerPinned(int layer) const;

    void setLayerOff(int layer, bool off);
    bool isLayerOff(int layer) const;

    /** Freeze the whole look */
    void setHold(bool hold);
    bool hold() const;

    /** Operator permission for the Accent layer */
    void setStrobeAllowed(bool allowed);
    bool strobeAllowed() const;

    /** Force the Accent layer on, regardless of what the music says */
    void setStrobeForced(bool forced);
    bool strobeForced() const;

    void setBlackout(bool blackout);
    bool blackout() const;

    void releaseAll();
    int activeOverrides() const;

    /** Move to the next candidate on every layer at the next evaluation */
    void requestNext();

    /** Restart what is running on $layer at the next evaluation */
    void requestRefresh(int layer);

    /*********************************************************************
     * Evaluation
     *********************************************************************/
public:
    /** To be called on the first beat of every bar */
    QList<Action> bar();

    /** Evaluate right now, without counting a bar */
    QList<Action> evaluate();

    /** Stop everything the engine started and forget the current look */
    QList<Action> stopAll();

    /** True when bar()/evaluate() would change something */
    bool isDirty() const;

    int activeRule() const;
    QString activeRuleName() const;
    Bundle layerBundle(int layer) const;
    int barsInRule() const;

private:
    int findRule() const;
    QList<Action> apply(bool countBar);

private:
    QList<Rule> m_rules;

    QString m_section;
    QString m_intensity;
    bool m_strobeOk;

    QString m_forcedSection;
    QString m_forcedIntensity;
    bool m_pinned[LayerCount];
    bool m_off[LayerCount];
    bool m_refresh[LayerCount];
    bool m_hold;
    bool m_strobeAllowed;
    bool m_strobeForced;
    bool m_blackout;

    int m_activeRule;
    int m_rotation;
    int m_barsInRule;
    bool m_dirty;
    Bundle m_current[LayerCount];
};

#endif
