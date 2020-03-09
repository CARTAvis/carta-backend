#ifndef CARTA_BACKEND__REQUIREMENTS_H_
#define CARTA_BACKEND__REQUIREMENTS_H_

struct HistogramConfig {
    int channel;
    int num_bins;
};

struct RegionHistogramConfig {
    int file_id;
    std::vector<HistogramConfig> configs;
};

// -------------------------------

struct SpectralConfig {
    std::string coordinate;
    int stokes_index;
    std::vector<int> stats_types;

    SpectralConfig() {}
    SpectralConfig(std::string& coordinate_, int stokes_index_, std::vector<int>& stats_types_) {
        coordinate = coordinate_;
        stokes_index = stokes_index_;
        stats_types = stats_types_;
    }
    bool operator==(const SpectralConfig& rhs) const {
        return ((coordinate == rhs.coordinate) && (stokes_index == rhs.stokes_index) && (stats_types == rhs.stats_types));
    }
};

struct RegionSpectralConfig {
    int file_id;
    std::vector<SpectralConfig> configs;
};

// -------------------------------

struct RegionStatsConfig {
    int file_id;
    std::vector<int> stats_types;
};

#endif // CARTA_BACKEND__REQUIREMENTS_H_
