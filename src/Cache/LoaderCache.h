/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_CACHE_LOADERCACHE_H_
#define CARTA_SRC_CACHE_LOADERCACHE_H_

#include <list>
#include <mutex>
#include <unordered_map>

#include "ImageData/FileLoader.h"

namespace carta {

// Cache of loaders for reading images from disk.
class LoaderCache {
public:
    LoaderCache(int capacity);
    std::shared_ptr<FileLoader> Get(const std::string& filename, const std::string& directory = "");
    void Remove(const std::string& filename, const std::string& directory = "");

private:
    std::string GetKey(const std::string& filename, const std::string& directory);
    int _capacity;
    std::unordered_map<std::string, std::shared_ptr<FileLoader>> _map;
    std::list<std::string> _queue;
    std::mutex _loader_cache_mutex;
};

} // namespace carta

#endif // CARTA_SRC_CACHE_LOADERCACHE_H_
