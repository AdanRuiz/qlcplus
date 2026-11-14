/*
  VertexAV Director
  systemmonitor.cpp

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

#include <QCoreApplication>
#include <QStandardPaths>
#include <QSysInfo>
#include <QDateTime>
#include <QProcess>
#include <QDebug>
#include <QDir>

#include "systemmonitor.h"

#if defined(Q_OS_WIN)
  #include <windows.h>
  #include <psapi.h>
  #include <pdh.h>
  #include <pdhmsg.h>
  #include <io.h>
#else
  #include <unistd.h>
#endif

/** The log is started over when it grows past this, keeping one old copy */
#define SYSMON_MAX_LOG_SIZE (8 * 1024 * 1024)

SystemMonitor::SystemMonitor(QString path, int intervalSeconds, QObject *parent)
    : QThread(parent)
    , m_path(path)
    , m_interval(qMax(1, intervalSeconds))
    , m_stop(false)
    , m_cpuIdle(0)
    , m_cpuTotal(0)
    , m_thermalQuery(nullptr)
    , m_thermalCounter(nullptr)
    , m_gpuAvailable(true)
{
    setObjectName("SystemMonitor");
}

SystemMonitor::~SystemMonitor()
{
    stop();
}

QString SystemMonitor::defaultLogPath()
{
    return QDir::homePath() + QDir::separator() + "VertexAV-Director-sysmon.log";
}

int SystemMonitor::intervalFromEnvironment()
{
    QByteArray value = qgetenv("VERTEXAV_SYSMON").trimmed();
    if (value.isEmpty() || value == "0")
        return 0;

    bool ok = false;
    int seconds = value.toInt(&ok);
    return (ok && seconds > 0) ? seconds : DefaultInterval;
}

void SystemMonitor::stop()
{
    {
        QMutexLocker locker(&m_mutex);
        m_stop = true;
        m_wake.wakeAll();
    }
    wait();
}

void SystemMonitor::writeLine(QFile &file, const QString &text)
{
    QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") + " " + text + "\n";
    file.write(line.toUtf8());
    file.flush();

    // Past the cache of the system and onto the disk: a machine that loses
    // power would otherwise take the last lines with it, the ones that matter
#if defined(Q_OS_WIN)
    _commit(file.handle());
#else
    fsync(file.handle());
#endif
}

void SystemMonitor::run()
{
    QFile file(m_path);
    if (file.size() > SYSMON_MAX_LOG_SIZE)
    {
        QFile::remove(m_path + ".old");
        file.rename(m_path + ".old");
        file.setFileName(m_path);
    }

    if (!file.open(QIODevice::Append))
    {
        qWarning() << "System monitor: unable to open" << m_path;
        return;
    }

    writeLine(file, QString("---- started: %1 %2, pid %3, %4, every %5 s")
                        .arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion())
                        .arg(QCoreApplication::applicationPid())
                        .arg(QSysInfo::prettyProductName())
                        .arg(m_interval));

    while (true)
    {
        writeLine(file, sample());

        QMutexLocker locker(&m_mutex);
        if (!m_stop)
            m_wake.wait(&m_mutex, m_interval * 1000);
        if (m_stop)
            break;
    }

    writeLine(file, "---- stopped (clean exit)");

#if defined(Q_OS_WIN)
    if (m_thermalQuery != nullptr)
        PdhCloseQuery((PDH_HQUERY)m_thermalQuery);
    m_thermalQuery = nullptr;
    m_thermalCounter = nullptr;
#endif
}

QString SystemMonitor::sample()
{
    QStringList parts;
    parts << memorySample() << cpuSample() << thermalSample() << gpuSample();
    parts.removeAll(QString());
    return parts.join(" ");
}

/*********************************************************************
 * Memory
 *********************************************************************/

QString SystemMonitor::memorySample()
{
    const double GB = 1024.0 * 1024.0 * 1024.0;
    const double MB = 1024.0 * 1024.0;

#if defined(Q_OS_WIN)
    QString text;

    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem))
    {
        double used = double(mem.ullTotalPhys - mem.ullAvailPhys);
        double commitUsed = double(mem.ullTotalPageFile - mem.ullAvailPageFile);
        text = QString("ram=%1% (%2/%3 GB) commit=%4%")
                   .arg(mem.dwMemoryLoad)
                   .arg(used / GB, 0, 'f', 1)
                   .arg(double(mem.ullTotalPhys) / GB, 0, 'f', 1)
                   .arg(mem.ullTotalPageFile ? qRound(commitUsed * 100.0 / double(mem.ullTotalPageFile)) : 0);
    }

    PROCESS_MEMORY_COUNTERS_EX pmc;
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof(pmc)))
    {
        text += QString(" app=%1MB private=%2MB")
                    .arg(qRound(double(pmc.WorkingSetSize) / MB))
                    .arg(qRound(double(pmc.PrivateUsage) / MB));
    }

    return text.trimmed();
#elif defined(Q_OS_LINUX)
    double total = 0, available = 0;
    QFile info("/proc/meminfo");
    if (info.open(QIODevice::ReadOnly))
    {
        // Sizes are in kB
        for (const QByteArray &line : info.readAll().split('\n'))
        {
            if (line.startsWith("MemTotal:"))
                total = line.mid(9).trimmed().split(' ').first().toDouble() * 1024.0;
            else if (line.startsWith("MemAvailable:"))
                available = line.mid(13).trimmed().split(' ').first().toDouble() * 1024.0;
        }
    }
    if (total <= 0)
        return QString();

    QString text = QString("ram=%1% (%2/%3 GB)")
                       .arg(qRound((total - available) * 100.0 / total))
                       .arg((total - available) / GB, 0, 'f', 1)
                       .arg(total / GB, 0, 'f', 1);

    QFile statm("/proc/self/statm");
    if (statm.open(QIODevice::ReadOnly))
    {
        QList<QByteArray> fields = statm.readAll().split(' ');
        if (fields.count() > 1)
            text += QString(" app=%1MB").arg(qRound(fields.at(1).toDouble() * sysconf(_SC_PAGESIZE) / MB));
    }
    return text;
#else
    Q_UNUSED(GB)
    Q_UNUSED(MB)
    return QString();
#endif
}

/*********************************************************************
 * Processor load
 *********************************************************************/

QString SystemMonitor::cpuSample()
{
    quint64 idle = 0, total = 0;

#if defined(Q_OS_WIN)
    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
        return QString();

    auto toInt = [](const FILETIME &ft) {
        return (quint64(ft.dwHighDateTime) << 32) | quint64(ft.dwLowDateTime);
    };
    idle = toInt(idleTime);
    // The kernel time already includes the idle time
    total = toInt(kernelTime) + toInt(userTime);
#elif defined(Q_OS_LINUX)
    QFile stat("/proc/stat");
    if (!stat.open(QIODevice::ReadOnly))
        return QString();

    QList<QByteArray> fields = stat.readLine().simplified().split(' ');
    for (int i = 1; i < fields.count(); i++)
    {
        quint64 value = fields.at(i).toULongLong();
        total += value;
        // idle and iowait
        if (i == 4 || i == 5)
            idle += value;
    }
#else
    return QString();
#endif

    QString text;
    // The first sample has nothing to compare with
    if (m_cpuTotal != 0 && total > m_cpuTotal)
    {
        double busy = 1.0 - double(idle - m_cpuIdle) / double(total - m_cpuTotal);
        text = QString("cpu=%1%").arg(qBound(0, qRound(busy * 100.0), 100));
    }
    m_cpuIdle = idle;
    m_cpuTotal = total;
    return text;
}

/*********************************************************************
 * Temperatures of the machine
 *********************************************************************/

QString SystemMonitor::thermalSample()
{
#if defined(Q_OS_WIN)
    // The thermal zones of the firmware, as the performance counters give
    // them. Unlike the WMI class with the same data, they need no privileges.
    if (m_thermalQuery == nullptr)
    {
        PDH_HQUERY query = nullptr;
        PDH_HCOUNTER counter = nullptr;
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS)
            return QString();

        if (PdhAddEnglishCounterW(query, L"\\Thermal Zone Information(*)\\High Precision Temperature",
                                  0, &counter) != ERROR_SUCCESS)
        {
            PdhCloseQuery(query);
            return QString();
        }
        m_thermalQuery = query;
        m_thermalCounter = counter;
    }

    if (PdhCollectQueryData((PDH_HQUERY)m_thermalQuery) != ERROR_SUCCESS)
        return QString();

    DWORD size = 0, count = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW((PDH_HCOUNTER)m_thermalCounter, PDH_FMT_DOUBLE,
                                                     &size, &count, nullptr);
    if (status != (PDH_STATUS)PDH_MORE_DATA || size == 0)
        return QString();

    QByteArray buffer(int(size), 0);
    PDH_FMT_COUNTERVALUE_ITEM_W *items = (PDH_FMT_COUNTERVALUE_ITEM_W *)buffer.data();
    if (PdhGetFormattedCounterArrayW((PDH_HCOUNTER)m_thermalCounter, PDH_FMT_DOUBLE,
                                     &size, &count, items) != ERROR_SUCCESS)
        return QString();

    QStringList zones;
    for (DWORD i = 0; i < count; i++)
    {
        if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
            items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
            continue;

        // Tenths of a kelvin
        double celsius = items[i].FmtValue.doubleValue / 10.0 - 273.15;
        QString name = QString::fromWCharArray(items[i].szName);
        name.remove("\\_TZ.").remove("\\");
        zones << QString("temp[%1]=%2C").arg(name).arg(celsius, 0, 'f', 1);
    }
    return zones.join(" ");
#elif defined(Q_OS_LINUX)
    QStringList zones;
    QDir dir("/sys/class/thermal");
    for (const QString &zone : dir.entryList(QStringList() << "thermal_zone*", QDir::Dirs, QDir::Name))
    {
        QFile temp(dir.filePath(zone + "/temp"));
        if (!temp.open(QIODevice::ReadOnly))
            continue;

        QString name = zone;
        QFile type(dir.filePath(zone + "/type"));
        if (type.open(QIODevice::ReadOnly))
            name = QString::fromLatin1(type.readAll().trimmed());

        // Thousandths of a degree
        zones << QString("temp[%1]=%2C").arg(name).arg(temp.readAll().trimmed().toDouble() / 1000.0, 0, 'f', 1);
    }
    return zones.join(" ");
#else
    return QString();
#endif
}

/*********************************************************************
 * Graphics card
 *********************************************************************/

QString SystemMonitor::gpuSample()
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    if (!m_gpuAvailable)
        return QString();

    // The tool that comes with the NVIDIA driver. Other cards are not read.
    QProcess smi;
    smi.start("nvidia-smi", QStringList()
              << "--query-gpu=temperature.gpu,utilization.gpu,memory.used,power.draw"
              << "--format=csv,noheader,nounits");
    if (!smi.waitForStarted(2000))
    {
        m_gpuAvailable = false;
        return QString();
    }
    if (!smi.waitForFinished(3000))
    {
        smi.kill();
        smi.waitForFinished(1000);
        return QString("gpu=timeout");
    }

    QStringList cards;
    const QList<QByteArray> lines = smi.readAllStandardOutput().trimmed().split('\n');
    for (int i = 0; i < lines.count(); i++)
    {
        QList<QByteArray> fields = lines.at(i).split(',');
        if (fields.count() < 4)
            continue;

        QString prefix = lines.count() > 1 ? QString("gpu%1").arg(i) : QString("gpu");
        cards << QString("%1=%2C %1load=%3% %1mem=%4MiB %1power=%5W")
                     .arg(prefix,
                          QString::fromLatin1(fields.at(0).trimmed()),
                          QString::fromLatin1(fields.at(1).trimmed()),
                          QString::fromLatin1(fields.at(2).trimmed()),
                          QString::fromLatin1(fields.at(3).trimmed()));
    }
    return cards.join(" ");
#else
    return QString();
#endif
}
