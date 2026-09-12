/*
 * SPlayerLyric - SPlayer Lyric Display Plugin for TrafficMonitor
 * 
 * Main Plugin Class Implementation
 */

#include "pch.h"
#include "SPlayerLyricPlugin.h"
#include "WebSocketClient.h"
#include "LyricManager.h"
#include "Config.h"
#include "JsonParser.h"

SPlayerLyricPlugin SPlayerLyricPlugin::m_instance;

SPlayerLyricPlugin::SPlayerLyricPlugin()
{
}

SPlayerLyricPlugin& SPlayerLyricPlugin::Instance()
{
    return m_instance;
}

IPluginItem* SPlayerLyricPlugin::GetItem(int index)
{
    if (index == 0)
        return &m_lyricItem;
    return nullptr;
}

void SPlayerLyricPlugin::DataRequired()
{
    // WebSocket is async, data already updated by callbacks
    // v13: 拉 TM 监控数据写快照（监控行自绘用）
    extern ITrafficMonitor* g_pTMInterface;
    extern void UpdateMonitorSnapshot(const double*, int);
    if (g_pTMInterface)
    {
        static const ITrafficMonitor::MonitorItem items[6] = {
            ITrafficMonitor::MI_UP, ITrafficMonitor::MI_DOWN,
            ITrafficMonitor::MI_CPU, ITrafficMonitor::MI_MEMORY,
            ITrafficMonitor::MI_GPU_USAGE, ITrafficMonitor::MI_CPU_TEMP,
        };
        double vals[6];
        for (int i = 0; i < 6; ++i)
            vals[i] = g_pTMInterface->GetMonitorValue(items[i]);
        UpdateMonitorSnapshot(vals, 6);
    }
}

const wchar_t* SPlayerLyricPlugin::GetInfo(PluginInfoIndex index)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    static CString str;

    switch (index)
    {
    case TMI_NAME:
        str.LoadString(IDS_PLUGIN_NAME);
        return str.GetString();

    case TMI_DESCRIPTION:
        str.LoadString(IDS_PLUGIN_DESCRIPTION);
        return str.GetString();

    case TMI_AUTHOR:
        return L"SPlayer Lyric Plugin";

    case TMI_COPYRIGHT:
        return L"Copyright (C) 2026";

    case TMI_VERSION:
        return L"1.0.0";

    case TMI_URL:
        return L"https://github.com/imsyy/SPlayer";

    default:
        break;
    }

    return L"";
}

#include "OptionsDialog.h"

ITMPlugin::OptionReturn SPlayerLyricPlugin::ShowOptionsDialog(void* hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    COptionsDialog dlg(CWnd::FromHandle((HWND)hParent));
    if (dlg.DoModal() == IDOK)
    {
        return OR_OPTION_CHANGED;
    }

    return OR_OPTION_UNCHANGED;
}

void SPlayerLyricPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    switch (index)
    {
    case EI_CONFIG_DIR:
        g_config.Load(data);

        if (!m_initialized)
        {
            InitWebSocketCallbacks();
            g_wsClient.Start(g_config.Data().wsPort);
            m_initialized = true;
        }
        break;

    default:
        break;
    }
}

const wchar_t* SPlayerLyricPlugin::GetTooltipInfo()
{
    std::wstring songInfo = g_lyricMgr.GetSongInfoText();

    if (!g_wsClient.IsConnected())
    {
        m_tooltipText = g_config.StringRes(IDS_NOT_CONNECTED);
    }
    else if (!songInfo.empty())
    {
        m_tooltipText = L"* " + songInfo;
        if (g_lyricMgr.IsPlaying())
            m_tooltipText += L" (Playing)";
        else
            m_tooltipText += L" (Paused)";
    }
    else
    {
        m_tooltipText.clear();
    }

    return m_tooltipText.c_str();
}

void SPlayerLyricPlugin::OnInitialize(ITrafficMonitor* pApp)
{
    m_pApp = pApp;
    extern ITrafficMonitor* g_pTMInterface;   // LyricDisplayItem.cpp 定义
    g_pTMInterface = pApp;   // v13: 监控行自绘读取数据用
}

void SPlayerLyricPlugin::InitWebSocketCallbacks()
{
    WebSocketCallbacks callbacks;

    callbacks.onConnected = [this]() {
        OutputDebugStringW(L"[SPlayerLyric] Connected to SPlayer\n");
        // Start high-frequency refresh if YRC is enabled
        if (g_config.Data().enableYrc)
        {
            m_lyricItem.StartHighFreqRefresh();
        }
    };

    callbacks.onDisconnected = [this]() {
        g_lyricMgr.Clear();
        m_lyricItem.StopHighFreqRefresh();
        OutputDebugStringW(L"[SPlayerLyric] Disconnected from SPlayer\n");
    };

    callbacks.onStatusChange = [this](bool isPlaying) {
        g_lyricMgr.UpdatePlayStatus(isPlaying);
        // Control high-frequency refresh based on play status
        // v10: 放开 YRC 限制——LRC 歌也需要高频重绘驱动换行动画（逐字自然缺席）
        if (isPlaying && g_config.Data().enableYrc && g_lyricMgr.HasAnyLyricData())
        {
            m_lyricItem.StartHighFreqRefresh();
        }
        else if (!isPlaying)
        {
            m_lyricItem.StopHighFreqRefresh();
        }
    };

    callbacks.onSongChange = [](const SPlayerProtocol::SongInfo& info) {
        g_lyricMgr.UpdateSongInfo(info);
    };

    callbacks.onProgressChange = [](const SPlayerProtocol::ProgressInfo& info) {
        g_lyricMgr.UpdateProgress(info.currentTime);
    };

    callbacks.onLyricChange = [this](const SPlayerProtocol::LyricData& data) {
        g_lyricMgr.UpdateLyrics(data);
        // Start high-frequency refresh if lyric data is available and playing
        // v10: LRC 也启动（换行动画驱动）；兜底 onStatusChange 早于歌词到达的时序
        if (g_config.Data().enableYrc && (data.hasYrc() || data.hasLrc()) && g_lyricMgr.IsPlaying())
        {
            m_lyricItem.StartHighFreqRefresh();
        }
    };

    callbacks.onError = [](const std::string& msg) {
        std::wstring wmsg = L"[SPlayerLyric] Error: ";
        wmsg += Utf8ToWide(msg);
        wmsg += L"\n";
        OutputDebugStringW(wmsg.c_str());
    };

    g_wsClient.SetCallbacks(callbacks);
}

// DLL Export
ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &SPlayerLyricPlugin::Instance();
}
