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

class GlobalValues {
public:
    static GlobalValues& GetInstance() {
        static GlobalValues global_values;
        return global_values;
    }

    ProgramSettings& GetSettings() {
        return _program_settings;
    }

private:
    GlobalValues() {}

    ProgramSettings _program_settings;
};

class Global {
    Global() {}
    ~Global() = default;

public:
    static bool NoLog() {
        return GlobalValues::GetInstance().GetSettings().no_log;
    }

    static int Verbosity() {
        return GlobalValues::GetInstance().GetSettings().verbosity;
    }

    static bool LogPerformance() {
        return GlobalValues::GetInstance().GetSettings().log_performance;
    }

    static bool LogProtocolMessages() {
        return GlobalValues::GetInstance().GetSettings().log_protocol_messages;
    }

    static fs::path UserDirectory() {
        return GlobalValues::GetInstance().GetSettings().user_directory;
    }

    static std::string TopLevelFolder() {
        return GlobalValues::GetInstance().GetSettings().top_level_folder;
    }

    static std::string StartingFolder() {
        return GlobalValues::GetInstance().GetSettings().starting_folder;
    }

    static bool ReadOnlyMode() {
        return GlobalValues::GetInstance().GetSettings().read_only_mode;
    }

    static bool EnableScripting() {
        return GlobalValues::GetInstance().GetSettings().enable_scripting;
    }

    static int IdleSessionWaitTime() {
        return GlobalValues::GetInstance().GetSettings().idle_session_wait_time;
    }

    static int WaitTime() {
        return GlobalValues::GetInstance().GetSettings().wait_time;
    }

    static int InitWaitTime() {
        return GlobalValues::GetInstance().GetSettings().init_wait_time;
    }
};

} // namespace carta
#endif // CARTA_BACKEND_SRC_MAIN_GLOBALVALUES_H_
