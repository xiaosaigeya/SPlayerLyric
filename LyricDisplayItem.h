/*
 * SPlayerLyric - SPlayer Lyric Display Plugin for TrafficMonitor
 * 
 * Lyric Display Item Interface
 */

#pragma once

#include "PluginInterface.h"
#include <string>
#include <vector>
#include <atomic>

class LyricDisplayItem : public IPluginItem
{
public:
    LyricDisplayItem();
    virtual ~LyricDisplayItem();

    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;

    virtual bool IsCustomDraw() const override { return true; }
    virtual int GetItemWidth() const override;
    virtual int GetItemWidthEx(void* hDC) const override;
    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

    virtual int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;

    // High-frequency refresh control
    void StartHighFreqRefresh();
    void StopHighFreqRefresh();

private:
    std::wstring GetDisplayText() const;
    HFONT GetFont(HDC hDC) const;
    
    void DrawSimpleText(HDC dc, int x, int y, int w, int h, bool dark_mode);
    void DrawDualLine(HDC dc, int x, int y, int w, int h, bool dark_mode);
    void DrawDualLineInner(HDC dc, int x, int y, int w, int h, bool dark_mode);
    void DrawWithYrcHighlight(HDC dc, int x, int y, int w, int h, bool dark_mode);
    void UpdateScrollAnimation(int textWidth, int areaWidth);
    
    static void CALLBACK HighFreqTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

    mutable HFONT m_font = nullptr;
    mutable int m_lastFontSize = 0;
    mutable std::wstring m_lastFontName;
    mutable bool m_lastFontBold = false;

    // Scroll animation state (time-based)
    mutable float m_scrollOffset = 0.0f;
    mutable ULONGLONG m_scrollStartTime = 0;
    mutable ULONGLONG m_scrollCycleLength = 0;

    // High-frequency refresh for smooth YRC
    mutable HWND m_taskbarWnd = nullptr;
    mutable RECT m_itemRect = {0};
    mutable UINT_PTR m_highFreqTimerId = 0;
    mutable std::atomic<bool> m_highFreqEnabled{false};
    
    mutable std::wstring m_itemName;

    // Transition state
    mutable int m_lastLineIndex = -1;
    int m_dualLastLineIndex = -1;
    bool m_dualInTransition = false;
    ULONGLONG m_dualTransitionStart = 0;
    mutable std::wstring m_dualPrevCache;
    mutable int m_dualSlide = 0;
    mutable bool m_dualJustSwitched = false;
    mutable std::vector<std::wstring> m_dualSnapTexts;
    mutable ULONGLONG m_transitionStartTime = 0;
    mutable std::wstring m_prevLineText;
    mutable bool m_inTransition = false;
};
