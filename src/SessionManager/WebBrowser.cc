/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "WebBrowser.h"
#include "Logger/Logger.h"

#include <signal.h>
#include <stdio.h>
#include <queue>
#include <sstream>
#include <stdexcept>

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

// https://stackoverflow.com/questions/478898/how-do-i-execute-a-command-and-get-the-output-of-the-command-within-c-using-po
std::string exec(const char* cmd) {
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(cmd, "r");
    if (!pipe)
        throw std::runtime_error("popen() failed!");
    try {
        while (fgets(buffer, sizeof buffer, pipe) != NULL) {
            result += buffer;
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }
    pclose(pipe);
    return result;
}

std::string find_path(std::string prog) {
    std::string path;
    std::vector<std::string> finders = {"which", "type", "command -v", "whereis"};
    for (const auto& finder : finders) {
        auto call = finder + " " + prog;
        path = exec(call.c_str());
        auto prog2 = path.substr(path.size() - prog.size() - 1, path.size());
        if (path[0] == '/') {
            break;
        } else {
            path = "";
        }
    }
    if (path.size() > 0) {
        return path.substr(0, path.size() - 1);
    }
    return path;
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
    if (_args[0][0] != '/') {
        // find path, and replace it
        auto path = find_path(_args[0]);
        if (path.size() > 0) {
            spdlog::debug("I found {} in PATH: {}.", _args[0], path);
            _args[0] = path;
        } else {
            spdlog::debug("Can't find {} in PATH, please check.", _args[0]);
        }
    }
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
        setsid();
        // double fork
        pid_t pid2 = fork();
        signal(SIGHUP, SIG_IGN);
        signal(SIGPIPE, SIG_IGN);
        if (pid2 == 0) {
            std::vector<char*> args;
            for (auto& arg : _args) {
                args.push_back(const_cast<char*>(arg.c_str()));
            }
            args.push_back(nullptr);
            auto result = execv(args[0], args.data());
            if (result == -1) {
                spdlog::debug("WebBrowser: execv failed. CARTA can't start with the requiered settings in --browser.", result);
            }
            _exit(1);
        } else if (pid2 == -1) {
            spdlog::debug("WebBrowser: Failed to fork a new process. CARTA can't start with the requiered settings in --browser.");
            if (chdir("/") == -1) {
                spdlog::debug("WebBrowser: Failed to change dir to \"/\"");
            }
            _exit(0);
        } else if (pid2 != 0) {
            // kill this child parent so that we don't endup with a zoombie process
            _exit(1);
        }
    }
    if (pid == -1) {
        spdlog::debug("WebBrowser: Failed to fork a new process. CARTA can't start with the requiered settings in --browser.");
        _exit(0);
    }
#endif
}

} // namespace carta
