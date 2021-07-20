/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "WebBrowser.h"
#include "Logger/Logger.h"

#include <signal.h>
#include <queue>
#include <sstream>

namespace carta {

WebBrowser::WebBrowser(const std::string& url, const std::string& browser_cmd) {
    _url = url;
    if (browser_cmd.size() > 0) {
        _cmd = browser_cmd;
        ParseCmd();
    }
    if (_cmd.size() > 0) {
        spdlog::debug("WebBrowser: custom command is {}, attempting to open the browser now.", _cmd);
        OpenBrowser();
    } else {
        spdlog::debug("WebBrowser: using default browser.");
        OpenSystemBrowser();
    }
}

void WebBrowser::ParseCmd() {
    if (_cmd[_cmd.size() - 1] == '&') {
        _cmd.pop_back();
        _isBkg = true; // so that we know what to do with commands ending with &
    }
    const std::string wildcard = "CARTA_URL";
    auto wildcard_pos = _cmd.find(wildcard);
    if (wildcard_pos != std::string::npos) {
        _cmd = fmt::format("{0}{2} {1}", _cmd.substr(0, wildcard_pos), _cmd.substr(wildcard_pos + wildcard.size(), _cmd.size()), _url);
    } else {
        _cmd = fmt::format("{} {}", _cmd, _url);
    }
#if defined(__APPLE__)
#else
    std::istringstream isstream(_cmd);
    std::copy(std::istream_iterator<std::string>(isstream), std::istream_iterator<std::string>(), std::back_inserter(_args));
#endif
}

void WebBrowser::OpenSystemBrowser() {
#if defined(__APPLE__)
    std::string cmd = "open";
#else
    std::string cmd = "xdg-open";
#endif
    cmd = fmt::format("{} \"{}\"", cmd, _url).c_str();
    spdlog::debug("WebBrowser: Trying to launch CARTA with the default browser using: {}", cmd);
    const int ans = std::system(cmd.c_str());
    if (ans) {
        _status = false;
        _error = "WebBrowser: Failed to open the default browser automatically.";
    }
}

void WebBrowser::OpenBrowser() {
    spdlog::debug("WebBrowser: Trying to launch CARTA with user provided browser command: {}", _cmd);
#if defined(__APPLE__)
    std::string cmd = "open -a " + _cmd;
    const int ans = std::system(cmd.c_str());
    if (ans) {
        _status = false;
        _error = "WebBrowser: Failed to open the browser automatically.";
    }
#else
    std::string cmd = "bash -c \"" + _cmd + ">/dev/null 2>1 & disown\"";
    const int ans = std::system(cmd.c_str());
    if (ans) {
        _status = false;
        _error = "WebBrowser: Failed to open the browser automatically.";
    }
#endif
}

} // namespace carta
