/*
  VertexAV Director
  systemmonitor.h

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

#ifndef SYSTEMMONITOR_H
#define SYSTEMMONITOR_H

#include <QWaitCondition>
#include <QThread>
#include <QMutex>
#include <QFile>

/**
 * Diagnostic log of the health of the machine while the application runs:
 * memory in use, processor load and temperatures, one line every few seconds.
 *
 * It is meant to find out why a computer hangs or powers off during a show,
 * so every line is forced to the disk before the next one is taken: what
 * the file ends with is what the machine looked like just before it died.
 * A line saying "stopped" is written on a clean exit, so a log without it
 * tells that the application did not close by itself.
 *
 * Sampling runs on its own thread and keeps going if the interface freezes.
 */
class SystemMonitor : public QThread
{
    Q_OBJECT

public:
    /** Seconds between samples when nothing else is asked for */
    static const int DefaultInterval = 5;

    SystemMonitor(QString path, int intervalSeconds = DefaultInterval, QObject *parent = nullptr);
    ~SystemMonitor();

    /** Where the log goes by default: the home folder of the user */
    static QString defaultLogPath();

    /**
     * Interval asked for through the environment variable VERTEXAV_SYSMON:
     * a number of seconds, or any other non empty value for the default.
     * Returns 0 when the variable does not ask for the monitor.
     */
    static int intervalFromEnvironment();

    QString path() const { return m_path; }

    /** Stop sampling and close the log. Called by the destructor too. */
    void stop();

protected:
    void run() override;

private:
    /** Append a line to the log and force it to the disk */
    void writeLine(QFile &file, const QString &text);

    /** One line with everything that could be read right now */
    QString sample();

    QString memorySample();
    QString cpuSample();
    QString thermalSample();
    QString gpuSample();

private:
    QString m_path;
    int m_interval;

    QMutex m_mutex;
    QWaitCondition m_wake;
    bool m_stop;

    /** Processor times of the previous sample, to work out the load */
    quint64 m_cpuIdle;
    quint64 m_cpuTotal;

    /** Opaque handles of the platform (thermal zone counters on Windows) */
    void *m_thermalQuery;
    void *m_thermalCounter;

    /** False once nvidia-smi turned out to be missing, to stop trying */
    bool m_gpuAvailable;
};

#endif // SYSTEMMONITOR_H
