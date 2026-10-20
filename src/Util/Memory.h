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
 * @brief Custom deleter for page-aligned data. It calls std::free on the underlying memory address.
 * @see UniqueAlignedDataPtr, SharedAlignedDataPtr
 * @note This deleter is included in the type aliased by UniqueAlignedDataPtr, but must be passed explicitly into the SharedAlignedDataPtr
 * constructor.
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
 * @brief Alias for a std::shared_ptr that manages a page-aligned dynamic array. It should be constructed with a custom deleter that
 * deallocates the data appropriately.
 * @see MakeSharedAlignedDataPtr
 * @tparam T The type of the data in the array managed by the pointer.
 */
template <typename T>
using SharedAlignedDataPtr = std::shared_ptr<T[]>;

/**
 * @brief Allocate page-aligned memory for an array and optionally mark it to be excluded from core dumps.
 *
 * @details This function allocates page-aligned memory using std::aligned_alloc. The array size is automatically converted to the raw
 * memory size for the given data type and padded to a multiple of the page size.
 *
 * The raw pointer returned by this function should not be used directly: it should be wrapped in a shared or unique pointer with a custom
 * deleter that frees the memory automatically.
 * @see MakeUniqueAlignedDataPtr, MakeSharedAlignedDataPtr
 * @note This function calls ExcludeFromCoreDump internally if exclude_from_core_dump is true, but ignores a failure result.
 *
 * @tparam T The type of the data in the array.
 * @param size The size of the array.
 * @param exclude_from_core_dump Whether the data should be excluded from core dumps.
 *
 * @return A pointer to the allocated memory, cast to the appropriate data type.
 */
template <typename T>
T* AllocateAlignedData(size_t size, bool exclude_from_core_dump) {
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    size_t padded_size = (size * sizeof(T) + page_size - 1) & -page_size;
    auto raw_ptr = static_cast<T*>(std::aligned_alloc(page_size, padded_size));
    if (exclude_from_core_dump) {
        ExcludeFromCoreDump(raw_ptr, padded_size);
    }
    return raw_ptr;
}

/**
 * @brief Create a unique pointer to a page-aligned array that may optionally be excluded from core dumps.
 *
 * @details This function wraps the raw pointer returned by AllocateAlignedData in a unique pointer with a custom deleter that automatically
 * handles deallocation correctly when the pointer is destroyed.
 *
 * @tparam T The type of the data in the array managed by the pointer.
 * @param size The size of the array.
 * @param exclude_from_core_dump Whether the data should be excluded from core dumps.
 *
 * @return The constructed UniqueAlignedDataPtr.
 */
template <typename T>
UniqueAlignedDataPtr<T> MakeUniqueAlignedDataPtr(size_t size, bool exclude_from_core_dump = true) {
    return UniqueAlignedDataPtr<T>(AllocateAlignedData<T>(size, exclude_from_core_dump));
}

/**
 * @brief Create a shared pointer to a page-aligned array that may optionally be excluded from core dumps.
 *
 * @details This function wraps the raw pointer returned by AllocateAlignedData in a shared pointer with a custom deleter that automatically
 * handles deallocation correctly when the pointer is destroyed.
 *
 * @tparam T The type of the data in the array managed by the pointer.
 * @param size The size of the array.
 * @param exclude_from_core_dump Whether the data should be excluded from core dumps.
 *
 * @return The constructed SharedAlignedDataPtr.
 */
template <typename T>
SharedAlignedDataPtr<T> MakeSharedAlignedDataPtr(size_t size, bool exclude_from_core_dump = true) {
    return SharedAlignedDataPtr<T>(AllocateAlignedData<T>(size, exclude_from_core_dump), AlignedDataDeleter());
}

#endif // CARTA_SRC_UTIL_MEMORY_H_
