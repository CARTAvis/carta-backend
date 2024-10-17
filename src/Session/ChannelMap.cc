/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// # ChannelMap.h: Parameters related to channel map view

#include "ChannelMap.h"

#include "Util/File.h" // ALL_FILES

namespace carta {

ChannelMap::ChannelMap(const CARTA::SetImageChannels& message) {
    SetChannelMapParams(message);
}

bool ChannelMap::SetChannelMap(const CARTA::SetImageChannels& message) {
    // Returns true if it is an entirely new channel view, with new channel range and tiles.
    return SetChannelMapParams(message);
}

bool ChannelMap::IsInChannelRange(int file_id, int channel) {
    // Returns true if input channel is in current channel range.
    std::unique_lock<std::mutex> lock(_file_mutexes[file_id]);
    if (_channel_ranges.find(file_id) == _channel_ranges.end()) {
        return false;
    }
    return _channel_ranges[file_id].is_in_range(channel);
}

bool ChannelMap::HasRequiredTiles(int file_id, const CARTA::AddRequiredTiles& required_tiles) {
    // Returns true if any input tiles are in current tiles, and compression is same.
    std::unique_lock<std::mutex> lock(_file_mutexes[file_id]);
    if (_required_tiles.find(file_id) == _required_tiles.end()) {
        return false;
    }

    RequiredTiles new_tiles(required_tiles);
    if (!_required_tiles[file_id].HasCompression(new_tiles)) {
        return false;
    }

    for (auto tile : new_tiles.encoded_tiles) {
        if (_required_tiles[file_id].HasTile(tile)) {
            return true;
        }
    }
    return false;
}

bool ChannelMap::HasTile(int file_id, int32_t tile) {
    std::unique_lock<std::mutex> lock(_file_mutexes[file_id]);
    if (_required_tiles.find(file_id) == _required_tiles.end()) {
        return false;
    }
    return _required_tiles[file_id].HasTile(tile);
}

void ChannelMap::RemoveFile(int file_id) {
    if (file_id == ALL_FILES) {
        _file_mutexes.clear();
        _channel_ranges.clear();
        _required_tiles.clear();
    } else {
        _file_mutexes.erase(file_id);
        _channel_ranges.erase(file_id);
        _required_tiles.erase(file_id);
    }
}

bool ChannelMap::SetChannelMapParams(const CARTA::SetImageChannels& message) {
    // Set new channel range and required tiles.
    // Returns true if new params (for cancel).
    AxisRange new_range;
    if (message.has_current_range()) {
        new_range = AxisRange(message.current_range().min(), message.current_range().max());
        std::cerr << "Received SET_IMAGE_CHANNELS current_range=" << new_range.from << "-" << new_range.to << std::endl;
    } else {
        new_range = AxisRange(message.channel());
    }

    auto file_id = message.file_id();
    bool is_new_range = !IsInChannelRange(file_id, new_range.from) && !IsInChannelRange(file_id, new_range.to);
    bool is_new_tiles = !HasRequiredTiles(file_id, message.required_tiles());

    // Set new range and tiles
    std::unique_lock<std::mutex> lock(_file_mutexes[file_id]);
    _channel_ranges[file_id] = new_range;
    _required_tiles[file_id] = RequiredTiles(message.required_tiles());
    return is_new_range || is_new_tiles;
}

} // namespace carta
