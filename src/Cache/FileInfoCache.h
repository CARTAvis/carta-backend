/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_CACHE_FILEINFOCACHE_H_
#define CARTA_SRC_CACHE_FILEINFOCACHE_H_

#include <chrono>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <carta-protobuf/file_info.pb.h>

namespace carta {

// How long a derived size and image list may be handed back before it is derived again.
constexpr std::chrono::seconds FILE_INFO_CACHE_TTL(30);
// The most images remembered at once. Large enough that listing a folder and then opening
// something in it is one derivation per image rather than one per request.
constexpr std::size_t FILE_INFO_CACHE_SIZE = 512;

/**
 * The size and image list of one directory image, remembered for a short while.
 *
 * Both are expensive to derive for a directory image -- a Zarr store is probed and then walked,
 * a casacore image is walked -- and the browser derives them three times over for the same image:
 * once to list the folder, once when the user selects the file, and once when the file is opened.
 *
 * Staleness is bounded by time rather than detected. A directory's own timestamp only moves when
 * its entry list changes, so for a store whose chunks live several levels down it reports nothing
 * at all: rewriting a chunk, adding one, or rewriting the root metadata in place all leave it
 * untouched. Since a size is precisely a measure of those writes, there is no cheap stamp that
 * tracks it -- the only one that would is the recursive walk this cache exists to avoid. So the
 * TTL is what makes an entry expire, and Stamp is only a fast way to notice a change that did
 * happen to be visible. A size is already explicitly approximate (see size_is_upper_bound), which
 * is what makes bounded staleness the right trade here rather than a compromise.
 *
 * An image that could not be read is remembered like any other entry. A broken store costs one
 * probe per TTL instead of one per request, and a store still being written reads as broken for
 * at most that long before it heals on its own.
 */
class FileInfoCache {
public:
    // What FillFileInfo would derive for the image, and what it would report having done so.
    struct Entry {
        // Deciding this probes the store, so it is remembered with everything the probe found
        // rather than asked again by the next caller that arrives without a type in hand.
        CARTA::FileType type = CARTA::FileType::UNKNOWN;
        int64_t size = 0;
        bool size_is_upper_bound = false;
        std::vector<std::string> hdu_list;
        bool complete = false;
    };

    // What the filesystem will say about the image without being walked. Two stamps that differ
    // prove the image changed; two that match prove nothing, which is what the TTL is for.
    struct Stamp {
        int64_t directory_mtime = 0;
        // A Zarr store keeps the schema that decides its type and its image list in one file at
        // its root. That file is rewritten in place, which the directory's own timestamp does not
        // see, so read it too -- for every directory, since whether this one is a store is itself
        // one of the answers being cached. A directory without it stamps as absent, which is
        // stable and correct.
        int64_t metadata_mtime = 0;
        int64_t metadata_size = -1;

        bool operator==(const Stamp& other) const;
    };

    FileInfoCache(std::size_t capacity, std::chrono::seconds ttl);

    // The one cache the whole process shares: two sessions browsing the same folder are the same
    // repetition as one session browsing it twice.
    static FileInfoCache& Instance();

    // The path a cached image is filed under: absolute, with symlinks resolved, so that two names
    // for one store share an entry.
    static std::string KeyFor(const std::string& filename);
    static Stamp StampFor(const std::string& key);

    std::optional<Entry> Get(const std::string& key, const Stamp& stamp);
    void Put(const std::string& key, const Stamp& stamp, Entry entry);
    // Forget everything. For tests, which cannot otherwise tell one run from the next.
    void Clear();

private:
    using Clock = std::chrono::steady_clock;

    struct Record {
        Stamp stamp;
        Entry entry;
        Clock::time_point derived_at;
    };

    // Callers hold _mutex.
    void Drop(const std::string& key);
    void Touch(const std::string& key);

    const std::size_t _capacity;
    const std::chrono::seconds _ttl;
    std::unordered_map<std::string, Record> _map;
    std::list<std::string> _queue; // most recently used first
    std::mutex _mutex;
};

} // namespace carta

#endif // CARTA_SRC_CACHE_FILEINFOCACHE_H_
