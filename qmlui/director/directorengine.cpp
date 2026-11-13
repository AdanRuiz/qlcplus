/*
  Q Light Controller Plus
  directorengine.cpp

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

#include "directorengine.h"

/*****************************************************************************
 * Rule
 *****************************************************************************/

bool DirectorEngine::Rule::matches(const QString &section, const QString &intensity) const
{
    if (sections.isEmpty() == false && sections.contains(section) == false)
        return false;
    if (intensities.isEmpty() == false && intensities.contains(intensity) == false)
        return false;
    return true;
}

int DirectorEngine::Rule::specificity() const
{
    return (sections.isEmpty() ? 0 : 2) + (intensities.isEmpty() ? 0 : 1);
}

bool DirectorEngine::Rule::isEmpty() const
{
    for (int i = 0; i < LayerCount; i++)
    {
        if (layers[i].isEmpty() == false)
            return false;
    }
    return true;
}

/*****************************************************************************
 * Initialization
 *****************************************************************************/

DirectorEngine::DirectorEngine()
    : m_strobeOk(false)
    , m_hold(false)
    , m_strobeAllowed(true)
    , m_strobeForced(false)
    , m_blackout(false)
    , m_activeRule(-1)
    , m_rotation(0)
    , m_barsInRule(0)
    , m_dirty(true)
{
    for (int i = 0; i < LayerCount; i++)
    {
        m_pinned[i] = false;
        m_off[i] = false;
        m_refresh[i] = false;
    }
}

QString DirectorEngine::layerName(int layer)
{
    switch (layer)
    {
        case Base: return QStringLiteral("Base");
        case Movement: return QStringLiteral("Movement");
        case Rhythm: return QStringLiteral("Rhythm");
        case Accent: return QStringLiteral("Accent");
        default: return QString();
    }
}

int DirectorEngine::layerFromName(const QString &name)
{
    for (int i = 0; i < LayerCount; i++)
    {
        if (layerName(i) == name)
            return i;
    }
    return -1;
}

/*****************************************************************************
 * Rules
 *****************************************************************************/

void DirectorEngine::setRules(const QList<Rule> &rules)
{
    m_rules = rules;
    m_activeRule = -1;
    m_rotation = 0;
    m_barsInRule = 0;
    m_dirty = true;
}

const QList<DirectorEngine::Rule> &DirectorEngine::rules() const
{
    return m_rules;
}

void DirectorEngine::clearRules()
{
    setRules(QList<Rule>());
}

void DirectorEngine::removeFunction(quint32 function)
{
    for (int r = 0; r < m_rules.count(); r++)
    {
        for (int l = 0; l < LayerCount; l++)
        {
            QList<Bundle> &bundles = m_rules[r].layers[l];
            for (int b = bundles.count() - 1; b >= 0; b--)
            {
                bundles[b].removeAll(function);
                if (bundles[b].isEmpty())
                    bundles.removeAt(b);
            }
        }
    }

    for (int l = 0; l < LayerCount; l++)
        m_current[l].removeAll(function);

    m_dirty = true;
}

/*****************************************************************************
 * Musical state
 *****************************************************************************/

void DirectorEngine::setSection(const QString &section)
{
    if (m_section == section)
        return;
    m_section = section;
    m_dirty = true;
}

void DirectorEngine::setIntensity(const QString &intensity)
{
    if (m_intensity == intensity)
        return;
    m_intensity = intensity;
    m_dirty = true;
}

void DirectorEngine::setStrobeOk(bool ok)
{
    if (m_strobeOk == ok)
        return;
    m_strobeOk = ok;
    m_dirty = true;
}

QString DirectorEngine::detectedSection() const
{
    return m_section;
}

QString DirectorEngine::detectedIntensity() const
{
    return m_intensity;
}

QString DirectorEngine::section() const
{
    return m_forcedSection.isEmpty() ? m_section : m_forcedSection;
}

QString DirectorEngine::intensity() const
{
    return m_forcedIntensity.isEmpty() ? m_intensity : m_forcedIntensity;
}

bool DirectorEngine::strobeOk() const
{
    return m_strobeOk;
}

/*****************************************************************************
 * Overrides
 *****************************************************************************/

void DirectorEngine::forceSection(const QString &section)
{
    if (m_forcedSection == section)
        return;
    m_forcedSection = section;
    m_dirty = true;
}

void DirectorEngine::forceIntensity(const QString &intensity)
{
    if (m_forcedIntensity == intensity)
        return;
    m_forcedIntensity = intensity;
    m_dirty = true;
}

QString DirectorEngine::forcedSection() const
{
    return m_forcedSection;
}

QString DirectorEngine::forcedIntensity() const
{
    return m_forcedIntensity;
}

void DirectorEngine::pinLayer(int layer, bool pin)
{
    if (layer < 0 || layer >= LayerCount || m_pinned[layer] == pin)
        return;
    m_pinned[layer] = pin;
    m_dirty = true;
}

bool DirectorEngine::isLayerPinned(int layer) const
{
    return (layer >= 0 && layer < LayerCount) ? m_pinned[layer] : false;
}

void DirectorEngine::setLayerOff(int layer, bool off)
{
    if (layer < 0 || layer >= LayerCount || m_off[layer] == off)
        return;
    m_off[layer] = off;
    m_dirty = true;
}

bool DirectorEngine::isLayerOff(int layer) const
{
    return (layer >= 0 && layer < LayerCount) ? m_off[layer] : false;
}

void DirectorEngine::setHold(bool hold)
{
    if (m_hold == hold)
        return;
    m_hold = hold;
    m_dirty = true;
}

bool DirectorEngine::hold() const
{
    return m_hold;
}

void DirectorEngine::setStrobeAllowed(bool allowed)
{
    if (m_strobeAllowed == allowed)
        return;
    m_strobeAllowed = allowed;
    m_dirty = true;
}

bool DirectorEngine::strobeAllowed() const
{
    return m_strobeAllowed;
}

void DirectorEngine::setStrobeForced(bool forced)
{
    if (m_strobeForced == forced)
        return;
    m_strobeForced = forced;
    m_dirty = true;
}

bool DirectorEngine::strobeForced() const
{
    return m_strobeForced;
}

void DirectorEngine::setBlackout(bool blackout)
{
    if (m_blackout == blackout)
        return;
    m_blackout = blackout;
    m_dirty = true;
}

bool DirectorEngine::blackout() const
{
    return m_blackout;
}

void DirectorEngine::releaseAll()
{
    m_forcedSection.clear();
    m_forcedIntensity.clear();
    for (int i = 0; i < LayerCount; i++)
    {
        m_pinned[i] = false;
        m_off[i] = false;
    }
    m_hold = false;
    m_strobeForced = false;
    m_blackout = false;
    m_dirty = true;
}

int DirectorEngine::activeOverrides() const
{
    int count = 0;
    if (m_forcedSection.isEmpty() == false) count++;
    if (m_forcedIntensity.isEmpty() == false) count++;
    for (int i = 0; i < LayerCount; i++)
    {
        if (m_pinned[i]) count++;
        if (m_off[i]) count++;
    }
    if (m_hold) count++;
    if (m_strobeForced) count++;
    if (m_blackout) count++;
    if (m_strobeAllowed == false) count++;
    return count;
}

void DirectorEngine::requestNext()
{
    m_rotation++;
    m_barsInRule = 0;
    m_dirty = true;
}

void DirectorEngine::requestRefresh(int layer)
{
    if (layer < 0 || layer >= LayerCount)
        return;
    m_refresh[layer] = true;
    m_dirty = true;
}

/*****************************************************************************
 * Evaluation
 *****************************************************************************/

int DirectorEngine::findRule() const
{
    const QString sec = section();
    const QString inten = intensity();
    int best = -1;
    int bestScore = -1;

    for (int i = 0; i < m_rules.count(); i++)
    {
        const Rule &rule = m_rules.at(i);
        if (rule.matches(sec, inten) == false)
            continue;
        int score = rule.specificity();
        if (score > bestScore)
        {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

QList<DirectorEngine::Action> DirectorEngine::bar()
{
    return apply(true);
}

QList<DirectorEngine::Action> DirectorEngine::evaluate()
{
    return apply(false);
}

QList<DirectorEngine::Action> DirectorEngine::apply(bool countBar)
{
    QList<Action> actions;

    // A blackout wins over everything, including a hold
    if (m_hold && m_blackout == false)
    {
        m_dirty = false;
        return actions;
    }

    int ruleIdx = m_blackout ? -1 : findRule();

    if (ruleIdx != m_activeRule)
    {
        m_activeRule = ruleIdx;
        m_barsInRule = 0;
    }
    else if (countBar && ruleIdx >= 0)
    {
        m_barsInRule++;
        int bars = m_rules.at(ruleIdx).bars;
        if (bars > 0 && m_barsInRule >= bars)
        {
            m_rotation++;
            m_barsInRule = 0;
        }
    }

    for (int l = 0; l < LayerCount; l++)
    {
        Bundle wanted;

        if (m_blackout || m_off[l])
        {
            // nothing on this layer
        }
        else if (m_pinned[l])
        {
            wanted = m_current[l];
        }
        else if (ruleIdx >= 0)
        {
            const QList<Bundle> &candidates = m_rules.at(ruleIdx).layers[l];
            bool allowed = true;

            // An accent is a hit, not a state: when the music allows it, it
            // fires on the first bar of the rule and of every rotation. The
            // operator's strobe button holds it for as long as it is pressed.
            if (l == Accent)
                allowed = m_strobeAllowed && ((m_strobeOk && m_barsInRule == 0) || m_strobeForced);

            if (allowed && candidates.isEmpty() == false)
                wanted = candidates.at(m_rotation % candidates.count());
        }

        // The operator can fire the accents even when the active rule has none
        if (l == Accent && wanted.isEmpty() && m_strobeForced && m_strobeAllowed &&
            m_blackout == false && m_off[l] == false && m_pinned[l] == false)
        {
            for (int r = 0; r < m_rules.count() && wanted.isEmpty(); r++)
            {
                if (m_rules.at(r).layers[Accent].isEmpty() == false)
                    wanted = m_rules.at(r).layers[Accent].first();
            }
        }

        const bool restart = m_refresh[l];
        m_refresh[l] = false;

        if (restart == false && wanted == m_current[l])
            continue;

        foreach (quint32 fid, m_current[l])
        {
            if (restart || wanted.contains(fid) == false)
                actions.append(Action{Action::Stop, l, fid});
        }
        foreach (quint32 fid, wanted)
        {
            if (restart || m_current[l].contains(fid) == false)
                actions.append(Action{Action::Start, l, fid});
        }

        m_current[l] = wanted;
    }

    m_dirty = false;
    return actions;
}

QList<DirectorEngine::Action> DirectorEngine::stopAll()
{
    QList<Action> actions;

    for (int l = 0; l < LayerCount; l++)
    {
        foreach (quint32 fid, m_current[l])
            actions.append(Action{Action::Stop, l, fid});
        m_current[l].clear();
        m_refresh[l] = false;
    }

    m_activeRule = -1;
    m_barsInRule = 0;
    m_dirty = true;
    return actions;
}

bool DirectorEngine::isDirty() const
{
    return m_dirty;
}

int DirectorEngine::activeRule() const
{
    return m_activeRule;
}

QString DirectorEngine::activeRuleName() const
{
    if (m_activeRule < 0 || m_activeRule >= m_rules.count())
        return QString();
    return m_rules.at(m_activeRule).name;
}

DirectorEngine::Bundle DirectorEngine::layerBundle(int layer) const
{
    if (layer < 0 || layer >= LayerCount)
        return Bundle();
    return m_current[layer];
}

int DirectorEngine::barsInRule() const
{
    return m_barsInRule;
}
