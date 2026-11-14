/*
  VertexAV Director
  graphicscard.cpp

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
#include <QSettings>
#include <QDebug>
#include <QDir>

#include "graphicscard.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dxgi1_6.h>
#endif

#define SETTINGS_GRAPHICS_CARD QStringLiteral("graphics/card")

int GraphicsCard::preference()
{
    QSettings settings;
    int pref = settings.value(SETTINGS_GRAPHICS_CARD, int(HighPerformance)).toInt();
    return (pref < HighPerformance || pref > SystemDefault) ? int(HighPerformance) : pref;
}

void GraphicsCard::setPreference(int preference)
{
    QSettings settings;
    settings.setValue(SETTINGS_GRAPHICS_CARD, preference);
    apply();
}

#if defined(Q_OS_WIN)

void GraphicsCard::apply()
{
    // Windows keeps one value per program, named after the full path of its
    // executable: 0 leaves the choice to the system, 1 is the power saving
    // card and 2 the high performance one
    int value = 2;
    switch (preference())
    {
        case PowerSaving: value = 1; break;
        case SystemDefault: value = 0; break;
        default: break;
    }

    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString data = QString("GpuPreference=%1;").arg(value);

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
                        0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        qWarning() << "Graphics card preference: cannot open the registry key";
        return;
    }

    const std::wstring name = exe.toStdWString();
    const std::wstring text = data.toStdWString();
    RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE *>(text.c_str()),
                   DWORD((text.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

QStringList GraphicsCard::cardNames()
{
    QStringList names;
    names << QString() << QString() << QString();

    IDXGIFactory6 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory6), reinterpret_cast<void **>(&factory))) || factory == nullptr)
        return names;

    // The two cards are told apart by their memory: asking the system which
    // one it prefers gives the same answer twice once this program has a
    // preference of its own. Display adapters that are not graphics cards
    // (virtual monitors, remote desktop) are left out by their vendor.
    SIZE_T mostMemory = 0, leastMemory = 0;
    IDXGIAdapter1 *adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
    {
        DXGI_ADAPTER_DESC1 desc;
        const bool ok = SUCCEEDED(adapter->GetDesc1(&desc));
        adapter->Release();
        if (ok == false || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            continue;
        if (desc.VendorId != 0x10DE && desc.VendorId != 0x1002 && desc.VendorId != 0x8086 && desc.VendorId != 0x5143)
            continue;

        const QString name = QString::fromWCharArray(desc.Description);
        if (names[HighPerformance].isEmpty() || desc.DedicatedVideoMemory > mostMemory)
        {
            mostMemory = desc.DedicatedVideoMemory;
            names[HighPerformance] = name;
        }
        if (names[PowerSaving].isEmpty() || desc.DedicatedVideoMemory < leastMemory)
        {
            leastMemory = desc.DedicatedVideoMemory;
            names[PowerSaving] = name;
        }
    }
    // a single card is both
    if (names[PowerSaving] == names[HighPerformance])
        names[PowerSaving].clear();
    factory->Release();

    return names;
}

#else

void GraphicsCard::apply()
{
}

QStringList GraphicsCard::cardNames()
{
    return QStringList() << QString() << QString() << QString();
}

#endif
