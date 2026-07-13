/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// # ZarrImage.cc: interpret an XRADIO Zarr image over a ZarrStore
#include "ZarrImage.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "ZarrStore.h"
#include "ZarrUtil.h"

#include <casacore/casa/BasicSL/Constants.h>
#include <casacore/casa/Quanta/MVTime.h>
#include <casacore/casa/Utilities/DataType.h>
#include <casacore/measures/Measures/MEpoch.h>
#include <casacore/measures/Measures/Stokes.h>
#include <casacore/scimath/Mathematics/GaussianBeam.h>

#include "tensorstore/array.h"
#include "tensorstore/index.h"
#include "tensorstore/util/span.h"

namespace ts = tensorstore;

namespace carta {

namespace {

constexpr std::chrono::milliseconds ZARR_SIZE_TIMEOUT(50);
constexpr size_t FITS_KEYWORD_MAX_LEN = 8;
constexpr casacore::uInt DATE_OBS_PRECISION = 12; // MVTime fractional-second digits = precision - 6.
constexpr double RAD_TO_DEG = 180.0 / M_PI;
constexpr double UNIX_EPOCH_MJD = 40587.0;

// XRADIO axis names used throughout the schema.
constexpr const char* FREQUENCY_AXIS = "frequency";
constexpr const char* POLARIZATION_AXIS = "polarization";
constexpr const char* TIME_AXIS = "time";
// Direction axis names for the image array.
constexpr const char* L_AXIS = "l";
constexpr const char* M_AXIS = "m";
// Beam parameter label axis/coordinate; its values name each parameter (e.g. "major", "minor", "pa").
constexpr const char* BEAM_PARAMS_LABEL = "beam_params_label";

constexpr std::array<const char*, 5> IMAGE_REQUIRED_AXES{
    TIME_AXIS, POLARIZATION_AXIS, FREQUENCY_AXIS, L_AXIS, M_AXIS};
constexpr std::array<const char*, 4> BEAM_REQUIRED_AXES{
    TIME_AXIS, POLARIZATION_AXIS, FREQUENCY_AXIS, BEAM_PARAMS_LABEL};

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

void ToLowerAscii(std::string& text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) { return std::tolower(character); });
}

bool FitsBitpixForDataType(casacore::DataType data_type, int& bitpix) {
    static const std::map<casacore::DataType, int> data_type_bitpix{
        {casacore::TpUChar, 8},
        {casacore::TpShort, 16},
        {casacore::TpInt, 32},
        {casacore::TpInt64, 64},
        {casacore::TpFloat, -32},
        {casacore::TpDouble, -64},
    };

    const auto entry = data_type_bitpix.find(data_type);
    if (entry == data_type_bitpix.end()) {
        return false;
    }

    bitpix = entry->second;
    return true;
}

casacore::DataType ParseZarrDataType(const nlohmann::json& array_metadata) {
    std::string data_type_name = FindJsonValue<std::string>(array_metadata, "/data_type").value_or("");
    ToLowerAscii(data_type_name);

    static const std::map<std::string, casacore::DataType> data_type_map{
        {"bool", casacore::TpBool},
        {"int8", casacore::TpChar},
        {"uint8", casacore::TpUChar},
        {"int16", casacore::TpShort},
        {"uint16", casacore::TpUShort},
        {"int32", casacore::TpInt},
        {"uint32", casacore::TpUInt},
        {"int64", casacore::TpInt64},
        {"float32", casacore::TpFloat},
        {"float64", casacore::TpDouble},
        {"complex64", casacore::TpComplex},
        {"complex128", casacore::TpDComplex},
    };

    auto data_type = data_type_map.find(data_type_name);
    return data_type == data_type_map.end() ? casacore::TpOther : data_type->second;
}

casacore::MEpoch::Types GetEpochTypeFromScale(std::string scale) {
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
    const casacore::MEpoch::Types epoch_type = GetEpochTypeFromScale(scale);

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

std::string MakeCtypeHeaderValue(const std::string& axis, const std::string& projection) {
    if (projection.empty()) {
        return axis;
    }
    // Pad the axis name to at least 4 chars with '-', then append the projection (e.g. "RA" -> "RA---SIN").
    return fmt::format("{:-<4}-{}", axis, projection);
}

// Emit a string FITS keyword from a JSON pointer if it resolves to a string.
void TryAddString(FitsHeaderBuilder& builder, const nlohmann::json& obj, const char* ptr, const std::string& key) {
    if (auto value = FindJsonValue<std::string>(obj, ptr)) {
        builder.AddString(key, *value);
    }
}

// Emit a numeric FITS keyword from a JSON pointer if it resolves to a number, applying an optional scale.
void TryAddDouble(FitsHeaderBuilder& builder, const nlohmann::json& obj, const char* ptr, const std::string& key, double scale = 1.0) {
    if (auto value = FindJsonValue<double>(obj, ptr)) {
        builder.AddDouble(key, *value * scale);
    }
}

template <typename Func>
void LogParseErrors(Func func, const std::string& context) {
    try {
        func();
    } catch (const std::exception& e) {
        spdlog::warn("Error parsing {}: {}", context, e.what());
    } catch (...) {
        spdlog::warn("Unknown error parsing {}", context);
    }
}

casacore::IPosition ParseZarrShape(const nlohmann::json& array_metadata) {
    const auto* shape = FindJsonPtr(array_metadata, "/shape");
    if (!shape || !shape->is_array()) {
        throw std::runtime_error("Zarr shape is not an array");
    }

    std::vector<int> shape_values;
    shape_values.reserve(shape->size());
    for (const auto& dim : *shape) {
        if (!dim.is_number_integer()) {
            throw std::runtime_error("Zarr shape contains a non-integer dimension");
        }

        shape_values.push_back(dim.get<int>());
    }

    return casacore::IPosition(shape_values);
}

bool TryComputeDirectorySize(const std::string& filename, std::chrono::milliseconds timeout, int64_t& size) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int64_t directory_size = 0;
    const std::filesystem::path root_path(filename);

    try {
        for (const auto& entry :
            std::filesystem::recursive_directory_iterator(root_path, std::filesystem::directory_options::skip_permission_denied)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            if (entry.is_regular_file()) {
                directory_size += entry.file_size();
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }

    size = directory_size;
    return true;
}

using ZarrAxisInfo = ZarrImage::ZarrAxisInfo;

std::map<std::string, ZarrAxisInfo> ParseZarrAxes(const nlohmann::json& metadata) {
    const auto* dimension_names = FindJsonPtr(metadata, "/dimension_names");
    if (!dimension_names || !dimension_names->is_array()) {
        throw std::runtime_error("XRADIO schema requires dimension_names array");
    }

    std::vector<std::string> dims = dimension_names->get<std::vector<std::string>>();
    const casacore::IPosition shape = ParseZarrShape(metadata);
    if (shape.size() != static_cast<int>(dims.size())) {
        throw std::runtime_error(
            fmt::format("Zarr dimension_names size {} does not match shape size {}", dims.size(), shape.size()));
    }

    std::map<std::string, ZarrAxisInfo> axes;
    for (size_t i = 0; i < dims.size(); ++i) {
        axes[dims[i]] = ZarrAxisInfo{i, static_cast<int>(shape[i])};
    }

    return axes;
}

const ZarrAxisInfo& RequireAxis(const std::map<std::string, ZarrAxisInfo>& axes, const std::string& axis_name) {
    auto axis = axes.find(axis_name);
    if (axis == axes.end()) {
        throw std::runtime_error("Missing required axis " + axis_name);
    }
    return axis->second;
}

template <size_t N>
void ValidateRequiredAxes(const std::map<std::string, ZarrAxisInfo>& axes, const std::array<const char*, N>& required_axes) {
    for (const auto& axis_name : required_axes) {
        const ZarrAxisInfo& axis = RequireAxis(axes, axis_name);
        if (std::string_view(axis_name) == TIME_AXIS && axis.size > 1) {
            throw std::runtime_error(fmt::format("Zarr images with time axis size > 1 are not supported: time={}", axis.size));
        }
    }
}

bool IsSupportedImageArray(const std::string& array_name) {
    return std::find(ZARR_SUPPORTED_IMAGE_ARRAYS.begin(), ZARR_SUPPORTED_IMAGE_ARRAYS.end(), array_name) !=
           ZARR_SUPPORTED_IMAGE_ARRAYS.end();
}

std::map<std::string, ZarrAxisInfo> ParseImageAxes(const std::string& array_name, const nlohmann::json& metadata) {
    if (!IsSupportedImageArray(array_name)) {
        throw std::runtime_error("Unsupported Zarr image array " + array_name);
    }
    if (metadata.empty()) {
        throw std::runtime_error("No metadata for Zarr image array " + array_name);
    }

    std::map<std::string, ZarrAxisInfo> axes = ParseZarrAxes(metadata);
    ValidateRequiredAxes(axes, IMAGE_REQUIRED_AXES);
    return axes;
}

std::vector<casacore::Stokes::StokesTypes> StokesTypesFromLabels(const std::vector<std::string>& labels) {
    std::vector<casacore::Stokes::StokesTypes> types;
    types.reserve(labels.size());
    for (const auto& label : labels) {
        types.push_back(casacore::Stokes::type(label));
    }
    return types;
}

// Reorder a storage-order chunk/shard shape into CARTA axis order (x, y, freq, stokes),
// dropping any extra axes (e.g. time). Missing entries in the shape array default to 1.
std::vector<int64_t> ReorderShapeToCartaAxes(const std::vector<int64_t>& shape, const std::map<std::string, ZarrAxisInfo>& axes) {
    auto value_at = [&](const std::string& name) -> int64_t {
        const ZarrAxisInfo& axis = RequireAxis(axes, name);
        return (axis.index >= shape.size()) ? 1 : shape[axis.index];
    };
    return {value_at(L_AXIS), value_at(M_AXIS), value_at(FREQUENCY_AXIS), value_at(POLARIZATION_AXIS)};
}

} // namespace

struct ZarrImage::Impl {
    // Low-level Zarr access. Created during Initialize(); shared so future pixel reads can reuse it.
    std::shared_ptr<ZarrStore> store;

    // XRADIO image array name and axis name -> {storage index, size}, parsed from the image array metadata.
    std::string image_name;
    std::map<std::string, ZarrAxisInfo> axes;

    // Lazily-loaded beam set: GetBeams() may be called more than once per image (e.g. once while
    // building the FITS header and once while setting up the image), so the array is read only once.
    bool beams_loaded = false;
    bool has_beams = false;
    casacore::ImageBeamSet cached_beam_set;

    // Builds the FITS header from the parsed metadata; defined out-of-line below.
    class FitsHeaderComposer;

    casacore::IPosition BuildCartaShape() const {
        const int x_size = RequireAxis(axes, L_AXIS).size;
        const int y_size = RequireAxis(axes, M_AXIS).size;
        const int frequency_size = RequireAxis(axes, FREQUENCY_AXIS).size;
        const int polarization_size = RequireAxis(axes, POLARIZATION_AXIS).size;

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

    nlohmann::json GetAttributes(const std::string& array_name) const {
        nlohmann::json metadata = store->ReadArrayMetadata(array_name);
        const auto attributes = metadata.find("attributes");
        if (attributes != metadata.end() && attributes->is_object()) {
            return *attributes;
        }

        return nlohmann::json::object();
    }

    bool LoadBeams(casacore::ImageBeamSet& beam_set) const {
        std::string beam_array_name = GetAttributes(image_name).value("beam_fit_params", "");
        if (beam_array_name.empty()) {
            return false;
        }

        try {
            nlohmann::json beam_metadata = store->ReadArrayMetadata(beam_array_name);
            std::map<std::string, ZarrAxisInfo> beam_axes = ParseZarrAxes(beam_metadata);
            ValidateRequiredAxes(beam_axes, BEAM_REQUIRED_AXES);

            const ZarrAxisInfo& time_axis = RequireAxis(beam_axes, TIME_AXIS);
            const ZarrAxisInfo& freq_axis = RequireAxis(beam_axes, FREQUENCY_AXIS);
            const ZarrAxisInfo& stokes_axis = RequireAxis(beam_axes, POLARIZATION_AXIS);
            const ZarrAxisInfo& param_axis = RequireAxis(beam_axes, BEAM_PARAMS_LABEL);

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
            std::string unit = GetAttributes(beam_array_name).value("units", "");
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
    std::vector<std::pair<std::string, std::string>> GetStorageInfo() const {
        std::vector<std::pair<std::string, std::string>> info;
        const ZarrStore::StorageLayout layout = store->GetStorageLayout(image_name);

        // Report shapes in CARTA axis order (x, y, freq, stokes), dropping time axis: "[a, b, c, d] (RA, DEC, FREQ, STOKES)".
        auto format_shape = [&](const std::vector<int64_t>& shape) {
            return fmt::format("[{}] (RA, DEC, FREQ, STOKES)", fmt::join(ReorderShapeToCartaAxes(shape, axes), ", "));
        };

        // Emit Shard shape (if any), then Chunk shape, then Compressor so they group next to "Shape" in the file browser.
        if (layout.sharded && !layout.shard_shape.empty()) {
            info.emplace_back("Shard shape", format_shape(layout.shard_shape));
        }
        if (!layout.chunk_shape.empty()) {
            info.emplace_back("Chunk shape", format_shape(layout.chunk_shape));
        }
        if (!layout.compressor.empty()) {
            info.emplace_back("Compressor", layout.compressor);
        }

        return info;
    }
};

// Assembles FITS header records from the parsed Zarr metadata and coordinate arrays.
class ZarrImage::Impl::FitsHeaderComposer {
public:
    FitsHeaderComposer(Impl& impl, const casacore::IPosition& shape, casacore::DataType data_type)
        : _impl(impl), _shape(shape), _data_type(data_type) {}

    casacore::Vector<casacore::String> Build() {
        AppendDataTypeHeaders();
        AppendNAxis();
        AppendDirectionHeaders();
        AppendDirectionIncrements();
        AppendSpectralAxis();
        AppendStokesAxis();
        AppendImageMetadata();
        AppendCasaBeamFlag();
        return _builder.Build();
    }

private:
    void AppendDataTypeHeaders() {
        int bitpix = 0;
        bool has_bitpix = false;
        LogParseErrors(
            [&]() {
                has_bitpix = FitsBitpixForDataType(_data_type, bitpix);
                if (!has_bitpix) {
                    spdlog::warn("Zarr data type cannot be represented as FITS BITPIX; omitting FITS BITPIX");
                }
            },
            "BITPIX");

        _builder.AddString("SIMPLE", "T");
        if (has_bitpix) {
            _builder.AddInt("BITPIX", bitpix);
        }
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

        const auto* coordinate_system_info = FindJsonPtr(zattrs, "/coordinate_system_info");
        if (!coordinate_system_info || !coordinate_system_info->is_object()) {
            return;
        }

        AppendEquinox(zattrs);
        std::string radesys = AppendRadesys(zattrs);
        AppendDirectionReference(zattrs);
        AppendCtypes(zattrs, radesys);

        TryAddDouble(_builder, zattrs, "/coordinate_system_info/native_pole_direction/data/0", "LONPOLE", RAD_TO_DEG);
        TryAddDouble(_builder, zattrs, "/coordinate_system_info/native_pole_direction/data/1", "LATPOLE", RAD_TO_DEG);

        // TODO: XRADIO has not finalized projection_parameters yet.

        AppendPcMatrix(zattrs);
    }

    void AppendEquinox(const nlohmann::json& zattrs) {
        LogParseErrors(
            [&]() {
                const auto* equinox = FindJsonPtr(zattrs, "/coordinate_system_info/reference_direction/attrs/equinox");
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
            },
            "EQUINOX");
    }

    std::string AppendRadesys(const nlohmann::json& zattrs) {
        std::string radesys;
        LogParseErrors(
            [&]() {
                if (auto frame = FindJsonValue<std::string>(zattrs, "/coordinate_system_info/reference_direction/attrs/frame")) {
                    radesys = *frame;
                    ToUpperAscii(radesys);
                    _builder.AddString("RADESYS", radesys);
                }
            },
            "RADESYS");
        return radesys;
    }

    void AppendDirectionReference(const nlohmann::json& zattrs) {
        LogParseErrors(
            [&]() {
                const auto* ref_data = FindJsonPtr(zattrs, "/coordinate_system_info/reference_direction/data");
                if (ref_data && ref_data->is_array() && ref_data->size() >= 2) {
                    _builder.AddDouble("CRVAL1", (*ref_data)[0].get<double>() * RAD_TO_DEG);
                    _builder.AddDouble("CRVAL2", (*ref_data)[1].get<double>() * RAD_TO_DEG);
                }
            },
            "CRVAL1/2");
    }

    void AppendCtypes(const nlohmann::json& zattrs, const std::string& radesys) {
        LogParseErrors(
            [&]() {
                std::string ctype1_prefix = "RA";
                std::string ctype2_prefix = "DEC";
                std::string projection_str;
                if (auto projection = FindJsonValue<std::string>(zattrs, "/coordinate_system_info/projection")) {
                    projection_str = *projection;
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
                _builder.AddString("CTYPE1", MakeCtypeHeaderValue(ctype1_prefix, projection_str));
                _builder.AddString("CTYPE2", MakeCtypeHeaderValue(ctype2_prefix, projection_str));
            },
            "CTYPE1/2");
    }

    void AppendPcMatrix(const nlohmann::json& zattrs) {
        LogParseErrors(
            [&]() {
                const auto* pc_val = FindJsonPtr(zattrs, "/coordinate_system_info/pixel_coordinate_transformation_matrix");
                if (pc_val && pc_val->is_array() && pc_val->size() >= 2 && (*pc_val)[0].is_array() && (*pc_val)[0].size() >= 2) {
                    _builder.AddDouble("PC1_1", (*pc_val)[0][0].get<double>());
                    _builder.AddDouble("PC1_2", (*pc_val)[0][1].get<double>());
                    _builder.AddDouble("PC2_1", (*pc_val)[1][0].get<double>());
                    _builder.AddDouble("PC2_2", (*pc_val)[1][1].get<double>());
                }
            },
            "PC Matrix");
    }

    void AppendDirectionIncrements() {
        LogParseErrors(
            [&]() {
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
            },
            "Direction Increments");
    }

    void AppendSpectralAxis() {
        LogParseErrors(
            [&]() {
                auto freq_arr = _impl.store->ReadDoubleArray(FREQUENCY_AXIS);
                nlohmann::json zattrs = _impl.GetAttributes(FREQUENCY_AXIS);

                if (freq_arr.num_elements() > 0) {
                    _builder.AddString("CTYPE3", "FREQ");
                    _builder.AddDouble("CRPIX3", 1.0);
                    _builder.AddDouble("CRVAL3", freq_arr(0));
                    if (freq_arr.num_elements() > 1) {
                        _builder.AddDouble("CDELT3", freq_arr(1) - freq_arr(0));
                    }

                    const auto* ref_attrs = FindJsonPtr(zattrs, "/reference_frequency/attrs");
                    if (ref_attrs && ref_attrs->is_object()) {
                        TryAddString(_builder, *ref_attrs, "/units", "CUNIT3");
                        if (auto observer = FindJsonValue<std::string>(*ref_attrs, "/observer")) {
                            std::string specsys = *observer;
                            ToUpperAscii(specsys);
                            _builder.AddString("SPECSYS", specsys);
                        }
                    }

                    TryAddDouble(_builder, zattrs, "/rest_frequency/data", "RESTFRQ");
                }
            },
            "Spectral Axis");
    }

    void AppendStokesAxis() {
        LogParseErrors(
            [&]() {
                std::vector<std::string> pol_strs = _impl.store->ReadStringArray(POLARIZATION_AXIS);
                _builder.AddString("CTYPE4", "STOKES");
                _builder.AddDouble("CRPIX4", 1.0);

                std::vector<casacore::Stokes::StokesTypes> stokes_types = StokesTypesFromLabels(pol_strs);
                bool has_stokes_axis = !stokes_types.empty() && (stokes_types.size() != 1 || stokes_types[0] != casacore::Stokes::I);
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
                    spdlog::warn("Non-uniform Stokes spacing for types [{}]; cannot be represented with CDELT4",
                        fmt::join(pol_strs, ", "));
                }
                _builder.AddString("CUNIT4", "");
            },
            "Stokes Axis");
    }

    void AppendImageMetadata() {
        nlohmann::json zattrs_image;
        try {
            zattrs_image = _impl.GetAttributes(_impl.image_name);
        } catch (...) {
            spdlog::warn("Error parsing Zarr image attributes");
            return;
        }

        TryAddString(_builder, zattrs_image, "/units", "BUNIT");
        TryAddString(_builder, zattrs_image, "/type", "BTYPE");
        TryAddString(_builder, zattrs_image, "/object_name", "OBJECT");
        TryAddString(_builder, zattrs_image, "/observer", "OBSERVER");

        AppendObsDate(zattrs_image);
        AppendTelescope(zattrs_image);
        AppendUserMetadata(zattrs_image);
    }

    void AppendObsDate(const nlohmann::json& zattrs_image) {
        LogParseErrors(
            [&]() {
                auto obs_format = FindJsonValue<std::string>(zattrs_image, "/obsdate/attrs/format");
                const auto* obs_data = FindJsonPtr(zattrs_image, "/obsdate/data");
                if (obs_format && obs_data) {
                    std::string scale = "UTC";
                    if (auto obs_scale = FindJsonValue<std::string>(zattrs_image, "/obsdate/attrs/scale")) {
                        scale = *obs_scale;
                    } else {
                        spdlog::warn("Missing or invalid obsdate time scale; falling back to UTC");
                    }
                    const ObsDate obs_date = ParseObsDate(*obs_data, *obs_format, scale);
                    ToUpperAscii(scale);
                    _builder.AddString("TIMESYS", scale);
                    _builder.AddString("DATE-OBS", obs_date.date);
                    _builder.AddDouble("MJD-OBS", obs_date.mjd);
                }
            },
            "TIMESYS/DATE-OBS/MJD-OBS");
    }

    void AppendTelescope(const nlohmann::json& zattrs_image) {
        LogParseErrors(
            [&]() {
                TryAddString(_builder, zattrs_image, "/telescope/name", "TELESCOP");
                const auto* telescope_dir = FindJsonPtr(zattrs_image, "/telescope/direction/data");
                const auto* telescope_dist = FindJsonPtr(zattrs_image, "/telescope/distance/data");
                if (telescope_dir && telescope_dist && telescope_dir->is_array() && telescope_dir->size() >= 2 &&
                    telescope_dist->is_array() && !telescope_dist->empty()) {
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
            },
            "TELESCOPE/OBSGEO");
    }

    void AppendUserMetadata(const nlohmann::json& zattrs_image) {
        LogParseErrors(
            [&]() {
                const auto* user = FindJsonPtr(zattrs_image, "/user");
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
            },
            "User Metadata");
    }

    void AppendCasaBeamFlag() {
        LogParseErrors(
            [&]() {
                casacore::ImageBeamSet beam_set;
                if (_impl.GetBeams(beam_set)) {
                    _builder.AddString("CASAMBM", "T");
                }
            },
            "Beam Parameters");
    }

    Impl& _impl;
    const casacore::IPosition& _shape;
    casacore::DataType _data_type;
    FitsHeaderBuilder _builder;
};

ZarrImage::ZarrImage(const std::string& filename, const std::string& image_name)
    : _impl(std::make_unique<Impl>()),
      _filename(filename),
      _image_name(image_name.empty() ? ZARR_DEFAULT_IMAGE_ARRAY : image_name) {}

ZarrImage::~ZarrImage() = default;

bool ZarrImage::ComputeImageDataSizeBytes(const std::string& filename, int64_t& size, bool& size_is_upper_bound) {
    size_is_upper_bound = false;
    if (TryComputeDirectorySize(filename, ZARR_SIZE_TIMEOUT, size)) {
        return true;
    }

    try {
        ZarrStore store(filename);
        if (!store.Open()) {
            return false;
        }

        size = store.ComputeTotalArraySizeBytes();
        size_is_upper_bound = true;
        return true;
    } catch (const std::exception& ex) {
        spdlog::debug("Failed to compute Zarr image data size: {}", ex.what());
    }

    return false;
}

bool ZarrImage::Initialize() {
    if (_initialized) {
        return true;
    }

    try {
        _impl->store = std::make_shared<ZarrStore>(_filename);
        if (!_impl->store->Open()) {
            return false;
        }

        _impl->image_name = _image_name;
        nlohmann::json image_metadata = _impl->store->ReadArrayMetadata(_impl->image_name);
        _impl->axes = ParseImageAxes(_impl->image_name, image_metadata);
        _shape = _impl->BuildCartaShape();
        _data_type = ParseZarrDataType(image_metadata);

        _initialized = true;

        std::string axes_str;
        for (const auto& [name, info] : _impl->axes) {
            if (!axes_str.empty()) {
                axes_str += ", ";
            }
            axes_str += name + "=" + std::to_string(info.size);
        }
        spdlog::debug("ZarrImage initialized: {}", axes_str);
        return true;
    } catch (const std::exception& ex) {
        spdlog::error("Exception initializing ZarrImage: {}", ex.what());
        return false;
    }
}

bool ZarrImage::IsInitialized() const {
    return _initialized;
}

const casacore::IPosition& ZarrImage::GetShape() const {
    return _shape;
}

casacore::DataType ZarrImage::GetDataType() const {
    return _data_type;
}

const std::map<std::string, ZarrImage::ZarrAxisInfo>& ZarrImage::GetAxes() const {
    return _impl->axes;
}

casacore::Vector<casacore::Int> ZarrImage::GetStokesTypes() {
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

bool ZarrImage::GetBeams(casacore::ImageBeamSet& beam_set) {
    return _impl->GetBeams(beam_set);
}

std::vector<std::pair<std::string, std::string>> ZarrImage::GetStorageInfo() {
    if (!_initialized) {
        return {};
    }

    try {
        return _impl->GetStorageInfo();
    } catch (const std::exception& ex) {
        spdlog::debug("Failed to read Zarr storage info: {}", ex.what());
    }

    return {};
}

casacore::Vector<casacore::String> ZarrImage::FitsHeaderStrings() {
    if (!IsInitialized()) {
        return casacore::Vector<casacore::String>();
    }

    Impl::FitsHeaderComposer composer(*_impl, _shape, _data_type);
    return composer.Build();
}

} // namespace carta
