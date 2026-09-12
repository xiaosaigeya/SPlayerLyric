/*
 * SPlayerLyric - SPlayer Lyric Display Plugin for TrafficMonitor
 * 
 * Lyric Manager Interface
 */

#pragma once

#include "SPlayerProtocol.h"
#include <mutex>

class LyricManager
{
public:
    static LyricManager& Instance();

    void UpdateLyrics(const SPlayerProtocol::LyricData& data);
    void UpdateProgress(int64_t currentTime);
    void UpdateSongInfo(const SPlayerProtocol::SongInfo& info);
    void UpdatePlayStatus(bool isPlaying);
    void Clear();

    std::wstring GetCurrentLyricText() const;
    std::wstring GetNextLyricText() const;
    std::wstring GetPrevLyricText() const;
    std::wstring GetCurrentTranslation() const;
    std::wstring GetSongInfoText() const;

    bool HasLyric() const;
    bool HasYrcData() const;
    bool HasAnyLyricData() const;   // v10: YRC 或 LRC 任一存在（高频重绘/动画驱动用，LRC 歌也平滑换行）
    bool IsPlaying() const { return m_isPlaying; }
    int GetCurrentLineIndex() const { return m_currentLineIndex; }
    float GetWordProgress() const;
    int64_t GetCurrentTime() const;
    
    std::vector<SPlayerProtocol::YrcWord> GetCurrentYrcWords() const;

private:
    LyricManager() = default;
    int FindCurrentLine(int64_t time) const;
    int64_t GetTimeWithOffset() const;

    mutable std::mutex m_mutex;

    SPlayerProtocol::LyricData m_lyricData;
    SPlayerProtocol::SongInfo m_songInfo;

    int64_t m_currentTime = 0;
    int64_t m_lastUpdateTick = 0;
    int m_currentLineIndex = -1;
    bool m_isPlaying = false;
};

#define g_lyricMgr LyricManager::Instance()
