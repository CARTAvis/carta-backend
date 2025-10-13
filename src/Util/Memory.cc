/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Memory.h"

#include <spdlog/fmt/fmt.h>
#include <cerrno>

bool ExcludeFromCoreDump(void* address, size_t size, std::string& message) {
#ifdef NO_CORE_DUMP_ADVICE
    if (madvise(address, size, NO_CORE_DUMP_ADVICE)) {
        auto e(errno);
        auto out = fmt::memory_buffer();
        fmt::format_system_error(out, e, message);
        message = fmt::to_string(out);
        return false;
    }
    return true;
#endif
    message = fmt::format("{}: unsupported platform.", message);
    return false;
}
