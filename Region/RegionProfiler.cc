// RegionProfiler.cc: implementation of RegionProfiler class to create x, y, z region profiles

#include "RegionProfiler.h"
#include "../InterfaceConstants.h"

using namespace carta;

std::pair<int, int> RegionProfiler::CoordinateToAxisStokes(std::string coordinate) {
    // converts profile string into <axis, stokes> pair
    int axis_index(-1), stokes_index(CURRENT_STOKES);
    // axis
    char axis_char(coordinate.back());
    if (axis_char == 'x')
        axis_index = 0;
    else if (axis_char == 'y')
        axis_index = 1;
    else if (axis_char == 'z')
        axis_index = 2;
    // stokes
    if (coordinate.size() == 2) {
        char stokes_char(coordinate.front());
        if (stokes_char == 'I')
            stokes_index = 0;
        else if (stokes_char == 'Q')
            stokes_index = 1;
        else if (stokes_char == 'U')
            stokes_index = 2;
        else if (stokes_char == 'V')
            stokes_index = 3;
    }
    return std::make_pair(axis_index, stokes_index);
}

// ***** spatial *****

bool RegionProfiler::SetSpatialRequirements(const std::vector<std::string>& profiles, const int num_stokes) {
    // Validate and set new spatial requirements for this region
    std::vector<SpatialProfile> last_profiles = _spatial_profiles; // for diff

    // Set spatial profiles to requested profiles
    _spatial_profiles.clear();
    for (const auto& profile : profiles) {
        // validate profile string
        if (profile.empty() || profile.size() > 2) {
            continue;
        }

        // validate and convert string to pair<axisIndex, stokesIndex>;
        std::pair<int, int> axis_stokes_index = CoordinateToAxisStokes(profile);
        if ((axis_stokes_index.first < 0) || (axis_stokes_index.first > 1)) { // invalid axis
            continue;
        }
        if (axis_stokes_index.second > (num_stokes - 1)) { // invalid stokes
            continue;
        }

        // add to spatial profiles
        SpatialProfile new_spatial_profile;
        new_spatial_profile.coordinate = profile;
        new_spatial_profile.profile_axes = axis_stokes_index;
        new_spatial_profile.profile_sent = false;
        _spatial_profiles.push_back(new_spatial_profile);
    }

    bool valid(false);
    if (profiles.size() == _spatial_profiles.size()) {
        // Determine diff for required data streams
        DiffSpatialRequirements(last_profiles);
        valid = true;
    }
    return valid;
}

void RegionProfiler::DiffSpatialRequirements(std::vector<SpatialProfile>& last_profiles) {
    // Determine which current profiles are new (have unsent data streams)
    for (size_t i = 0; i < NumSpatialProfiles(); ++i) {
        bool found(false);
        for (size_t j = 0; j < last_profiles.size(); ++j) {
            if (_spatial_profiles[i].coordinate == last_profiles[j].coordinate) {
                found = true;
                break;
            }
        }
        _spatial_profiles[i].profile_sent = found;
    }
}

size_t RegionProfiler::NumSpatialProfiles() {
    return _spatial_profiles.size();
}

std::pair<int, int> RegionProfiler::GetSpatialProfileAxes(int profile_index) {
    if (profile_index < _spatial_profiles.size()) {
        return _spatial_profiles[profile_index].profile_axes;
    }
    return std::make_pair(-1, -1);
}

std::string RegionProfiler::GetSpatialCoordinate(int profile_index) {
    if (profile_index < _spatial_profiles.size()) {
        return _spatial_profiles[profile_index].coordinate;
    }
    return std::string();
}

bool RegionProfiler::GetSpatialProfileSent(int profile_index) {
    if (profile_index < _spatial_profiles.size()) {
        return _spatial_profiles[profile_index].profile_sent;
    }
    return false;
}

void RegionProfiler::SetSpatialProfileSent(int profile_index, bool sent) {
    if (profile_index < _spatial_profiles.size()) {
        _spatial_profiles[profile_index].profile_sent = sent;
    }
}

void RegionProfiler::SetAllSpatialProfilesUnsent() {
    for (auto& profile : _spatial_profiles) {
        profile.profile_sent = false;
    }
}

// ***** spectral *****

bool RegionProfiler::SetSpectralRequirements(
    const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& configs, const int num_stokes) {
    // Validate and set new spectral requirements for this region
    std::vector<SpectralProfile> last_profiles = _spectral_profiles; // for diff

    // Set spectral profiles to requested profiles
    std::vector<SpectralProfile> new_profiles;
    for (const auto& config : configs) {
        std::string coordinate(config.coordinate());

        // validate coordinate string, axis, stokes
        if (coordinate.empty() || coordinate.size() > 2) // ignore invalid profile string
            continue;
        std::pair<int, int> axis_stokes = CoordinateToAxisStokes(coordinate);
        if (axis_stokes.first != 2) // invalid axis
            continue;
        if (axis_stokes.second > (num_stokes - 1)) // invalid stokes
            continue;

        // add to spectral profiles
        SpectralProfile new_spectral_profile;
        new_spectral_profile.coordinate = coordinate;
        SpectralConfig new_spectral_config;
        new_spectral_config.stokes_index = axis_stokes.second;
        new_spectral_config.stats_types = {config.stats_types().begin(), config.stats_types().end()};
        new_spectral_profile.config = new_spectral_config;
        new_spectral_profile.profiles_sent = std::vector<bool>(config.stats_types_size(), false);
        new_profiles.push_back(new_spectral_profile);
    }
    bool valid(false);
    _spectral_profiles = new_profiles;
    if (configs.size() == NumSpectralProfiles()) {
        // Determine diff for required data streams
        DiffSpectralRequirements(last_profiles);
        valid = true;
    }
    return valid;
}

void RegionProfiler::DiffSpectralRequirements(std::vector<SpectralProfile>& last_profiles) {
    // Determine which current profiles have sent data streams
    for (size_t i = 0; i < NumSpectralProfiles(); ++i) {
        for (size_t j = 0; j < last_profiles.size(); ++j) {
            if (_spectral_profiles[i].coordinate == last_profiles[j].coordinate) {
                // search for each stats_type
                for (size_t k = 0; k < _spectral_profiles[i].config.stats_types.size(); ++k) {
                    for (size_t l = 0; l < last_profiles[j].config.stats_types.size(); ++l) {
                        if (_spectral_profiles[i].config.stats_types[k] == last_profiles[j].config.stats_types[l]) {
                            _spectral_profiles[i].profiles_sent[k] = last_profiles[j].profiles_sent[l];
                        }
                    }
                }
            }
        }
    }
}

size_t RegionProfiler::NumSpectralProfiles() {
    return _spectral_profiles.size();
}

bool RegionProfiler::IsValidSpectralConfig(const SpectralConfig& config) {
    // check if spectral profile still exists
    std::vector<SpectralProfile> profiles = GetSpectralProfiles();
    for (const auto& profile : profiles) {
        if (profile.config == config) { // same stokes index, same stats
            return true;
        }
    }
    return false;
}

std::vector<SpectralProfile> RegionProfiler::GetSpectralProfiles() {
    std::vector<SpectralProfile> profiles;
    for (auto& profile : _spectral_profiles) {
        profiles.push_back(profile);
    }
    return profiles;
}

std::string RegionProfiler::GetSpectralCoordinate(int config_stokes) {
    // return SpectralProfile.coordinate
    std::vector<SpectralProfile> profiles = GetSpectralProfiles();
    for (auto& iprofile : profiles) {
        if (iprofile.config.stokes_index == config_stokes) { // found profile
            return iprofile.coordinate;
        }
    }
    return std::string();
}

bool RegionProfiler::GetSpectralConfig(int config_stokes, SpectralConfig& config) {
    // returns whether found config for profile
    std::vector<SpectralProfile> profiles = GetSpectralProfiles();
    for (auto& profile : profiles) {
        if (profile.config.stokes_index == config_stokes) { // found profile
            config = profile.config;
            return true;
        }
    }
    return false;
}

bool RegionProfiler::GetUnsentStatsForProfile(int config_stokes, std::vector<int>& stats) {
    // return stats for given stokes index if sent=false; returns true if profile found
    std::vector<SpectralProfile> profiles = GetSpectralProfiles();
    for (auto& iprofile : profiles) {
        if (iprofile.config.stokes_index == config_stokes) { // found profile
            for (size_t i = 0; i < iprofile.profiles_sent.size(); ++i) {
                if (!iprofile.profiles_sent[i]) {
                    stats.push_back(iprofile.config.stats_types[i]);
                }
            }
            return true;
        }
    }
    return false;
}

bool RegionProfiler::GetSpectralProfileAllStatsSent(int config_stokes) {
    // return whether all stats_types for this profile have been sent or are no longer needed
    bool all_sent(true); // true until proven false
    std::vector<SpectralProfile> profiles = GetSpectralProfiles();
    bool found_profile(false);
    for (auto& profile : profiles) {
        if (found_profile) {
            break;
        }
        if (profile.config.stokes_index == config_stokes) { // found config_stokes
            found_profile = true;
            for (size_t j = 0; j < profile.config.stats_types.size(); ++j) {
                all_sent &= profile.profiles_sent[j]; // false if any false
            }
        }
    }
    return all_sent;
}

void RegionProfiler::SetSpectralProfileStatSent(int config_stokes, int stats_type, bool sent) {
    // mark this stat sent so not sent again if requirements change
    // stats_type -1 means all stats
    bool found_profile(false);
    for (size_t i = 0; i < NumSpectralProfiles(); ++i) {
        if (found_profile) {
            break;
        }
        if (_spectral_profiles[i].config.stokes_index == config_stokes) { // found profile
            found_profile = true;
            for (size_t j = 0; j < _spectral_profiles[i].config.stats_types.size(); ++j) {
                if ((stats_type == -1) || (_spectral_profiles[i].config.stats_types[j] == stats_type)) { // found stats_type
                    _spectral_profiles[i].profiles_sent[j] = sent;
                    break;
                }
            }
        }
    }
}

void RegionProfiler::SetSpectralProfileAllStatsSent(int config_stokes, bool sent) {
    // mark all stats for this profile sent
    SetSpectralProfileStatSent(config_stokes, -1, sent);
}

void RegionProfiler::SetAllSpectralProfilesUnsent() {
    // mark all stats for all profiles sent
    for (size_t i = 0; i < NumSpectralProfiles(); ++i) {
        for (size_t j = 0; j < _spectral_profiles[i].profiles_sent.size(); ++j) {
            _spectral_profiles[i].profiles_sent[j] = false;
        }
    }
}
