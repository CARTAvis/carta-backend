/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_MEMORY_H_
#define CARTA_SRC_UTIL_MEMORY_H_

// If either of these madvise flags is defined, we can support excluding memory addresses from core dumps.
#if defined(MADV_DONTDUMP)
    // Available on Linux
    #define NO_CORE_DUMP_ADVICE MADV_DONTDUMP
#elif defined(MADV_NOCORE)
    // Available on FreeBSD
    #define NO_CORE_DUMP_ADVICE MADV_NOCORE
#endif

#endif // CARTA_SRC_UTIL_MEMORY_H_
