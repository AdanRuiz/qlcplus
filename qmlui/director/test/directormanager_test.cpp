/*
  Q Light Controller Plus
  directormanager_test.cpp

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
#include <QBuffer>
#include <QtTest>

#include "../directormanager.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "fixturegroup.h"
#include "stagewizard.h"
#include "mastertimer.h"
#include "qlcchannel.h"
#include "qlcpalette.h"
#include "fixture.h"
#include "scene.h"
#include "doc.h"

/**
 * Drives the real DirectorManager against a real Doc: default rules from a
 * manifest, the demo song, the overrides and the project file round trip.
 */
class DirectorManager_Test : public QObject
{
    Q_OBJECT

private:
    Doc *m_doc = nullptr;
    DirectorManager *m_director = nullptr;
    quint32 m_washGroup = 0;
    quint32 m_blinderGroup = 0;
    quint32 m_wave = 0;
    quint32 m_blinderHit = 0;

    static QLCChannel *makeChannel(const QString &name, QLCChannel::PrimaryColour colour)
    {
        QLCChannel *ch = new QLCChannel();
        ch->setName(name);
        ch->setGroup(QLCChannel::Intensity);
        ch->setColour(colour);
        return ch;
    }

    Fixture *addRGB(quint32 address)
    {
        QLCFixtureDef *def = new QLCFixtureDef();
        def->setManufacturer("Test");
        def->setModel("RGB");
        def->addChannel(makeChannel("Red", QLCChannel::Red));
        def->addChannel(makeChannel("Green", QLCChannel::Green));
        def->addChannel(makeChannel("Blue", QLCChannel::Blue));

        QLCFixtureMode *mode = new QLCFixtureMode(def);
        mode->setName("RGB");
        mode->insertChannel(def->channel("Red"), 0);
        mode->insertChannel(def->channel("Green"), 1);
        mode->insertChannel(def->channel("Blue"), 2);
        def->addMode(mode);

        Fixture *fxi = new Fixture(m_doc);
        fxi->setAddress(address);
        fxi->setUniverse(0);
        fxi->setFixtureDefinition(def, mode);
        m_doc->addFixture(fxi);
        return fxi;
    }

    Fixture *addDimmer(quint32 address)
    {
        Fixture *fxi = new Fixture(m_doc);
        fxi->setAddress(address);
        fxi->setUniverse(0);
        QLCFixtureDef *def = fxi->genericDimmerDef(1);
        QLCFixtureMode *mode = fxi->genericDimmerMode(def, 1);
        fxi->setFixtureDefinition(def, mode);
        m_doc->addFixture(fxi);
        return fxi;
    }

    quint32 addRawScene(const QString &name, quint32 fixture)
    {
        Scene *scene = new Scene(m_doc);
        scene->setName(name);
        scene->setValue(fixture, 0, 200);
        m_doc->addFunction(scene);
        return scene->id();
    }

    DirectorManifest manifest() const
    {
        DirectorManifest m;

        DirectorManifest::Group wash;
        wash.id = m_washGroup;
        wash.name = "Wash";
        wash.role = StageWizard::RoleKey;
        wash.hasColor = true;
        wash.hasDimmer = true;
        m.groups << wash;

        DirectorManifest::Group blinders;
        blinders.id = m_blinderGroup;
        blinders.name = "Blinders";
        blinders.role = StageWizard::RoleBlinder;
        blinders.hasDimmer = true;
        m.groups << blinders;

        DirectorManifest::Item wave;
        wave.function = m_wave;
        wave.group = m_washGroup;
        wave.effect = StageWizard::EffectMarquee;
        m.items << wave;

        DirectorManifest::Item hit;
        hit.function = m_blinderHit;
        hit.group = m_blinderGroup;
        hit.effect = StageWizard::EffectBlinderHit;
        m.items << hit;

        return m;
    }

    QList<quint32> running() const
    {
        QList<quint32> list;
        foreach (Function *f, m_doc->functions())
        {
            if (f->isRunning())
                list << f->id();
        }
        return list;
    }

    QString functionName(quint32 id) const
    {
        Function *f = m_doc->function(id);
        return f ? f->name() : QString();
    }

private slots:
    void initTestCase()
    {
        m_doc = new Doc(this);

        FixtureGroup *wash = new FixtureGroup(m_doc);
        wash->setName("Wash");
        m_doc->addFixtureGroup(wash);
        m_washGroup = wash->id();

        Fixture *first = nullptr;
        for (quint32 i = 0; i < 4; i++)
        {
            Fixture *fxi = addRGB(i * 3);
            if (first == nullptr)
                first = fxi;
            wash->assignFixture(fxi->id());
        }

        FixtureGroup *blinders = new FixtureGroup(m_doc);
        blinders->setName("Blinders");
        m_doc->addFixtureGroup(blinders);
        m_blinderGroup = blinders->id();
        Fixture *blinder = addDimmer(100);
        blinders->assignFixture(blinder->id());

        m_wave = addRawScene("Wash - Marquee", first->id());
        m_blinderHit = addRawScene("Blinders - Hit", blinder->id());

        m_doc->masterTimer()->start();
        m_director = new DirectorManager(m_doc, this);
    }

    void cleanupTestCase()
    {
        m_director->setEnabled(false);
        QTest::qWait(100);
        m_doc->masterTimer()->stop();
        delete m_director;
        delete m_doc;
    }

    void initial()
    {
        QVERIFY(m_director->enabled() == false);
        QVERIFY(m_director->hasRules() == false);
        QVERIFY(m_director->connected() == false);
        QCOMPARE(m_director->layersModel().count(), int(DirectorEngine::LayerCount));
        QCOMPARE(m_director->sections().count(), 10);
        QCOMPARE(m_director->intensities().count(), 4);
        QVERIFY(m_director->demoSong().count() > 4);
    }

    void defaultRulesFromManifest()
    {
        int functionsBefore = m_doc->functions().count();
        int palettesBefore = m_doc->palettes().count();

        m_director->setupFromManifest(manifest(), false);

        QVERIFY(m_director->hasRules());
        QVERIFY(m_director->rulesCount() >= 6);
        QVERIFY(m_director->enabled() == false);

        // Wash: primary + secondary. Blinders: nothing in the wash
        QCOMPARE(m_doc->functions().count(), functionsBefore + 2);
        // Three Director colours, dimmer full, shutter open, strobe fast
        QCOMPARE(m_doc->palettes().count(), palettesBefore + 6);

        // Running the wizard again must not duplicate anything
        m_director->setupFromManifest(manifest(), false);
        QCOMPARE(m_doc->functions().count(), functionsBefore + 2);
        QCOMPARE(m_doc->palettes().count(), palettesBefore + 6);
    }

    void demoRunsTheShow()
    {
        m_director->setDemoBpm(200);
        m_director->setEnabled(true);
        m_director->startDemo();
        QVERIFY(m_director->demoRunning());
        QVERIFY(m_director->connected());
        QCOMPARE(m_director->bpm(), 200);

        QTRY_COMPARE_WITH_TIMEOUT(m_director->section(), QString("intro"), 1000);
        QCOMPARE(m_director->intensity(), QString("low"));
        QCOMPARE(m_director->ruleName(), QString("Calm"));

        // The wash is up, in the colour the demo song asked for
        QTRY_VERIFY_WITH_TIMEOUT(running().count() == 1, 1000);
        QCOMPARE(functionName(running().first()), QString("Wash - Primary"));

        // Jump to the drop: wash + rhythm, and the blinder as accent
        m_director->demoJumpTo(3);
        QTRY_COMPARE_WITH_TIMEOUT(m_director->section(), QString("drop"), 3000);
        QCOMPARE(m_director->ruleName(), QString("Drop"));
        QVERIFY(m_director->strobeOk());
        QTRY_VERIFY_WITH_TIMEOUT(running().contains(m_wave), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(running().contains(m_blinderHit), 1000);
    }

    void overrides()
    {
        // Strobes blocked by the operator: the accent goes away at once
        m_director->setStrobeAllowed(false);
        QTRY_VERIFY_WITH_TIMEOUT(running().contains(m_blinderHit) == false, 1000);
        QCOMPARE(m_director->activeOverrides(), 1);
        m_director->setStrobeAllowed(true);

        // Rhythm layer off
        m_director->setLayerOff(DirectorEngine::Rhythm, true);
        QTRY_VERIFY_WITH_TIMEOUT(running().contains(m_wave) == false, 1000);
        m_director->setLayerOff(DirectorEngine::Rhythm, false);

        // Blackout stops everything, releasing it brings the look back
        m_director->setBlackout(true);
        QTRY_VERIFY_WITH_TIMEOUT(running().isEmpty(), 2000);
        m_director->setBlackout(false);
        QTRY_VERIFY_WITH_TIMEOUT(running().isEmpty() == false, 1000);

        // Forced section
        m_director->forceSection("verse");
        QTRY_COMPARE_WITH_TIMEOUT(m_director->ruleName(), QString("Verse"), 3000);
        QCOMPARE(m_director->section(), QString("verse"));
        m_director->releaseAll();
        QCOMPARE(m_director->activeOverrides(), 0);
        QCOMPARE(m_director->forcedSection(), QString());
    }

    void stopping()
    {
        m_director->stopDemo();
        QVERIFY(m_director->demoRunning() == false);

        m_director->setEnabled(false);
        QTRY_VERIFY_WITH_TIMEOUT(running().isEmpty(), 2000);
    }

    void saveAndLoad()
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QXmlStreamWriter writer(&buffer);
        writer.writeStartDocument();
        QVERIFY(m_director->saveXML(&writer));
        writer.writeEndDocument();
        buffer.close();

        int rules = m_director->rulesCount();
        QVERIFY(rules > 0);
        QVERIFY(buffer.data().contains("<Director"));

        DirectorManager other(m_doc);
        QVERIFY(other.hasRules() == false);

        QXmlStreamReader reader(buffer.data());
        QVERIFY(reader.readNextStartElement());
        QVERIFY(other.loadXML(reader));
        QCOMPARE(other.rulesCount(), rules);
        QCOMPARE(other.rulesModel(), m_director->rulesModel());
        other.setEnabled(false);
    }

    void deletedFunctionsLeaveTheRules()
    {
        QTRY_VERIFY_WITH_TIMEOUT(running().isEmpty(), 2000);
        m_doc->deleteFunction(m_wave);
        // Nothing may reference the deleted Function
        m_director->setEnabled(true);
        m_director->forceSection("drop");
        QTRY_COMPARE_WITH_TIMEOUT(m_director->ruleName(), QString("Drop"), 3000);
        QVERIFY(running().contains(m_wave) == false);
        m_director->releaseAll();
        m_director->setEnabled(false);
    }

    void clearRules()
    {
        int functions = m_doc->functions().count();
        m_director->clearRules();
        QVERIFY(m_director->hasRules() == false);
        // The two wash scenes created by the Director are gone
        QCOMPARE(m_doc->functions().count(), functions - 2);
    }
};

QTEST_GUILESS_MAIN(DirectorManager_Test)
#include "directormanager_test.moc"
