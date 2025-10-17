/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Memory.h"

#include <cerrno>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

/**
 * @details Mark the memory address range provided to be excluded from core dumps, using the madvise system function, if this is supported
 * by the platform (either MADV_DONTDUMP nor MADV_NOCORE must be defined). The address range must be page-aligned.
 *
 * @note If the functionality is unsupported or the address range is invalid, this function prints a warning message.
 */
bool ExcludeFromCoreDump(void* address, size_t size) {
#ifdef NO_CORE_DUMP_ADVICE
    if (madvise(address, size, NO_CORE_DUMP_ADVICE)) {
        auto e(errno);
        auto out = fmt::memory_buffer();
        fmt::format_system_error(out, e, "Failed to exclude data from core dump");
        spdlog::warn(fmt::to_string(out));
        return false;
    }
    return true;
#endif
    spdlog::warn("Could not exclude data from core dump: unsupported platform.");
    return false;
}
