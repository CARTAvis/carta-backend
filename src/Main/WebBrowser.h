/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_MAIN_WEBBROWSER_H_
#define CARTA_SRC_MAIN_WEBBROWSER_H_

#include <string>
#include <vector>

namespace carta {
class WebBrowser {
public:
    WebBrowser(const std::string&, const std::string&);
    ~WebBrowser() = default;
    bool Status() const {
        return _status;
    };
    std::string Error() const {
        return _error;
    }

private:
    void ParseCmd();
    void OpenSystemBrowser();
    void OpenBrowser();
    bool _status = true;
    bool _path_exists = false;
    std::string _cmd;
    std::vector<std::string> _args;
    std::string _url;
    std::string _error;
};

} // namespace carta

#endif // CARTA_SRC_MAIN_WEBBROWSER_H_
