#ifndef CARTA_BACKEND__REQUIREMENTS_H_
#define CARTA_BACKEND__REQUIREMENTS_H_

struct HistogramConfig {
    int channel;
    int num_bins;
};

struct SpectralConfig {
    int stokes_index;
    std::vector<int> stats_types;

    SpectralConfig() {}
    SpectralConfig(int stokes_index_, std::vector<int> stats_types_) {
        stokes_index = stokes_index_;
        stats_types = stats_types_;
    }
    bool operator==(const SpectralConfig& rhs) const {
        return ((stokes_index == rhs.stokes_index) && (stats_types == rhs.stats_types));
    }
};

#endif // CARTA_BACKEND__REQUIREMENTS_H_
