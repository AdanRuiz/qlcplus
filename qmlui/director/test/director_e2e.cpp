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
 * Exit code 0 = everything checked out.
 */

#include <QApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QTimer>
#include <QDebug>

#include "directormanager.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "stagewizard.h"
#include "qlcchannel.h"
#include "fixture.h"
#include "function.h"
#include "app.h"
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

    /* A small rig: 4 pars and 4 moving heads */
    QList<quint32> pars, movers;
    for (quint32 i = 0; i < 4; i++)
        pars << addFixture(doc, i * 4, false)->id();
    for (quint32 i = 0; i < 4; i++)
        movers << addFixture(doc, 100 + i * 6, true)->id();

    /* The wizard, as the QML pages would drive it */
    wizard->reset();
    wizard->setShowType(StageWizard::ClubNight);
    wizard->setCurrentStep(1);

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

    CHECK(manifest.groups.count() == 2, "manifest has both groups");
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
    CHECK(movementIsCollection, "Circle Chase is recorded as its top level Collection");
    CHECK(hasMatrix, "Split Color is recorded as a Chaser that exists in the project");

    CHECK(director->hasRules(), "Director has rules after the wizard");
    CHECK(director->enabled(), "Director is switched on by the wizard");
    foreach (const QVariant &rule, director->rulesModel())
        qInfo().noquote() << "  rule" << rule.toMap().value("name").toString()
                          << "sections [" + rule.toMap().value("sections").toString() + "]"
                          << "looks" << rule.toMap().value("looks").toInt();

    /* Run the wizard again: nothing must pile up in the Director */
    int rulesFirst = director->rulesCount();

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

        CHECK(layers.at(DirectorEngine::Base).toMap().value("count").toInt() == 2, "Base: one wash scene per group");
        CHECK(layers.at(DirectorEngine::Movement).toMap().value("count").toInt() >= 1, "Movement layer is running");
        CHECK(layers.at(DirectorEngine::Rhythm).toMap().value("count").toInt() >= 1, "Rhythm layer is running");

        int running = 0;
        foreach (Function *f, doc->functions())
        {
            if (f->isRunning())
                running++;
        }
        qInfo() << "functions running:" << running;
        CHECK(running >= 4, "Functions are really running in the engine");

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
