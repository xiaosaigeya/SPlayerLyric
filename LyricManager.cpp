/*
 * SPlayerLyric - SPlayer Lyric Display Plugin for TrafficMonitor
 *
 * Lyric Manager Implementation
 */

#include "pch.h"
#include <afxwin.h>
#include "LyricManager.h"
#include "Config.h"
#include <algorithm>

LyricManager& LyricManager::Instance()
{
    static LyricManager instance;
    return instance;
}

void LyricManager::UpdateLyrics(const SPlayerProtocol::LyricData& data)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lyricData = data;
    m_currentLineIndex = -1;

    // Debug
    wchar_t buf[256];
    swprintf_s(buf, L"[SPlayerLyric] UpdateLyrics: LRC=%zu, YRC=%zu\n", 
        data.lrcData.size(), data.yrcData.size());
    OutputDebugStringW(buf);
}

void LyricManager::UpdateProgress(int64_t currentTime)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentTime = currentTime; // Base time from SPlayer
    m_lastUpdateTick = GetTickCount64();
    m_currentLineIndex = FindCurrentLine(GetTimeWithOffset()); // Use helper for indexing
}

int64_t LyricManager::GetTimeWithOffset() const
{
    return m_currentTime + g_config.Data().lyricOffset;
}

void LyricManager::UpdateSongInfo(const SPlayerProtocol::SongInfo& info)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_songInfo = info;
}

void LyricManager::UpdatePlayStatus(bool isPlaying)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_isPlaying = isPlaying;
}

void LyricManager::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lyricData = SPlayerProtocol::LyricData();
    m_songInfo = SPlayerProtocol::SongInfo();
    m_currentTime = 0;
    m_currentLineIndex = -1;
    m_isPlaying = false;
}

std::wstring LyricManager::GetCurrentLyricText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (g_config.Data().enableYrc && m_lyricData.hasYrc())
    {
        if (m_currentLineIndex >= 0 && m_currentLineIndex < (int)m_lyricData.yrcData.size())
        {
            const auto& line = m_lyricData.yrcData[m_currentLineIndex];
            std::wstring result;
            for (const auto& word : line.words)
            {
                result += word.text;
            }
            return result;
        }
    }
    else if (m_lyricData.hasLrc())
    {
        if (m_currentLineIndex >= 0 && m_currentLineIndex < (int)m_lyricData.lrcData.size())
        {
            return m_lyricData.lrcData[m_currentLineIndex].text;
        }
    }

    return std::wstring();
}

std::wstring LyricManager::GetPrevLyricText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    int prevIndex = m_currentLineIndex - 1;

    if (g_config.Data().enableYrc && m_lyricData.hasYrc())
    {
        if (prevIndex >= 0 && prevIndex < (int)m_lyricData.yrcData.size())
        {
            const auto& line = m_lyricData.yrcData[prevIndex];
            std::wstring result;
            for (const auto& word : line.words)
            {
                result += word.text;
            }
            return result;
        }
    }
    else if (m_lyricData.hasLrc())
    {
        if (prevIndex >= 0 && prevIndex < (int)m_lyricData.lrcData.size())
        {
            return m_lyricData.lrcData[prevIndex].text;
        }
    }

    return std::wstring();
}

std::wstring LyricManager::GetNextLyricText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    int nextIndex = m_currentLineIndex + 1;

    if (g_config.Data().enableYrc && m_lyricData.hasYrc())
    {
        if (nextIndex >= 0 && nextIndex < (int)m_lyricData.yrcData.size())
        {
            const auto& line = m_lyricData.yrcData[nextIndex];
            std::wstring result;
            for (const auto& word : line.words)
            {
                result += word.text;
            }
            return result;
        }
    }
    else if (m_lyricData.hasLrc())
    {
        if (nextIndex >= 0 && nextIndex < (int)m_lyricData.lrcData.size())
        {
            return m_lyricData.lrcData[nextIndex].text;
        }
    }

    return std::wstring();
}

std::wstring LyricManager::GetCurrentTranslation() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Get translation from current line's translation field
    if (g_config.Data().enableYrc && m_lyricData.hasYrc())
    {
        if (m_currentLineIndex >= 0 && m_currentLineIndex < (int)m_lyricData.yrcData.size())
        {
            return m_lyricData.yrcData[m_currentLineIndex].translation;
        }
    }
    else if (m_lyricData.hasLrc())
    {
        if (m_currentLineIndex >= 0 && m_currentLineIndex < (int)m_lyricData.lrcData.size())
        {
            return m_lyricData.lrcData[m_currentLineIndex].translation;
        }
    }

    return std::wstring();
}

std::wstring LyricManager::GetSongInfoText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_songInfo.title.empty())
        return m_songInfo.title;

    if (!m_songInfo.name.empty())
    {
        if (!m_songInfo.artist.empty())
            return m_songInfo.name + L" - " + m_songInfo.artist;
        return m_songInfo.name;
    }

    return std::wstring();
}

bool LyricManager::HasLyric() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_lyricData.empty();
}

bool LyricManager::HasYrcData() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lyricData.hasYrc();
}

std::vector<SPlayerProtocol::YrcWord> LyricManager::GetCurrentYrcWords() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_lyricData.hasYrc() || m_currentLineIndex < 0 || 
        m_currentLineIndex >= (int)m_lyricData.yrcData.size())
    {
        return std::vector<SPlayerProtocol::YrcWord>();
    }
    
    return m_lyricData.yrcData[m_currentLineIndex].words;
}

float LyricManager::GetWordProgress() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!g_config.Data().enableYrc || !m_lyricData.hasYrc())
        return 1.0f;

    if (m_currentLineIndex < 0 || m_currentLineIndex >= (int)m_lyricData.yrcData.size())
        return 0.0f;

    const auto& line = m_lyricData.yrcData[m_currentLineIndex];
    if (line.words.empty())
        return 1.0f;

    int64_t lineStart = line.startTime;
    int64_t lineEnd = line.endTime;

    if (m_currentTime < lineStart)
        return 0.0f;
    if (m_currentTime >= lineEnd)
        return 1.0f;

    int64_t lineElapsed = m_currentTime - lineStart;
    return static_cast<float>(lineElapsed) / static_cast<float>(lineEnd - lineStart);
}

int LyricManager::FindCurrentLine(int64_t time) const
{
    if (g_config.Data().enableYrc && m_lyricData.hasYrc())
    {
        for (int i = (int)m_lyricData.yrcData.size() - 1; i >= 0; --i)
        {
            if (time >= m_lyricData.yrcData[i].startTime)
                return i;
        }
    }
    else if (m_lyricData.hasLrc())
    {
        for (int i = (int)m_lyricData.lrcData.size() - 1; i >= 0; --i)
        {
            if (time >= m_lyricData.lrcData[i].time)
                return i;
        }
    }

    return -1;
}

int64_t LyricManager::GetCurrentTime() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isPlaying)
    {
        return m_currentTime + g_config.Data().lyricOffset;
    }
    
    // Extrapolate
    int64_t elapsed = GetTickCount64() - m_lastUpdateTick;
    // Cap extrapolation to avoid running too far ahead if connection lost
    if (elapsed > 2000) elapsed = 2000;
    
    return m_currentTime + elapsed + g_config.Data().lyricOffset;
}
