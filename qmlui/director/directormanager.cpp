/*
  Q Light Controller Plus
  directormanager.cpp

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

#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <QFile>

#include "directormanager.h"
#include "vertexavchannels.h"
#include "inputoutputmap.h"
#include "functionparent.h"
#include "qlccapability.h"
#include "fixturegroup.h"
#include "chaserstep.h"
#include "qlcchannel.h"
#include "fixture.h"
#include "chaser.h"
#include "stagewizard.h"
#include "mastertimer.h"
#include "inputpatch.h"
#include "qlcpalette.h"
#include "function.h"
#include "scene.h"
#include "doc.h"

#define KXMLQLCDirectorEnabled      QStringLiteral("Enabled")
#define KXMLQLCDirectorStrobe       QStringLiteral("StrobeAllowed")
#define KXMLQLCDirectorPaletteSlot  QStringLiteral("PaletteSlot")
#define KXMLQLCDirectorIndex        QStringLiteral("Index")
#define KXMLQLCDirectorPalette      QStringLiteral("Palette")
#define KXMLQLCDirectorOwn          QStringLiteral("Own")
#define KXMLQLCDirectorRule         QStringLiteral("Rule")
#define KXMLQLCDirectorName         QStringLiteral("Name")
#define KXMLQLCDirectorSections     QStringLiteral("Sections")
#define KXMLQLCDirectorIntensities  QStringLiteral("Intensities")
#define KXMLQLCDirectorBars         QStringLiteral("Bars")
#define KXMLQLCDirectorLayer        QStringLiteral("Layer")
#define KXMLQLCDirectorBundle       QStringLiteral("Bundle")

#define DIRECTOR_PLUGIN_NAME        QStringLiteral("VertexAV")
#define DIRECTOR_FOLDER             QStringLiteral("Director")
#define DIRECTOR_SIGNAL_TIMEOUT_MS  5000
#define DIRECTOR_BEAT_TIMEOUT_MS    1500

namespace
{
    struct DemoStep
    {
        const char *section;
        int bars;
        const char *intensity;
        bool strobe;
        const char *colors[3];
    };

    /* A short synthetic song, looped. Colours change with the sections so
     * that the palette path gets exercised too. */
    const DemoStep demoSongSteps[] =
    {
        { "intro",     4, "low",    false, { "#2040FF", "#00C0FF", "#FFFFFF" } },
        { "verse",     8, "medium", false, { "#FF2060", "#8020FF", "#FFFFFF" } },
        { "build",     4, "high",   false, { "#FF8000", "#FF2060", "#FFFFFF" } },
        { "drop",      8, "peak",   true,  { "#FF0020", "#FFB000", "#FFFFFF" } },
        { "breakdown", 4, "low",    false, { "#00B0A0", "#2040FF", "#FFFFFF" } },
        { "build",     4, "high",   false, { "#A020FF", "#FF2060", "#FFFFFF" } },
        { "drop",      8, "peak",   true,  { "#20FF60", "#00C0FF", "#FFFFFF" } },
        { "outro",     4, "low",    false, { "#2040FF", "#8020FF", "#FFFFFF" } }
    };
    const int demoSongCount = int(sizeof(demoSongSteps) / sizeof(demoSongSteps[0]));

    QString bundleToString(const DirectorEngine::Bundle &bundle)
    {
        QStringList list;
        foreach (quint32 id, bundle)
            list << QString::number(id);
        return list.join(",");
    }

    DirectorEngine::Bundle bundleFromString(const QString &str)
    {
        DirectorEngine::Bundle bundle;
        foreach (const QString &s, str.split(",", Qt::SkipEmptyParts))
        {
            bool ok = false;
            quint32 id = s.trimmed().toUInt(&ok);
            if (ok)
                bundle << id;
        }
        return bundle;
    }
}

/*****************************************************************************
 * Initialization
 *****************************************************************************/

DirectorManager::DirectorManager(Doc *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_enabled(false)
    , m_bpm(0)
    , m_energy(0)
    , m_playing(false)
    , m_beatInBar(0)
    , m_hasDownbeat(false)
    , m_gotInput(false)
    , m_paletteDirty(false)
    , m_readingRulesFile(false)
    , m_logPath(qEnvironmentVariable("QLCPLUS_DIRECTOR_LOG"))
    , m_beatCount(0)
    , m_demoBpm(128)
    , m_demoBeat(0)
    , m_demoStep(-1)
    , m_demoBarsLeft(0)
    , m_demoJump(-1)
{
    Q_ASSERT(doc != nullptr);

    for (int i = 0; i < PaletteSlots; i++)
        m_slotPalette[i] = QLCPalette::invalidId();

    connect(m_doc->inputOutputMap(), SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)),
            this, SLOT(slotInputValueChanged(quint32,quint32,uchar,QString)));
    connect(m_doc, SIGNAL(functionRemoved(quint32)), this, SLOT(slotFunctionRemoved(quint32)));
    connect(m_doc, SIGNAL(clearing()), this, SLOT(slotDocClearing()));

    m_watchdog.setInterval(500);
    connect(&m_watchdog, SIGNAL(timeout()), this, SLOT(slotWatchdog()));
    m_watchdog.start();

    m_demoTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_demoTimer, SIGNAL(timeout()), this, SLOT(slotDemoTick()));

    // Editors save in several steps (truncate, write, rename): wait for the
    // file to settle before reading it
    m_rulesReload.setSingleShot(true);
    m_rulesReload.setInterval(250);
    connect(&m_rulesReload, SIGNAL(timeout()), this, SLOT(slotRulesFileChanged()));
    connect(&m_rulesWatcher, SIGNAL(fileChanged(QString)), &m_rulesReload, SLOT(start()));
    connect(&m_rulesWatcher, SIGNAL(directoryChanged(QString)), &m_rulesReload, SLOT(start()));
    connect(this, SIGNAL(rulesChanged()), this, SLOT(slotRulesEdited()));

    log("started");
}

void DirectorManager::log(const QString &text) const
{
    if (m_logPath.isEmpty())
        return;

    QFile file(m_logPath);
    if (file.open(QIODevice::Append | QIODevice::Text) == false)
        return;

    QString line = QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + " " + text + "\n";
    file.write(line.toUtf8());
}

void DirectorManager::logLook() const
{
    if (m_logPath.isEmpty())
        return;

    QStringList parts;
    for (int l = 0; l < DirectorEngine::LayerCount; l++)
    {
        QStringList names;
        foreach (quint32 fid, m_engine.layerBundle(l))
        {
            Function *f = m_doc->function(fid);
            names << (f != nullptr ? f->name() : QString::number(fid));
        }
        parts << DirectorEngine::layerName(l) + "=[" + names.join(" + ") + "]";
    }
    log(QString("look rule=\"%1\" section=%2 intensity=%3 %4")
            .arg(m_engine.activeRuleName(), m_engine.section(), m_engine.intensity(), parts.join(" ")));
}

DirectorManager::~DirectorManager()
{
}

void DirectorManager::reset()
{
    m_demoTimer.stop();
    m_demoStep = -1;
    m_pendingStarts.clear();
    m_started.clear();
    m_engine.stopAll();
    m_engine.clearRules();
    m_engine.releaseAll();
    m_engine.setStrobeAllowed(true);
    m_engine.setSection(QString());
    m_engine.setIntensity(QString());
    m_engine.setStrobeOk(false);
    m_ownFunctions.clear();
    m_enabled = false;
    m_gotInput = false;
    m_bpm = 0;
    m_energy = 0;
    m_mood.clear();
    m_beatInBar = 0;
    m_hasDownbeat = false;
    m_paletteDirty = false;
    for (int i = 0; i < PaletteSlots; i++)
    {
        m_slotPalette[i] = QLCPalette::invalidId();
        m_appliedColors[i] = QColor();
    }
    for (int i = 0; i < 6; i++)
        m_inColors[i] = QColor();

    emit enabledChanged();
    emit rulesChanged();
    emit musicChanged();
    emit lookChanged();
    emit overridesChanged();
    emit demoChanged();
}

/*****************************************************************************
 * State
 *****************************************************************************/

bool DirectorManager::enabled() const
{
    return m_enabled;
}

void DirectorManager::setEnabled(bool enable)
{
    if (m_enabled == enable)
        return;

    m_enabled = enable;

    if (enable)
    {
        evaluateNow();
    }
    else
    {
        m_pendingStarts.clear();
        execute(m_engine.stopAll());
    }

    m_doc->setModified();
    emit enabledChanged();
    emit lookChanged();
}

bool DirectorManager::hasRules() const
{
    return m_engine.rules().isEmpty() == false;
}

int DirectorManager::rulesCount() const
{
    return m_engine.rules().count();
}

QVariantList DirectorManager::rulesModel() const
{
    QVariantList list;
    const QList<DirectorEngine::Rule> &rules = m_engine.rules();

    for (int i = 0; i < rules.count(); i++)
    {
        const DirectorEngine::Rule &rule = rules.at(i);
        QVariantMap map;
        map.insert("index", i);
        map.insert("name", rule.name);
        map.insert("sections", rule.sections.join(", "));
        map.insert("intensities", rule.intensities.join(", "));
        map.insert("bars", rule.bars);
        int count = 0;
        for (int l = 0; l < DirectorEngine::LayerCount; l++)
            count += rule.layers[l].count();
        map.insert("looks", count);
        list.append(map);
    }
    return list;
}

QString DirectorManager::source() const
{
    if (m_demoTimer.isActive())
        return tr("Demo");
    if (connected())
        return DIRECTOR_PLUGIN_NAME;
    return tr("No signal");
}

bool DirectorManager::connected() const
{
    if (m_demoTimer.isActive())
        return true;
    return m_gotInput && m_lastInput.isValid() && m_lastInput.elapsed() < DIRECTOR_SIGNAL_TIMEOUT_MS;
}

int DirectorManager::bpm() const
{
    return m_bpm;
}

QString DirectorManager::section() const
{
    return m_engine.section();
}

QString DirectorManager::intensity() const
{
    return m_engine.intensity();
}

QString DirectorManager::detectedSection() const
{
    return m_engine.detectedSection();
}

QString DirectorManager::detectedIntensity() const
{
    return m_engine.detectedIntensity();
}

QString DirectorManager::mood() const
{
    return m_mood;
}

int DirectorManager::energy() const
{
    return m_energy;
}

bool DirectorManager::strobeOk() const
{
    return m_engine.strobeOk();
}

QVariantList DirectorManager::paletteColors() const
{
    QVariantList list;
    for (int i = 0; i < PaletteSlots; i++)
        list.append(m_inColors[i].isValid() ? m_inColors[i] : QColor(Qt::black));
    return list;
}

int DirectorManager::beatInBar() const
{
    return m_beatInBar;
}

QString DirectorManager::ruleName() const
{
    return m_engine.activeRuleName();
}

QVariantList DirectorManager::layersModel() const
{
    static const char *names[DirectorEngine::LayerCount] =
    {
        QT_TR_NOOP("Base"), QT_TR_NOOP("Movement"), QT_TR_NOOP("Rhythm"), QT_TR_NOOP("Accent")
    };

    QVariantList list;
    for (int l = 0; l < DirectorEngine::LayerCount; l++)
    {
        QStringList running;
        foreach (quint32 fid, m_engine.layerBundle(l))
        {
            Function *f = m_doc->function(fid);
            if (f != nullptr)
                running << f->name();
        }

        QVariantMap map;
        map.insert("index", l);
        map.insert("name", tr(names[l]));
        map.insert("functions", running.join(" + "));
        map.insert("count", running.count());
        map.insert("pinned", m_engine.isLayerPinned(l));
        map.insert("off", m_engine.isLayerOff(l));
        list.append(map);
    }
    return list;
}

int DirectorManager::barsInRule() const
{
    return m_engine.barsInRule();
}

QStringList DirectorManager::sections() const
{
    return VertexAVChannels::sectionRoles();
}

QStringList DirectorManager::intensities() const
{
    return VertexAVChannels::intensities();
}

/*****************************************************************************
 * Overrides
 *****************************************************************************/

QString DirectorManager::forcedSection() const
{
    return m_engine.forcedSection();
}

QString DirectorManager::forcedIntensity() const
{
    return m_engine.forcedIntensity();
}

bool DirectorManager::hold() const
{
    return m_engine.hold();
}

void DirectorManager::setHold(bool hold)
{
    m_engine.setHold(hold);
    emit overridesChanged();
}

bool DirectorManager::strobeAllowed() const
{
    return m_engine.strobeAllowed();
}

void DirectorManager::setStrobeAllowed(bool allowed)
{
    if (m_engine.strobeAllowed() == allowed)
        return;
    m_engine.setStrobeAllowed(allowed);
    // Taking the permission away must be immediate
    evaluateNow();
    m_doc->setModified();
    emit overridesChanged();
}

bool DirectorManager::blackout() const
{
    return m_engine.blackout();
}

void DirectorManager::setBlackout(bool blackout)
{
    if (m_engine.blackout() == blackout)
        return;
    m_engine.setBlackout(blackout);
    evaluateNow();
    emit overridesChanged();
}

bool DirectorManager::strobeForced() const
{
    return m_engine.strobeForced();
}

void DirectorManager::setStrobeForced(bool forced)
{
    if (m_engine.strobeForced() == forced)
        return;
    m_engine.setStrobeForced(forced);
    evaluateNow();
    emit overridesChanged();
}

int DirectorManager::activeOverrides() const
{
    return m_engine.activeOverrides() + (m_forcedColor.isValid() ? 1 : 0) + (m_laserMode.isEmpty() ? 0 : 1);
}

QString DirectorManager::laserMode() const
{
    return m_laserMode;
}

bool DirectorManager::isLaserFunction(quint32 function) const
{
    Scene *scene = qobject_cast<Scene *>(m_doc->function(function));
    if (scene == nullptr)
        return false;

    const QList<SceneValue> values = scene->values();
    bool lit = false;

    foreach (const SceneValue &sv, values)
    {
        Fixture *fxi = m_doc->fixture(sv.fxi);
        const QLCChannel *channel = fxi != nullptr ? fxi->channel(sv.channel) : nullptr;
        if (channel == nullptr || channel->name().contains("laser", Qt::CaseInsensitive) == false)
            return false;
        if (sv.value > 0)
            lit = true;
    }
    // a Scene that only brings lasers down is not one that lights them
    return lit;
}

/** Effect channels that the Scene $function sets above zero */
static void effectChannels(Doc *doc, quint32 function, QSet<QPair<quint32, quint32> > &channels)
{
    Scene *scene = qobject_cast<Scene *>(doc->function(function));
    if (scene == nullptr)
        return;

    foreach (const SceneValue &sv, scene->values())
    {
        if (sv.value == 0)
            continue;
        Fixture *fxi = doc->fixture(sv.fxi);
        const QLCChannel *channel = fxi != nullptr ? fxi->channel(sv.channel) : nullptr;
        if (channel != nullptr && channel->group() == QLCChannel::Effect)
            channels.insert(QPair<quint32, quint32>(sv.fxi, sv.channel));
    }
}

void DirectorManager::releaseEffects(const QList<quint32> &stopped)
{
    QSet<QPair<quint32, quint32> > release, wanted;

    foreach (quint32 fid, stopped)
        effectChannels(m_doc, fid, release);
    if (release.isEmpty())
        return;

    if (m_enabled)
    {
        for (int l = 0; l < DirectorEngine::LayerCount; l++)
        {
            foreach (quint32 fid, m_engine.layerBundle(l))
            {
                if (m_laserMode == "off" && isLaserFunction(fid))
                    continue;
                effectChannels(m_doc, fid, wanted);
            }
        }
        if (m_laserMode == "on")
        {
            foreach (quint32 fid, laserFunctions())
                effectChannels(m_doc, fid, wanted);
        }
    }

    release.subtract(wanted);
    if (release.isEmpty())
        return;

    // A Scene of zeros, run for a moment and thrown away: what it writes stays
    Scene *scene = new Scene(m_doc);
    scene->setName(tr("Director - Release"));
    scene->setPath(DIRECTOR_FOLDER);
    for (auto it = release.constBegin(); it != release.constEnd(); ++it)
    {
        scene->addFixture(it->first);
        scene->setValue(it->first, it->second, 0);
    }
    if (m_doc->addFunction(scene) == false)
    {
        delete scene;
        return;
    }

    const quint32 id = scene->id();
    scene->start(m_doc->masterTimer(), FunctionParent::master());
    QTimer::singleShot(300, this, [this, id]()
    {
        Function *f = m_doc->function(id);
        if (f == nullptr)
            return;
        f->stopAndWait();
        m_doc->deleteFunction(id);
    });
}

QList<quint32> DirectorManager::laserFunctions() const
{
    QList<quint32> list;
    foreach (Function *f, m_doc->functions())
    {
        if (f != nullptr && isLaserFunction(f->id()))
            list << f->id();
    }
    return list;
}

bool DirectorManager::hasLaser() const
{
    return laserFunctions().isEmpty() == false;
}

void DirectorManager::setLaserMode(const QString &mode)
{
    const QString value = (mode == "on" || mode == "off") ? mode : QString();
    if (value == m_laserMode)
        return;

    m_laserMode = value;
    log(QString("laser %1").arg(value.isEmpty() ? QString("back to the rules") : value));
    applyLaserMode();
    emit overridesChanged();
}

void DirectorManager::applyLaserMode()
{
    if (m_enabled == false)
        return;

    QList<quint32> released;

    foreach (quint32 fid, laserFunctions())
    {
        Function *f = m_doc->function(fid);
        if (f == nullptr)
            continue;

        bool wanted = (m_laserMode == "on");
        if (m_laserMode.isEmpty())
        {
            for (int l = 0; l < DirectorEngine::LayerCount && wanted == false; l++)
                wanted = m_engine.layerBundle(l).contains(fid);
        }

        if (wanted && f->isRunning() == false)
        {
            f->start(m_doc->masterTimer(), FunctionParent::master());
            if (m_started.contains(fid) == false)
                m_started.append(fid);
        }
        else if (wanted == false && f->isRunning())
        {
            // taking a laser away must be immediate
            f->stop(FunctionParent::master());
            released << fid;
        }
        else if (wanted == false)
        {
            // already stopped, but its channels may still hold their value
            released << fid;
        }
    }

    releaseEffects(released);
}

QString DirectorManager::forcedColor() const
{
    return m_forcedColor.isValid() ? m_forcedColor.name() : QString();
}

void DirectorManager::forceColor(const QString &color)
{
    const QColor col = color.isEmpty() ? QColor() : QColor(color);
    if (col == m_forcedColor)
        return;

    m_forcedColor = col;
    log(QString("colour %1").arg(col.isValid() ? col.name() : QString("back to the music")));

    // The palettes are written at once and the Base layer restarted, rather
    // than at the next bar: an override has to be seen when it is pressed
    m_paletteDirty = true;
    evaluateNow();
    emit overridesChanged();
    emit musicChanged();
}

void DirectorManager::forceSection(const QString &section)
{
    m_engine.forceSection(section);
    emit overridesChanged();
    emit musicChanged();
}

void DirectorManager::forceIntensity(const QString &intensity)
{
    m_engine.forceIntensity(intensity);
    emit overridesChanged();
    emit musicChanged();
}

void DirectorManager::pinLayer(int layer, bool pin)
{
    m_engine.pinLayer(layer, pin);
    emit overridesChanged();
    emit lookChanged();
}

void DirectorManager::setLayerOff(int layer, bool off)
{
    m_engine.setLayerOff(layer, off);
    // Switching a layer off is expected to be immediate
    evaluateNow();
    emit overridesChanged();
}

void DirectorManager::nextLook()
{
    m_engine.requestNext();
}

void DirectorManager::releaseAll()
{
    m_engine.releaseAll();
    m_engine.setStrobeAllowed(true);
    if (m_forcedColor.isValid())
    {
        m_forcedColor = QColor();
        m_paletteDirty = true;
        evaluateNow();
    }
    if (m_laserMode.isEmpty() == false)
    {
        m_laserMode.clear();
        applyLaserMode();
    }
    emit overridesChanged();
    emit musicChanged();
    emit lookChanged();
}

/*****************************************************************************
 * Musical input
 *****************************************************************************/

void DirectorManager::slotInputValueChanged(quint32 universe, quint32 channel, uchar value, const QString &key)
{
    Q_UNUSED(key)

    // The demo replaces the real input while it runs
    if (m_demoTimer.isActive())
        return;

    InputPatch *patch = m_doc->inputOutputMap()->inputPatch(universe);
    if (patch == nullptr || patch->pluginName().contains(DIRECTOR_PLUGIN_NAME, Qt::CaseInsensitive) == false)
        return;

    bool wasConnected = connected();
    m_gotInput = true;
    m_lastInput.restart();
    if (wasConnected == false)
    {
        log(QString("input connected: universe %1, plugin \"%2\"").arg(universe + 1).arg(patch->pluginName()));
        emit musicChanged();
    }

    handleChannel(channel, value);
}

void DirectorManager::handleChannel(quint32 channel, uchar value)
{
    using namespace VertexAVChannels;

    if (channel == Beat)
    {
        if (value == 0)
            return;
        m_lastBeat.restart();
        m_beatCount++;
        if ((m_beatCount % 16) == 0)
            log(QString("beats=%1 bpm=%2 energy=%3").arg(m_beatCount).arg(m_bpm).arg(m_energy));
        if (m_hasDownbeat == false && m_beatInBar >= 4)
        {
            m_beatInBar = 0;
            handleBar();
        }
        if (m_beatInBar < 4)
            m_beatInBar++;
        emit beat();
    }
    else if (channel == Downbeat)
    {
        if (value == 0)
            return;
        m_hasDownbeat = true;
        m_beatInBar = 0;
        handleBar();
    }
    else if (channel == Bpm)
    {
        m_bpm = int(value) + 50;
        emit musicChanged();
    }
    else if (channel == Playing)
    {
        m_playing = value > 0;
        emit musicChanged();
    }
    else if (channel == Energy)
    {
        m_energy = value;
        emit musicChanged();
    }
    else if (channel == MixCut)
    {
        // A cut between two songs: do not wait for the next bar
        if (value > 0)
            evaluateNow();
    }
    else if (channel >= SectionBase && channel < SectionBase + quint32(sectionRoles().count()))
    {
        if (value == 0)
            return;
        m_engine.setSection(sectionRoles().at(int(channel - SectionBase)));
        log("section " + m_engine.detectedSection());
        emit musicChanged();
    }
    else if (channel == StrobeOk)
    {
        m_engine.setStrobeOk(value > 0);
        emit musicChanged();
    }
    else if (channel >= IntensityBase && channel < IntensityBase + quint32(intensities().count()))
    {
        if (value == 0)
            return;
        m_engine.setIntensity(intensities().at(int(channel - IntensityBase)));
        log("intensity " + m_engine.detectedIntensity());
        emit musicChanged();
    }
    else if (channel >= MoodBase && channel < MoodBase + quint32(moods().count()))
    {
        if (value == 0)
            return;
        m_mood = moods().at(int(channel - MoodBase));
        emit musicChanged();
    }
    else if (channel >= PaletteBase && channel < PaletteBase + quint32(PaletteColors * 3))
    {
        int idx = int(channel - PaletteBase) / 3;
        int comp = int(channel - PaletteBase) % 3;
        QColor col = m_inColors[idx].isValid() ? m_inColors[idx] : QColor(Qt::black);
        if (comp == 0)
            col.setRed(value);
        else if (comp == 1)
            col.setGreen(value);
        else
            col.setBlue(value);
        m_inColors[idx] = col;
        if (idx < PaletteSlots)
            m_paletteDirty = true;
        emit musicChanged();
    }
}

void DirectorManager::handleBar()
{
    if (m_enabled == false)
        return;

    applyPalette();
    const QList<DirectorEngine::Action> actions = m_engine.bar();
    execute(actions);
    applyBaseLevel();
    if (actions.isEmpty() == false)
        logLook();
    emit lookChanged();
}

void DirectorManager::evaluateNow()
{
    if (m_enabled == false)
        return;

    applyPalette();
    const QList<DirectorEngine::Action> actions = m_engine.evaluate();
    execute(actions);
    applyBaseLevel();
    if (actions.isEmpty() == false)
        logLook();
    emit lookChanged();
}

void DirectorManager::slotWatchdog()
{
    // Without beats there are no bars: apply pending changes anyway
    if (m_enabled && m_engine.isDirty() &&
        (m_lastBeat.isValid() == false || m_lastBeat.elapsed() > DIRECTOR_BEAT_TIMEOUT_MS))
    {
        evaluateNow();
    }

    stopStragglers();

    // Refresh the "connected" indication when the signal goes away
    static bool lastConnected = false;
    bool now = connected();
    if (now != lastConnected)
    {
        lastConnected = now;
        if (now == false)
            log("input lost");
        emit musicChanged();
    }
}

void DirectorManager::execute(const QList<DirectorEngine::Action> &actions)
{
    bool stopped = false;
    QList<quint32> released;

    foreach (const DirectorEngine::Action &action, actions)
    {
        if (action.type != DirectorEngine::Action::Stop)
            continue;
        Function *f = m_doc->function(action.function);
        if (f == nullptr)
            continue;
        // A laser the operator forced on outlives the look that started it
        if (m_enabled && m_laserMode == "on" && isLaserFunction(action.function))
            continue;
        // Not conditional on isRunning(): a Function started a moment ago
        // is still waiting for its first timer tick
        f->stop(FunctionParent::master());
        stopped = true;
        released << action.function;
    }

    releaseEffects(released);

    foreach (const DirectorEngine::Action &action, actions)
    {
        if (action.type == DirectorEngine::Action::Start)
            m_pendingStarts.append(QPair<int, quint32>(action.layer, action.function));
    }

    if (m_pendingStarts.isEmpty())
        return;

    // A Function that has just been asked to stop only stops at the next
    // MasterTimer tick. Starting it again right now would be ignored.
    if (stopped)
        QTimer::singleShot(3 * int(MasterTimer::tick()), this, SLOT(slotStartPending()));
    else
        slotStartPending();
}

void DirectorManager::slotStartPending()
{
    QList<QPair<int, quint32> > pending = m_pendingStarts;
    m_pendingStarts.clear();

    if (m_enabled == false)
        return;

    for (int i = 0; i < pending.count(); i++)
    {
        int layer = pending.at(i).first;
        quint32 fid = pending.at(i).second;

        // The look may have changed again in the meantime
        if (m_engine.layerBundle(layer).contains(fid) == false)
            continue;

        Function *f = m_doc->function(fid);
        if (f == nullptr || f->isRunning())
            continue;

        // The operator switched the lasers off
        if (m_laserMode == "off" && isLaserFunction(fid))
            continue;

        f->start(m_doc->masterTimer(), FunctionParent::master());
        if (m_started.contains(fid) == false)
            m_started.append(fid);
    }

    applyBaseLevel();
}

void DirectorManager::stopStragglers()
{
    for (int i = m_started.count() - 1; i >= 0; i--)
    {
        quint32 fid = m_started.at(i);
        bool wanted = false;

        if (m_enabled)
        {
            for (int l = 0; l < DirectorEngine::LayerCount && wanted == false; l++)
                wanted = m_engine.layerBundle(l).contains(fid);
            // forced on, a laser is wanted whatever the look
            if (m_laserMode == "on" && isLaserFunction(fid))
                wanted = true;
        }
        if (wanted)
            continue;

        Function *f = m_doc->function(fid);
        if (f != nullptr && f->isRunning())
            f->stop(FunctionParent::master());
        else
            m_started.removeAt(i);
    }
}

qreal DirectorManager::baseLevel() const
{
    const QString level = m_engine.intensity();
    if (level == QLatin1String("low"))
        return 0.45;
    if (level == QLatin1String("medium"))
        return 0.7;
    if (level == QLatin1String("high"))
        return 0.9;
    return 1.0;
}

void DirectorManager::applyBaseLevel()
{
    qreal level = baseLevel();
    foreach (quint32 fid, m_engine.layerBundle(DirectorEngine::Base))
    {
        Function *f = m_doc->function(fid);
        if (f != nullptr)
            f->adjustAttribute(level, Function::Intensity);
    }
}

/** Colours of the three Director palettes until the music sends its own.
 *  Deliberately not part of the wizard's vocabulary of fixed colours. */
static QColor defaultSlotColor(int slot)
{
    switch (slot)
    {
        case 0: return QColor(255, 0, 48);
        case 1: return QColor(0, 48, 255);
        default: return QColor(255, 255, 254);
    }
}

void DirectorManager::applyPalette()
{
    if (m_paletteDirty == false)
        return;
    m_paletteDirty = false;

    bool changed = false;
    for (int i = 0; i < PaletteSlots; i++)
    {
        QColor col = m_inColors[i];
        // Black means "slot not used" in the VertexAV palette
        const bool fromMusic = col.isValid() && col != QColor(Qt::black);

        if (m_forcedColor.isValid() && i < 2)
        {
            // The operator's colour takes the primary and the secondary slot;
            // the accent stays, so a strobe is still a strobe
            col = m_forcedColor;
        }
        else if (fromMusic == false)
        {
            // Nothing from the music: only go back to the default if a
            // forced colour, now released, was written over it
            if (m_appliedColors[i].isValid() == false)
                continue;
            col = defaultSlotColor(i);
        }

        if (col == m_appliedColors[i])
            continue;

        QLCPalette *palette = m_doc->palette(m_slotPalette[i]);
        if (palette == nullptr)
            continue;

        palette->setValue(QLCPalette::colorToString(col, QColor()));
        m_appliedColors[i] = col;
        changed = true;
    }

    // A running Scene resolves its palettes only when it starts
    if (changed)
        m_engine.requestRefresh(DirectorEngine::Base);
}

void DirectorManager::slotFunctionRemoved(quint32 id)
{
    m_started.removeAll(id);
    m_ownFunctions.removeAll(id);
    m_engine.removeFunction(id);
    emit rulesChanged();
    emit lookChanged();
}

void DirectorManager::slotDocClearing()
{
    // The rules about to be dropped must not reach the file of the project
    setProjectFile(QString());
    reset();
}

/*****************************************************************************
 * Rules
 *****************************************************************************/

quint32 DirectorManager::findOrCreatePalette(int type, const QString &name, const QVariantList &values, bool reuse)
{
    foreach (QLCPalette *p, m_doc->palettes())
    {
        if (p == nullptr || int(p->type()) != type)
            continue;
        // Director colours are found by name: their value changes with the music
        if (reuse ? (p->values() == values) : (p->name() == name))
            return p->id();
    }

    QLCPalette *palette = new QLCPalette(QLCPalette::PaletteType(type));
    palette->setValues(values);
    palette->setName(name);
    if (m_doc->addPalette(palette) == false)
    {
        delete palette;
        return QLCPalette::invalidId();
    }
    return palette->id();
}

quint32 DirectorManager::makeScene(const QString &name, quint32 groupId, const QList<quint32> &palettes)
{
    Scene *scene = new Scene(m_doc);
    scene->setName(name);
    scene->addFixtureGroup(groupId);
    foreach (quint32 pid, palettes)
    {
        if (pid != QLCPalette::invalidId())
            scene->addPalette(pid);
    }
    scene->setFadeInSpeed(300);
    scene->setFadeOutSpeed(300);
    scene->setPath(DIRECTOR_FOLDER);

    if (m_doc->addFunction(scene) == false)
    {
        delete scene;
        return Function::invalidId();
    }

    m_ownFunctions.append(scene->id());
    return scene->id();
}

quint32 DirectorManager::makeTiltSweep(const QString &groupName, quint32 groupId)
{
    FixtureGroup *group = m_doc->fixtureGroup(groupId);
    if (group == nullptr)
        return Function::invalidId();

    // (fixture, tilt channel) of every fixture that tilts but does not pan.
    // The channel is looked up on the fixture, not on its heads: a pixel bar
    // declares one head per LED and its single tilt motor belongs to none.
    QList<QPair<quint32, quint32> > tilts;
    foreach (quint32 fxID, group->fixtureList())
    {
        Fixture *fxi = m_doc->fixture(fxID);
        if (fxi == nullptr)
            continue;

        bool pan = false;
        quint32 tilt = QLCChannel::invalid();
        for (quint32 ch = 0; ch < fxi->channels(); ch++)
        {
            const QLCChannel *channel = fxi->channel(ch);
            if (channel == nullptr)
                continue;
            if (channel->group() == QLCChannel::Pan)
                pan = true;
            else if (channel->group() == QLCChannel::Tilt && channel->controlByte() == QLCChannel::MSB &&
                     tilt == QLCChannel::invalid())
                tilt = ch;
        }

        if (pan == false && tilt != QLCChannel::invalid())
            tilts.append(QPair<quint32, quint32>(fxID, tilt));
    }

    if (tilts.isEmpty())
        return Function::invalidId();

    // Two ends of the sweep, around the centre of the travel
    const uchar ends[2] = { 72, 184 };
    const char *endNames[2] = { QT_TR_NOOP("%1 - Tilt down"), QT_TR_NOOP("%1 - Tilt up") };

    Chaser *chaser = new Chaser(m_doc);
    chaser->setName(tr("%1 - Tilt sweep").arg(groupName));
    chaser->setFadeInMode(Chaser::Common);
    chaser->setFadeOutMode(Chaser::Common);
    chaser->setDurationMode(Chaser::Common);
    chaser->setFadeInSpeed(1800);
    chaser->setFadeOutSpeed(0);
    chaser->setDuration(2000);

    for (int i = 0; i < 2; i++)
    {
        Scene *scene = new Scene(m_doc);
        scene->setName(tr(endNames[i]).arg(groupName));
        for (int t = 0; t < tilts.count(); t++)
        {
            scene->addFixture(tilts.at(t).first);
            scene->setValue(tilts.at(t).first, tilts.at(t).second, ends[i]);
        }
        scene->setPath(DIRECTOR_FOLDER);
        if (m_doc->addFunction(scene) == false)
        {
            delete scene;
            delete chaser;
            return Function::invalidId();
        }
        m_ownFunctions.append(scene->id());

        ChaserStep step(scene->id());
        step.fadeIn = 1800;
        step.hold = 200;
        step.fadeOut = 0;
        chaser->addStep(step);
    }

    chaser->setPath(DIRECTOR_FOLDER);
    if (m_doc->addFunction(chaser) == false)
    {
        delete chaser;
        return Function::invalidId();
    }

    m_ownFunctions.append(chaser->id());
    return chaser->id();
}

void DirectorManager::clearRules()
{
    bool wasEnabled = m_enabled;
    if (wasEnabled)
        setEnabled(false);

    m_engine.clearRules();

    // deleteFunction() triggers slotFunctionRemoved(), which edits the list
    QList<quint32> own = m_ownFunctions;
    m_ownFunctions.clear();
    foreach (quint32 fid, own)
    {
        // The scenes may still be running or fading out: deleting them under
        // the MasterTimer would crash it, so wait for them, like the Function
        // Manager does.
        Function *f = m_doc->function(fid);
        if (f != nullptr)
            f->stopAndWait();
        m_doc->deleteFunction(fid);
    }

    m_doc->setModified();
    emit rulesChanged();
    emit lookChanged();
}

QList<int> DirectorManager::rulesFor(const QStringList &sections) const
{
    QList<int> indexes;
    const QList<DirectorEngine::Rule> &rules = m_engine.rules();
    for (int r = 0; r < rules.count(); r++)
    {
        bool match = false;
        if (sections.isEmpty())
        {
            match = (rules.at(r).sections.contains("silence") == false);
        }
        else
        {
            foreach (const QString &section, sections)
            {
                if (rules.at(r).sections.contains(section))
                    match = true;
            }
        }
        if (match)
            indexes << r;
    }
    return indexes;
}

void DirectorManager::replaceRules(const QList<DirectorEngine::Rule> &rules)
{
    // The engine forgets the current look with its rules: stop what it
    // started first, or those Functions would be left running
    bool wasEnabled = m_enabled;
    if (wasEnabled)
        setEnabled(false);

    m_engine.setRules(rules);
    m_doc->setModified();
    emit rulesChanged();
    emit lookChanged();

    if (wasEnabled)
        setEnabled(true);
}

int DirectorManager::addCompanion(int layer, quint32 function, const QStringList &sections)
{
    if (layer < 0 || layer >= DirectorEngine::LayerCount || m_doc->function(function) == nullptr)
        return 0;

    QList<DirectorEngine::Rule> rules = m_engine.rules();
    int added = 0;
    foreach (int r, rulesFor(sections))
    {
        QList<DirectorEngine::Bundle> &looks = rules[r].layers[layer];
        if (looks.isEmpty())
        {
            looks.append(DirectorEngine::Bundle() << function);
        }
        else
        {
            for (int b = 0; b < looks.count(); b++)
            {
                if (looks.at(b).contains(function) == false)
                    looks[b] << function;
            }
        }
        added++;
    }

    if (added > 0)
        replaceRules(rules);
    log(QString("companion %1 on %2: %3 rules").arg(function).arg(DirectorEngine::layerName(layer)).arg(added));
    return added;
}

int DirectorManager::addLook(int layer, const QList<quint32> &functions, const QStringList &sections)
{
    if (layer < 0 || layer >= DirectorEngine::LayerCount)
        return 0;

    DirectorEngine::Bundle bundle;
    foreach (quint32 fid, functions)
    {
        if (m_doc->function(fid) != nullptr && bundle.contains(fid) == false)
            bundle << fid;
    }
    if (bundle.isEmpty())
        return 0;

    QList<DirectorEngine::Rule> rules = m_engine.rules();
    int added = 0;
    foreach (int r, rulesFor(sections))
    {
        if (rules.at(r).layers[layer].contains(bundle) == false)
        {
            rules[r].layers[layer].append(bundle);
            added++;
        }
    }

    if (added > 0)
        replaceRules(rules);
    log(QString("look of %1 on %2: %3 rules").arg(bundle.count()).arg(DirectorEngine::layerName(layer)).arg(added));
    return added;
}

void DirectorManager::setupFromManifest(const DirectorManifest &manifest, bool enable)
{
    typedef DirectorEngine::Bundle Bundle;

    // Running the wizard again must not pile up a second set of rules
    clearRules();

    /* Palettes. The three Director colours follow the music, so they are
     * kept apart from the wizard's fixed colours: the values below are
     * deliberately not part of its vocabulary. */
    const char *slotNames[PaletteSlots] =
    {
        QT_TR_NOOP("Director - Primary"), QT_TR_NOOP("Director - Secondary"), QT_TR_NOOP("Director - Accent")
    };
    for (int i = 0; i < PaletteSlots; i++)
    {
        QVariantList values;
        values << QLCPalette::colorToString(defaultSlotColor(i), QColor());
        m_slotPalette[i] = findOrCreatePalette(QLCPalette::Color, tr(slotNames[i]), values, false);
        m_appliedColors[i] = QColor();
    }
    m_paletteDirty = true;

    quint32 dimmerFull = findOrCreatePalette(QLCPalette::Dimmer, tr("Dimmer Full"),
                                             QVariantList() << 255, true);
    quint32 shutterOpen = findOrCreatePalette(QLCPalette::Shutter, tr("Shutter Open"),
                                              QVariantList() << int(QLCCapability::ShutterOpen) << 100, true);
    quint32 strobeFast = findOrCreatePalette(QLCPalette::Shutter, tr("Strobe Fast"),
                                             QVariantList() << int(QLCCapability::StrobeSlowToFast) << 85, true);

    /* Base and strobe scenes, one set per group */
    Bundle baseA, baseB, strobes;
    int colorGroups = 0;

    foreach (const DirectorManifest::Group &grp, manifest.groups)
    {
        if (grp.id == FixtureGroup::invalidId())
            continue;

        // Blinders are an accent: they must not sit in the wash
        const bool blinder = (grp.role == StageWizard::RoleBlinder);

        if (blinder == false && (grp.hasColor || grp.hasDimmer))
        {
            QList<quint32> common;
            common << dimmerFull;
            if (grp.hasShutter)
                common << shutterOpen;

            if (grp.hasColor)
            {
                quint32 a = makeScene(tr("%1 - Primary").arg(grp.name), grp.id,
                                      QList<quint32>() << m_slotPalette[0] << common);
                quint32 b = makeScene(tr("%1 - Secondary").arg(grp.name), grp.id,
                                      QList<quint32>() << m_slotPalette[1] << common);
                if (a != Function::invalidId() && b != Function::invalidId())
                {
                    // Second look: every other group swaps to the secondary colour
                    baseA << a;
                    baseB << ((colorGroups % 2) ? a : b);
                    colorGroups++;
                }
            }
            else
            {
                quint32 w = makeScene(tr("%1 - On").arg(grp.name), grp.id, common);
                if (w != Function::invalidId())
                {
                    baseA << w;
                    baseB << w;
                }
            }
        }

        if (grp.hasShutter && blinder == false)
        {
            QList<quint32> pals;
            pals << strobeFast << dimmerFull;
            if (grp.hasColor)
                pals << m_slotPalette[2];
            quint32 s = makeScene(tr("%1 - Strobe").arg(grp.name), grp.id, pals);
            if (s != Function::invalidId())
                strobes << s;
        }
    }

    /* Motorised bars: they tilt but do not pan, so the wizard gives them no
     * movement effect. They get a slow tilt sweep instead. */
    Bundle tiltSweeps;
    foreach (const DirectorManifest::Group &grp, manifest.groups)
    {
        quint32 sweep = makeTiltSweep(grp.name, grp.id);
        if (sweep != Function::invalidId())
            tiltSweeps << sweep;
    }

    /* What the wizard generated, by effect */
    auto effect = [&manifest](int flag) -> Bundle
    {
        Bundle bundle;
        foreach (const DirectorManifest::Item &item, manifest.items)
        {
            if (item.effect == flag && item.function != Function::invalidId())
                bundle << item.function;
        }
        return bundle;
    };

    const Bundle circle = effect(StageWizard::EffectCircleChase);
    const Bundle eight = effect(StageWizard::EffectFigureEight);
    const Bundle sweep = effect(StageWizard::EffectAudienceSweep);
    const Bundle heartbeat = effect(StageWizard::EffectHeartbeat);
    const Bundle rainbow = effect(StageWizard::EffectColorRainbow);
    const Bundle split = effect(StageWizard::EffectSplitColor);
    const Bundle wave = effect(StageWizard::EffectWave);
    const Bundle pixel = effect(StageWizard::EffectPixelChase);
    const Bundle marquee = effect(StageWizard::EffectMarquee);
    const Bundle fireworks = effect(StageWizard::EffectFireworks);
    const Bundle plasma = effect(StageWizard::EffectPlasma);
    // The wizard makes a "Blinder Hit" for every group: full white on a
    // wash is not an accent, so only the groups whose role is Blinder count.
    Bundle blinders;
    foreach (const DirectorManifest::Item &item, manifest.items)
    {
        if (item.effect != StageWizard::EffectBlinderHit || item.function == Function::invalidId())
            continue;
        foreach (const DirectorManifest::Group &grp, manifest.groups)
        {
            if (grp.id == item.group && grp.role == StageWizard::RoleBlinder)
                blinders << item.function;
        }
    }
    const Bundle ambient = effect(StageWizard::EffectAmbientLoop);

    auto add = [](DirectorEngine::Rule &rule, int layer, const Bundle &bundle)
    {
        if (bundle.isEmpty() == false && rule.layers[layer].contains(bundle) == false)
            rule.layers[layer].append(bundle);
    };

    QList<DirectorEngine::Rule> rules;

    {
        DirectorEngine::Rule rule;
        rule.name = tr("Silence");
        rule.sections << "silence";
        rule.bars = 0;
        add(rule, DirectorEngine::Base, ambient.isEmpty() ? baseA : ambient);
        rules << rule;
    }
    {
        DirectorEngine::Rule rule;
        rule.name = tr("Calm");
        rule.sections << "intro" << "outro" << "breakdown";
        rule.bars = 8;
        add(rule, DirectorEngine::Base, baseA);
        add(rule, DirectorEngine::Base, baseB);
        add(rule, DirectorEngine::Movement, circle);
        add(rule, DirectorEngine::Rhythm, heartbeat);
        rules << rule;
    }
    {
        DirectorEngine::Rule rule;
        rule.name = tr("Verse");
        rule.sections << "verse" << "bridge";
        rule.bars = 8;
        add(rule, DirectorEngine::Base, baseA);
        add(rule, DirectorEngine::Base, baseB);
        add(rule, DirectorEngine::Movement, circle);
        add(rule, DirectorEngine::Movement, eight);
        add(rule, DirectorEngine::Rhythm, wave);
        add(rule, DirectorEngine::Rhythm, heartbeat);
        rules << rule;
    }
    {
        DirectorEngine::Rule rule;
        rule.name = tr("Chorus");
        rule.sections << "chorus";
        rule.bars = 8;
        add(rule, DirectorEngine::Base, baseB);
        add(rule, DirectorEngine::Base, baseA);
        add(rule, DirectorEngine::Movement, eight);
        add(rule, DirectorEngine::Movement, sweep);
        add(rule, DirectorEngine::Rhythm, wave);
        add(rule, DirectorEngine::Rhythm, pixel);
        add(rule, DirectorEngine::Rhythm, rainbow);
        rules << rule;
    }
    {
        DirectorEngine::Rule rule;
        rule.name = tr("Build");
        rule.sections << "build";
        rule.bars = 4;
        add(rule, DirectorEngine::Base, baseA);
        add(rule, DirectorEngine::Movement, sweep);
        add(rule, DirectorEngine::Movement, eight);
        add(rule, DirectorEngine::Rhythm, pixel);
        add(rule, DirectorEngine::Rhythm, marquee);
        rules << rule;
    }
    {
        DirectorEngine::Rule rule;
        rule.name = tr("Drop");
        rule.sections << "drop";
        rule.bars = 4;
        add(rule, DirectorEngine::Base, baseA);
        add(rule, DirectorEngine::Base, baseB);
        add(rule, DirectorEngine::Movement, circle);
        add(rule, DirectorEngine::Movement, eight);
        add(rule, DirectorEngine::Rhythm, fireworks);
        add(rule, DirectorEngine::Rhythm, plasma);
        add(rule, DirectorEngine::Rhythm, split);
        add(rule, DirectorEngine::Rhythm, marquee);
        add(rule, DirectorEngine::Accent, strobes);
        add(rule, DirectorEngine::Accent, blinders);
        rules << rule;
    }
    {
        DirectorEngine::Rule rule;
        rule.name = tr("Default");
        rule.bars = 8;
        add(rule, DirectorEngine::Base, baseA);
        add(rule, DirectorEngine::Base, baseB);
        add(rule, DirectorEngine::Movement, circle);
        add(rule, DirectorEngine::Rhythm, wave);
        rules << rule;
    }

    // The bars sweep along with whatever the moving heads do, and on their
    // own when there are no moving heads. They rest when the music does.
    if (tiltSweeps.isEmpty() == false)
    {
        for (int r = 0; r < rules.count(); r++)
        {
            if (rules.at(r).sections.contains("silence"))
                continue;

            QList<Bundle> &movement = rules[r].layers[DirectorEngine::Movement];
            if (movement.isEmpty())
            {
                movement.append(tiltSweeps);
            }
            else
            {
                for (int b = 0; b < movement.count(); b++)
                    movement[b] << tiltSweeps;
            }
        }
    }

    // A rule with nothing to run would only produce a blackout
    QList<DirectorEngine::Rule> usable;
    foreach (const DirectorEngine::Rule &rule, rules)
    {
        if (rule.isEmpty() == false)
            usable << rule;
    }

    m_engine.setRules(usable);
    m_doc->setModified();
    log(QString("rules built: %1 rules, %2 groups, %3 wizard effects, %4 tilt sweeps")
            .arg(usable.count()).arg(manifest.groups.count()).arg(manifest.items.count()).arg(tiltSweeps.count()));

    emit rulesChanged();
    emit lookChanged();

    if (enable && usable.isEmpty() == false)
        setEnabled(true);
}

/*****************************************************************************
 * Demo mode
 *****************************************************************************/

bool DirectorManager::demoRunning() const
{
    return m_demoTimer.isActive();
}

int DirectorManager::demoBpm() const
{
    return m_demoBpm;
}

void DirectorManager::setDemoBpm(int bpm)
{
    bpm = qBound(60, bpm, 200);
    if (m_demoBpm == bpm)
        return;

    m_demoBpm = bpm;
    if (m_demoTimer.isActive())
    {
        m_demoTimer.setInterval(60000 / m_demoBpm);
        handleChannel(VertexAVChannels::Bpm, uchar(qBound(0, m_demoBpm - 50, 255)));
    }
    emit demoChanged();
}

QVariantList DirectorManager::demoSong() const
{
    QVariantList list;
    for (int i = 0; i < demoSongCount; i++)
    {
        QVariantMap map;
        map.insert("index", i);
        map.insert("section", QString(demoSongSteps[i].section));
        map.insert("bars", demoSongSteps[i].bars);
        map.insert("intensity", QString(demoSongSteps[i].intensity));
        list.append(map);
    }
    return list;
}

int DirectorManager::demoStep() const
{
    return m_demoStep;
}

void DirectorManager::startDemo()
{
    if (m_demoTimer.isActive())
        return;

    m_demoBeat = 0;
    m_demoStep = -1;
    m_demoBarsLeft = 0;
    m_demoJump = -1;
    m_hasDownbeat = true;
    m_beatInBar = 0;

    handleChannel(VertexAVChannels::Bpm, uchar(qBound(0, m_demoBpm - 50, 255)));
    handleChannel(VertexAVChannels::Playing, 255);

    m_demoTimer.start(60000 / m_demoBpm);
    emit demoChanged();
    emit musicChanged();

    // Do not wait a whole beat for the first look
    slotDemoTick();
}

void DirectorManager::stopDemo()
{
    if (m_demoTimer.isActive() == false)
        return;

    m_demoTimer.stop();
    m_demoStep = -1;
    m_hasDownbeat = false;
    m_beatInBar = 0;
    handleChannel(VertexAVChannels::Playing, 0);
    m_engine.setSection(QString());
    m_engine.setIntensity(QString());
    m_engine.setStrobeOk(false);

    emit demoChanged();
    emit musicChanged();
}

void DirectorManager::demoJumpTo(int step)
{
    if (step < 0 || step >= demoSongCount)
        return;
    m_demoJump = step;
}

void DirectorManager::pushDemoStep()
{
    using namespace VertexAVChannels;

    const DemoStep &step = demoSongSteps[m_demoStep];

    for (int i = 0; i < 3; i++)
    {
        QColor col(step.colors[i]);
        handleChannel(PaletteBase + quint32(i * 3), uchar(col.red()));
        handleChannel(PaletteBase + quint32(i * 3 + 1), uchar(col.green()));
        handleChannel(PaletteBase + quint32(i * 3 + 2), uchar(col.blue()));
    }

    int sec = sectionRoles().indexOf(step.section);
    if (sec >= 0)
        handleChannel(SectionBase + quint32(sec), 255);

    int inten = intensities().indexOf(step.intensity);
    if (inten >= 0)
    {
        handleChannel(IntensityBase + quint32(inten), 255);
        handleChannel(Energy, uchar(60 + inten * 60));
    }

    handleChannel(StrobeOk, step.strobe ? 255 : 0);
}

void DirectorManager::slotDemoTick()
{
    if ((m_demoBeat % 4) == 0)
    {
        if (m_demoJump >= 0)
        {
            m_demoStep = m_demoJump;
            m_demoJump = -1;
            m_demoBarsLeft = demoSongSteps[m_demoStep].bars;
            pushDemoStep();
            emit demoChanged();
        }
        else if (m_demoBarsLeft <= 0)
        {
            m_demoStep = (m_demoStep + 1) % demoSongCount;
            m_demoBarsLeft = demoSongSteps[m_demoStep].bars;
            pushDemoStep();
            emit demoChanged();
        }

        m_demoBarsLeft--;
        handleChannel(VertexAVChannels::Downbeat, 255);
    }

    handleChannel(VertexAVChannels::Beat, 255);
    m_demoBeat++;
}

/*****************************************************************************
 * Load & Save
 *****************************************************************************/

bool DirectorManager::loadXML(QXmlStreamReader &root)
{
    if (root.name() != KXMLQLCDirector)
    {
        qWarning() << Q_FUNC_INFO << "Director node not found";
        return false;
    }

    QXmlStreamAttributes attrs = root.attributes();
    bool enable = attrs.value(KXMLQLCDirectorEnabled).toString() == QLatin1String("1");
    if (attrs.hasAttribute(KXMLQLCDirectorStrobe))
        m_engine.setStrobeAllowed(attrs.value(KXMLQLCDirectorStrobe).toString() != QLatin1String("0"));

    QList<DirectorEngine::Rule> rules;
    m_ownFunctions.clear();

    while (root.readNextStartElement())
    {
        if (root.name() == KXMLQLCDirectorPaletteSlot)
        {
            QXmlStreamAttributes a = root.attributes();
            int idx = a.value(KXMLQLCDirectorIndex).toInt();
            if (idx >= 0 && idx < PaletteSlots)
                m_slotPalette[idx] = a.value(KXMLQLCDirectorPalette).toUInt();
            root.skipCurrentElement();
        }
        else if (root.name() == KXMLQLCDirectorOwn)
        {
            m_ownFunctions = bundleFromString(root.readElementText());
        }
        else if (root.name() == KXMLQLCDirectorRule)
        {
            DirectorEngine::Rule rule;
            QXmlStreamAttributes a = root.attributes();
            rule.name = a.value(KXMLQLCDirectorName).toString();
            rule.sections = a.value(KXMLQLCDirectorSections).toString().split(",", Qt::SkipEmptyParts);
            rule.intensities = a.value(KXMLQLCDirectorIntensities).toString().split(",", Qt::SkipEmptyParts);
            rule.bars = a.value(KXMLQLCDirectorBars).toInt();

            while (root.readNextStartElement())
            {
                if (root.name() == KXMLQLCDirectorLayer)
                {
                    int layer = DirectorEngine::layerFromName(root.attributes().value(KXMLQLCDirectorName).toString());
                    while (root.readNextStartElement())
                    {
                        if (root.name() == KXMLQLCDirectorBundle)
                        {
                            DirectorEngine::Bundle bundle;
                            // Drop the Functions that do not exist anymore
                            foreach (quint32 fid, bundleFromString(root.readElementText()))
                            {
                                if (m_doc->function(fid) != nullptr)
                                    bundle << fid;
                            }
                            if (layer >= 0 && bundle.isEmpty() == false)
                                rule.layers[layer].append(bundle);
                        }
                        else
                        {
                            root.skipCurrentElement();
                        }
                    }
                }
                else
                {
                    qWarning() << Q_FUNC_INFO << "Unknown Director rule tag:" << root.name();
                    root.skipCurrentElement();
                }
            }

            if (rule.isEmpty() == false)
                rules << rule;
        }
        else
        {
            qWarning() << Q_FUNC_INFO << "Unknown Director tag:" << root.name();
            root.skipCurrentElement();
        }
    }

    m_engine.setRules(rules);
    m_paletteDirty = false;

    emit rulesChanged();
    emit overridesChanged();

    if (enable && rules.isEmpty() == false)
        setEnabled(true);

    return true;
}

bool DirectorManager::saveXML(QXmlStreamWriter *doc) const
{
    Q_ASSERT(doc != nullptr);

    const QList<DirectorEngine::Rule> &rules = m_engine.rules();

    // Keep project files untouched for whoever does not use the Director
    if (rules.isEmpty())
        return true;

    doc->writeStartElement(KXMLQLCDirector);
    doc->writeAttribute(KXMLQLCDirectorEnabled, m_enabled ? "1" : "0");
    doc->writeAttribute(KXMLQLCDirectorStrobe, m_engine.strobeAllowed() ? "1" : "0");

    for (int i = 0; i < PaletteSlots; i++)
    {
        if (m_slotPalette[i] == QLCPalette::invalidId())
            continue;
        doc->writeStartElement(KXMLQLCDirectorPaletteSlot);
        doc->writeAttribute(KXMLQLCDirectorIndex, QString::number(i));
        doc->writeAttribute(KXMLQLCDirectorPalette, QString::number(m_slotPalette[i]));
        doc->writeEndElement();
    }

    if (m_ownFunctions.isEmpty() == false)
        doc->writeTextElement(KXMLQLCDirectorOwn, bundleToString(m_ownFunctions));

    foreach (const DirectorEngine::Rule &rule, rules)
    {
        doc->writeStartElement(KXMLQLCDirectorRule);
        doc->writeAttribute(KXMLQLCDirectorName, rule.name);
        if (rule.sections.isEmpty() == false)
            doc->writeAttribute(KXMLQLCDirectorSections, rule.sections.join(","));
        if (rule.intensities.isEmpty() == false)
            doc->writeAttribute(KXMLQLCDirectorIntensities, rule.intensities.join(","));
        doc->writeAttribute(KXMLQLCDirectorBars, QString::number(rule.bars));

        for (int l = 0; l < DirectorEngine::LayerCount; l++)
        {
            if (rule.layers[l].isEmpty())
                continue;
            doc->writeStartElement(KXMLQLCDirectorLayer);
            doc->writeAttribute(KXMLQLCDirectorName, DirectorEngine::layerName(l));
            foreach (const DirectorEngine::Bundle &bundle, rule.layers[l])
                doc->writeTextElement(KXMLQLCDirectorBundle, bundleToString(bundle));
            doc->writeEndElement();
        }

        doc->writeEndElement();
    }

    doc->writeEndElement();
    return true;
}

/*****************************************************************************
 * Rules file
 *****************************************************************************/

void DirectorManager::setProjectFile(const QString &projectFile)
{
    QString path;
    if (projectFile.isEmpty() == false)
    {
        QFileInfo info(projectFile);
        path = info.absolutePath() + "/" + info.completeBaseName() + ".director.json";
    }

    if (path == m_rulesFile)
    {
        // The project has just been saved with the rules that are running
        if (m_rulesFile.isEmpty() == false)
            stampRulesFile();
        return;
    }

    if (m_rulesWatcher.files().isEmpty() == false)
        m_rulesWatcher.removePaths(m_rulesWatcher.files());
    if (m_rulesWatcher.directories().isEmpty() == false)
        m_rulesWatcher.removePaths(m_rulesWatcher.directories());
    m_rulesReload.stop();

    m_rulesFile = path;
    m_rulesFileError.clear();
    m_rulesFileData.clear();
    m_rulesFileRules = QJsonArray();
    m_rulesFileOverrides = QJsonObject();

    if (m_rulesFile.isEmpty() == false)
    {
        // Right now the engine holds the rules of the project
        m_projectStamp = rulesStamp();

        bool stale = false;
        QFile file(m_rulesFile);
        if (file.open(QIODevice::ReadOnly))
        {
            const QJsonObject old = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            const QString stamp = old.value("project").toString();
            // A file with no stamp was written by hand: it is left in charge
            stale = (stamp.isEmpty() == false && m_projectStamp.isEmpty() == false && stamp != m_projectStamp);
            if (stale)
                m_rulesFileOverrides = old.value("overrides").toObject();
        }

        if (stale)
        {
            QFile::remove(m_rulesFile + ".bak");
            QFile::rename(m_rulesFile, m_rulesFile + ".bak");
            log("rules file: older than the project, kept as .bak and written again");
            writeRulesFile();
        }
        else if (QFile::exists(m_rulesFile))
        {
            readRulesFile();
        }
        else
        {
            writeRulesFile();
        }
        watchRulesFile();
    }
    else
    {
        m_projectStamp.clear();
    }

    emit rulesFileChanged();
}

QString DirectorManager::rulesFile() const
{
    return m_rulesFile;
}

QString DirectorManager::rulesFileError() const
{
    return m_rulesFileError;
}

void DirectorManager::watchRulesFile()
{
    // Many editors replace the file instead of writing into it, and a
    // replaced file is no longer watched: watch the folder as well
    if (QFile::exists(m_rulesFile) && m_rulesWatcher.files().contains(m_rulesFile) == false)
        m_rulesWatcher.addPath(m_rulesFile);
    QString dir = QFileInfo(m_rulesFile).absolutePath();
    if (m_rulesWatcher.directories().contains(dir) == false)
        m_rulesWatcher.addPath(dir);
}

quint32 DirectorManager::functionByName(const QString &name) const
{
    foreach (Function *f, m_doc->functions())
    {
        if (f->name() == name)
            return f->id();
    }
    return Function::invalidId();
}

void DirectorManager::slotRulesFileChanged()
{
    if (m_rulesFile.isEmpty() || QFile::exists(m_rulesFile) == false)
        return;

    // The folder is watched too: only a file that really changed is read
    QFile file(m_rulesFile);
    if (file.open(QIODevice::ReadOnly) == false)
        return;
    const QByteArray data = file.readAll();
    file.close();
    if (data == m_rulesFileData)
    {
        watchRulesFile();
        return;
    }

    readRulesFile();
    watchRulesFile();
    emit rulesFileChanged();
}

void DirectorManager::slotRulesEdited()
{
    // Rules changed from inside the application (wizard, companions): the
    // file follows. Not while the file itself is being read, and not when
    // the project is being closed.
    if (m_readingRulesFile || m_rulesFile.isEmpty() || m_engine.rules().isEmpty())
        return;

    writeRulesFile();
    watchRulesFile();
}

bool DirectorManager::readRulesFile()
{
    QFile file(m_rulesFile);
    if (file.open(QIODevice::ReadOnly) == false)
    {
        m_rulesFileError = tr("Cannot read %1").arg(m_rulesFile);
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();
    m_rulesFileData = data;

    QJsonParseError parseError;
    QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if (json.isObject() == false)
    {
        // Keep running with the rules there were: a half typed file must
        // not take the show down
        m_rulesFileError = tr("Line %1: %2")
            .arg(data.left(parseError.offset).count('\n') + 1).arg(parseError.errorString());
        log("rules file: " + m_rulesFileError);
        return false;
    }

    const QJsonArray rulesJson = json.object().value("rules").toArray();
    const QJsonObject overridesJson = json.object().value("overrides").toObject();
    const bool overridesEdited = (overridesJson != m_rulesFileOverrides);

    // Only the overrides were touched: the looks keep running as they are
    if (rulesJson == m_rulesFileRules && m_engine.rules().isEmpty() == false)
    {
        m_rulesFileError.clear();
        if (overridesEdited)
        {
            m_rulesFileOverrides = overridesJson;
            applyOverrides(overridesJson);
        }
        return true;
    }

    QStringList missing;
    QList<DirectorEngine::Rule> rules;

    foreach (const QJsonValue &rv, rulesJson)
    {
        QJsonObject ro = rv.toObject();
        DirectorEngine::Rule rule;
        rule.name = ro.value("name").toString();
        foreach (const QJsonValue &v, ro.value("sections").toArray())
            rule.sections << v.toString();
        foreach (const QJsonValue &v, ro.value("intensities").toArray())
            rule.intensities << v.toString();
        rule.bars = ro.value("bars").toInt(8);

        QJsonObject layers = ro.value("layers").toObject();
        for (auto it = layers.constBegin(); it != layers.constEnd(); ++it)
        {
            int layer = DirectorEngine::layerFromName(it.key());
            if (layer < 0)
            {
                missing << tr("layer \"%1\"").arg(it.key());
                continue;
            }
            foreach (const QJsonValue &lookValue, it.value().toArray())
            {
                DirectorEngine::Bundle bundle;
                foreach (const QJsonValue &fv, lookValue.toArray())
                {
                    // A Function goes by its name, or by its number
                    quint32 fid = fv.isString() ? functionByName(fv.toString()) : quint32(fv.toInt(-1));
                    if (m_doc->function(fid) == nullptr)
                        missing << (fv.isString() ? fv.toString() : QString::number(fv.toInt(-1)));
                    else if (bundle.contains(fid) == false)
                        bundle << fid;
                }
                if (bundle.isEmpty() == false)
                    rule.layers[layer].append(bundle);
            }
        }

        if (rule.isEmpty() == false)
            rules << rule;
    }

    if (rules.isEmpty())
    {
        m_rulesFileError = tr("No usable rule in the file");
        log("rules file: " + m_rulesFileError);
        return false;
    }

    missing.removeDuplicates();
    m_rulesFileError = missing.isEmpty() ? QString() : tr("Not found: %1").arg(missing.join(", "));

    m_readingRulesFile = true;
    replaceRules(rules);
    m_readingRulesFile = false;
    m_rulesFileRules = rulesJson;

    if (overridesEdited)
    {
        m_rulesFileOverrides = overridesJson;
        applyOverrides(overridesJson);
    }

    log(QString("rules file read: %1 rules%2").arg(rules.count())
            .arg(m_rulesFileError.isEmpty() ? QString() : " (" + m_rulesFileError + ")"));
    return true;
}

QJsonObject DirectorManager::overridesToJson() const
{
    QJsonObject o;
    o.insert("section", forcedSection());
    o.insert("intensity", forcedIntensity());
    o.insert("hold", hold());
    o.insert("blackout", blackout());
    o.insert("strobeAllowed", strobeAllowed());
    o.insert("laser", laserMode());
    o.insert("color", forcedColor());
    return o;
}

void DirectorManager::applyOverrides(const QJsonObject &o)
{
    // Only what the file names: a key left out is left alone
    if (o.contains("section"))
        forceSection(o.value("section").toString());
    if (o.contains("intensity"))
        forceIntensity(o.value("intensity").toString());
    if (o.contains("strobeAllowed"))
        setStrobeAllowed(o.value("strobeAllowed").toBool(true));
    if (o.contains("laser"))
        setLaserMode(o.value("laser").toString());
    if (o.contains("color"))
        forceColor(o.value("color").toString());
    if (o.contains("hold"))
        setHold(o.value("hold").toBool());
    if (o.contains("blackout"))
        setBlackout(o.value("blackout").toBool());
    log("rules file: overrides applied");
}

QString DirectorManager::rulesStamp() const
{
    if (m_engine.rules().isEmpty())
        return QString();
    // Functions go by name here, so a project built again with the same
    // looks under other numbers still counts as the same rules
    const QByteArray data = QJsonDocument(rulesToJson()).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(16));
}

void DirectorManager::stampRulesFile()
{
    const QString stamp = rulesStamp();
    if (stamp.isEmpty() || stamp == m_projectStamp)
        return;
    m_projectStamp = stamp;

    QFile file(m_rulesFile);
    if (file.open(QIODevice::ReadOnly) == false)
        return;
    QString text = QString::fromUtf8(file.readAll());
    file.close();

    // Only the stamp changes: the rest stays as its author wrote it
    static const QRegularExpression re("(\"project\"\\s*:\\s*\")[0-9a-f]*(\")");
    if (re.match(text).hasMatch() == false)
        return;
    text.replace(re, "\\1" + stamp + "\\2");

    const QByteArray data = text.toUtf8();
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate) == false)
        return;
    file.write(data);
    file.close();
    m_rulesFileData = data;
}

QJsonArray DirectorManager::rulesToJson() const
{
    const QList<DirectorEngine::Rule> &rules = m_engine.rules();

    QJsonArray rulesArray;
    foreach (const DirectorEngine::Rule &rule, rules)
    {
        QJsonObject ro;
        ro.insert("name", rule.name);
        ro.insert("sections", QJsonArray::fromStringList(rule.sections));
        ro.insert("intensities", QJsonArray::fromStringList(rule.intensities));
        ro.insert("bars", rule.bars);

        QJsonObject layers;
        for (int l = 0; l < DirectorEngine::LayerCount; l++)
        {
            QJsonArray looks;
            foreach (const DirectorEngine::Bundle &bundle, rule.layers[l])
            {
                QJsonArray look;
                foreach (quint32 fid, bundle)
                {
                    Function *f = m_doc->function(fid);
                    if (f == nullptr)
                        continue;
                    // By name when the name says which Function it is
                    if (functionByName(f->name()) == fid)
                        look.append(f->name());
                    else
                        look.append(int(fid));
                }
                looks.append(look);
            }
            layers.insert(DirectorEngine::layerName(l), looks);
        }
        ro.insert("layers", layers);
        rulesArray.append(ro);
    }

    return rulesArray;
}

void DirectorManager::writeRulesFile()
{
    if (m_engine.rules().isEmpty())
        return;

    const QJsonArray rulesArray = rulesToJson();

    // What the file says already: leave it as its author wrote it
    if (rulesArray == m_rulesFileRules && QFile::exists(m_rulesFile))
        return;

    // The overrides stay as the file had them: the panel does not write
    // here, or every button press would rewrite the file under the editor
    if (m_rulesFileOverrides.isEmpty())
        m_rulesFileOverrides = overridesToJson();

    QJsonObject root;
    root.insert("project", m_projectStamp);
    root.insert("overrides", m_rulesFileOverrides);
    root.insert("rules", rulesArray);
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);

    // Do not touch a file that already says the same: an editor that has it
    // open would ask to reload it for nothing
    if (data == m_rulesFileData && QFile::exists(m_rulesFile))
        return;

    QFile file(m_rulesFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate) == false)
    {
        m_rulesFileError = tr("Cannot write %1").arg(m_rulesFile);
        return;
    }
    file.write(data);
    file.close();
    m_rulesFileData = data;
    m_rulesFileRules = rulesArray;
    log("rules file written: " + m_rulesFile);
}
