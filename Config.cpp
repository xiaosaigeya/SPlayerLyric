/*
 * SPlayerLyric - SPlayer Lyric Display Plugin for TrafficMonitor
 * 
 * Configuration Implementation
 */

#include "pch.h"
#include <afxwin.h>
#include "Config.h"
#include <shlwapi.h>

Config& Config::Instance()
{
    static Config instance;
    return instance;
}

void Config::Load(const std::wstring& configDir)
{
    m_configPath = configDir + L"\\SPlayerLyric.ini";

    m_config.wsPort = GetPrivateProfileIntW(L"Connection", L"Port", 25885, m_configPath.c_str());
    m_config.reconnectInterval = GetPrivateProfileIntW(L"Connection", L"ReconnectInterval", 5000, m_configPath.c_str());

    m_config.displayWidth = GetPrivateProfileIntW(L"Display", L"Width", 300, m_configPath.c_str());
    m_config.fontSize = GetPrivateProfileIntW(L"Display", L"FontSize", 11, m_configPath.c_str());
    m_config.dualLineFontSize = GetPrivateProfileIntW(L"Display", L"DualLineFontSize", 9, m_configPath.c_str());
    m_config.fontWeightBold = GetPrivateProfileIntW(L"Display", L"FontBold", 0, m_configPath.c_str()) != 0;
    
    wchar_t fontBuffer[LF_FACESIZE];
    GetPrivateProfileStringW(L"Display", L"FontName", L"Microsoft YaHei UI", fontBuffer, LF_FACESIZE, m_configPath.c_str());
    m_config.fontName = fontBuffer;

    m_config.enableScrolling = GetPrivateProfileIntW(L"Lyric", L"EnableScrolling", 1, m_configPath.c_str()) != 0;
    m_config.enableYrc = GetPrivateProfileIntW(L"Lyric", L"EnableYrc", 0, m_configPath.c_str()) != 0;  // Default OFF
    m_config.highlightColor = GetPrivateProfileIntW(L"Lyric", L"HighlightColor", RGB(0, 120, 215), m_configPath.c_str());
    m_config.normalColor = GetPrivateProfileIntW(L"Lyric", L"NormalColor", RGB(180, 180, 180), m_configPath.c_str());
    m_config.bgColor = GetPrivateProfileIntW(L"Lyric", L"BgColor", RGB(0, 0, 0), m_configPath.c_str());
    m_config.bgOpacity = GetPrivateProfileIntW(L"Lyric", L"BgOpacity", 8, m_configPath.c_str());
    m_config.lyricOffset = (int)GetPrivateProfileIntW(L"Lyric", L"LyricOffset", 0, m_configPath.c_str());
    m_config.dualLineDisplay = GetPrivateProfileIntW(L"Lyric", L"DualLine", 0, m_configPath.c_str()) != 0;
    m_config.secondLineType = GetPrivateProfileIntW(L"Lyric", L"SecondLineType", 0, m_configPath.c_str());
    m_config.threeLine = GetPrivateProfileIntW(L"Lyric", L"ThreeLine", 0, m_configPath.c_str());
    m_config.dualLineAlignment = GetPrivateProfileIntW(L"Lyric", L"DualLineAlignment", 0, m_configPath.c_str());
    // Load adaptive setting
    m_config.adaptiveColor = GetPrivateProfileIntW(L"Lyric", L"AdaptiveColor", 1, m_configPath.c_str()) != 0;
    
    // Load separate colors, defaulting to legacy values if not present to preserve user config
    m_config.darkHighlightColor = GetPrivateProfileIntW(L"Lyric", L"DarkHighlightColor", m_config.highlightColor, m_configPath.c_str());
    m_config.darkNormalColor = GetPrivateProfileIntW(L"Lyric", L"DarkNormalColor", m_config.normalColor, m_configPath.c_str());
    m_config.lightHighlightColor = GetPrivateProfileIntW(L"Lyric", L"LightHighlightColor", RGB(0, 80, 160), m_configPath.c_str());
    m_config.lightNormalColor = GetPrivateProfileIntW(L"Lyric", L"LightNormalColor", RGB(60, 60, 60), m_configPath.c_str());

    m_config.desktopXOffset = GetPrivateProfileIntW(L"Desktop", L"XOffset", 60, m_configPath.c_str());
    m_config.desktopTransparency = GetPrivateProfileIntW(L"Desktop", L"Transparency", 255, m_configPath.c_str());
    m_config.desktopDualLine = GetPrivateProfileIntW(L"Desktop", L"DualLine", 0, m_configPath.c_str()) != 0;
    m_config.autoStart = GetPrivateProfileIntW(L"Desktop", L"AutoStart", 1, m_configPath.c_str()) != 0;
    m_config.hideWhenNotPlaying = GetPrivateProfileIntW(L"Desktop", L"HideWhenNotPlaying", 0, m_configPath.c_str()) != 0;
}

void Config::Save()
{
    if (m_configPath.empty())
        return;

    wchar_t buffer[64];

    swprintf_s(buffer, L"%d", m_config.wsPort);
    WritePrivateProfileStringW(L"Connection", L"Port", buffer, m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.reconnectInterval);
    WritePrivateProfileStringW(L"Connection", L"ReconnectInterval", buffer, m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.displayWidth);
    WritePrivateProfileStringW(L"Display", L"Width", buffer, m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.fontSize);
    WritePrivateProfileStringW(L"Display", L"FontSize", buffer, m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.dualLineFontSize);
    WritePrivateProfileStringW(L"Display", L"DualLineFontSize", buffer, m_configPath.c_str());

    WritePrivateProfileStringW(L"Display", L"FontBold", m_config.fontWeightBold ? L"1" : L"0", m_configPath.c_str());

    WritePrivateProfileStringW(L"Display", L"FontName", m_config.fontName.c_str(), m_configPath.c_str());

    WritePrivateProfileStringW(L"Lyric", L"EnableScrolling", m_config.enableScrolling ? L"1" : L"0", m_configPath.c_str());
    WritePrivateProfileStringW(L"Lyric", L"EnableYrc", m_config.enableYrc ? L"1" : L"0", m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.highlightColor);
    WritePrivateProfileStringW(L"Lyric", L"HighlightColor", buffer, m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.normalColor);
    WritePrivateProfileStringW(L"Lyric", L"NormalColor", buffer, m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.bgColor);
    WritePrivateProfileStringW(L"Lyric", L"BgColor", buffer, m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.bgOpacity);
    WritePrivateProfileStringW(L"Lyric", L"BgOpacity", buffer, m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.lyricOffset);
    WritePrivateProfileStringW(L"Lyric", L"LyricOffset", buffer, m_configPath.c_str());

    WritePrivateProfileStringW(L"Lyric", L"DualLine", m_config.dualLineDisplay ? L"1" : L"0", m_configPath.c_str());
    
    swprintf_s(buffer, L"%d", m_config.secondLineType);
    WritePrivateProfileStringW(L"Lyric", L"SecondLineType", buffer, m_configPath.c_str());
    swprintf_s(buffer, L"%d", m_config.threeLine);
    WritePrivateProfileStringW(L"Lyric", L"ThreeLine", buffer, m_configPath.c_str());
    WritePrivateProfileStringW(L"Lyric", L"SecondLineType", buffer, m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.dualLineAlignment);
    WritePrivateProfileStringW(L"Lyric", L"DualLineAlignment", buffer, m_configPath.c_str());
    
    // Save adaptive setting
    WritePrivateProfileStringW(L"Lyric", L"AdaptiveColor", m_config.adaptiveColor ? L"1" : L"0", m_configPath.c_str());

    // Save separate colors
    swprintf_s(buffer, L"%d", m_config.darkHighlightColor);
    WritePrivateProfileStringW(L"Lyric", L"DarkHighlightColor", buffer, m_configPath.c_str());
    
    swprintf_s(buffer, L"%d", m_config.darkNormalColor);
    WritePrivateProfileStringW(L"Lyric", L"DarkNormalColor", buffer, m_configPath.c_str());
    
    swprintf_s(buffer, L"%d", m_config.lightHighlightColor);
    WritePrivateProfileStringW(L"Lyric", L"LightHighlightColor", buffer, m_configPath.c_str());
    
    swprintf_s(buffer, L"%d", m_config.lightNormalColor);
    WritePrivateProfileStringW(L"Lyric", L"LightNormalColor", buffer, m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.desktopXOffset);
    WritePrivateProfileStringW(L"Desktop", L"XOffset", buffer, m_configPath.c_str());

    swprintf_s(buffer, L"%d", m_config.desktopTransparency);
    WritePrivateProfileStringW(L"Desktop", L"Transparency", buffer, m_configPath.c_str());

    WritePrivateProfileStringW(L"Desktop", L"DualLine", m_config.desktopDualLine ? L"1" : L"0", m_configPath.c_str());
    WritePrivateProfileStringW(L"Desktop", L"AutoStart", m_config.autoStart ? L"1" : L"0", m_configPath.c_str());
    WritePrivateProfileStringW(L"Desktop", L"HideWhenNotPlaying", m_config.hideWhenNotPlaying ? L"1" : L"0", m_configPath.c_str());
}

const wchar_t* Config::StringRes(UINT id)
{
    m_strBuffer.LoadStringW(id);
    return m_strBuffer.GetString();
}
