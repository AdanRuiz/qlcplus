/*
  Q Light Controller Plus
  directormanager.h

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

#ifndef DIRECTORMANAGER_H
#define DIRECTORMANAGER_H

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QVariantList>
#include <QStringList>
#include <QObject>
#include <QColor>
#include <QTimer>
#include <QPair>
#include <QList>

#include "directormanifest.h"
#include "directorengine.h"

class QXmlStreamReader;
class QXmlStreamWriter;
class Doc;

#define KXMLQLCDirector QStringLiteral("Director")

/**
 * DirectorManager runs a show on its own: it listens to the musical
 * information published by the VertexAV Link input plugin (beats, section,
 * intensity, palette), asks DirectorEngine what should be running and
 * starts/stops the Functions accordingly.
 *
 * It also provides a demo mode, which feeds the engine with a synthetic
 * song so that a show can be previewed in the 2D/3D views with no music,
 * no fixtures and no emitter.
 */
class DirectorManager final : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(DirectorManager)

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool hasRules READ hasRules NOTIFY rulesChanged)
    Q_PROPERTY(int rulesCount READ rulesCount NOTIFY rulesChanged)
    Q_PROPERTY(QVariantList rules READ rulesModel NOTIFY rulesChanged)
    Q_PROPERTY(QString rulesFile READ rulesFile NOTIFY rulesFileChanged)
    Q_PROPERTY(QString rulesFileError READ rulesFileError NOTIFY rulesFileChanged)

    Q_PROPERTY(QString source READ source NOTIFY musicChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY musicChanged)
    Q_PROPERTY(int bpm READ bpm NOTIFY musicChanged)
    Q_PROPERTY(QString section READ section NOTIFY musicChanged)
    Q_PROPERTY(QString intensity READ intensity NOTIFY musicChanged)
    Q_PROPERTY(QString detectedSection READ detectedSection NOTIFY musicChanged)
    Q_PROPERTY(QString detectedIntensity READ detectedIntensity NOTIFY musicChanged)
    Q_PROPERTY(QString mood READ mood NOTIFY musicChanged)
    Q_PROPERTY(int energy READ energy NOTIFY musicChanged)
    Q_PROPERTY(bool strobeOk READ strobeOk NOTIFY musicChanged)
    Q_PROPERTY(QVariantList paletteColors READ paletteColors NOTIFY musicChanged)
    Q_PROPERTY(int beatInBar READ beatInBar NOTIFY beat)

    Q_PROPERTY(QString ruleName READ ruleName NOTIFY lookChanged)
    Q_PROPERTY(QVariantList layers READ layersModel NOTIFY lookChanged)
    Q_PROPERTY(int barsInRule READ barsInRule NOTIFY lookChanged)

    Q_PROPERTY(QString forcedSection READ forcedSection NOTIFY overridesChanged)
    Q_PROPERTY(QString forcedIntensity READ forcedIntensity NOTIFY overridesChanged)
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY overridesChanged)
    Q_PROPERTY(bool strobeAllowed READ strobeAllowed WRITE setStrobeAllowed NOTIFY overridesChanged)
    Q_PROPERTY(bool blackout READ blackout WRITE setBlackout NOTIFY overridesChanged)
    Q_PROPERTY(bool strobeForced READ strobeForced WRITE setStrobeForced NOTIFY overridesChanged)
    Q_PROPERTY(QString forcedColor READ forcedColor NOTIFY overridesChanged)
    Q_PROPERTY(QString laserMode READ laserMode NOTIFY overridesChanged)
    Q_PROPERTY(bool hasLaser READ hasLaser NOTIFY rulesChanged)
    Q_PROPERTY(int activeOverrides READ activeOverrides NOTIFY overridesChanged)

    Q_PROPERTY(bool demoRunning READ demoRunning NOTIFY demoChanged)
    Q_PROPERTY(int demoBpm READ demoBpm WRITE setDemoBpm NOTIFY demoChanged)
    Q_PROPERTY(QVariantList demoSong READ demoSong CONSTANT)
    Q_PROPERTY(int demoStep READ demoStep NOTIFY demoChanged)

    Q_PROPERTY(QStringList sections READ sections CONSTANT)
    Q_PROPERTY(QStringList intensities READ intensities CONSTANT)

public:
    explicit DirectorManager(Doc *doc, QObject *parent = nullptr);
    ~DirectorManager() override;

    /*********************************************************************
     * State
     *********************************************************************/
public:
    bool enabled() const;
    void setEnabled(bool enable);

    bool hasRules() const;
    int rulesCount() const;
    QVariantList rulesModel() const;

    QString source() const;
    bool connected() const;
    int bpm() const;
    QString section() const;
    QString intensity() const;
    QString detectedSection() const;
    QString detectedIntensity() const;
    QString mood() const;
    int energy() const;
    bool strobeOk() const;
    QVariantList paletteColors() const;
    int beatInBar() const;

    QString ruleName() const;
    QVariantList layersModel() const;
    int barsInRule() const;

    QStringList sections() const;
    QStringList intensities() const;

    /*********************************************************************
     * Overrides
     *********************************************************************/
public:
    QString forcedSection() const;
    QString forcedIntensity() const;
    /** Colour the operator took from the music, as "#rrggbb". Empty = none */
    QString forcedColor() const;
    bool hold() const;
    void setHold(bool hold);
    bool strobeAllowed() const;
    void setStrobeAllowed(bool allowed);
    bool blackout() const;
    void setBlackout(bool blackout);
    bool strobeForced() const;
    void setStrobeForced(bool forced);
    int activeOverrides() const;

    /** Force a section/intensity. An empty string gives control back */
    Q_INVOKABLE void forceSection(const QString &section);
    Q_INVOKABLE void forceIntensity(const QString &intensity);

    /** What the operator decided about the lasers: "" = the rules decide,
     *  "off" = never, "on" = always while the Director is on */
    QString laserMode() const;
    Q_INVOKABLE void setLaserMode(const QString &mode);

    /** True when the project has Functions that only drive a laser */
    bool hasLaser() const;

    /** Paint the show in $color ("#rrggbb"): it replaces the primary and the
     *  secondary colour, whatever the music sends. An empty string gives the
     *  colours back to the music. */
    Q_INVOKABLE void forceColor(const QString &color);

    /** Keep on $layer what is running now, until released */
    Q_INVOKABLE void pinLayer(int layer, bool pin);
    Q_INVOKABLE void setLayerOff(int layer, bool off);

    /** Jump to the next candidate of every layer */
    Q_INVOKABLE void nextLook();

    /** Drop every override and give control back to the music */
    Q_INVOKABLE void releaseAll();

    /*********************************************************************
     * Rules
     *********************************************************************/
public:
    /** Build Director scenes, palettes and default rules from what the
     *  Stage Wizard has just generated */
    void setupFromManifest(const DirectorManifest &manifest, bool enable);

    /** Delete the rules and the Functions the Director created */
    Q_INVOKABLE void clearRules();

    /** Make $function go along with whatever runs on $layer: it is added to
     *  every look of that layer, and becomes a look of its own where the
     *  layer has none. This is how Functions made outside the wizard join
     *  the show, for what only one kind of fixture can do (a spinning head,
     *  a laser, a second light source).
     *
     *  $sections limits it to the rules for those sections; when empty it
     *  goes into every rule except the one for silence. The Function stays
     *  the user's: clearing the rules does not delete it.
     *
     *  @return the number of rules it was added to */
    Q_INVOKABLE int addCompanion(int layer, quint32 function, const QStringList &sections = QStringList());

    /** Add $functions as one more look for $layer to choose from, in the
     *  same rules addCompanion() would pick.
     *
     *  @return the number of rules it was added to */
    int addLook(int layer, const QList<quint32> &functions, const QStringList &sections = QStringList());

    /*********************************************************************
     * Demo mode
     *********************************************************************/
public:
    bool demoRunning() const;
    int demoBpm() const;
    void setDemoBpm(int bpm);
    QVariantList demoSong() const;
    int demoStep() const;

    Q_INVOKABLE void startDemo();
    Q_INVOKABLE void stopDemo();

    /** Jump to the given step of the demo song at the next bar */
    Q_INVOKABLE void demoJumpTo(int step);

    /*********************************************************************
     * Load & Save
     *********************************************************************/
public:
    /*********************************************************************
     * Rules file
     *********************************************************************/
public:
    /** Tell the Director which project it belongs to. The rules live next
     *  to it as "<project>.director.json", a text file that names the
     *  Functions of every look. It is read when the project is opened and
     *  again every time it is saved from an editor, while the show runs:
     *  tuning the behaviour needs no build and no restart.
     *
     *  The file also carries the overrides of the operator (forced section
     *  and intensity, hold, blackout, strobes, laser, colour). They are
     *  applied when that part of the file changes, so that what is done
     *  on the panel afterwards is not undone by an edit of the rules.
     *
     *  When the file does not exist yet it is written from the rules of
     *  the project. An empty $projectFile forgets the file.
     *
     *  The file remembers which rules of the project it was written for.
     *  A project that comes back with other rules (run through the wizard
     *  again, or built anew) is newer than the file: the file is kept as
     *  ".bak" and written again from the project, so nobody has to delete
     *  it by hand. Telling the same project file again, as saving does,
     *  makes the file belong to the rules that were just saved. */
    void setProjectFile(const QString &projectFile);

    QString rulesFile() const;
    /** What is wrong with the rules file, empty when it was read fine */
    QString rulesFileError() const;

private:
    bool readRulesFile();
    void writeRulesFile();
    void watchRulesFile();
    void stampRulesFile();
    QJsonArray rulesToJson() const;
    QString rulesStamp() const;
    QJsonObject overridesToJson() const;
    void applyOverrides(const QJsonObject &overrides);
    quint32 functionByName(const QString &name) const;

public:
    bool loadXML(QXmlStreamReader &root);
    bool saveXML(QXmlStreamWriter *doc) const;

signals:
    void enabledChanged();
    void rulesChanged();
    void rulesFileChanged();
    void musicChanged();
    void lookChanged();
    void overridesChanged();
    void demoChanged();
    void beat();

private slots:
    void slotInputValueChanged(quint32 universe, quint32 channel, uchar value, const QString &key);
    void slotFunctionRemoved(quint32 id);
    void slotDocClearing();
    void slotWatchdog();
    void slotDemoTick();
    void slotStartPending();
    void slotRulesFileChanged();
    void slotRulesEdited();

private:
    /** Entry point of the musical information, real or simulated */
    void handleChannel(quint32 channel, uchar value);
    void handleBar();
    void execute(const QList<DirectorEngine::Action> &actions);
    void evaluateNow();
    void stopStragglers();
    void applyBaseLevel();
    void applyPalette();
    qreal baseLevel() const;
    void reset();

    quint32 findOrCreatePalette(int type, const QString &name, const QVariantList &values, bool reuse);
    quint32 makeScene(const QString &name, quint32 groupId, const QList<quint32> &palettes);

    /** Build a slow tilt sweep for the fixtures of $groupId that can tilt but
     *  not pan (motorised bars). Returns invalidId() when there are none. */
    quint32 makeTiltSweep(const QString &groupName, quint32 groupId);

    /** True when $function is a Scene that drives laser channels and nothing
     *  else. Those are the Functions the laser override switches. A laser is
     *  recognised by the name of its channel: QLC+ has no channel type for it. */
    bool isLaserFunction(quint32 function) const;
    QList<quint32> laserFunctions() const;
    /** Start or stop the laser Functions to match the laser override */
    void applyLaserMode();

    /** Bring back to zero the effect channels that the Scenes in $stopped
     *  had raised and nothing running now wants.
     *
     *  Stopping a Scene is not enough for them: an effect channel keeps the
     *  last value it was given, so a laser, a spinning head or a strobe
     *  would go on after the look that asked for them has ended. */
    void releaseEffects(const QList<quint32> &stopped);

    /** Rules addCompanion() and addLook() apply to, as indexes */
    QList<int> rulesFor(const QStringList &sections) const;
    /** Replace the rules of a Director that may be running */
    void replaceRules(const QList<DirectorEngine::Rule> &rules);

    /** Append a line to the file named by the QLCPLUS_DIRECTOR_LOG
     *  environment variable. Does nothing when it is not set. */
    void log(const QString &text) const;
    void logLook() const;
    void pushDemoStep();

private:
    Doc *m_doc;
    DirectorEngine m_engine;
    bool m_enabled;

    /* Musical state */
    int m_bpm;
    int m_energy;
    QString m_mood;
    bool m_playing;
    int m_beatInBar;
    bool m_hasDownbeat;
    QElapsedTimer m_lastInput;
    QElapsedTimer m_lastBeat;
    bool m_gotInput;
    QTimer m_watchdog;

    /* Palette */
    static const int PaletteSlots = 3;
    QColor m_inColors[6];
    QColor m_appliedColors[PaletteSlots];
    quint32 m_slotPalette[PaletteSlots];
    /* Colour forced by the operator. Invalid = the music decides */
    QColor m_forcedColor;
    /* Laser override: "", "off" or "on" */
    QString m_laserMode;
    bool m_paletteDirty;

    /* Functions created by the Director, deleted when rules are rebuilt */
    QList<quint32> m_ownFunctions;

    /* Everything the Director has started and not yet seen stopped. A
     * Function asked to stop before its first timer tick is not "running"
     * yet, so the stop has to be checked again later. */
    QList<quint32> m_started;

    /* Starts deferred by one timer tick, to let the stops happen first */
    QList<QPair<int, quint32> > m_pendingStarts;

    /* Rules file, see setProjectFile() */
    QString m_rulesFile;
    QString m_rulesFileError;
    QByteArray m_rulesFileData;
    /* What identifies the rules of the project as it is on disk */
    QString m_projectStamp;
    QJsonArray m_rulesFileRules;
    QJsonObject m_rulesFileOverrides;
    QFileSystemWatcher m_rulesWatcher;
    QTimer m_rulesReload;
    bool m_readingRulesFile;

    /* Optional trace file, see log() */
    QString m_logPath;
    int m_beatCount;

    /* Demo */
    QTimer m_demoTimer;
    int m_demoBpm;
    int m_demoBeat;
    int m_demoStep;
    int m_demoBarsLeft;
    int m_demoJump;
};

#endif
