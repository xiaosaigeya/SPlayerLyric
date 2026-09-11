/*
 * SPlayerLyric - SPlayer Lyric Display Plugin for TrafficMonitor
 * 
 * Configuration Management
 */

#pragma once

#include <string>
#include <Windows.h>

struct PluginConfig
{
    // Connection
    int wsPort = 25885;
    int reconnectInterval = 5000;

    // Display
    int displayWidth = 300;
    int fontSize = 11;
    int dualLineFontSize = 9;  // Font size for dual line mode (usually smaller)
    bool fontWeightBold = false;
    std::wstring fontName = L"Microsoft YaHei UI";

    bool enableScrolling = true;
    bool enableYrc = false;  // Default OFF due to TrafficMonitor's low refresh rate
    COLORREF highlightColor = RGB(0, 120, 215);
    COLORREF normalColor = RGB(180, 180, 180);
    COLORREF bgColor = RGB(0, 0, 0);
    int bgOpacity = 8; // 0-255
    int lyricOffset = 0; // in milliseconds
    
    // Dual line display
    bool dualLineDisplay = false;
    int secondLineType = 0; // 0 = next line, 1 = translation, 2 = artist/song info
    int threeLine = 0; // 0 = dual line (legacy), 1 = three line (prev/cur/next)
    int dualLineAlignment = 0; // 0=Left, 1=Center, 2=Right, 3=Split

    // Desktop mode specific
    int desktopXOffset = 60;
    int desktopTransparency = 255; // 0-255
    bool desktopDualLine = false;
    bool autoStart = true;
    bool hideWhenNotPlaying = false;  // Hide lyric display when not playing
    
    // Auto color adaptation
    bool adaptiveColor = true;       // Adaptive color based on dark/light mode
    COLORREF darkHighlightColor = RGB(0, 120, 215);
    COLORREF darkNormalColor = RGB(200, 200, 200);   // Default light grey for dark mode
    COLORREF lightHighlightColor = RGB(0, 80, 160);  // Dark blue for light mode
    COLORREF lightNormalColor = RGB(60, 60, 60);     // Dark grey for light mode
};

class Config
{
public:
    static Config& Instance();

    void Load(const std::wstring& configDir);
    void Save();

    PluginConfig& Data() { return m_config; }
    const PluginConfig& Data() const { return m_config; }

    const wchar_t* StringRes(UINT id);

private:
    Config() = default;

    std::wstring m_configPath;
    PluginConfig m_config;
    CString m_strBuffer;
};

#define g_config Config::Instance()
