/*
  Q Light Controller Plus
  directorpanel_preview.cpp

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
 * Development tool: loads DirectorPanel.qml against a real DirectorManager
 * running the demo song, reports every QML warning and saves a screenshot.
 *
 *   directorpanel_preview <DirectorPanel.qml> <output.png>
 *
 * The QML file must sit next to UISettings.qml and its qmldir. It exits
 * with 1 when the panel does not load or prints a QML warning.
 */

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QTimer>
#include <QImage>
#include <QDebug>

#include "../directormanager.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "fixturegroup.h"
#include "stagewizard.h"
#include "mastertimer.h"
#include "qlcchannel.h"
#include "fixture.h"
#include "scene.h"
#include "doc.h"

static int s_qmlWarnings = 0;
static QtMessageHandler s_previous = nullptr;

static void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (type == QtDebugMsg)
        return;
    if (msg.contains(".qml"))
        s_qmlWarnings++;
    if (s_previous)
        s_previous(type, ctx, msg);
}

static Fixture *addRGB(Doc *doc, quint32 address)
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel("RGB");
    const char *names[3] = { "Red", "Green", "Blue" };
    const QLCChannel::PrimaryColour colours[3] = { QLCChannel::Red, QLCChannel::Green, QLCChannel::Blue };
    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("RGB");
    for (int i = 0; i < 3; i++)
    {
        QLCChannel *ch = new QLCChannel();
        ch->setName(names[i]);
        ch->setGroup(QLCChannel::Intensity);
        ch->setColour(colours[i]);
        def->addChannel(ch);
        mode->insertChannel(ch, i);
    }
    def->addMode(mode);

    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(address);
    fxi->setUniverse(0);
    fxi->setFixtureDefinition(def, mode);
    doc->addFixture(fxi);
    return fxi;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    if (argc < 3)
    {
        qWarning("Usage: directorpanel_preview <DirectorPanel.qml> <output.png>");
        return 2;
    }

    s_previous = qInstallMessageHandler(messageHandler);

    Doc doc(&app);
    DirectorManifest manifest;

    const char *groupNames[2] = { "Front wash", "Back wash" };
    for (int g = 0; g < 2; g++)
    {
        FixtureGroup *group = new FixtureGroup(&doc);
        group->setName(groupNames[g]);
        doc.addFixtureGroup(group);

        Fixture *first = nullptr;
        for (quint32 i = 0; i < 4; i++)
        {
            Fixture *fxi = addRGB(&doc, quint32(g) * 100 + i * 3);
            if (first == nullptr)
                first = fxi;
            group->assignFixture(fxi->id());
        }

        DirectorManifest::Group mg;
        mg.id = group->id();
        mg.name = group->name();
        mg.role = g == 0 ? StageWizard::RoleKey : StageWizard::RoleBack;
        mg.hasColor = true;
        mg.hasDimmer = true;
        manifest.groups << mg;

        // Stand-ins for what the wizard would generate
        const int effects[2] = { StageWizard::EffectWave, StageWizard::EffectMarquee };
        const char *effectNames[2] = { "Wave", "Marquee" };
        for (int e = 0; e < 2; e++)
        {
            Scene *scene = new Scene(&doc);
            scene->setName(QString("%1 - %2").arg(group->name()).arg(effectNames[e]));
            scene->setValue(first->id(), 0, 200);
            doc.addFunction(scene);

            DirectorManifest::Item item;
            item.function = scene->id();
            item.group = group->id();
            item.effect = effects[e];
            manifest.items << item;
        }
    }

    doc.masterTimer()->start();

    DirectorManager director(&doc);
    director.setupFromManifest(manifest, true);
    director.setDemoBpm(180);
    director.startDemo();

    QQuickView view;
    view.setColor(QColor("#404040"));
    view.rootContext()->setContextProperty("director", &director);
    view.rootContext()->setContextProperty("screenPixelDensity", 4.0);
    view.setResizeMode(QQuickView::SizeViewToRootObject);
    view.setSource(QUrl::fromLocalFile(argv[1]));

    if (view.status() != QQuickView::Ready)
    {
        foreach (const QQmlError &err, view.errors())
            qWarning() << err.toString();
        return 1;
    }
    view.show();

    // Jump to the drop and add a couple of overrides, so the screenshot
    // shows a busy panel
    QTimer::singleShot(1500, &app, [&director]()
    {
        director.demoJumpTo(3);
    });
    QTimer::singleShot(4000, &app, [&director]()
    {
        director.forceIntensity("high");
        director.pinLayer(DirectorEngine::Rhythm, true);
    });
    QTimer::singleShot(5000, &app, [&]()
    {
        QImage img = view.grabWindow();
        qWarning() << "Saved" << argv[2] << img.size() << img.save(argv[2]);
        director.stopDemo();
        director.setEnabled(false);
        QTimer::singleShot(500, &app, &QCoreApplication::quit);
    });

    int ret = app.exec();
    doc.masterTimer()->stop();

    if (s_qmlWarnings > 0)
    {
        qWarning() << s_qmlWarnings << "QML warning(s)";
        return 1;
    }
    return ret;
}
