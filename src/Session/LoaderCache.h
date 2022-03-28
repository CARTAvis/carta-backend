/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_LOADERCACHE_H
#define CARTA_BACKEND_LOADERCACHE_H

#include "ImageData/FileLoader.h"

#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace carta {

// Cache of loaders for reading images from disk.
class LoaderCache {
public:
    LoaderCache(int capacity);
    std::shared_ptr<FileLoader<float>> Get(const std::string& filename, const std::string& directory = "");
    void Remove(const std::string& filename);

private:
    int _capacity;
    std::unordered_map<std::string, std::shared_ptr<FileLoader<float>>> _map;
    std::list<std::string> _queue;
    std::mutex _loader_cache_mutex;
};

} // namespace carta

#endif // CARTA_BACKEND_LOADERCACHE_H
