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
    pid_t pid = fork();
    if (pid == 0) {
        struct sigaction noaction;
        memset(&noaction, 0, sizeof(noaction));
        noaction.sa_handler = SIG_IGN;
        ::sigaction(SIGPIPE, &noaction, 0);
        ::setsid();
        // double fork
        pid_t pid2 = fork();
        if (pid2 == 0) {
            char** args;
            args = (char**)malloc((_args.size() + 1) * sizeof(*args));
            if (args == nullptr) {
                spdlog::debug(
                    "WebBrowser: Can't process command line argumet - This should almost never happen. The browser won't start "
                    "automatically.");
                _error = "WebBrowser: Failed to open the browser automatically.";
                ::exit(0);
            }
            for (int i = 0; i < _args.size(); ++i) {
                args[i] = (char*)malloc(_args[i].size() * sizeof(*args[i]));
                if (args[i] == nullptr) {
                    spdlog::debug(
                        "WebBrowser: Can't process command line argumet - This should almost never happen. The browser won't start "
                        "automatically.");
                    _error = "WebBrowser: Failed to open the browser automatically.";
                    ::exit(0);
                }
                strcpy(args[i], _args[i].c_str());
            }
            args[_args.size()] = NULL; // args need to be NULL terminated, C-style
            auto result = ::execv(args[0], args);
            struct sigaction noaction;
            memset(&noaction, 0, sizeof(noaction));
            noaction.sa_handler = SIG_IGN;
            ::sigaction(SIGPIPE, &noaction, 0);
            ::_exit(1);
        } else if (pid2 == -1) {
            spdlog::debug("WebBrowser: Failed to fork a new process. CARTA can't start with the requiered settings in --browser.");
            _error = "WebBrowser: Failed to open the browser automatically.";
            struct sigaction noaction;
            memset(&noaction, 0, sizeof(noaction));
            noaction.sa_handler = SIG_IGN;
            ::sigaction(SIGPIPE, &noaction, 0);
            if (::chdir("/") == -1) {
                spdlog::debug("WebBrowser: Failed to change dir to \"/\"");
            }
            ::_exit(0);
        } else if (pid2 != 0) {
            // kill this child parent so that we don't endup with a zoombie process
            ::_exit(1);
        }
    }
    if (pid == -1) {
        spdlog::debug("WebBrowser: Failed to fork a new process. CARTA can't start with the requiered settings in --browser.");
        _error = "WebBrowser: Failed to open the browser automatically.";
        struct sigaction noaction;
        memset(&noaction, 0, sizeof(noaction));
        noaction.sa_handler = SIG_IGN;
        ::sigaction(SIGPIPE, &noaction, 0);
        ::_exit(0);
    }
#endif
}

} // namespace carta
