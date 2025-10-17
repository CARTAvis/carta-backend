/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_MEMORY_H_
#define CARTA_SRC_UTIL_MEMORY_H_

#include <sys/mman.h>
#include <unistd.h>
#include <memory>
#include <string>

// If either of these madvise flags is defined, we can support excluding memory addresses from core dumps.
#if defined(MADV_DONTDUMP)
// Available on Linux
#define NO_CORE_DUMP_ADVICE MADV_DONTDUMP
#elif defined(MADV_NOCORE)
// Available on FreeBSD
#define NO_CORE_DUMP_ADVICE MADV_NOCORE
#endif

/**
 * @brief Mark the memory address range provided to be excluded from core dumps, if this is supported by the platform. The range must be
 * page-aligned.
 * @param address The starting address of the memory range, which must be page-aligned.
 * @param size The size of the memory range, which must be an exact multiple of the page size.
 *
 * @return Whether the address range was successfully marked.
 */
bool ExcludeFromCoreDump(void* address, size_t size);

/**
 * @brief Custom deleter for page-aligned data.
 * @see UniqueAlignedDataPtr
 * @note This is a wrapper around std::free which allows UniqueAlignedDataPtr to be constructed without the need for an explicit deleter
 * parameter.
 */
struct AlignedDataDeleter {
    void operator()(void* ptr) {
        std::free(ptr);
    }
};

/**
 * @brief Alias for a std::unique_ptr that manages a page-aligned dynamic array and has a custom deleter.
 * @tparam T The type of the data in the array managed by the pointer.
 */
template <typename T>
using UniqueAlignedDataPtr = std::unique_ptr<T[], AlignedDataDeleter>;

/**
 * @brief Create a UniqueAlignedDataPtr and optionally mark its data to be excluded from core dumps.
 *
 * @details This function allocates page-aligned memory using std::aligned_alloc. The array size is automatically converted to the raw
 * memory size for the given data type and padded to a multiple of the page size. The custom deleter of the returned pointer automatically
 * handles deallocation correctly when the pointer is destroyed.
 * @note This function calls ExcludeFromCoreDump internally if exclude_from_core_dump is true, but ignores a failure result.
 *
 * @tparam T The type of the data in the array managed by the pointer.
 * @param size The size of the array.
 * @param exclude_from_core_dump Whether the data should be excluded from core dumps.
 *
 * @return The constructed UniqueAlignedDataPtr.
 */
template <typename T>
UniqueAlignedDataPtr<T> MakeUniqueAlignedDataPtr(size_t size, bool exclude_from_core_dump = true) {
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    size_t padded_size = (size * sizeof(T) + page_size - 1) & -page_size;
    auto ptr = UniqueAlignedDataPtr<T>(static_cast<T*>(std::aligned_alloc(page_size, padded_size)));
    if (exclude_from_core_dump) {
        ExcludeFromCoreDump(ptr.get(), padded_size);
    }
    return ptr;
}

#endif // CARTA_SRC_UTIL_MEMORY_H_
