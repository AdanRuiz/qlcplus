/*
  Q Light Controller Plus
  director_e2e.cpp

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

/*
 * End to end check, built in place of main.cpp against the whole QLC+ 5
 * application: patches a small rig, runs the real Stage Wizard with the
 * Director option, starts the demo song and verifies that the Director
 * runs wizard-generated Functions on its layers. Also opens the Director
 * panel through the same Loader the toolbar button uses.
 *
 * Environment:
 *   DIRECTOR_E2E_BAR_DEF  path of a .qxf of a motorised bar (tilt, no pan);
 *                         two of them are added to the rig as a third group
 *   DIRECTOR_E2E_NOVA_DEF path of the .qxf of the SteelPro Nova; two of them
 *                         are added as a group of their own, with the
 *                         Functions for what only a Nova does (see addNovaShow)
 *   DIRECTOR_E2E_PAR_DEF  path of the .qxf of an LED par; two of them are
 *                         added as a group of their own
 *   DIRECTOR_E2E_RIG      "bars" = patch only what the variables above name
 *   DIRECTOR_E2E_SAVE     save the generated project to this path
 *
 * Exit code 0 = everything checked out.
 */

#include <QApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QTimer>
#include <QDebug>
#include <QFile>

#include "directormanager.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "chaserstep.h"
#include "stagewizard.h"
#include "chaser.h"
#include "scene.h"
#include "efx.h"
#include "qlcchannel.h"
#include "fixture.h"
#include "function.h"
#include "app.h"
#include "monitorproperties.h"
#include "doc.h"

static int s_failures = 0;

#define CHECK(cond, what) \
    do { \
        if (cond) { qInfo().noquote() << "ok  :" << what; } \
        else { qInfo().noquote() << "FAIL:" << what; s_failures++; } \
    } while (0)

static QLCChannel *channel(const QString &name, QLCChannel::Group group,
                           QLCChannel::PrimaryColour colour = QLCChannel::NoColour)
{
    QLCChannel *ch = new QLCChannel();
    ch->setName(name);
    ch->setGroup(group);
    ch->setColour(colour);
    return ch;
}

static Fixture *addFixture(Doc *doc, quint32 address, bool mover)
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel(mover ? "Mover" : "Par");

    QList<QLCChannel *> channels;
    channels << channel("Dimmer", QLCChannel::Intensity)
             << channel("Red", QLCChannel::Intensity, QLCChannel::Red)
             << channel("Green", QLCChannel::Intensity, QLCChannel::Green)
             << channel("Blue", QLCChannel::Intensity, QLCChannel::Blue);
    if (mover)
        channels << channel("Pan", QLCChannel::Pan) << channel("Tilt", QLCChannel::Tilt);

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("Mode");
    for (int i = 0; i < channels.count(); i++)
    {
        def->addChannel(channels.at(i));
        mode->insertChannel(channels.at(i), i);
    }
    def->addMode(mode);

    Fixture *fxi = new Fixture(doc);
    fxi->setName(QString("%1 %2").arg(def->model()).arg(address));
    fxi->setAddress(address);
    fxi->setUniverse(0);
    fxi->setFixtureDefinition(def, mode);
    doc->addFixture(fxi);
    return fxi;
}

/** Two fixtures of the definition in $path, as $name 1 and $name 2 */
static QList<quint32> addPair(Doc *doc, const QString &path, const QString &name, quint32 base)
{
    QList<quint32> ids;
    QLCFixtureDef *def = new QLCFixtureDef();
    if (def->loadXML(path) != QFile::NoError || def->modes().isEmpty())
        return ids;

    QLCFixtureMode *mode = def->modes().first();
    for (quint32 i = 0; i < 2; i++)
    {
        Fixture *fxi = new Fixture(doc);
        fxi->setName(QString("%1 %2").arg(name).arg(i + 1));
        fxi->setAddress(base + i * quint32(mode->channels().count()));
        fxi->setUniverse(0);
        fxi->setFixtureDefinition(def, mode);
        doc->addFixture(fxi);
        ids << fxi->id();
    }
    return ids;
}

static quint32 channelByName(const Fixture *fxi, const QString &name)
{
    for (quint32 ch = 0; ch < fxi->channels(); ch++)
    {
        if (fxi->channel(ch) != nullptr && fxi->channel(ch)->name() == name)
            return ch;
    }
    return QLCChannel::invalid();
}

static quint32 functionByName(Doc *doc, const QString &name)
{
    foreach (Function *f, doc->functions())
    {
        if (f->name() == name)
            return f->id();
    }
    return Function::invalidId();
}

typedef QPair<QString, uchar> NovaValue;

/** A Scene that sets the channels named in $values on each Nova */
static quint32 novaScene(Doc *doc, const QList<quint32> &novas, const QString &name,
                         const QList<NovaValue> &values)
{
    Scene *scene = new Scene(doc);
    scene->setName(name);
    scene->setPath("Nova");
    foreach (quint32 id, novas)
    {
        Fixture *fxi = doc->fixture(id);
        scene->addFixture(id);
        for (int v = 0; v < values.count(); v++)
        {
            quint32 ch = channelByName(fxi, values.at(v).first);
            if (ch != QLCChannel::invalid())
                scene->setValue(id, ch, values.at(v).second);
        }
    }
    doc->addFunction(scene);
    return scene->id();
}

/**
 * What only a Nova does, which the wizard knows nothing about. A Nova is a
 * moving head whose head also spins on its own axis, with a green laser
 * through the front and two yellow LEDs in the head that are its strobe,
 * separate from the beams:
 *
 *  - the head spins and the laser stays on whenever there is movement
 *  - the yellow LEDs strobe on their own in the loud sections
 *  - one more movement to choose from: a 5 second sweep of about 80 degrees
 *    of pan, the tilt rising at the centre and dropping at both ends
 */
static void addNovaShow(Doc *doc, DirectorManager *director, const QList<quint32> &novas)
{
    // 128 - 191 turns from fast to slow: the middle is a steady spin
    quint32 spin = novaScene(doc, novas, "Novas - Spin and laser",
                             QList<NovaValue>() << NovaValue("Head rotation", 160) << NovaValue("Green laser", 255));

    // The fixture does the flashing: the strobe channel only sets how fast
    quint32 strobe = novaScene(doc, novas, "Novas - Yellow strobe",
                               QList<NovaValue>() << NovaValue("Gold light", 255) << NovaValue("Yellow strobe", 150));

    // x = cos(t), y = -cos(2t): one pan sweep and back per cycle, the tilt
    // highest when crossing the centre
    EFX *arc = new EFX(doc);
    arc->setName("Novas - Arc sweep");
    arc->setPath("Nova");
    arc->setAlgorithm(EFX::Lissajous);
    arc->setXFrequency(1);
    arc->setYFrequency(2);
    arc->setXPhase(0);
    arc->setYPhase(180);
    arc->setIsRelative(true);
    arc->setWidth(19);  // 80 of 540 degrees of pan
    arc->setHeight(7);  // 14 of 270 degrees of tilt
    arc->setDuration(5000);
    arc->setFadeInSpeed(1000);
    arc->setPropagationMode(EFX::Parallel);
    foreach (quint32 id, novas)
        arc->addFixture(id, 0);
    doc->addFunction(arc);

    // The sweep rides on the centre position of the wizard, and the bars keep
    // sweeping their tilt next to it
    QList<quint32> look;
    look << functionByName(doc, QString::fromUtf8("Novas – Position Centre")) << arc->id()
         << functionByName(doc, "Bars - Tilt sweep");
    look.removeAll(Function::invalidId());

    CHECK(director->addLook(DirectorEngine::Movement, look) > 0, "Nova arc sweep is one more movement");
    CHECK(director->addCompanion(DirectorEngine::Movement, spin) > 0, "Nova spin and laser go along with the movement");
    CHECK(director->addCompanion(DirectorEngine::Rhythm, strobe,
                                 QStringList() << "chorus" << "build" << "drop") > 0,
          "Nova yellow strobe goes along with the rhythm of the loud sections");
}

template <class T>
static T *contextObject(App &app, const char *name)
{
    return qobject_cast<T *>(app.rootContext()->contextProperty(name).value<QObject *>());
}

int main(int argc, char *argv[])
{
    QApplication qapp(argc, argv);
    QApplication::setOrganizationName("qlcplus");
    QApplication::setOrganizationDomain("qlcplus.org");
    QApplication::setApplicationName("Q Light Controller Plus Director test");

    App app;
    app.startup();
    app.show();

    Doc *doc = app.doc();
    StageWizard *wizard = contextObject<StageWizard>(app, "stageWizard");
    DirectorManager *director = contextObject<DirectorManager>(app, "director");

    CHECK(wizard != nullptr, "stageWizard context property");
    CHECK(director != nullptr, "director context property");
    if (wizard == nullptr || director == nullptr)
        return 1;

    const QString barDef = qEnvironmentVariable("DIRECTOR_E2E_BAR_DEF");
    const QString novaDef = qEnvironmentVariable("DIRECTOR_E2E_NOVA_DEF");
    const QString parDef = qEnvironmentVariable("DIRECTOR_E2E_PAR_DEF");
    const QString savePath = qEnvironmentVariable("DIRECTOR_E2E_SAVE");
    const bool barsOnly = qEnvironmentVariable("DIRECTOR_E2E_RIG") == "bars";

    /* A small rig: 4 pars and 4 moving heads */
    QList<quint32> pars, movers, bars, novas, ledPars;
    if (barsOnly == false)
    {
        for (quint32 i = 0; i < 4; i++)
            pars << addFixture(doc, i * 4, false)->id();
        for (quint32 i = 0; i < 4; i++)
            movers << addFixture(doc, 100 + i * 6, true)->id();
    }

    /* Optionally, two real motorised bars: they tilt but do not pan */
    if (barDef.isEmpty() == false)
    {
        QLCFixtureDef *def = new QLCFixtureDef();
        if (def->loadXML(barDef) == QFile::NoError && def->modes().isEmpty() == false)
        {
            QLCFixtureMode *mode = def->modes().first();
            quint32 base = barsOnly ? 0 : 200;
            for (quint32 i = 0; i < 2; i++)
            {
                Fixture *fxi = new Fixture(doc);
                fxi->setName(QString("Bar %1").arg(i + 1));
                fxi->setAddress(base + i * quint32(mode->channels().count()));
                fxi->setUniverse(0);
                fxi->setFixtureDefinition(def, mode);
                doc->addFixture(fxi);
                bars << fxi->id();
            }
        }
        CHECK(bars.count() == 2, "motorised bar definition loaded: " + barDef);
    }

    /* Optionally, two SteelPro Novas after everything else */
    if (novaDef.isEmpty() == false)
    {
        novas = addPair(doc, novaDef, "Nova", 300);
        CHECK(novas.count() == 2, "Nova definition loaded: " + novaDef);
    }

    /* Optionally, two LED pars */
    if (parDef.isEmpty() == false)
    {
        ledPars = addPair(doc, parDef, "Par", 400);
        CHECK(ledPars.count() == 2, "LED par definition loaded: " + parDef);
    }

    const int expectedGroups = (barsOnly ? 0 : 2) + (bars.isEmpty() ? 0 : 1) + (novas.isEmpty() ? 0 : 1) +
                               (ledPars.isEmpty() ? 0 : 1);

    /* The wizard, as the QML pages would drive it */
    wizard->reset();
    wizard->setShowType(StageWizard::ClubNight);
    wizard->setCurrentStep(1);

    if (barsOnly == false)
    {
        int parBox = wizard->addGroup();
        wizard->renameGroup(parBox, "Pars");
        foreach (quint32 id, pars)
            wizard->assignFixtureToGroup(parBox, id);
        wizard->setGroupSelected(parBox, true);

        int moverBox = wizard->addGroup();
        wizard->renameGroup(moverBox, "Movers");
        foreach (quint32 id, movers)
            wizard->assignFixtureToGroup(moverBox, id);
        wizard->setGroupSelected(moverBox, true);
    }

    if (bars.isEmpty() == false)
    {
        int barBox = wizard->addGroup();
        wizard->renameGroup(barBox, "Bars");
        foreach (quint32 id, bars)
            wizard->assignFixtureToGroup(barBox, id);
        wizard->setGroupSelected(barBox, true);
    }

    if (novas.isEmpty() == false)
    {
        int novaBox = wizard->addGroup();
        wizard->renameGroup(novaBox, "Novas");
        foreach (quint32 id, novas)
            wizard->assignFixtureToGroup(novaBox, id);
        wizard->setGroupSelected(novaBox, true);
    }

    if (ledPars.isEmpty() == false)
    {
        int parBox = wizard->addGroup();
        wizard->renameGroup(parBox, "LED Pars");
        foreach (quint32 id, ledPars)
            wizard->assignFixtureToGroup(parBox, id);
        wizard->setGroupSelected(parBox, true);
    }

    wizard->autoDetectRoles();
    CHECK(wizard->canGoNext(), "wizard has selected groups");
    wizard->setCurrentStep(2);
    wizard->setCurrentStep(3);
    wizard->setCurrentStep(4);
    wizard->setCurrentStep(5);

    CHECK(wizard->useDirector(), "Director option is on by default");
    CHECK(director->hasRules() == false, "no rules before generating");

    int functionsBefore = doc->functions().count();
    wizard->generate();

    const DirectorManifest &manifest = wizard->manifest();
    qInfo() << "functions generated:" << doc->functions().count() - functionsBefore
            << "manifest groups:" << manifest.groups.count()
            << "manifest items:" << manifest.items.count();

    foreach (const DirectorManifest::Group &grp, manifest.groups)
        qInfo().noquote() << "  group" << grp.name << "role" << grp.role << "movement" << grp.hasMovement
                          << "color" << grp.hasColor << "shutter" << grp.hasShutter << "dimmer" << grp.hasDimmer;
    foreach (const DirectorManifest::Item &item, manifest.items)
    {
        Function *f = doc->function(item.function);
        qInfo().noquote() << "  item effect" << item.effect << "->"
                          << (f ? f->name() + " [" + Function::typeToString(f->type()) + "]" : QString("MISSING"));
    }

    CHECK(manifest.groups.count() == expectedGroups, "manifest has every group");
    CHECK(manifest.items.isEmpty() == false, "manifest has wizard effects");

    bool allExist = true, movementIsCollection = false, hasMatrix = false;
    foreach (const DirectorManifest::Item &item, manifest.items)
    {
        Function *f = doc->function(item.function);
        if (f == nullptr)
        {
            allExist = false;
            continue;
        }
        if (item.effect == StageWizard::EffectCircleChase && f->type() == Function::CollectionType)
            movementIsCollection = true;
        if (item.effect == StageWizard::EffectSplitColor && f->type() == Function::ChaserType)
            hasMatrix = true;
    }
    CHECK(allExist, "every manifest item points to an existing Function");
    if (barsOnly == false)
        CHECK(movementIsCollection, "Circle Chase is recorded as its top level Collection");
    CHECK(hasMatrix, "Split Color is recorded as a Chaser that exists in the project");

    CHECK(director->hasRules(), "Director has rules after the wizard");
    CHECK(director->enabled(), "Director is switched on by the wizard");

    if (novas.isEmpty() == false)
    {
        addNovaShow(doc, director, novas);

        /* As on the real rig, which stands behind the dance floor: everything
         * is on the rear truss. The bars share its middle, tipped towards the
         * front, and a Nova hangs under each of its ends. The wizard hangs
         * both groups from the same span of the front truss, one on top of
         * the other.
         *
         * Z runs from the rear (0) to the front, and a unit loads head down:
         * 180 degrees about X stands it upright, and anything above that
         * tips it towards the audience. */
        MonitorProperties *monitor = doc->monitorProperties();
        const QVector3D grid = monitor->gridSize() * 1000.0f;

        const float novaX[2] = { 300.0f, grid.x() - 550.0f };
        for (int i = 0; i < novas.count() && i < 2; i++)
        {
            monitor->setFixturePosition(novas.at(i), 0, 0, QVector3D(novaX[i], 3850.0f, -100.0f));
            monitor->setFixtureRotation(novas.at(i), 0, 0, QVector3D(0.0f, 0.0f, 0.0f));
        }

        for (int i = 0; i < bars.count(); i++)
        {
            QVector3D pos = monitor->fixturePosition(bars.at(i), 0, 0);
            pos.setZ(0.0f);
            monitor->setFixturePosition(bars.at(i), 0, 0, pos);
            monitor->setFixtureRotation(bars.at(i), 0, 0, QVector3D(260.0f, 0.0f, 0.0f));
        }
    }

    if (ledPars.isEmpty() == false)
    {
        /* As on the real rig: the pars hang under the rear truss, between
         * each Nova and the bars, tipped towards the front. A hanging unit
         * points straight down, and a positive rotation about X tips it
         * towards the audience. */
        MonitorProperties *monitor = doc->monitorProperties();
        const QVector3D grid = monitor->gridSize() * 1000.0f;
        const float parX[2] = { 900.0f, grid.x() - 1150.0f };
        for (int i = 0; i < ledPars.count() && i < 2; i++)
        {
            monitor->setFixturePosition(ledPars.at(i), 0, 0, QVector3D(parX[i], 3700.0f, 0.0f));
            monitor->setFixtureRotation(ledPars.at(i), 0, 0, QVector3D(35.0f, 0.0f, 0.0f));
        }
    }

    foreach (const QVariant &rule, director->rulesModel())
        qInfo().noquote() << "  rule" << rule.toMap().value("name").toString()
                          << "sections [" + rule.toMap().value("sections").toString() + "]"
                          << "looks" << rule.toMap().value("looks").toInt();

    int rulesFirst = director->rulesCount();

    if (savePath.isEmpty() == false)
    {
        QFile::remove(savePath);
        CHECK(app.saveWorkspace(savePath), "project saved to " + savePath);
        CHECK(QFile::exists(savePath), "project file exists");
    }

    /* The panel, through the toolbar button's Loader */
    QQuickItem *root = app.rootObject();
    CHECK(root != nullptr, "main view loaded");
    if (root != nullptr)
    {
        root->setProperty("directorVisible", true);
        QQuickItem *loader = root->findChild<QQuickItem *>("directorOverlay");
        Q_UNUSED(loader)
    }

    director->setDemoBpm(200);
    director->startDemo();
    director->demoJumpTo(3); // drop

    QTimer::singleShot(4500, &qapp, [&]()
    {
        CHECK(director->section() == "drop", "demo reached the drop");
        CHECK(director->ruleName() == "Drop", "Drop rule is active");

        QVariantList layers = director->layersModel();
        for (int l = 0; l < layers.count(); l++)
            qInfo().noquote() << "  layer" << layers.at(l).toMap().value("name").toString() << ":"
                              << layers.at(l).toMap().value("functions").toString();

        CHECK(layers.at(DirectorEngine::Base).toMap().value("count").toInt() == expectedGroups, "Base: one wash scene per group");
        if (bars.isEmpty() == false)
            CHECK(layers.at(DirectorEngine::Movement).toMap().value("functions").toString().contains("Tilt sweep"),
                  "the bars get a tilt sweep on the Movement layer");
        CHECK(layers.at(DirectorEngine::Movement).toMap().value("count").toInt() >= 1, "Movement layer is running");
        if (novas.isEmpty() == false)
        {
            CHECK(layers.at(DirectorEngine::Movement).toMap().value("functions").toString().contains("Spin and laser"),
                  "the Novas spin with the laser on while there is movement");
            CHECK(layers.at(DirectorEngine::Rhythm).toMap().value("functions").toString().contains("Yellow strobe"),
                  "the Novas strobe their yellow LEDs in the drop");
        }
        CHECK(layers.at(DirectorEngine::Rhythm).toMap().value("count").toInt() >= 1, "Rhythm layer is running");

        int running = 0;
        foreach (Function *f, doc->functions())
        {
            if (f->isRunning())
                running++;
        }
        qInfo() << "functions running:" << running;
        CHECK(running >= 3, "Functions are really running in the engine");

        // The panel must have been created by the Loader in MainView
        bool panelFound = false;
        foreach (QQuickItem *item, app.rootObject()->findChildren<QQuickItem *>())
        {
            if (QString(item->metaObject()->className()).startsWith("DirectorPanel"))
                panelFound = true;
        }
        CHECK(panelFound, "DirectorPanel.qml loaded inside the main view");

        CHECK(director->rulesCount() == rulesFirst, "rules are stable while running");

        director->stopDemo();
        director->setEnabled(false);
    });

    QTimer::singleShot(5500, &qapp, [&]()
    {
        int running = 0;
        foreach (Function *f, doc->functions())
        {
            if (f->isRunning())
                running++;
        }
        CHECK(running == 0, "switching the Director off stops everything");

        // Second wizard run on the same groups
        int functions = doc->functions().count();
        Q_UNUSED(functions)
        director->clearRules();
        CHECK(director->hasRules() == false, "rules cleared");

        qInfo() << (s_failures == 0 ? "E2E PASSED" : "E2E FAILED") << "failures:" << s_failures;
        QCoreApplication::exit(s_failures == 0 ? 0 : 1);
    });

    return qapp.exec();
}
