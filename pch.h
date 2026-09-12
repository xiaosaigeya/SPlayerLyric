// pch.h: Precompiled header file
// SPlayerLyric - SPlayer Lyric Display Plugin for TrafficMonitor

#ifndef PCH_H
#define PCH_H

#include "framework.h"
#include "resource.h"

// STL
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>

// Windows Socket
#include <WinSock2.h>
#include <WS2tcpip.h>

// v13: GDI+ 柔和阴影文字（半透明画刷，需初始化见 SPlayerLyricPlugin）
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;
#pragma comment(lib, "ws2_32.lib")

#endif //PCH_H
