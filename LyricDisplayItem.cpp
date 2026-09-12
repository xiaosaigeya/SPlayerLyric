/*
 * SPlayerLyric - SPlayer Lyric Display Plugin for TrafficMonitor
 * 
 * Lyric Display Item Implementation with Dual Line and Scroll Animation
 */

#include "pch.h"
#include "LyricDisplayItem.h"
#include "LyricManager.h"
#include "WebSocketClient.h"
#include "Config.h"

// Static instance pointer for timer callback
static LyricDisplayItem* g_pLyricItem = nullptr;

// v13: TM 主程序接口（SPlayerLyricPlugin::OnInitialize 赋值），监控行自绘读取数据用
ITrafficMonitor* g_pTMInterface = nullptr;
// 监控数据快照（DataRequired 线程写，绘制线程读；顺序: ↑ ↓ CPU% 内存% 显卡% CPU温度）
static double g_monSnap[6] = { -1,-1,-1,-1,-1,-1 };

void UpdateMonitorSnapshot(const double* vals, int n)
{
    for (int i = 0; i < 6 && i < n; ++i)
        g_monSnap[i] = vals[i];
}

// GDI+ 初始化（SoftShadowText 依赖；进程级一次）
static struct GdiplusInit {
    GdiplusInit() {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&token, &si, NULL);
    }
    ~GdiplusInit() { if (token) Gdiplus::GdiplusShutdown(token); }
    ULONG_PTR token = 0;
} s_gdiplusInit;

// v13: 速度格式化（字节/秒 -> 自适应单位）
static std::wstring FormatSpeed(double bytesPerSec)
{
    wchar_t buf[32];
    if (bytesPerSec >= 1024.0 * 1024.0)
        swprintf_s(buf, L"%.1fMB/s", bytesPerSec / 1024.0 / 1024.0);
    else if (bytesPerSec >= 1024.0)
        swprintf_s(buf, L"%.0fKB/s", bytesPerSec / 1024.0);
    else
        swprintf_s(buf, L"%.0fB/s", bytesPerSec);
    return buf;
}

// ---- v13: 柔和阴影文字（GDI+ 半透明画刷，右下 2px 投影，无勾边感）----
// 对比 v12 描边：阴影只在右下方向偏移且半透明（alpha 0.55），白字本体完整保留，
// 视觉"字浮在桌面上"而非"字加了一圈框"。深色背景下阴影几乎不可见，白字原样清晰。
static void SoftShadowText(HDC dc, int x, int y, const std::wstring& text,
                           COLORREF color, HFONT font)
{
    if (text.empty() || font == nullptr) return;
    Gdiplus::Graphics gfx(dc);
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    // GDI HFONT -> GDI+ Font（继承用户字体设置）
    Gdiplus::Font gfont(font);
    if (gfont.GetLastStatus() != Gdiplus::Ok) return;
    Gdiplus::StringFormat sf;
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap
                      | Gdiplus::StringFormatFlagsNoClip);
    sf.SetAlignment(Gdiplus::StringAlignmentNear);
    sf.SetLineAlignment(Gdiplus::StringAlignmentNear);
    Gdiplus::RectF layout((Gdiplus::REAL)x, (Gdiplus::REAL)y,
                          4096.0f, 256.0f);
    // 阴影层（右下偏移）
    Gdiplus::Color shadowC(140, 10, 10, 14);   // alpha 0.55 深色
    Gdiplus::SolidBrush shadowB(shadowC);
    Gdiplus::RectF layoutS = layout;
    layoutS.X += 1.6f; layoutS.Y += 1.8f;
    gfx.DrawString(text.c_str(), -1, &gfont, layoutS, &sf, &shadowB);
    // 正文层
    Gdiplus::Color bodyC(255, GetRValue(color), GetGValue(color), GetBValue(color));
    Gdiplus::SolidBrush bodyB(bodyC);
    gfx.DrawString(text.c_str(), -1, &gfont, layout, &sf, &bodyB);
}

LyricDisplayItem::LyricDisplayItem()
{
    g_pLyricItem = this;
}

LyricDisplayItem::~LyricDisplayItem()
{
    StopHighFreqRefresh();
    g_pLyricItem = nullptr;
    
    if (m_font != nullptr)
    {
        DeleteObject(m_font);
        m_font = nullptr;
    }
}

const wchar_t* LyricDisplayItem::GetItemName() const
{
    if (m_itemName.empty())
    {
        AFX_MANAGE_STATE(AfxGetStaticModuleState());
        CString str;
        str.LoadString(IDS_LYRIC_ITEM_NAME);
        m_itemName = str.GetString();
    }
    return m_itemName.c_str();
}

const wchar_t* LyricDisplayItem::GetItemId() const
{
    return L"SPlayerLyric";
}

const wchar_t* LyricDisplayItem::GetItemLableText() const
{
    return L"";
}

const wchar_t* LyricDisplayItem::GetItemValueText() const
{
    return L"";
}

const wchar_t* LyricDisplayItem::GetItemValueSampleText() const
{
    return L"Sample Lyric Text For Width Calculation";
}

int LyricDisplayItem::GetItemWidth() const
{
    return g_config.Data().displayWidth;
}

int LyricDisplayItem::GetItemWidthEx(void* hDC) const
{
    return 0;
}

HFONT LyricDisplayItem::GetFont(HDC hDC) const
{
    // Try to capture taskbar window handle if not already captured
    if (g_pLyricItem && g_pLyricItem->m_taskbarWnd == NULL)
    {
        HWND hWnd = WindowFromDC(hDC);
        if (hWnd)
        {
            g_pLyricItem->m_taskbarWnd = hWnd;
             wchar_t buf[64];
             swprintf_s(buf, L"[SPlayerLyric] Captured hWnd from DC: %p\n", hWnd);
             OutputDebugStringW(buf);
        }
    }

    const auto& config = g_config.Data();

    if (m_font == nullptr ||
        m_lastFontSize != config.fontSize ||
        m_lastFontName != config.fontName ||
        m_lastFontBold != config.fontWeightBold)
    {
        if (m_font != nullptr)
        {
            DeleteObject(m_font);
        }

        int dpi = GetDeviceCaps(hDC, LOGPIXELSY);
        int fontHeight = -MulDiv(config.fontSize, dpi, 72);

        m_font = CreateFontW(
            fontHeight,
            0,
            0,
            0,
            config.fontWeightBold ? FW_BOLD : FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            config.fontName.c_str()
        );

        m_lastFontSize = config.fontSize;
        m_lastFontName = config.fontName;
        m_lastFontBold = config.fontWeightBold;
    }

    return m_font;
}

std::wstring LyricDisplayItem::GetDisplayText() const
{
    if (!g_wsClient.IsConnected())
    {
        return g_config.StringRes(IDS_NOT_CONNECTED);
    }

    std::wstring lyric = g_lyricMgr.GetCurrentLyricText();
    if (!lyric.empty())
    {
        return lyric;
    }

    std::wstring songInfo = g_lyricMgr.GetSongInfoText();
    if (!songInfo.empty())
    {
        return songInfo;
    }

    return g_config.StringRes(IDS_NO_LYRIC);
}

void LyricDisplayItem::UpdateScrollAnimation(int textWidth, int areaWidth)
{
    if (!g_config.Data().enableScrolling || textWidth <= areaWidth)
    {
        m_scrollOffset = 0;
        m_scrollStartTime = 0;
        m_scrollCycleLength = 0;
        return;
    }

    int maxOffset = textWidth - areaWidth + 20; // 20px padding
    
    // Scroll speed: 50 pixels per second (adjustable)
    float scrollSpeed = 50.0f;
    
    // Calculate time for one-way scroll
    float scrollDuration = (float)maxOffset / scrollSpeed * 1000.0f; // in ms
    float pauseDuration = 1500.0f; // 1.5 second pause at each end
    
    // Total cycle: scroll right -> pause -> scroll left -> pause
    float cycleDuration = scrollDuration * 2.0f + pauseDuration * 2.0f;
    
    ULONGLONG now = GetTickCount64();
    
    // Initialize start time if needed
    if (m_scrollStartTime == 0 || m_scrollCycleLength != (ULONGLONG)cycleDuration)
    {
        m_scrollStartTime = now;
        m_scrollCycleLength = (ULONGLONG)cycleDuration;
    }
    
    // Calculate position in cycle
    float cyclePos = (float)((now - m_scrollStartTime) % (ULONGLONG)cycleDuration);
    
    if (cyclePos < pauseDuration)
    {
        // Pause at start
        m_scrollOffset = 0;
    }
    else if (cyclePos < pauseDuration + scrollDuration)
    {
        // Scrolling right
        float t = (cyclePos - pauseDuration) / scrollDuration;
        // Use ease-in-out for smoother animation
        float eased = t < 0.5f ? 2.0f * t * t : 1.0f - pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
        m_scrollOffset = eased * maxOffset;
    }
    else if (cyclePos < pauseDuration * 2.0f + scrollDuration)
    {
        // Pause at end
        m_scrollOffset = (float)maxOffset;
    }
    else
    {
        // Scrolling left
        float t = (cyclePos - pauseDuration * 2.0f - scrollDuration) / scrollDuration;
        float eased = t < 0.5f ? 2.0f * t * t : 1.0f - pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
        m_scrollOffset = (1.0f - eased) * maxOffset;
    }
}

void LyricDisplayItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    HDC dc = static_cast<HDC>(hDC);
    const auto& config = g_config.Data();

    // Hide when not playing if enabled
    if (config.hideWhenNotPlaying && (!g_wsClient.IsConnected() || !g_lyricMgr.IsPlaying()))
    {
        return;
    }

    // Set font
    HFONT font = GetFont(dc);
    HFONT oldFont = nullptr;
    if (font != nullptr)
    {
        oldFont = (HFONT)SelectObject(dc, font);
    }

    SetBkMode(dc, TRANSPARENT);

    // Check if dual line display is enabled
    if (config.desktopDualLine)
    {
        DrawDualLine(dc, x, y, w, h, dark_mode);
    }
    // Check if we have YRC data for highlight rendering
    else if (config.enableYrc && g_lyricMgr.HasYrcData() && g_wsClient.IsConnected())
    {
        DrawWithYrcHighlight(dc, x, y, w, h, dark_mode);
    }
    else
    {
        DrawSimpleText(dc, x, y, w, h, dark_mode);
    }

    // ---- v13: 监控行插件自绘（全透明+柔和阴影，仅悬浮窗三行模式；任务栏两行不放）----
    // 行1: ↑速度 CPU% 温度   行2: ↓速度 内存% 显卡%（数据源 TM 主程序接口快照）
    if (config.desktopDualLine && h >= 100 && g_monSnap[0] >= 0)
    {
        int dpi2 = GetDeviceCaps(dc, LOGPIXELSY);
        int mFontH = -MulDiv(9, dpi2, 72);
        HFONT monFont = CreateFontW(
            mFontH, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            config.fontName.c_str());
        if (monFont)
        {
            std::wstring lineA = L"\u2191 " + FormatSpeed(g_monSnap[0])
                + L"   CPU " + std::to_wstring((int)(g_monSnap[2] + 0.5)) + L"%"
                + L"   " + std::to_wstring((int)(g_monSnap[5] + 0.5)) + L"\u00B0C";
            std::wstring lineB = L"\u2193 " + FormatSpeed(g_monSnap[1])
                + L"   \u5185\u5b58 " + std::to_wstring((int)(g_monSnap[3] + 0.5)) + L"%"
                + L"   \u663e\u5361 " + std::to_wstring((int)(g_monSnap[4] + 0.5)) + L"%";
            COLORREF monColor = dark_mode ? RGB(200, 216, 232) : RGB(70, 84, 100);
            SIZE szA;
            HFONT oldMon = (HFONT)SelectObject(dc, monFont);
            GetTextExtentPoint32W(dc, lineA.c_str(), (int)lineA.length(), &szA);
            int textH = szA.cy;
            int monY = y + h - textH * 2 - 8;
            int xa = x + (w - szA.cx) / 2;
            SoftShadowText(dc, xa, monY, lineA, monColor, monFont);
            SoftShadowText(dc, xa, monY + textH + 4, lineB, monColor, monFont);
            SelectObject(dc, oldMon);
            DeleteObject(monFont);
        }
    }

    // Restore font
    if (oldFont != nullptr)
    {
        SelectObject(dc, oldFont);
    }
}

void LyricDisplayItem::DrawDualLine(HDC dc, int x, int y, int w, int h, bool dark_mode)
{
    const auto& config = g_config.Data();
    
    // Create font for dual line mode (uses dualLineFontSize instead of fontSize)
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    int fontHeight = -MulDiv(config.dualLineFontSize, dpi, 72);
    HFONT dualFont = CreateFontW(
        fontHeight, 0, 0, 0,
        config.fontWeightBold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        config.fontName.c_str()
    );
    HFONT oldFont = (HFONT)SelectObject(dc, dualFont);
    
    // Get current and second line text
    std::wstring line1 = g_lyricMgr.GetCurrentLyricText();
    std::wstring line2;
    
    if (config.secondLineType == 0)
    {
        // Next line
        line2 = g_lyricMgr.GetNextLyricText();
    }
    else if (config.secondLineType == 1)
    {
        // Translation
        line2 = g_lyricMgr.GetCurrentTranslation();
    }
    else
    {
        // Artist/Song info
        line2 = g_lyricMgr.GetSongInfoText();
    }
    
    // If no current lyric, show song info or default
    if (line1.empty())
    {
        if (!g_wsClient.IsConnected())
        {
            line1 = g_config.StringRes(IDS_NOT_CONNECTED);
        }
        else
        {
            line1 = g_lyricMgr.GetSongInfoText();
            if (line1.empty())
                line1 = g_config.StringRes(IDS_NO_LYRIC);
        }
        line2.clear();
    }
    
    // If second line is empty, and we are not in "Artist" mode, check if we should show single line
    if (line2.empty() && config.secondLineType != 2)
    {
        // Restore old font and use DrawSimpleText instead to use the larger font size if appropriate,
        // OR just draw it centered here with the current dual line font.
        // The user said "single line display", usually implying the standard single line look.
        
        SelectObject(dc, oldFont);
        DeleteObject(dualFont);
        
        DrawSimpleText(dc, x, y, w, h, dark_mode);
        return;
    }
    
    // Fallback to song info if second line is still empty (e.g., in Artist mode but info empty)
    if (line2.empty())
    {
        line2 = g_lyricMgr.GetSongInfoText();
    }

    // Double check if we still have only one line after fallback
    if (line2.empty())
    {
        SelectObject(dc, oldFont);
        DeleteObject(dualFont);
        DrawSimpleText(dc, x, y, w, h, dark_mode);
        return;
    }
    
    // Calculate layout
    // Instead of h/2, we'll give each line a bit more breathing room by not clipping too strictly
    // v11: 按绘制区高度自动分叉——悬浮窗（h>=45）三行 / 任务栏（h<45）两行（当前+下一句）。
    // 用户需求：任务栏精简两行+换行动画，悬浮窗保留三行。皮肤 layout_s 给矮区即走两行路径。
    // twoLineTaskbar 模式下：prev 槽画"当前句"（角色=cur），next 槽画"下一句"，无 prev 行。
    bool twoLineTaskbar = (h < 45);
    bool threeLineMode = (config.threeLine != 0) && !twoLineTaskbar;
    int lineHeight = threeLineMode ? h / 3 : h / 2;

    // ---- 平滑换行 v8/v11：连续歌词带整体上滚一行（数学上不可能跳）----
    // 三行：[A,B,C] -> [B,C,D]（A=旧prev B=旧cur C=旧next D=新next），4 行带上移一行。
    // 两行任务栏(v11)：[B,C] -> [C,D]，带 = [B,C,D] 上移一行——A 不存在（顶行直接是 cur），
    //   静态时 B 行画在槽1（=drawY 的 cur 位）、C 行画在槽2；换行时 B 滑出、C 升到槽1、D 从底滑入。
    //   v8 数学通用：bandShift 从 0 滚到 lineHeight，起止帧与静态帧逐像素吻合。
    int curLineIdx = g_lyricMgr.GetCurrentLineIndex();
    // v11 两行任务栏：换行检测【之前】缓存当前句（此刻仍是旧 cur = 下次动画的 B/A 行）
    if (twoLineTaskbar && curLineIdx != m_dualLastLineIndex && !line1.empty())
        m_dualTopRowText = line1;
    if (curLineIdx != m_dualLastLineIndex)
    {
        if (m_dualLastLineIndex != -1 && (threeLineMode || twoLineTaskbar))   // v11: 两行任务栏也启动动画
        {
            m_dualTransitionStart = GetTickCount64();
            m_dualInTransition = true;
        }
        m_dualLastLineIndex = curLineIdx;
    }
    int bandShift = lineHeight;   // 静止 = 换行后布局（drawY=y）；动画中从 0 连续滚到 lineHeight
    if (m_dualInTransition)
    {
        ULONGLONG tnow = GetTickCount64();
        if (tnow - m_dualTransitionStart > 400)
        {
            m_dualInTransition = false;
            // bandShift 保持 lineHeight，与下一静态帧无缝
        }
        else
        {
            float prog = (float)(tnow - m_dualTransitionStart) / 400.0f;
            prog = 1.0f - pow(1.0f - prog, 3.0f);   // ease-out（原作者同款）
            bandShift = (int)(lineHeight * prog + 0.5f);   // 0 → lineHeight（只滚一个行高）
        }
    }
    int drawY = y + lineHeight - bandShift;   // 动画: y+lineHeight→y；静止: y（三行顶=窗口顶）
    // bandT: 动画进度 0→1（颜色插值）；静止恒 1（换行后角色色）
    float bandT = (lineHeight > 0) ? (float)bandShift / (float)lineHeight : 1.0f;
    
    // Set colors based on dark mode and adaptive setting
    COLORREF primaryColor, secondaryColor, highlightColor;
    
    // Choose color set based on mode
    bool useDarkModeColors = dark_mode;
    if (!config.adaptiveColor)
    {
        // If adaptive disabled, maybe force dark mode colors (legacy behavior) or stick to user preference?
        // Let's assume non-adaptive means using the "Dark" set (or the legacy set which mapped to Dark)
        useDarkModeColors = true; 
    }

    if (useDarkModeColors)
    {
        primaryColor = config.darkNormalColor;
        // For secondary, we can make it slightly dimmer or same as primary
        // Let's make it 80% brightness of primary or just hardcoded dim if user doesn't specify secondary
        // Since we only have "Normal" and "Highlight" in config, we derive secondary line color.
        // Simple approach: Secondary is same as Primary for now, or slight transparency if we could? 
        // GDI doesn't support alpha easily. Let's stick to Primary.
        // Actually, user requested "Deep/Light mode color customization". 
        // We have lightNormalColor and darkNormalColor.
        
        secondaryColor = primaryColor; 
        
        // To distinguish secondary line, maybe hardcode a dimmer simple calculation or just use same color.
        // Previous hardcoded logic: RGB(200, 200, 200) vs White.
        // Let's try to slightly dim it if it's near white.
        if (GetRValue(primaryColor) > 200 && GetGValue(primaryColor) > 200 && GetBValue(primaryColor) > 200)
             secondaryColor = RGB(200, 200, 200);
             
        highlightColor = config.darkHighlightColor;
    }
    else
    {
        primaryColor = config.lightNormalColor;
        secondaryColor = primaryColor;
        // Dim slightly if near black?
        if (GetRValue(primaryColor) < 50 && GetGValue(primaryColor) < 50 && GetBValue(primaryColor) < 50)
             secondaryColor = RGB(80, 80, 80);
             
        highlightColor = config.lightHighlightColor;
    }
    
    // Dim if not playing
    if (!g_wsClient.IsConnected() || !g_lyricMgr.IsPlaying())
    {
        if (dark_mode)
        {
            primaryColor = RGB(150, 150, 150);
            secondaryColor = RGB(120, 120, 120);
        }
        else
        {
            primaryColor = RGB(100, 100, 100);
            secondaryColor = RGB(130, 130, 130);
        }
    }
    
    // v8 连续带角色着色（几何 = 一条带上移一行，颜色随角色同步过渡）：
    //   B 行（旧当前→新prev）：highlight(YRC唱满)/primary → prevRoleColor（bandT 插值）
    //   C 行（旧next→新当前）：secondary → primary（bandT 插值；YRC 基色用它）
    //   A 行（旧顶行离场）：恒 prevRoleColor   D 行（新next 入场）：恒 secondary
    COLORREF prevRoleColor = RGB(
        (GetRValue(secondaryColor) * 3) / 5,
        (GetGValue(secondaryColor) * 3) / 5,
        (GetBValue(secondaryColor) * 3) / 5);   // 60% dim（与 prev 槽一致）
    COLORREF leaveCurColor = primaryColor;    // B 行（YRC 时换成 highlight 起点）
    COLORREF enterCurColor = primaryColor;    // C 行（静止时=primary 正确）
    {
        auto words = g_lyricMgr.GetCurrentYrcWords();
        bool curIsYrc = (config.enableYrc && !words.empty() && g_wsClient.IsConnected() && g_lyricMgr.IsPlaying());
        if (!curIsYrc)
            m_dualPrevYrc = false;   // 本帧非 YRC（简单文本），重置标记
        else
            m_dualPrevYrc = true;    // 本帧 line1 走 YRC——下一帧换行时 B 行离场起点用 highlight
        if (m_dualInTransition && m_dualPrevYrc)
            leaveCurColor = highlightColor;   // B 行换行前是全高亮（逐字填满），离场起点用 highlight
    }
    if (m_dualInTransition)
    {
        leaveCurColor = RGB(
            GetRValue(leaveCurColor) + (int)((GetRValue(prevRoleColor) - GetRValue(leaveCurColor)) * bandT),
            GetGValue(leaveCurColor) + (int)((GetGValue(prevRoleColor) - GetGValue(leaveCurColor)) * bandT),
            GetBValue(leaveCurColor) + (int)((GetBValue(prevRoleColor) - GetBValue(leaveCurColor)) * bandT));
        enterCurColor = RGB(
            GetRValue(secondaryColor) + (int)((GetRValue(primaryColor) - GetRValue(secondaryColor)) * bandT),
            GetGValue(secondaryColor) + (int)((GetGValue(primaryColor) - GetGValue(secondaryColor)) * bandT),
            GetBValue(secondaryColor) + (int)((GetBValue(primaryColor) - GetBValue(secondaryColor)) * bandT));
    }

    // Draw first line (current lyric) - use top half
    // If YRC is enabled, use word-by-word discrete highlight for the first line
    auto words = g_lyricMgr.GetCurrentYrcWords();
    if (config.enableYrc && !words.empty() && g_wsClient.IsConnected() && g_lyricMgr.IsPlaying())
    {
        int64_t currentTime = g_lyricMgr.GetCurrentTime();
        // Use the highlightColor determined by adaptive logic above, do NOT re-read from config
        
        // Calculate total width of YRC words
        int totalWidth = 0;
        std::vector<SIZE> wordSizes;
        for (const auto& word : words)
        {
            SIZE sz;
            GetTextExtentPoint32W(dc, word.text.c_str(), (int)word.text.length(), &sz);
            wordSizes.push_back(sz);
            totalWidth += sz.cx;
        }

        // Calculate alignment X
        int textX1 = x + 5; // Default Left
        if (totalWidth < w) // Only apply alignment if text fits (otherwise we scroll)
        {
            if (config.dualLineAlignment == 1) // Center
                textX1 = x + (w - totalWidth) / 2;
            else if (config.dualLineAlignment == 2) // Right
                textX1 = x + w - totalWidth - 5;
            else if (config.dualLineAlignment == 3) // Split (Line 1 Left)
                textX1 = x + 5;
        }

        // Apply scroll if needed (override alignment if scrolling)
        if (config.enableScrolling && totalWidth > w - 10)
        {
            UpdateScrollAnimation(totalWidth, w - 10);
            textX1 = x + 5 - (int)m_scrollOffset;
        }

        // v11: 三行 cur 画槽2（drawY+lineHeight）；两行任务栏/双行 cur 画槽1（drawY）
        int line1Y = (threeLineMode) ? (drawY + lineHeight) : drawY;
        int textY1 = line1Y + (lineHeight - (wordSizes.empty() ? 0 : wordSizes[0].cy)) / 2;

        // Clip region for first line
        HRGN clipRgn1 = CreateRectRgn(x, line1Y, x + w, line1Y + lineHeight + 2);
        SelectClipRgn(dc, clipRgn1);

        int curX = textX1;
        for (size_t i = 0; i < words.size(); ++i)
        {
            const auto& word = words[i];
            int width = wordSizes[i].cx;

            // 1. Draw Normal Text (Background) —— v8 角色色 + v12 描边（全背景可读）
            SoftShadowText(dc, curX, textY1, word.text,
                            m_dualInTransition ? enterCurColor : primaryColor, dualFont);

            // 2. Draw Highlight Text (Foreground with clip)
            long long endTime = word.startTime + word.duration;
            double progress = 0.0;
            
            if (currentTime >= endTime) 
                progress = 1.0;
            else if (currentTime >= word.startTime && word.duration > 0)
                progress = (double)(currentTime - word.startTime) / word.duration;

            if (progress > 0.001)
            {
                int effectiveWidth = (int)(width * progress);
                if (effectiveWidth > 0)
                {
                    int saveId = SaveDC(dc);
                    HRGN wordClip = CreateRectRgn(curX, line1Y, curX + effectiveWidth, line1Y + lineHeight + 2);
                    ExtSelectClipRgn(dc, wordClip, RGN_AND);
                    
                    SetTextColor(dc, highlightColor);
                    TextOutW(dc, curX, textY1, word.text.c_str(), (int)word.text.length());  // 高亮填充层（描边由底色层负责）
                    
                    RestoreDC(dc, saveId);
                    DeleteObject(wordClip);
                }
            }

            curX += width;
        }
        
        SelectClipRgn(dc, NULL);
        DeleteObject(clipRgn1);
    }
    else
    {
        // Simple text for first line —— v8 角色色 + v12 描边
        SetTextColor(dc, m_dualInTransition ? enterCurColor : primaryColor);
        SIZE size1;
        GetTextExtentPoint32W(dc, line1.c_str(), (int)line1.length(), &size1);
        
        // v11: 三行 cur 画槽2（drawY+lineHeight）；两行任务栏/双行 cur 画槽1（drawY）
        int line1Y = (threeLineMode) ? (drawY + lineHeight) : drawY;
        int textY1 = line1Y + (lineHeight - size1.cy) / 2;
        int textX1 = x + 5; // Default Left
        
        if (size1.cx < w)
        {
            if (config.dualLineAlignment == 1) // Center
                textX1 = x + (w - size1.cx) / 2;
            else if (config.dualLineAlignment == 2) // Right
                textX1 = x + w - size1.cx - 5;
            else if (config.dualLineAlignment == 3) // Split (Line 1 Left)
                textX1 = x + 5;
        }
        
        if (config.enableScrolling && size1.cx > w - 10)
        {
            UpdateScrollAnimation(size1.cx, w - 10);
            textX1 = x + 5 - (int)m_scrollOffset;
        }
        
        HRGN clipRgn1 = CreateRectRgn(x, line1Y, x + w, line1Y + lineHeight + 2);
        SelectClipRgn(dc, clipRgn1);
        SoftShadowText(dc, textX1, textY1, line1,
                        m_dualInTransition ? enterCurColor : primaryColor, dualFont);
        SelectClipRgn(dc, NULL);
        DeleteObject(clipRgn1);
    }
    
    // Draw second line (next): dual=bottom half, three-line=bottom third
    // v8: 三行动画期 line1=C（已由第一行块做 next→cur 渐变），此处 line2=D（新 next）恒 secondary。
    //     两行模式不参与连续带，保持静态。
    SetTextColor(dc, secondaryColor);
    SIZE size2;
    GetTextExtentPoint32W(dc, line2.c_str(), (int)line2.length(), &size2);

    int line2Y = threeLineMode ? (drawY + 2 * lineHeight) : (drawY + lineHeight);
    int textY2 = line2Y + (lineHeight - size2.cy) / 2;
    int textX2 = x + 5;
    
    // Apply alignment for second line
    if (size2.cx < w) // Only if fits
    {
        if (config.dualLineAlignment == 1) // Center
            textX2 = x + (w - size2.cx) / 2;
        else if (config.dualLineAlignment == 2) // Right
            textX2 = x + w - size2.cx - 5;
        else if (config.dualLineAlignment == 3) // Split (Line 2 Right)
            textX2 = x + w - size2.cx - 5;
    }
    
    // Scrolling for second line (independent or just static?) - keep it static for now as per design
    
    // Clip for second line - allow a bit room at top for ascenders
    HRGN clipRgn2 = CreateRectRgn(x, y, x + w, y + h);
    SelectClipRgn(dc, clipRgn2);
    SoftShadowText(dc, textX2, textY2, line2, secondaryColor, dualFont);
    SelectClipRgn(dc, NULL);
    DeleteObject(clipRgn2);

    // ---- v8/v11: A 行（旧顶行）随带整体上移离场 ----
    // 三行：A=旧prev；两行任务栏：A=旧cur（v11 静态缓存于下方 prev 块的 twoLine 分支）。
    // 动画期画在 y - bandShift（与主体同一位移场），滚出顶边自然裁掉。
    if (m_dualInTransition && (threeLineMode || twoLineTaskbar) && bandShift > 0 && !m_dualTopRowText.empty())
    {
        // 三行：A=旧prev（prevRole 恒暗色离场）。
        // 两行任务栏：A=旧cur（从全高亮绿渐变到暗色离场，与 v8 B 行同款角色过渡）
        COLORREF aColor = prevRoleColor;
        if (twoLineTaskbar)
        {
            aColor = RGB(
                GetRValue(leaveCurColor) + (int)((GetRValue(prevRoleColor) - GetRValue(leaveCurColor)) * bandT),
                GetGValue(leaveCurColor) + (int)((GetGValue(prevRoleColor) - GetGValue(leaveCurColor)) * bandT),
                GetBValue(leaveCurColor) + (int)((GetBValue(prevRoleColor) - GetBValue(leaveCurColor)) * bandT));
        }
        SetTextColor(dc, aColor);
        SIZE szA;
        GetTextExtentPoint32W(dc, m_dualTopRowText.c_str(), (int)m_dualTopRowText.length(), &szA);
        int tY = (y - bandShift) + (lineHeight - szA.cy) / 2;
        int tX = x + 5;
        if (szA.cx < w)
        {
            if (config.dualLineAlignment == 1)
                tX = x + (w - szA.cx) / 2;
            else if (config.dualLineAlignment == 2)
                tX = x + w - szA.cx - 5;
        }
        HRGN clipA = CreateRectRgn(x, y, x + w, y + h);   // 裁剪到显示区，滚出部分不绘制
        SelectClipRgn(dc, clipA);
        SoftShadowText(dc, tX, tY, m_dualTopRowText, aColor, dualFont);
        SelectClipRgn(dc, NULL);
        DeleteObject(clipA);
    }

    // ---- Draw prev line (three-line mode only): top slot, dimmed ----
    // v8 连续带：顶槽文本 = 实时 prev（换行后即 B 行），绘制基准 = drawY
    // （随 bandShift 从 y+lineHeight 滚到 y：B 行从 cur 槽滑到 prev 槽）。
    // 动画期间颜色 bandT 插值（cur 基色→prev 暗色）；静止 = prevRoleColor。
    // A 行（旧顶行）由上方 v8 滑出块绘制，与本块无重叠。
    // v11 两行任务栏：无 prev 行，此块跳过；但静止期缓存"当前句"作下次动画的 A 行（旧cur）。
    if (threeLineMode)
    {
        std::wstring line0 = g_lyricMgr.GetPrevLyricText();
        if (!m_dualInTransition)          // 动画期间不重缓存：保持 A 行（换行前的顶行）
            m_dualTopRowText = line0;     // 静止期刷新缓存 = 下次换行的 A 行
        if (!line0.empty())
        {
            SetTextColor(dc, m_dualInTransition ? leaveCurColor : prevRoleColor);
            SIZE size0;
            GetTextExtentPoint32W(dc, line0.c_str(), (int)line0.length(), &size0);
            int textY0 = drawY + (lineHeight - size0.cy) / 2;
            int textX0 = x + 5;
            if (size0.cx < w)
            {
                if (config.dualLineAlignment == 1)
                    textX0 = x + (w - size0.cx) / 2;
                else if (config.dualLineAlignment == 2)
                    textX0 = x + w - size0.cx - 5;
            }
            HRGN clipRgn0 = CreateRectRgn(x, y, x + w, y + h);
            SelectClipRgn(dc, clipRgn0);
            SoftShadowText(dc, textX0, textY0, line0,
                            m_dualInTransition ? leaveCurColor : prevRoleColor, dualFont);
            SelectClipRgn(dc, NULL);
            DeleteObject(clipRgn0);
        }
    }


    // Restore original font and cleanup
    SelectObject(dc, oldFont);
    DeleteObject(dualFont);
}

void LyricDisplayItem::DrawSimpleText(HDC dc, int x, int y, int w, int h, bool dark_mode)
{
    std::wstring text = GetDisplayText();
    if (text.empty())
        return;

    // Set text color based on dark mode
    COLORREF textColor;
    
    bool useDarkModeColors = dark_mode;
    if (!g_config.Data().adaptiveColor) useDarkModeColors = true; // Force dark set if not adaptive

    if (useDarkModeColors)
        textColor = g_config.Data().darkNormalColor;
    else
        textColor = g_config.Data().lightNormalColor;

    // Dim if not playing
    if (!g_wsClient.IsConnected() || !g_lyricMgr.IsPlaying())
    {
        if (dark_mode)
            textColor = RGB(150, 150, 150);
        else
            textColor = RGB(100, 100, 100);
    }

    SetTextColor(dc, textColor);

    // Calculate text size
    SIZE textSize;
    GetTextExtentPoint32W(dc, text.c_str(), (int)text.length(), &textSize);

    // Update scroll animation
    UpdateScrollAnimation(textSize.cx, w);

    int textY = y + (h - textSize.cy) / 2;

    if (textSize.cx > w && g_config.Data().enableScrolling)
    {
        // Create clipping region
        HRGN clipRgn = CreateRectRgn(x, y, x + w, y + h);
        SelectClipRgn(dc, clipRgn);

        int textX = x - (int)m_scrollOffset + g_config.Data().desktopXOffset; // Apply offset
        TextOutW(dc, textX, textY, text.c_str(), (int)text.length());

        SelectClipRgn(dc, NULL);
        DeleteObject(clipRgn);
    }
    else if (textSize.cx > w)
    {
        // Ellipsis mode
        RECT drawRect = { x, textY, x + w, textY + textSize.cy };
        DrawTextW(dc, text.c_str(), (int)text.length(), &drawRect,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    else
    {
        // Center
        int textX = x + (w - textSize.cx) / 2;
        textX += g_config.Data().desktopXOffset; // Apply offset
        TextOutW(dc, textX, textY, text.c_str(), (int)text.length());
    }
}

void LyricDisplayItem::DrawWithYrcHighlight(HDC dc, int x, int y, int w, int h, bool dark_mode)
{
    auto words = g_lyricMgr.GetCurrentYrcWords();
    int currentLineIdx = g_lyricMgr.GetCurrentLineIndex();

    // Check for line change to trigger transition
    if (currentLineIdx != m_lastLineIndex)
    {
        if (m_lastLineIndex != -1) // Don't animate first load
        {
            m_transitionStartTime = GetTickCount64();
            m_inTransition = true;
            m_prevLineText = GetDisplayText(); // Use full text for prev line
            // Wait, GetDisplayText() returns CURRENT text. We need previous text.
            // But m_lastLineIndex is old, LyricManager already has new data.
            // We should have stored previous text before update? No, LyricManager updates async.
            // We can't query old text easily from LyricManager unless we cache it.
            // However, m_prevLineText should be updated AFTER drawing, or assume we missed it?
            // Actually, best way: Store m_currentText in class, update it at end of frame.
            // But let's cheat: we can't easily get old YRC words, so for "Previous Line", 
            // we will just draw it as simple text (m_prevLineText).
        }
        m_lastLineIndex = currentLineIdx;
    }

    if (words.empty())
    {
        // If we are transitioning FROM valid words to empty, we should still animate?
        // For simplicity, falls back to SimpleText if empty
        DrawSimpleText(dc, x, y, w, h, dark_mode);
        
        // Update valid cache
        m_prevLineText = GetDisplayText();
        return;
    }

    int64_t currentTime = g_lyricMgr.GetCurrentTime();
    const auto& config = g_config.Data();

    // Set colors based on dark mode
    COLORREF normalColor, highlightColor;
    
    bool useDarkModeColors = dark_mode;
    if (!config.adaptiveColor) useDarkModeColors = true;

    if (useDarkModeColors)
    {
        normalColor = config.darkNormalColor;
        highlightColor = config.darkHighlightColor;
    }
    else
    {
        normalColor = config.lightNormalColor;
        highlightColor = config.lightHighlightColor;
    }

    // Dim if not playing
    if (!g_wsClient.IsConnected() || !g_lyricMgr.IsPlaying())
    {
        if (dark_mode)
        {
            normalColor = RGB(120, 120, 120);
            highlightColor = RGB(150, 150, 150);
        }
        else
        {
            normalColor = RGB(150, 150, 150);
            highlightColor = RGB(100, 100, 100);
        }
    }

    // First pass: calculate total width
    int totalWidth = 0;
    std::vector<SIZE> wordSizes;
    for (const auto& word : words)
    {
        SIZE size;
        GetTextExtentPoint32W(dc, word.text.c_str(), (int)word.text.length(), &size);
        wordSizes.push_back(size);
        totalWidth += size.cx;
    }

    // Update scroll
    UpdateScrollAnimation(totalWidth, w);

    int textY = y + (h - (wordSizes.empty() ? 0 : wordSizes[0].cy)) / 2;
    
    // Calculate starting X position
    // Calculate starting X position based on alignment
    int startX = x + 5; // Default Left
    
    if (totalWidth < w)
    {
        if (config.dualLineAlignment == 1) // Center
        {
            startX = x + (w - totalWidth) / 2;
        }
        else if (config.dualLineAlignment == 2) // Right
        {
            startX = x + w - totalWidth - 5;
        }
        else if (config.dualLineAlignment == 3) // Split -> Line 1 Left
        {
            startX = x + 5;
        }
        // else 0 (Left) -> x + 5
    }
    else if (config.enableScrolling)
    {
        startX = x + 5 - (int)m_scrollOffset;
    }
    
    // Apply global offset
    startX += config.desktopXOffset;

    // Create clipping region if needed
    HRGN clipRgn = nullptr;
    if (totalWidth > w)
    {
        clipRgn = CreateRectRgn(x, y, x + w, y + h);
        SelectClipRgn(dc, clipRgn);
    }

    // Draw each word
    // Draw Transition (Scroll Up)
    if (m_inTransition)
    {
        ULONGLONG now = GetTickCount64();
        if (now - m_transitionStartTime > 400) // 400ms duration
        {
            m_inTransition = false;
        }
        else
        {
            float progress = (float)(now - m_transitionStartTime) / 400.0f;
            // Ease out
            progress = 1.0f - pow(1.0f - progress, 3.0f);
            
            int yOffset = (int)(h * progress);
            
            // Draw Previous Line (moving up: y -> y - h)
            if (!m_prevLineText.empty())
            {
                 int prevY = y - yOffset;
                 
                 // Fade out simulation: blend color with background?
                 // Since we don't know bg, let's just use clip.
                 // Text fading is hard in GDI without AlphaBlend.
                 // We just let it move out.
                 
                 // But wait, DrawSimpleText centers vertically usually.
                 // We need custom drawing for prev line to ensure position matches.
                 // Let's reuse basic TextOut logic for prev line.
                 
                 SIZE prevSize;
                 GetTextExtentPoint32W(dc, m_prevLineText.c_str(), (int)m_prevLineText.length(), &prevSize);
                 
                 // Align prev line same as current (approx)
                 int prevX = startX; // This might jump if alignment changes, but usually consistent
                 if (config.dualLineAlignment == 1) prevX = x + (w - prevSize.cx) / 2 + config.desktopXOffset;
                 else if (config.dualLineAlignment == 2) prevX = x + w - prevSize.cx - 5 + config.desktopXOffset;
                 
                 // Center vertically in the OFFSET position
                 int prevTextY = prevY + (h - prevSize.cy) / 2;
                 
                 SetTextColor(dc, normalColor); // Old line is normal color
                 
                 HRGN prevClip = CreateRectRgn(x, y, x + w, y + h);
                 SelectClipRgn(dc, prevClip);
                 TextOutW(dc, prevX, prevTextY, m_prevLineText.c_str(), (int)m_prevLineText.length());
                 SelectClipRgn(dc, NULL);
                 DeleteObject(prevClip);
            }
            
            // Draw Current Line (moving up: y + h -> y)
            // We modify 'y' and 'textY' for the main drawing loop below
            // Original textY calculation:
            // int textY = y + (h - (wordSizes.empty() ? 0 : wordSizes[0].cy)) / 2;
            
            // We want it to start at y+h and move to y
            // So offset is (1 - progress) * h? No.
            // Current line should be at y + h - yOffset = y + h - (h*progress) = y + h * (1 - progress).
            
            // Adjust textY for the loop below
            int enterOffset = (int)(h * (1.0f - progress));
            textY += enterOffset;
        }
    }

    if (clipRgn)
    {
        // Re-select clip region if we messed with it (we did)
        SelectClipRgn(dc, clipRgn);
    }
    
    // Draw each word with smooth highlight
    int currentX = startX;
    for (size_t i = 0; i < words.size(); i++)
    {
        const auto& word = words[i];
        const auto& size = wordSizes[i];

        // 1. Draw Background (Normal Color)
        SetTextColor(dc, normalColor);
        TextOutW(dc, currentX, textY, word.text.c_str(), (int)word.text.length());

        // 2. Calculate Progress
        double progress = 0.0;
        long long endTime = word.startTime + word.duration;
        
        if (currentTime >= endTime)
            progress = 1.0;
        else if (currentTime >= word.startTime && word.duration > 0)
            progress = (double)(currentTime - word.startTime) / word.duration;

        // 3. Draw Foreground (Highlight Color) with Clip
        if (progress > 0.001)
        {
            int fillWidth = (int)(size.cx * progress);
            if (fillWidth > 0)
            {
                int saveId = SaveDC(dc);
                
                // Clip rect: currentX to currentX + fillWidth
                // Must intersect with existing clip if scrolling
                HRGN wordClip = CreateRectRgn(currentX, y, currentX + fillWidth, y + h);
                ExtSelectClipRgn(dc, wordClip, RGN_AND);
                
                SetTextColor(dc, highlightColor);
                TextOutW(dc, currentX, textY, word.text.c_str(), (int)word.text.length());
                
                RestoreDC(dc, saveId);
                DeleteObject(wordClip);
            }
        }

        currentX += size.cx;
    }

    if (clipRgn)
    {
        SelectClipRgn(dc, NULL);
        DeleteObject(clipRgn);
    }
    
    // Update cache for next frame detection
    // Note: this is slightly wrong because "GetDisplayText()" might not match "words" if race condition
    // But close enough for visual transition
    m_prevLineText = GetDisplayText();
}

int LyricDisplayItem::OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag)
{
    // Save taskbar window handle for high-frequency refresh
    if (flag & MF_TASKBAR_WND)
    {
        m_taskbarWnd = (HWND)hWnd;
    }
    
    switch (type)
    {
    case MT_LCLICKED:
        g_wsClient.SendControl(SPlayerProtocol::ControlCommand::Toggle);
        return 1;

    case MT_DBCLICKED:
        return 0;

    case MT_RCLICKED:
        return 0;

    case MT_WHEEL_UP:
        g_wsClient.SendControl(SPlayerProtocol::ControlCommand::Prev);
        return 1;

    case MT_WHEEL_DOWN:
        g_wsClient.SendControl(SPlayerProtocol::ControlCommand::Next);
        return 1;

    default:
        return 0;
    }
}

// High-frequency refresh timer callback
void CALLBACK LyricDisplayItem::HighFreqTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    if (g_pLyricItem && g_pLyricItem->m_highFreqEnabled && g_pLyricItem->m_taskbarWnd)
    {
        // Only refresh when lyric available and playing
        // v10: LRC 歌也驱动重绘（换行动画），YRC 逐字缺席时动画照常
        if (g_config.Data().enableYrc && g_lyricMgr.IsPlaying() && g_lyricMgr.HasAnyLyricData())
        {
            // Invalidate the entire taskbar window to trigger redraw
            // TrafficMonitor will call DrawItem when processing WM_PAINT
            InvalidateRect(g_pLyricItem->m_taskbarWnd, NULL, FALSE);
        }
    }
}

void LyricDisplayItem::StartHighFreqRefresh()
{
    if (m_highFreqTimerId == 0)
    {
        // Create timer with ~100ms interval (10 FPS boost)
        // Using NULL for hwnd makes it a thread timer
        m_highFreqTimerId = SetTimer(NULL, 0, 16, HighFreqTimerProc);
        m_highFreqEnabled = true;
        OutputDebugStringW(L"[SPlayerLyric] High-frequency refresh started\n");
    }
}

void LyricDisplayItem::StopHighFreqRefresh()
{
    if (m_highFreqTimerId != 0)
    {
        KillTimer(NULL, m_highFreqTimerId);
        m_highFreqTimerId = 0;
        m_highFreqEnabled = false;
        OutputDebugStringW(L"[SPlayerLyric] High-frequency refresh stopped\n");
    }
}
