/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// # ChannelMap.h: Parameters related to channel map view

#ifndef CARTA_SRC_SESSION_CHANNELMAPSETTINGS_H_
#define CARTA_SRC_SESSION_CHANNELMAPSETTINGS_H_

#include <vector>

#include <carta-protobuf/set_image_channels.pb.h>

#include "Util/Image.h"

namespace carta {

struct RequiredTiles {
    // Settings in AddRequiredTiles
    std::vector<int> tiles;
    CARTA::CompressionType compression_type;
    float compression_quality;
    std::vector<int> current_tiles;

    RequiredTiles() {}
    RequiredTiles(const CARTA::AddRequiredTiles& required_tiles) {
        if (required_tiles.tiles_size() > 0) {
            tiles = {required_tiles.tiles().begin(), required_tiles.tiles().end()};
        }

        if (required_tiles.current_tiles_size() > 0) {
            current_tiles = {required_tiles.current_tiles().begin(), required_tiles.current_tiles().end()};
        }
        compression_type = required_tiles.compression_type();
        compression_quality = required_tiles.compression_quality();
    }

    bool HasCompression(const RequiredTiles& other) {
        return compression_type == other.compression_type && compression_quality == other.compression_quality;
    }

    bool HasTile(int tile) {
        return std::find(current_tiles.begin(), current_tiles.end(), tile) != current_tiles.end();
    }
};

class ChannelMapSettings {
public:
    ChannelMapSettings(const CARTA::SetImageChannels& message);
    ~ChannelMapSettings() = default;

    bool SetChannelMap(const CARTA::SetImageChannels& message);

    // Checks to support channel map cancel.
    bool IsInChannelRange(int file_id, int channel);
    bool HasRequiredTiles(int file_id, const CARTA::AddRequiredTiles& required_tiles);
    bool HasTile(int file_id, int tile);

    // Remove a file or all files from channel maps when closed in Session.
    void RemoveFile(int file_id);

private:
    bool SetChannelMapParams(const CARTA::SetImageChannels& message);

    // Map key is file_id
    std::unordered_map<int, std::mutex> _file_mutexes;
    std::unordered_map<int, AxisRange> _channel_ranges;
    std::unordered_map<int, RequiredTiles> _required_tiles;
};

} // namespace carta

#endif // CARTA_SRC_SESSION_CHANNELMAPSETTINGS_H_
