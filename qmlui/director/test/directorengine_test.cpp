/*
  Q Light Controller Plus
  directorengine_test.cpp

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

#include <QtTest>

#include "../directorengine.h"

typedef DirectorEngine::Bundle Bundle;
typedef DirectorEngine::Action Action;

class DirectorEngine_Test : public QObject
{
    Q_OBJECT

private:
    /** Default rule (1 on Base) plus a Drop rule using the four layers */
    static QList<DirectorEngine::Rule> sampleRules()
    {
        DirectorEngine::Rule def;
        def.name = "Default";
        def.bars = 2;
        def.layers[DirectorEngine::Base] << (Bundle() << 1) << (Bundle() << 2);
        def.layers[DirectorEngine::Movement] << (Bundle() << 10);

        DirectorEngine::Rule drop;
        drop.name = "Drop";
        drop.sections << "drop";
        drop.bars = 0;
        drop.layers[DirectorEngine::Base] << (Bundle() << 3 << 4);
        drop.layers[DirectorEngine::Movement] << (Bundle() << 10);
        drop.layers[DirectorEngine::Rhythm] << (Bundle() << 20);
        drop.layers[DirectorEngine::Accent] << (Bundle() << 30);

        DirectorEngine::Rule dropPeak;
        dropPeak.name = "Drop peak";
        dropPeak.sections << "drop";
        dropPeak.intensities << "peak";
        dropPeak.bars = 0;
        dropPeak.layers[DirectorEngine::Base] << (Bundle() << 5);

        return QList<DirectorEngine::Rule>() << def << drop << dropPeak;
    }

    static QList<quint32> ids(const QList<Action> &actions, Action::Type type)
    {
        QList<quint32> list;
        foreach (const Action &a, actions)
        {
            if (a.type == type)
                list << a.function;
        }
        std::sort(list.begin(), list.end());
        return list;
    }

private slots:
    void noRules()
    {
        DirectorEngine e;
        QVERIFY(e.bar().isEmpty());
        QCOMPARE(e.activeRule(), -1);
        QCOMPARE(e.activeRuleName(), QString());
    }

    void mostSpecificRuleWins()
    {
        DirectorEngine e;
        e.setRules(sampleRules());

        e.setSection("verse");
        e.bar();
        QCOMPARE(e.activeRuleName(), QString("Default"));

        e.setSection("drop");
        e.setIntensity("high");
        e.bar();
        QCOMPARE(e.activeRuleName(), QString("Drop"));

        e.setIntensity("peak");
        e.bar();
        QCOMPARE(e.activeRuleName(), QString("Drop peak"));
    }

    void changeOnlyWhatDiffers()
    {
        DirectorEngine e;
        e.setRules(sampleRules());
        e.setSection("verse");

        QList<Action> first = e.bar();
        QCOMPARE(ids(first, Action::Start), QList<quint32>() << 1 << 10);
        QVERIFY(ids(first, Action::Stop).isEmpty());

        // Same state, next bar: nothing to do
        QVERIFY(e.bar().isEmpty());

        e.setSection("drop");
        QVERIFY(e.isDirty());
        QList<Action> second = e.bar();
        QVERIFY(e.isDirty() == false);

        // Function 10 is in both looks on the same layer: it must keep running
        QVERIFY(ids(second, Action::Stop).contains(10) == false);
        QVERIFY(ids(second, Action::Start).contains(10) == false);
        QVERIFY(ids(second, Action::Start).contains(3));
        QVERIFY(ids(second, Action::Start).contains(4));
        QVERIFY(ids(second, Action::Start).contains(20));
        // No strobe permission from the music yet
        QVERIFY(ids(second, Action::Start).contains(30) == false);
    }

    void rotation()
    {
        DirectorEngine e;
        e.setRules(sampleRules());
        e.setSection("verse");

        e.bar();
        QCOMPARE(e.layerBundle(DirectorEngine::Base), Bundle() << 1);
        e.bar();
        QCOMPARE(e.layerBundle(DirectorEngine::Base), Bundle() << 1);
        // Default rotates every 2 bars
        QList<Action> actions = e.bar();
        QCOMPARE(e.layerBundle(DirectorEngine::Base), Bundle() << 2);
        QCOMPARE(ids(actions, Action::Stop), QList<quint32>() << 1);
        QCOMPARE(ids(actions, Action::Start), QList<quint32>() << 2);

        e.requestNext();
        e.evaluate();
        QCOMPARE(e.layerBundle(DirectorEngine::Base), Bundle() << 1);
    }

    void accentNeedsPermission()
    {
        DirectorEngine e;
        e.setRules(sampleRules());
        e.setSection("drop");
        e.bar();
        QVERIFY(e.layerBundle(DirectorEngine::Accent).isEmpty());

        e.setStrobeOk(true);
        e.evaluate();
        QCOMPARE(e.layerBundle(DirectorEngine::Accent), Bundle() << 30);

        // The operator's veto wins over the music
        e.setStrobeAllowed(false);
        QList<Action> actions = e.evaluate();
        QVERIFY(e.layerBundle(DirectorEngine::Accent).isEmpty());
        QCOMPARE(ids(actions, Action::Stop), QList<quint32>() << 30);

        // ... and over the operator's own strobe button
        e.setStrobeForced(true);
        e.evaluate();
        QVERIFY(e.layerBundle(DirectorEngine::Accent).isEmpty());
    }

    void accentIsAHit()
    {
        DirectorEngine::Rule rule;
        rule.name = "Drop";
        rule.bars = 4;
        rule.layers[DirectorEngine::Base] << (Bundle() << 1);
        rule.layers[DirectorEngine::Accent] << (Bundle() << 30);

        DirectorEngine e;
        e.setRules(QList<DirectorEngine::Rule>() << rule);
        e.setStrobeOk(true);

        // First bar of the rule: hit. Then three bars without, then again.
        QList<bool> on;
        for (int bar = 0; bar < 9; bar++)
        {
            e.bar();
            on << (e.layerBundle(DirectorEngine::Accent).isEmpty() == false);
        }
        QCOMPARE(on, QList<bool>() << true << false << false << false
                                   << true << false << false << false << true);
        // The wash never drops meanwhile
        QCOMPARE(e.layerBundle(DirectorEngine::Base), Bundle() << 1);
    }

    void forcedStrobe()
    {
        DirectorEngine e;
        e.setRules(sampleRules());
        e.setSection("verse");
        e.bar();
        QVERIFY(e.layerBundle(DirectorEngine::Accent).isEmpty());

        // The Default rule has no accent: the first one available is used
        e.setStrobeForced(true);
        e.evaluate();
        QCOMPARE(e.layerBundle(DirectorEngine::Accent), Bundle() << 30);

        e.setStrobeForced(false);
        e.evaluate();
        QVERIFY(e.layerBundle(DirectorEngine::Accent).isEmpty());
    }

    void pinAndOff()
    {
        DirectorEngine e;
        e.setRules(sampleRules());
        e.setSection("verse");
        e.bar();

        e.pinLayer(DirectorEngine::Base, true);
        e.setSection("drop");
        e.bar();
        // Base stays, the other layers follow the music
        QCOMPARE(e.layerBundle(DirectorEngine::Base), Bundle() << 1);
        QCOMPARE(e.layerBundle(DirectorEngine::Rhythm), Bundle() << 20);

        e.pinLayer(DirectorEngine::Base, false);
        e.bar();
        QCOMPARE(e.layerBundle(DirectorEngine::Base), Bundle() << 3 << 4);

        e.setLayerOff(DirectorEngine::Rhythm, true);
        QList<Action> actions = e.evaluate();
        QCOMPARE(ids(actions, Action::Stop), QList<quint32>() << 20);
        QVERIFY(e.layerBundle(DirectorEngine::Rhythm).isEmpty());
        QCOMPARE(e.activeOverrides(), 1);
    }

    void forcedSection()
    {
        DirectorEngine e;
        e.setRules(sampleRules());
        e.setSection("verse");
        e.forceSection("drop");
        e.bar();
        QCOMPARE(e.activeRuleName(), QString("Drop"));
        QCOMPARE(e.section(), QString("drop"));
        QCOMPARE(e.detectedSection(), QString("verse"));

        e.forceSection(QString());
        e.bar();
        QCOMPARE(e.activeRuleName(), QString("Default"));
    }

    void holdAndBlackout()
    {
        DirectorEngine e;
        e.setRules(sampleRules());
        e.setSection("verse");
        e.bar();

        e.setHold(true);
        e.setSection("drop");
        QVERIFY(e.bar().isEmpty());
        QCOMPARE(e.activeRuleName(), QString("Default"));

        // A blackout goes through a hold
        e.setBlackout(true);
        QList<Action> off = e.evaluate();
        QCOMPARE(ids(off, Action::Stop), QList<quint32>() << 1 << 10);
        QVERIFY(ids(off, Action::Start).isEmpty());

        e.setBlackout(false);
        e.setHold(false);
        QList<Action> on = e.evaluate();
        QCOMPARE(e.activeRuleName(), QString("Drop"));
        QVERIFY(ids(on, Action::Start).contains(3));
    }

    void refreshRestartsLayer()
    {
        DirectorEngine e;
        e.setRules(sampleRules());
        e.setSection("verse");
        e.bar();

        e.requestRefresh(DirectorEngine::Base);
        QList<Action> actions = e.evaluate();
        QCOMPARE(ids(actions, Action::Stop), QList<quint32>() << 1);
        QCOMPARE(ids(actions, Action::Start), QList<quint32>() << 1);

        // Only once
        QVERIFY(e.evaluate().isEmpty());
    }

    void removeFunction()
    {
        DirectorEngine e;
        e.setRules(sampleRules());
        e.setSection("drop");
        e.bar();

        e.removeFunction(3);
        e.removeFunction(20);
        QCOMPARE(e.rules().at(1).layers[DirectorEngine::Base].first(), Bundle() << 4);
        QVERIFY(e.rules().at(1).layers[DirectorEngine::Rhythm].isEmpty());
        QVERIFY(e.layerBundle(DirectorEngine::Base).contains(3) == false);
    }

    void stopAll()
    {
        DirectorEngine e;
        e.setRules(sampleRules());
        e.setSection("drop");
        e.setStrobeOk(true);
        e.bar();

        QList<Action> actions = e.stopAll();
        QCOMPARE(ids(actions, Action::Stop), QList<quint32>() << 3 << 4 << 10 << 20 << 30);
        QCOMPARE(e.activeRule(), -1);
        for (int l = 0; l < DirectorEngine::LayerCount; l++)
            QVERIFY(e.layerBundle(l).isEmpty());

        // And it starts again from scratch
        QCOMPARE(ids(e.bar(), Action::Start), QList<quint32>() << 3 << 4 << 10 << 20 << 30);
    }

    void layerNames()
    {
        for (int l = 0; l < DirectorEngine::LayerCount; l++)
            QCOMPARE(DirectorEngine::layerFromName(DirectorEngine::layerName(l)), l);
        QCOMPARE(DirectorEngine::layerFromName("Nope"), -1);
    }
};

QTEST_APPLESS_MAIN(DirectorEngine_Test)
#include "directorengine_test.moc"
