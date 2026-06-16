/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# ZarrMetadata.cc: parse Zarr metadata
#include "ZarrMetadata.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "ZarrStore.h"
#include "ZarrUtil.h"

#include <casacore/casa/BasicSL/Constants.h>
#include <casacore/casa/Quanta/MVTime.h>
#include <casacore/measures/Measures/MEpoch.h>
#include <casacore/measures/Measures/Stokes.h>
#include <casacore/scimath/Mathematics/GaussianBeam.h>

#include "tensorstore/array.h"
#include "tensorstore/index.h"
#include "tensorstore/util/span.h"

namespace ts = tensorstore;

namespace carta {

namespace {

constexpr size_t FITS_KEYWORD_MAX_LEN = 8;
constexpr casacore::uInt DATE_OBS_PRECISION = 12; // MVTime fractional-second digits = precision - 6.
constexpr double RAD_TO_DEG = 180.0 / M_PI;
constexpr double UNIX_EPOCH_MJD = 40587.0;

// XRADIO axis names used throughout the schema.
constexpr const char* FREQUENCY_AXIS = "frequency";
constexpr const char* POLARIZATION_AXIS = "polarization";
constexpr const char* TIME_AXIS = "time";
// Direction axis names for SKY.
constexpr const char* L_AXIS = "l";
constexpr const char* M_AXIS = "m";
// Beam parameter label axis/coordinate; its values name each parameter (e.g. "major", "minor", "pa").
constexpr const char* BEAM_PARAMS_LABEL = "beam_params_label";

class FitsHeaderBuilder {
public:
    void AddRecord(const std::string& key_value) {
        _headers.emplace_back(fmt::format("{:<80}", key_value));
    }

    void AddString(const std::string& key, const std::string& value) {
        if (!value.empty()) {
            AddRecord(fmt::format("{:<8}= '{}'", key, value));
        }
    }

    void AddDouble(const std::string& key, double value) {
        AddRecord(fmt::format("{:<8}= {:#.13G}", key, value));
    }

    void AddInt(const std::string& key, int value) {
        AddRecord(fmt::format("{:<8}= {}", key, value));
    }

    casacore::Vector<casacore::String> Build() {
        AddRecord("END");
        casacore::Vector<casacore::String> result(_headers.size());
        for (size_t i = 0; i < _headers.size(); ++i) {
            result[i] = _headers[i];
        }
        return result;
    }

private:
    std::vector<std::string> _headers;
};

void ToUpperAscii(std::string& text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) { return std::toupper(character); });
}

casacore::MEpoch::Types EpochTypeFromScale(std::string scale) {
    ToUpperAscii(scale);
    casacore::MEpoch::Types type;
    if (!casacore::MEpoch::getType(type, scale)) {
        throw std::runtime_error("Unsupported obsdate time scale: " + scale);
    }
    return type;
}

struct ObsDate {
    std::string date;
    double mjd;
};

ObsDate FormatObsDate(const casacore::MEpoch& epoch) {
    const double mjd = epoch.getValue().get();
    const std::string date_obs = casacore::MVTime(epoch.getValue()).string(casacore::MVTime::FITS, DATE_OBS_PRECISION);
    return {date_obs, mjd};
}

ObsDate ParseObsDate(const nlohmann::json& data, std::string format, const std::string& scale) {
    ToUpperAscii(format);
    const casacore::MEpoch::Types epoch_type = EpochTypeFromScale(scale);

    if (data.is_number()) {
        double mjd = data.get<double>();
        if (format == "JD") {
            mjd -= casacore::C::MJD0;
        } else if (format == "UNIX" || format == "POSIX" || format == "UNIX_SECONDS") {
            mjd = UNIX_EPOCH_MJD + (mjd / casacore::C::day);
        } else if (format != "MJD") {
            throw std::runtime_error("Unsupported numeric obsdate format: " + format);
        }
        return FormatObsDate(casacore::MEpoch(casacore::MVEpoch(casacore::Quantity(mjd, "d")), epoch_type));
    }

    if (data.is_string()) {
        casacore::Quantity parsed_date;
        const std::string date_string = data.get<std::string>();
        if (!casacore::MVTime::read(parsed_date, date_string, true, true)) {
            throw std::runtime_error("Failed to parse obsdate string: " + date_string);
        }
        return FormatObsDate(casacore::MEpoch(casacore::MVEpoch(parsed_date), epoch_type));
    }

    throw std::runtime_error("Unsupported obsdate data type: " + std::string(data.type_name()));
}

std::string MakeCtype(const std::string& axis, const std::string& projection) {
    std::string axis_str(axis);
    if (!projection.empty()) {
        while (axis_str.size() < 4) {
            axis_str += '-';
        }
        return axis_str + "-" + projection;
    }
    return axis_str;
}

// Emit a string FITS keyword from a JSON pointer if it resolves to a string. Returns true if added.
bool TryAddString(FitsHeaderBuilder& builder, const nlohmann::json& obj, const char* ptr, const std::string& key) {
    const auto* value = GetJsonPtr(obj, ptr);
    if (value && value->is_string()) {
        builder.AddString(key, value->get<std::string>());
        return true;
    }
    return false;
}

// Emit a numeric FITS keyword from a JSON pointer if it resolves to a number, applying an optional scale.
bool TryAddDouble(FitsHeaderBuilder& builder, const nlohmann::json& obj, const char* ptr, const std::string& key, double scale = 1.0) {
    const auto* value = GetJsonPtr(obj, ptr);
    if (value && value->is_number()) {
        builder.AddDouble(key, value->get<double>() * scale);
        return true;
    }
    return false;
}

template <typename Func>
void SafeParseBlock(Func func, const std::string& context) {
    try {
        func();
    } catch (const std::exception& e) {
        spdlog::warn("Error parsing {}: {}", context, e.what());
    } catch (...) {
        spdlog::warn("Unknown error parsing {}", context);
    }
}

using AxisInfo = ZarrMetadata::AxisInfo;

std::map<std::string, AxisInfo> ParseAxes(const nlohmann::json& metadata) {
    const auto* dimension_names = GetJsonPtr(metadata, "/dimension_names");
    if (!dimension_names || !dimension_names->is_array()) {
        throw std::runtime_error("XRADIO schema requires dimension_names array");
    }

    std::vector<std::string> dims = dimension_names->get<std::vector<std::string>>();

    const auto* shape_json = GetJsonPtr(metadata, "/shape");
    if (!shape_json || !shape_json->is_array()) {
        throw std::runtime_error("Zarr shape is not an array");
    }

    std::map<std::string, AxisInfo> axes;
    for (size_t i = 0; i < dims.size(); ++i) {
        axes[dims[i]] = AxisInfo{i, (*shape_json)[i].get<int>()};
    }

    return axes;
}

const AxisInfo& RequiredAxis(const std::map<std::string, AxisInfo>& axes, const std::string& axis_name) {
    auto axis = axes.find(axis_name);
    if (axis == axes.end()) {
        throw std::runtime_error("Missing required axis " + axis_name);
    }
    return axis->second;
}

void ValidateSupportedAxes(const std::map<std::string, AxisInfo>& axes, const std::vector<std::string>& required_axes) {
    for (const auto& axis_name : required_axes) {
        const AxisInfo& axis = RequiredAxis(axes, axis_name);
        if (axis_name == TIME_AXIS && axis.size > 1) {
            throw std::runtime_error(fmt::format("Zarr images with time axis size > 1 are not supported: time={}", axis.size));
        }
    }
}

std::vector<casacore::Stokes::StokesTypes> StokesTypesFromLabels(const std::vector<std::string>& labels) {
    std::vector<casacore::Stokes::StokesTypes> types;
    types.reserve(labels.size());
    for (const auto& label : labels) {
        types.push_back(casacore::Stokes::type(label));
    }
    return types;
}

// Format a JSON integer array (e.g. a chunk/shard shape) as "[a, b, c, d]".
std::string FormatIntArray(const nlohmann::json& arr) {
    std::string result = "[";
    for (size_t i = 0; i < arr.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += std::to_string(arr[i].get<int64_t>());
    }
    result += "]";
    return result;
}

// Reorder a storage-order chunk/shard shape into CARTA axis order (x, y, freq, stokes),
// dropping any extra axes (e.g. time). Missing entries in the shape array default to 1.
nlohmann::json CartaOrderedShape(
    const nlohmann::json& arr, const std::map<std::string, AxisInfo>& axes, const std::string& x_axis, const std::string& y_axis) {
    auto value_at = [&](const std::string& name) -> int64_t {
        const AxisInfo& axis = RequiredAxis(axes, name);
        return (axis.index >= arr.size()) ? 1 : arr[axis.index].get<int64_t>();
    };
    return {value_at(x_axis), value_at(y_axis), value_at(FREQUENCY_AXIS), value_at(POLARIZATION_AXIS)};
}

// Describe a Blosc codec, e.g. "Blosc + zstd (level 5, shuffle)".
std::string FormatBloscCompressor(const nlohmann::json& codec) {
    std::string compressor = "Blosc";
    const auto* cname = GetJsonPtr(codec, "/configuration/cname");
    if (cname && cname->is_string()) {
        compressor += " + " + cname->get<std::string>();
    }

    std::string params;
    const auto* clevel = GetJsonPtr(codec, "/configuration/clevel");
    if (clevel && clevel->is_number()) {
        params += "level " + std::to_string(clevel->get<int>());
    }
    const auto* shuffle = GetJsonPtr(codec, "/configuration/shuffle");
    if (shuffle && shuffle->is_string() && shuffle->get<std::string>() != "noshuffle") {
        params += (params.empty() ? "" : ", ") + shuffle->get<std::string>();
    }
    if (!params.empty()) {
        compressor += " (" + params + ")";
    }
    return compressor;
}

// Pick a supported compression codec from a Zarr v3 codec pipeline and describe it.
// Returns "" if no supported compressor is found.
std::string FormatCompressor(const nlohmann::json* compression_codecs) {
    if (!compression_codecs || !compression_codecs->is_array()) {
        return {};
    }

    for (const auto& codec : *compression_codecs) {
        std::string name = codec.value("name", "");
        if (name == "blosc") {
            return FormatBloscCompressor(codec);
        }
        if (name == "zstd" || name == "gzip") {
            std::string compressor = name;
            const auto* level = GetJsonPtr(codec, "/configuration/level");
            if (level && level->is_number()) {
                compressor += " (level " + std::to_string(level->get<int>()) + ")";
            }
            return compressor;
        }
    }
    return {};
}

} // namespace

struct ZarrMetadata::Impl {
    // Low-level Zarr access. Created during Initialize(); shared so future pixel reads can reuse it.
    std::shared_ptr<ZarrStore> store;

    // XRADIO axis name -> {storage index, size}, parsed from the SKY array metadata.
    std::map<std::string, AxisInfo> axes;

    // Lazily-loaded beam set: GetBeams() may be called more than once per image (e.g. once while
    // building the FITS header and once while setting up the image), so the array is read only once.
    bool beams_loaded = false;
    bool has_beams = false;
    casacore::ImageBeamSet cached_beam_set;

    // Builds the FITS header from the parsed metadata; defined out-of-line below.
    class FitsHeaderComposer;

    casacore::IPosition CartaShape() const {
        const int x_size = RequiredAxis(axes, L_AXIS).size;
        const int y_size = RequiredAxis(axes, M_AXIS).size;
        const int frequency_size = RequiredAxis(axes, FREQUENCY_AXIS).size;
        const int polarization_size = RequiredAxis(axes, POLARIZATION_AXIS).size;

        return casacore::IPosition(std::vector<int>{x_size, y_size, frequency_size, polarization_size});
    }

    bool GetBeams(casacore::ImageBeamSet& beam_set) {
        if (!beams_loaded) {
            has_beams = LoadBeams(cached_beam_set);
            beams_loaded = true;
        }
        if (has_beams) {
            beam_set = cached_beam_set;
        }
        return has_beams;
    }

    bool LoadBeams(casacore::ImageBeamSet& beam_set) const {
        std::string beam_array_name = store->GetAttributeString(store->GetImageName(), "beam_fit_params");
        if (beam_array_name.empty()) {
            return false;
        }

        try {
            nlohmann::json beam_metadata = store->GetArrayMetadata(beam_array_name);
            std::map<std::string, AxisInfo> beam_axes = ParseAxes(beam_metadata);
            ValidateSupportedAxes(beam_axes, {TIME_AXIS, POLARIZATION_AXIS, FREQUENCY_AXIS, BEAM_PARAMS_LABEL});

            const AxisInfo& time_axis = RequiredAxis(beam_axes, TIME_AXIS);
            const AxisInfo& freq_axis = RequiredAxis(beam_axes, FREQUENCY_AXIS);
            const AxisInfo& stokes_axis = RequiredAxis(beam_axes, POLARIZATION_AXIS);
            const AxisInfo& param_axis = RequiredAxis(beam_axes, BEAM_PARAMS_LABEL);

            // The BEAM_PARAMS_LABEL coordinate names each position along the parameter axis
            // (e.g. ["major", "minor", "pa"]); look up each parameter by name, as the order may be arbitrary.
            std::vector<std::string> param_labels = store->ReadStringArray(BEAM_PARAMS_LABEL);
            auto label_position = [&](const std::string& wanted) -> ts::Index {
                for (size_t i = 0; i < param_labels.size(); ++i) {
                    std::string label = param_labels[i];
                    ToUpperAscii(label);
                    if (label == wanted) {
                        return static_cast<ts::Index>(i);
                    }
                }
                throw std::runtime_error(fmt::format("{} has no '{}' entry", BEAM_PARAMS_LABEL, wanted));
            };
            const ts::Index major_pos = label_position("MAJOR");
            const ts::Index minor_pos = label_position("MINOR");
            const ts::Index pa_pos = label_position("PA");

            auto beam_array = store->ReadDoubleArray(beam_array_name);
            const size_t beam_param_axis = param_axis.index;
            const int n_chan = freq_axis.size;
            const int n_stokes = stokes_axis.size;
            std::string unit = store->GetAttributeString(beam_array_name, "units");
            if (unit.empty()) {
                throw std::runtime_error(fmt::format("Beam array {} missing units attribute", beam_array_name));
            }

            beam_set.resize(n_chan, n_stokes);

            for (int chan = 0; chan < n_chan; ++chan) {
                for (int stokes = 0; stokes < n_stokes; ++stokes) {
                    std::vector<ts::Index> indices(beam_axes.size(), 0);
                    indices[time_axis.index] = 0;
                    indices[freq_axis.index] = chan;
                    indices[stokes_axis.index] = stokes;

                    indices[beam_param_axis] = major_pos;
                    const double bmaj = beam_array(ts::span<const ts::Index>(indices));
                    indices[beam_param_axis] = minor_pos;
                    const double bmin = beam_array(ts::span<const ts::Index>(indices));
                    indices[beam_param_axis] = pa_pos;
                    const double bpa = beam_array(ts::span<const ts::Index>(indices));
                    beam_set.setBeam(chan, stokes,
                        casacore::GaussianBeam(
                            casacore::Quantity(bmaj, unit), casacore::Quantity(bmin, unit), casacore::Quantity(bpa, unit)));
                }
            }

            return true;
        } catch (const std::exception& ex) {
            spdlog::warn("Error reading beams {}: {}", beam_array_name, ex.what());
            return false;
        }
    }

    // Extract storage encoding info (compressor, compression level, chunk shape, shard shape if any)
    // for the main array, as ordered label/value pairs.
    std::vector<std::pair<std::string, std::string>> StorageInfo() const {
        std::vector<std::pair<std::string, std::string>> info;
        const nlohmann::json& zarray = store->GetImageMetadata();

        // The chunk_grid shape is the shard shape when sharding is used, otherwise the chunk shape.
        const auto* grid_shape = GetJsonPtr(zarray, "/chunk_grid/configuration/chunk_shape");

        // Locate the sharding codec (if any); the compressor lives in its nested codec list when sharded.
        const auto* codecs = GetJsonPtr(zarray, "/codecs");
        const nlohmann::json* inner_chunk = nullptr;
        const nlohmann::json* compression_codecs = codecs;
        bool sharded = false;
        if (codecs && codecs->is_array()) {
            for (const auto& codec : *codecs) {
                if (codec.value("name", "") == "sharding_indexed") {
                    sharded = true;
                    inner_chunk = GetJsonPtr(codec, "/configuration/chunk_shape");
                    compression_codecs = GetJsonPtr(codec, "/configuration/codecs");
                    break;
                }
            }
        }

        // Report shapes in CARTA axis order (x, y, freq, stokes), dropping time axis.
        const std::string axis_labels = " (RA, DEC, FREQ, STOKES)";

        // Emit Shard shape (if any), then Chunk shape, then Compressor so they group next to "Shape" in the file browser.
        if (sharded && grid_shape && grid_shape->is_array()) {
            info.emplace_back("Shard shape", FormatIntArray(CartaOrderedShape(*grid_shape, axes, L_AXIS, M_AXIS)) + axis_labels);
        }
        // Chunk shape: the inner chunk when sharded, otherwise the chunk_grid shape.
        const nlohmann::json* chunk_shape = sharded ? inner_chunk : grid_shape;
        if (chunk_shape && chunk_shape->is_array()) {
            info.emplace_back("Chunk shape", FormatIntArray(CartaOrderedShape(*chunk_shape, axes, L_AXIS, M_AXIS)) + axis_labels);
        }
        const std::string compressor = FormatCompressor(compression_codecs);
        if (!compressor.empty()) {
            info.emplace_back("Compressor", compressor);
        }

        return info;
    }
};

// Assembles FITS header records from the parsed Zarr metadata and coordinate arrays.
class ZarrMetadata::Impl::FitsHeaderComposer {
public:
    FitsHeaderComposer(Impl& impl, const casacore::IPosition& shape) : _impl(impl), _shape(shape) {}

    casacore::Vector<casacore::String> Build() {
        AppendDataTypeHeaders();
        AppendNAxis();
        AppendDirectionHeaders();
        AppendDirectionIncrements();
        AppendSpectralAxis();
        AppendStokesAxis();
        AppendSkyMetadata();
        AppendBeams();
        return _builder.Build();
    }

private:
    void AppendDataTypeHeaders() {
        const nlohmann::json zarray = _impl.store->GetArrayMetadata(_impl.store->GetImageName());

        static constexpr int bitpix_float32 = -32;
        static constexpr int bitpix_float64 = -64;
        int bitpix = bitpix_float32;
        SafeParseBlock([&]() {
            // Currently XRADIO only supports float32 and float64
            const auto* dtype = GetJsonPtr(zarray, "/data_type");
            if (dtype && dtype->is_string()) {
                std::string dtype_str = dtype->get<std::string>();
                if (dtype_str == "float32" || dtype_str == "<f4") {
                    bitpix = bitpix_float32;
                } else if (dtype_str == "float64" || dtype_str == "<f8") {
                    bitpix = bitpix_float64;
                }
            }
        }, "BITPIX");

        _builder.AddString("SIMPLE", "T");
        _builder.AddInt("BITPIX", bitpix);
    }

    void AppendNAxis() {
        const int ndim = _shape.size();
        if (ndim > 0) {
            _builder.AddInt("NAXIS", ndim);
            for (int i = 0; i < ndim; ++i) {
                _builder.AddInt("NAXIS" + std::to_string(i + 1), _shape[i]);
            }
        }
    }

    void AppendDirectionHeaders() {
        if (!_impl.store->GetRootMetadata().contains("attributes")) {
            return;
        }
        const auto& zattrs = _impl.store->GetRootMetadata()["attributes"];

        const auto* coordinate_system_info = GetJsonPtr(zattrs, "/coordinate_system_info");
        if (!(coordinate_system_info && coordinate_system_info->is_object())) {
            return;
        }

        AppendEquinox(zattrs);
        std::string radesys = AppendRadesys(zattrs);
        AppendDirectionReference(zattrs);
        AppendCtype(zattrs, radesys);

        TryAddDouble(_builder, zattrs, "/coordinate_system_info/native_pole_direction/data/0", "LONPOLE", RAD_TO_DEG);
        TryAddDouble(_builder, zattrs, "/coordinate_system_info/native_pole_direction/data/1", "LATPOLE", RAD_TO_DEG);

        // TODO: XRADIO has not finalized projection_parameters yet.

        AppendPcMatrix(zattrs);
    }

    void AppendEquinox(const nlohmann::json& zattrs) {
        SafeParseBlock([&]() {
            const auto* equinox = GetJsonPtr(zattrs, "/coordinate_system_info/reference_direction/attrs/equinox");
            if (!equinox) {
                return;
            }
            if (equinox->is_number()) {
                _builder.AddDouble("EQUINOX", equinox->get<double>());
            } else if (equinox->is_string()) {
                std::string val = equinox->get<std::string>();
                if (!val.empty()) {
                    size_t start_pos = 0;
                    if (std::toupper(val[0]) == 'J' || std::toupper(val[0]) == 'B') {
                        start_pos = 1;
                    }
                    try {
                        _builder.AddDouble("EQUINOX", std::stod(val.substr(start_pos)));
                    } catch (...) {
                        spdlog::warn("Invalid EQUINOX format: '{}'", val);
                    }
                }
            }
        }, "EQUINOX");
    }

    std::string AppendRadesys(const nlohmann::json& zattrs) {
        std::string radesys;
        SafeParseBlock([&]() {
            const auto* frame = GetJsonPtr(zattrs, "/coordinate_system_info/reference_direction/attrs/frame");
            if (frame && frame->is_string()) {
                radesys = frame->get<std::string>();
                ToUpperAscii(radesys);
                _builder.AddString("RADESYS", radesys);
            }
        }, "RADESYS");
        return radesys;
    }

    void AppendDirectionReference(const nlohmann::json& zattrs) {
        SafeParseBlock([&]() {
            const auto* ref_data = GetJsonPtr(zattrs, "/coordinate_system_info/reference_direction/data");
            if (ref_data && ref_data->is_array() && ref_data->size() >= 2) {
                _builder.AddDouble("CRVAL1", (*ref_data)[0].get<double>() * RAD_TO_DEG);
                _builder.AddDouble("CRVAL2", (*ref_data)[1].get<double>() * RAD_TO_DEG);
            }
        }, "CRVAL1/2");
    }

    void AppendCtype(const nlohmann::json& zattrs, const std::string& radesys) {
        SafeParseBlock([&]() {
            std::string ctype1_prefix = "RA";
            std::string ctype2_prefix = "DEC";
            std::string projection_str;
            const auto* projection = GetJsonPtr(zattrs, "/coordinate_system_info/projection");
            if (projection && projection->is_string()) {
                projection_str = projection->get<std::string>();
            }
            if (radesys == "GALACTIC") {
                ctype1_prefix = "GLON";
                ctype2_prefix = "GLAT";
            } else if (radesys == "ECLIPTIC") {
                ctype1_prefix = "ELON";
                ctype2_prefix = "ELAT";
            } else if (radesys == "SUPERGALACTIC") {
                ctype1_prefix = "SLON";
                ctype2_prefix = "SLAT";
            }
            _builder.AddString("CTYPE1", MakeCtype(ctype1_prefix, projection_str));
            _builder.AddString("CTYPE2", MakeCtype(ctype2_prefix, projection_str));
        }, "CTYPE1/2");
    }

    void AppendPcMatrix(const nlohmann::json& zattrs) {
        SafeParseBlock([&]() {
            const auto* pc_val = GetJsonPtr(zattrs, "/coordinate_system_info/pixel_coordinate_transformation_matrix");
            if (pc_val && pc_val->is_array() && pc_val->size() >= 2 && (*pc_val)[0].is_array() && (*pc_val)[0].size() >= 2) {
                _builder.AddDouble("PC1_1", (*pc_val)[0][0].get<double>());
                _builder.AddDouble("PC1_2", (*pc_val)[0][1].get<double>());
                _builder.AddDouble("PC2_1", (*pc_val)[1][0].get<double>());
                _builder.AddDouble("PC2_2", (*pc_val)[1][1].get<double>());
            }
        }, "PC Matrix");
    }

    void AppendDirectionIncrements() {
        SafeParseBlock([&]() {
            // XRADIO uses linear spacing, so the increment is the step between the first two labels.
            auto add_axis = [&](const ts::SharedOffsetArray<double>& arr, const char* cdelt, const char* crpix, const char* cunit) {
                if (arr.num_elements() > 1) {
                    double cdelt_rad = arr(1) - arr(0);
                    _builder.AddDouble(cdelt, cdelt_rad * RAD_TO_DEG);
                    if (cdelt_rad != 0.0) {
                        _builder.AddDouble(crpix, (-arr(0) / cdelt_rad) + 1.0); // +1 for FITS 1-indexed
                    }
                    _builder.AddString(cunit, "deg");
                }
            };
            add_axis(_impl.store->ReadDoubleArray(L_AXIS), "CDELT1", "CRPIX1", "CUNIT1");
            add_axis(_impl.store->ReadDoubleArray(M_AXIS), "CDELT2", "CRPIX2", "CUNIT2");
        }, "Direction Increments");
    }

    void AppendSpectralAxis() {
        SafeParseBlock([&]() {
            auto freq_arr = _impl.store->ReadDoubleArray(FREQUENCY_AXIS);
            nlohmann::json zattrs = _impl.store->GetAttributes(FREQUENCY_AXIS);

            if (freq_arr.num_elements() > 0) {
                _builder.AddString("CTYPE3", "FREQ");
                _builder.AddDouble("CRPIX3", 1.0);
                _builder.AddDouble("CRVAL3", freq_arr(0));
                if (freq_arr.num_elements() > 1) {
                    _builder.AddDouble("CDELT3", freq_arr(1) - freq_arr(0));
                }

                const auto* ref_attrs = GetJsonPtr(zattrs, "/reference_frequency/attrs");
                if (ref_attrs && ref_attrs->is_object()) {
                    TryAddString(_builder, *ref_attrs, "/units", "CUNIT3");
                    const auto* observer = GetJsonPtr(*ref_attrs, "/observer");
                    if (observer && observer->is_string()) {
                        std::string specsys = observer->get<std::string>();
                        ToUpperAscii(specsys);
                        _builder.AddString("SPECSYS", specsys);
                    }
                }

                TryAddDouble(_builder, zattrs, "/rest_frequency/data", "RESTFRQ");
            }
        }, "Spectral Axis");
    }

    void AppendStokesAxis() {
        SafeParseBlock([&]() {
            std::vector<std::string> pol_strs = _impl.store->ReadStringArray(POLARIZATION_AXIS);
            _builder.AddString("CTYPE4", "STOKES");
            _builder.AddDouble("CRPIX4", 1.0);

            std::vector<casacore::Stokes::StokesTypes> stokes_types = StokesTypesFromLabels(pol_strs);
            bool has_stokes_axis =
                !stokes_types.empty() && (stokes_types.size() != 1 || stokes_types[0] != casacore::Stokes::I);
            if (!has_stokes_axis) {
                _builder.AddDouble("CDELT4", 1.0);
                _builder.AddDouble("CRVAL4", 1.0);
                _builder.AddString("CUNIT4", "");
                return;
            }

            // FITS represents the Stokes axis linearly in FITS Stokes values (I=1, Q=2, U=3, V=4,
            // RR=-1, LL=-2, RL=-3, LR=-4, ...), which differ from the casacore enum ordinals. Convert
            // before computing CRVAL4/CDELT4 and before testing whether the axis is linear at all.
            std::vector<int> fits_values;
            fits_values.reserve(stokes_types.size());
            for (const auto& type : stokes_types) {
                fits_values.push_back(static_cast<int>(casacore::Stokes::FITSValue(type)));
            }

            int stokes_first = fits_values[0];
            _builder.AddDouble("CRVAL4", static_cast<double>(stokes_first));
            if (fits_values.size() == 1) {
                _builder.AddDouble("CDELT4", 1.0);
                _builder.AddString("CUNIT4", "");
                return;
            }

            int delta = fits_values[1] - stokes_first;
            bool uniform = true;
            for (size_t i = 2; i < fits_values.size(); ++i) {
                int expected = stokes_first + (static_cast<int>(i) * delta);
                if (fits_values[i] != expected) {
                    uniform = false;
                    break;
                }
            }
            if (uniform) {
                _builder.AddDouble("CDELT4", static_cast<double>(delta));
            } else {
                std::string stokes_list;
                for (size_t i = 0; i < pol_strs.size(); ++i) {
                    stokes_list += (i == 0 ? "" : ", ") + pol_strs[i];
                }
                spdlog::warn("Non-uniform Stokes spacing for types [{}]; cannot be represented with CDELT4", stokes_list);
            }
            _builder.AddString("CUNIT4", "");
        }, "Stokes Axis");
    }

    void AppendSkyMetadata() {
        nlohmann::json zattrs_sky;
        try {
            zattrs_sky = _impl.store->GetAttributes(_impl.store->GetImageName());
        } catch (...) {
            spdlog::warn("Error parsing SKY zattrs");
            return;
        }

        TryAddString(_builder, zattrs_sky, "/units", "BUNIT");
        TryAddString(_builder, zattrs_sky, "/type", "BTYPE");
        TryAddString(_builder, zattrs_sky, "/object_name", "OBJECT");
        TryAddString(_builder, zattrs_sky, "/observer", "OBSERVER");

        AppendObsDate(zattrs_sky);
        AppendTelescope(zattrs_sky);
        AppendUserMetadata(zattrs_sky);
    }

    void AppendObsDate(const nlohmann::json& zattrs_sky) {
        SafeParseBlock([&]() {
            const auto* obs_scale = GetJsonPtr(zattrs_sky, "/obsdate/attrs/scale");
            const auto* obs_format = GetJsonPtr(zattrs_sky, "/obsdate/attrs/format");
            const auto* obs_data = GetJsonPtr(zattrs_sky, "/obsdate/data");
            if (obs_format && obs_data && obs_format->is_string()) {
                std::string scale = "UTC";
                if (obs_scale && obs_scale->is_string()) {
                    scale = obs_scale->get<std::string>();
                } else {
                    spdlog::warn("Missing or invalid obsdate time scale; falling back to UTC");
                }
                const ObsDate obs_date = ParseObsDate(*obs_data, obs_format->get<std::string>(), scale);
                ToUpperAscii(scale);
                _builder.AddString("TIMESYS", scale);
                _builder.AddString("DATE-OBS", obs_date.date);
                _builder.AddDouble("MJD-OBS", obs_date.mjd);
            }
        }, "TIMESYS/DATE-OBS/MJD-OBS");
    }

    void AppendTelescope(const nlohmann::json& zattrs_sky) {
        SafeParseBlock([&]() {
            TryAddString(_builder, zattrs_sky, "/telescope/name", "TELESCOP");
            const auto* telescope_dir = GetJsonPtr(zattrs_sky, "/telescope/direction/data");
            const auto* telescope_dist = GetJsonPtr(zattrs_sky, "/telescope/distance/data");
            if (telescope_dir && telescope_dist && telescope_dir->is_array() && telescope_dir->size() >= 2 && telescope_dist->is_array() &&
                !telescope_dist->empty()) {
                double lon = (*telescope_dir)[0].get<double>();
                double lat = (*telescope_dir)[1].get<double>();
                double radius = (*telescope_dist)[0].get<double>();
                double obsgeo_x = radius * cos(lat) * cos(lon);
                double obsgeo_y = radius * cos(lat) * sin(lon);
                double obsgeo_z = radius * sin(lat);
                _builder.AddDouble("OBSGEO-X", obsgeo_x);
                _builder.AddDouble("OBSGEO-Y", obsgeo_y);
                _builder.AddDouble("OBSGEO-Z", obsgeo_z);
            }
        }, "TELESCOPE/OBSGEO");
    }

    void AppendUserMetadata(const nlohmann::json& zattrs_sky) {
        SafeParseBlock([&]() {
            const auto* user = GetJsonPtr(zattrs_sky, "/user");
            if (user && user->is_object()) {
                for (const auto& [item_key, item_value] : user->items()) {
                    std::string key = item_key;
                    ToUpperAscii(key);
                    if (key.size() > FITS_KEYWORD_MAX_LEN) {
                        key = key.substr(0, FITS_KEYWORD_MAX_LEN);
                    }
                    if (item_value.is_string()) {
                        _builder.AddString(key, item_value.get<std::string>());
                    } else if (item_value.is_number_float() || item_value.is_number_integer() || item_value.is_number_unsigned()) {
                        _builder.AddDouble(key, item_value.get<double>());
                    }
                }
            }
        }, "User Metadata");
    }

    void AppendBeams() {
        SafeParseBlock([&]() {
            casacore::ImageBeamSet beam_set;
            if (_impl.GetBeams(beam_set)) {
                _builder.AddString("CASAMBM", "T");
            }
        }, "Beam Parameters");
    }

    Impl& _impl;
    const casacore::IPosition& _shape;
    FitsHeaderBuilder _builder;
};

ZarrMetadata::ZarrMetadata(const std::string& filename) : _impl(std::make_unique<Impl>()), _filename(filename) {}

ZarrMetadata::~ZarrMetadata() = default;

bool ZarrMetadata::Initialize() {
    if (_initialized) {
        return true;
    }

    try {
        _impl->store = std::make_shared<ZarrStore>(_filename);
        if (!_impl->store->Open()) {
            return false;
        }

        _impl->axes = ParseAxes(_impl->store->GetImageMetadata());
        ValidateSupportedAxes(_impl->axes, {TIME_AXIS, POLARIZATION_AXIS, FREQUENCY_AXIS, L_AXIS, M_AXIS});
        _shape = _impl->CartaShape();

        _initialized = true;

        std::string axes_str;
        for (const auto& [name, info] : _impl->axes) {
            if (!axes_str.empty()) {
                axes_str += ", ";
            }
            axes_str += name + "=" + std::to_string(info.size);
        }
        spdlog::debug("ZarrMetadata initialized: {}", axes_str);
        return true;
    } catch (const std::exception& ex) {
        spdlog::error("Exception initializing ZarrMetadata: {}", ex.what());
        return false;
    }
}

bool ZarrMetadata::IsInitialized() const {
    return _initialized;
}

const casacore::IPosition& ZarrMetadata::GetShape() const {
    return _shape;
}

const std::map<std::string, ZarrMetadata::AxisInfo>& ZarrMetadata::GetAxes() const {
    return _impl->axes;
}

casacore::Vector<casacore::Int> ZarrMetadata::GetStokesTypes() {
    casacore::Vector<casacore::Int> stokes_types;
    if (!_initialized) {
        return stokes_types;
    }

    try {
        std::vector<std::string> pol_strs = _impl->store->ReadStringArray(POLARIZATION_AXIS);
        std::vector<casacore::Stokes::StokesTypes> types = StokesTypesFromLabels(pol_strs);
        stokes_types.resize(types.size());
        for (size_t i = 0; i < types.size(); ++i) {
            stokes_types[i] = static_cast<casacore::Int>(types[i]);
        }
    } catch (const std::exception& ex) {
        spdlog::debug("Failed to read polarization stokes types: {}", ex.what());
        stokes_types.resize(0);
    }

    return stokes_types;
}

bool ZarrMetadata::GetBeams(casacore::ImageBeamSet& beam_set) {
    return _impl->GetBeams(beam_set);
}

std::vector<std::pair<std::string, std::string>> ZarrMetadata::GetStorageInfo() {
    if (!_initialized) {
        return {};
    }

    try {
        return _impl->StorageInfo();
    } catch (const std::exception& ex) {
        spdlog::debug("Failed to read Zarr storage info: {}", ex.what());
    }

    return {};
}

casacore::Vector<casacore::String> ZarrMetadata::FitsHeaderStrings() {
    if (!IsInitialized()) {
        return casacore::Vector<casacore::String>();
    }

    Impl::FitsHeaderComposer composer(*_impl, _shape);
    return composer.Build();
}

} // namespace carta
