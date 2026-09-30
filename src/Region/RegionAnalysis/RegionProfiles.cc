/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "RegionProfiles.h"

#include <cmath>

#include "ImageStats/DerivedStatistics.h"
#include "Util/Nan.h"

namespace carta {

std::map<CARTA::StatsType, std::vector<double>> RegionProfileStatistics(const std::vector<ChannelTotals>& channels, double beam_area) {
    const bool has_flux = !std::isnan(beam_area);
    std::vector<CARTA::StatsType> reported{CARTA::StatsType::NumPixels, CARTA::StatsType::NanCount, CARTA::StatsType::Sum,
        CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::SumSq, CARTA::StatsType::Min,
        CARTA::StatsType::Max, CARTA::StatsType::Extrema};
    if (has_flux) {
        reported.push_back(CARTA::StatsType::FluxDensity);
    }
    std::map<CARTA::StatsType, std::vector<double>> stats;
    for (const auto stat : reported) {
        stats[stat] = std::vector<double>(channels.size(), DOUBLE_NAN);
    }

    for (std::size_t c = 0; c < channels.size(); ++c) {
        const auto& channel = channels[c];
        if (!channel.read) {
            continue;
        }
        stats[CARTA::StatsType::NumPixels][c] = channel.num_pixels;
        stats[CARTA::StatsType::NanCount][c] = channel.nan_count;
        if (channel.num_pixels == 0.0) {
            // Nothing but the two counts is defined for a channel with no valid pixel.
            continue;
        }
        // A profile says zero for a lone pixel's sigma.
        const auto derived =
            DeriveStatistics({channel.num_pixels, channel.sum, channel.sum_sq, channel.min, channel.max}, LonePixelSigma::zero);
        stats[CARTA::StatsType::Sum][c] = channel.sum;
        stats[CARTA::StatsType::SumSq][c] = channel.sum_sq;
        stats[CARTA::StatsType::Min][c] = channel.min;
        stats[CARTA::StatsType::Max][c] = channel.max;
        stats[CARTA::StatsType::Mean][c] = derived.mean;
        stats[CARTA::StatsType::RMS][c] = derived.rms;
        stats[CARTA::StatsType::Sigma][c] = derived.sigma;
        stats[CARTA::StatsType::Extrema][c] = derived.extrema;
        if (has_flux) {
            stats[CARTA::StatsType::FluxDensity][c] = channel.sum / beam_area;
        }
    }
    return stats;
}

BatchOutcome RegionProfiles::Continue(const RegionProfileKey& key, const RegionProfileRequest& request, RegionProfileReader& reader,
    std::mutex& image_mutex, std::chrono::milliseconds step, const RegionProfileReport& report,
    std::map<CARTA::StatsType, std::vector<double>>& profile, float& progress) {
    if (!request.mask || request.channels.to < request.channels.from) {
        return BatchOutcome::declined;
    }
    const auto shape = request.mask->shape();
    const auto channel_count = static_cast<std::size_t>(request.channels.to - request.channels.from + 1);

    // This region's profile so far, or a new one if the request is not a continuation of it. Sizes
    // are compared first: casacore throws rather than answering when two positions do not conform.
    std::shared_ptr<Entry> held;
    bool fresh = false;
    {
        std::scoped_lock lock(_mutex);
        auto& entry = _entries[key];
        const bool continues = entry && entry->origin.size() == request.origin.size() && entry->shape.size() == shape.size() &&
                               entry->origin == request.origin && entry->shape == shape && entry->channels.from == request.channels.from &&
                               entry->channels.to == request.channels.to;
        if (!continues) {
            entry = std::make_shared<Entry>();
            entry->origin = request.origin;
            entry->shape = shape;
            entry->channels = request.channels;
            entry->progress.channels.resize(channel_count);
            fresh = true;
        }
        held = entry;
    }
    std::scoped_lock entry_lock(held->mutex);
    auto& made = held->progress;
    const double beam_area = reader.BeamArea();

    if (!made.Complete()) {
        const auto deadline = std::chrono::steady_clock::now() + step;
        const auto tell = [&](double fraction) {
            return !report || report(static_cast<float>(fraction), [&]() { return RegionProfileStatistics(made.channels, beam_area); });
        };
        const auto outcome = reader.ReadOn(request, image_mutex, made, deadline, tell);
        if (outcome == BatchOutcome::declined) {
            if (fresh) {
                std::scoped_lock lock(_mutex);
                auto found = _entries.find(key);
                if (found != _entries.end() && found->second == held) {
                    _entries.erase(found);
                }
            }
            return outcome;
        }
        if (outcome != BatchOutcome::finished) {
            return outcome;
        }
        if (made.total == 0) {
            // A reader that read on and said nothing of how far is not one to believe.
            return BatchOutcome::failed;
        }
    }

    profile = RegionProfileStatistics(made.channels, beam_area);
    progress = made.Complete() ? 1.0F : static_cast<float>(static_cast<double>(made.done) / static_cast<double>(made.total));
    return BatchOutcome::finished;
}

void RegionProfiles::Release(int region_id) {
    std::scoped_lock lock(_mutex);
    if (region_id == ALL_REGIONS) {
        _entries.clear();
        return;
    }
    for (auto it = _entries.begin(); it != _entries.end();) {
        it = it->first.region_id == region_id ? _entries.erase(it) : std::next(it);
    }
}

void RegionProfiles::ReleaseFile(int file_id) {
    std::scoped_lock lock(_mutex);
    for (auto it = _entries.begin(); it != _entries.end();) {
        it = it->first.file_id == file_id ? _entries.erase(it) : std::next(it);
    }
}

std::size_t RegionProfiles::Size() const {
    std::scoped_lock lock(_mutex);
    return _entries.size();
}

} // namespace carta
