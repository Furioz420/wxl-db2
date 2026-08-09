// wxl-db2: the extension-wide service table pointer, shared by every translation unit in this DLL.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "wxl/PluginApi.h"

/// The core hands this pointer to WXL_Load once and it lives for the process lifetime, so every call
/// site reaches it through here instead of threading an `api` parameter through the call chain.
namespace wxl_db2
{
    extern const WXL_Api* g_api;

    bool InstallLightStore(); // schemas/LightStore.cpp
}

// common/Log.hpp's WLOG_* macros need common/Log.cpp linked in, which is core/host/patcher-only; an
// extension has no such object file, so these route the same call-site syntax through WXL_Api::Log.
#define WLOG_TRACE(...) ::wxl_db2::g_api->Log(WXL_LOG_TRACE, "wxl-db2", __VA_ARGS__)
#define WLOG_DEBUG(...) ::wxl_db2::g_api->Log(WXL_LOG_DEBUG, "wxl-db2", __VA_ARGS__)
#define WLOG_INFO(...)  ::wxl_db2::g_api->Log(WXL_LOG_INFO,  "wxl-db2", __VA_ARGS__)
#define WLOG_WARN(...)  ::wxl_db2::g_api->Log(WXL_LOG_WARN,  "wxl-db2", __VA_ARGS__)
#define WLOG_ERROR(...) ::wxl_db2::g_api->Log(WXL_LOG_ERROR, "wxl-db2", __VA_ARGS__)
