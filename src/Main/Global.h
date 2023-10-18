/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_SRC_MAIN_GLOBALVALUES_H_
#define CARTA_BACKEND_SRC_MAIN_GLOBALVALUES_H_

#include "ProgramSettings.h"

#include <iostream>

namespace carta {

class Global {
public:
    static Global& GetInstance() {
        static Global global_values;
        return global_values;
    }

    ProgramSettings& GetSettings() {
        return _program_settings;
    }

    static bool NoLog() {
        return Global::GetInstance().GetSettings().no_log;
    }

    static int Verbosity() {
        return Global::GetInstance().GetSettings().verbosity;
    }

    static bool LogPerformance() {
        return Global::GetInstance().GetSettings().log_performance;
    }

    static bool LogProtocolMessages() {
        return Global::GetInstance().GetSettings().log_protocol_messages;
    }

    static fs::path UserDirectory() {
        return Global::GetInstance().GetSettings().user_directory;
    }

    static std::string TopLevelFolder() {
        return Global::GetInstance().GetSettings().top_level_folder;
    }

    static std::string StartingFolder() {
        return Global::GetInstance().GetSettings().starting_folder;
    }

    static bool ReadOnlyMode() {
        return Global::GetInstance().GetSettings().read_only_mode;
    }

    static bool EnableScripting() {
        return Global::GetInstance().GetSettings().enable_scripting;
    }

    static int IdleSessionWaitTime() {
        return Global::GetInstance().GetSettings().idle_session_wait_time;
    }

    static int WaitTime() {
        return Global::GetInstance().GetSettings().wait_time;
    }

    static int InitWaitTime() {
        return Global::GetInstance().GetSettings().init_wait_time;
    }

private:
    Global() {}

    ProgramSettings _program_settings;
};

} // namespace carta
#endif // CARTA_BACKEND_SRC_MAIN_GLOBALVALUES_H_
