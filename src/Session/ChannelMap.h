/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// # ChannelMap.h: Parameters related to channel map view

#ifndef CARTA_SRC_SESSION_CHANNELMAP_H_
#define CARTA_SRC_SESSION_CHANNELMAP_H_

#include <vector>

#include <carta-protobuf/set_image_channels.pb.h>

#include "Util/Image.h"

namespace carta {

struct RequiredTiles {
    std::vector<int32_t> encoded_tiles;
    CARTA::CompressionType compression_type;
    float compression_quality;

    RequiredTiles() {}
    RequiredTiles(const CARTA::AddRequiredTiles& required_tiles) {
        if (required_tiles.tiles_size() > 0) {
            encoded_tiles = {required_tiles.tiles().begin(), required_tiles.tiles().end()};
        }
        compression_type = required_tiles.compression_type();
        compression_quality = required_tiles.compression_quality();
    }

    bool HasCompression(const RequiredTiles& other) {
        return compression_type == other.compression_type && compression_quality == other.compression_quality;
    }

    bool HasTile(int32_t tile) {
        return std::find(encoded_tiles.begin(), encoded_tiles.end(), tile) != encoded_tiles.end();
    }
};

class ChannelMap {
public:
    ChannelMap(const CARTA::SetImageChannels& message);
    ~ChannelMap() = default;

    bool SetChannelMap(const CARTA::SetImageChannels& message);

    // Checks to support channel map cancel.
    bool IsInChannelRange(int file_id, int channel);
    bool HasRequiredTiles(int file_id, const CARTA::AddRequiredTiles& required_tiles);
    bool HasTile(int file_id, int32_t tile);

    // Remove a file or all files from channel maps when closed in Session.
    // This cannot happen during a channel map loop due to Session frame mutex.
    void RemoveFile(int file_id);

private:
    bool SetChannelMapParams(const CARTA::SetImageChannels& message);

    // Map key is file_id
    std::unordered_map<int, std::mutex> _file_mutexes;
    std::unordered_map<int, AxisRange> _channel_ranges;
    std::unordered_map<int, RequiredTiles> _required_tiles;
};

} // namespace carta

#endif // CARTA_SRC_SESSION_CHANNELMAP_H_
