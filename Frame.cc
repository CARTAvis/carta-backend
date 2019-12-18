#include "Frame.h"

#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <fstream>
#include <thread>

#include <casacore/tables/DataMan/TiledFileAccess.h>
#include <imageanalysis/Annotations/AnnotationBase.h>
#include <imageanalysis/Annotations/RegionTextList.h>

#include "Compression.h"
#include "Contouring.h"
#include "Ds9Parser.h"
#include "Smoothing.h"

using namespace carta;

Frame::Frame(uint32_t session_id, carta::FileLoader* loader, const std::string& hdu, bool verbose, int default_channel)
    : _session_id(session_id),
      _valid(true),
      _z_profile_count(0),
      _cursor_set(false),
      _loader(loader),
      _spectral_axis(-1),
      _stokes_axis(-1),
      _channel_index(-1),
      _stokes_index(-1),
      _num_channels(1),
      _num_stokes(1),
      _verbose(verbose) {
    if (!_loader) {
        _open_image_error = fmt::format("Problem loading image: image type not supported.");
        if (_verbose) {
            Log(session_id, _open_image_error);
        }
        _valid = false;
        return;
    }

    _loader->SetFramePtr(this);

    try {
        _loader->OpenFile(hdu);
    } catch (casacore::AipsError& err) {
        _open_image_error = fmt::format("Problem opening image: {}", err.getMesg());
        if (_verbose) {
            Log(session_id, _open_image_error);
        }
        _valid = false;
        return;
    }

    // Get shape and axis values from the loader
    std::string log_message;
    if (!_loader->FindCoordinateAxes(_image_shape, _spectral_axis, _stokes_axis, log_message)) {
        _open_image_error = fmt::format("Problem determining file shape: {}", log_message);
        if (_verbose) {
            Log(session_id, _open_image_error);
        }
        _valid = false;
        return;
    }
    _num_channels = (_spectral_axis >= 0 ? _image_shape(_spectral_axis) : 1);
    _num_stokes = (_stokes_axis >= 0 ? _image_shape(_stokes_axis) : 1);

    // make Region for entire image (after current channel/stokes set)
    SetImageRegion(IMAGE_REGION_ID);
    SetDefaultCursor();  // frontend sets requirements for cursor before cursor set
    _cursor_set = false; // only true if set by frontend

    // set current channel, stokes, imageCache
    _channel_index = default_channel;
    _stokes_index = DEFAULT_STOKES;
    SetImageCache();

    try {
        // Resize stats vectors and load data from image, if the format supports it.
        // A failure here shouldn't invalidate the frame
        _loader->LoadImageStats();
    } catch (casacore::AipsError& err) {
        _open_image_error = fmt::format("Problem loading statistics from file: {}", err.getMesg());
        if (_verbose) {
            Log(session_id, _open_image_error);
        }
    }
}

Frame::~Frame() {
    for (auto& region : _regions) {
        region.second.reset();
    }
    _regions.clear();
}

bool Frame::IsValid() {
    return _valid;
}

std::string Frame::GetErrorMessage() {
    return _open_image_error;
}

void Frame::DisconnectCalled() {
    SetConnectionFlag(false); // set a false flag to interrupt the running jobs in loader
    while (_z_profile_count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } // wait for the jobs finished
}

std::vector<int> Frame::GetRegionIds() {
    // return list of region ids for this frame
    std::vector<int> region_ids;
    for (auto& region : _regions) {
        region_ids.push_back(region.first);
    }
    return region_ids;
}

int Frame::GetMaxRegionId() {
    std::vector<int> ids(GetRegionIds());
    return *std::max_element(ids.begin(), ids.end());
}

size_t Frame::NumChannels() {
    return _num_channels;
}

size_t Frame::NumStokes() {
    return _num_stokes;
}

int Frame::CurrentChannel() {
    return _channel_index;
}

int Frame::CurrentStokes() {
    return _stokes_index;
}

bool Frame::CheckChannel(int channel) {
    return ((channel >= 0) && (channel < NumChannels()));
}

bool Frame::CheckStokes(int stokes) {
    return ((stokes >= 0) && (stokes < NumStokes()));
}

bool Frame::ChannelsChanged(int channel, int stokes) {
    return (channel != _channel_index || stokes != _stokes_index);
}

// ********************************************************************
// Set regions

bool Frame::SetRegion(int region_id, const std::string& name, CARTA::RegionType type, std::vector<CARTA::Point>& points, float rotation,
    std::string& message) {
    // Create or update Region
    bool region_set(false);

    // create or update Region
    if (_regions.count(region_id)) { // update Region
        auto& region = _regions[region_id];
        region_set = region->UpdateRegionParameters(name, type, points, rotation);
        if (region->RegionChanged()) {
            region->SetAllProfilesUnsent(); // force new profiles for new region settings
        }
    } else { // map new Region to region id
        casacore::CoordinateSystem coord_sys;
        if (!_loader->GetCoordinateSystem(coord_sys)) {
            region_set = false;
            message = "Image has no coordinate system, cannot create region.";
        } else {
            auto region = std::unique_ptr<carta::Region>(
                new carta::Region(name, type, points, rotation, _image_shape, _spectral_axis, _stokes_axis, coord_sys));
            if (region->IsValid()) {
                _regions[region_id] = move(region);
                region_set = true;
            }
        }

        if (region_set) {
            if (name == "cursor" && type == CARTA::RegionType::POINT) {
                // update current cursor's x-y coordinate
                SetCursorXy(points[0].x(), points[0].y());
            }
        } else {
            message = fmt::format("Region parameters failed to validate for region id {}", region_id);
        }
    }
    return region_set;
}

// special cases of setRegion for image and cursor
void Frame::SetImageRegion(int region_id) {
    // Create a Region for the entire image plane: Image or Cube
    if ((region_id != IMAGE_REGION_ID) && (region_id != CUBE_REGION_ID)) {
        return;
    }

    std::string name = (region_id == IMAGE_REGION_ID ? "image" : "cube");
    // control points: center pt [cx, cy], [width, height]
    std::vector<CARTA::Point> points(2);
    CARTA::Point point;
    point.set_x(_image_shape(0) / 2.0); // center x
    point.set_y(_image_shape(1) / 2.0); // center y
    points[0] = point;
    point.set_x(_image_shape(0) + 1.0); // entire width
    point.set_y(_image_shape(1) + 1.0); // entire height
    points[1] = point;
    // rotation
    float rotation(0.0);

    // create new region
    std::string message;
    SetRegion(region_id, name, CARTA::RECTANGLE, points, rotation, message);
    if (region_id == IMAGE_REGION_ID) { // set histogram requirements: use current channel
        CARTA::SetHistogramRequirements_HistogramConfig config;
        config.set_channel(CURRENT_CHANNEL);
        config.set_num_bins(AUTO_BIN_SIZE);
        std::vector<CARTA::SetHistogramRequirements_HistogramConfig> default_configs(1, config);
        SetRegionHistogramRequirements(IMAGE_REGION_ID, default_configs);
    }
}

bool Frame::SetCursorRegion(int region_id, const CARTA::Point& point) {
    // a cursor is a region with one control point and all channels for spectral profile
    std::vector<CARTA::Point> points(1, point);
    float rotation(0.0);
    std::string message;
    _cursor_set = SetRegion(region_id, "cursor", CARTA::POINT, points, rotation, message);
    return _cursor_set;
}

void Frame::SetDefaultCursor() {
    CARTA::Point default_point;
    default_point.set_x(0);
    default_point.set_y(0);
    SetCursorRegion(CURSOR_REGION_ID, default_point);
    _cursor_set = false;
}

bool Frame::RegionChanged(int region_id) {
    bool changed(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        changed = region->RegionChanged();
    }
    return changed;
}

void Frame::RemoveRegion(int region_id) {
    if (_regions.count(region_id)) {
        _regions[region_id]->DisconnectCalled();
        _regions[region_id].reset();
        _regions.erase(region_id);
    }
}

void Frame::ImportRegion(
    CARTA::FileType file_type, std::string& filename, std::vector<std::string>& contents, CARTA::ImportRegionAck& import_ack) {
    // Import region from file or contents

    // cannot create annotation regions with no direction coordinate
    casacore::CoordinateSystem coord_sys;
    if (!_loader->GetCoordinateSystem(coord_sys)) {
        import_ack.set_success(false);
        import_ack.set_message("Import region failed: image has no coordinate system.");
        import_ack.add_regions();
        return;
    }

    if (!coord_sys.hasDirectionCoordinate()) {
        import_ack.set_success(false);
        import_ack.set_message("Import region failed: image coordinate system has no direction coordinate.");
        import_ack.add_regions();
        return;
    }

    // concat contents vector into one string delimited by newline
    std::string file_contents;
    if (!contents.empty()) {
        // concat contents into one string delimited by newline
        for (auto& line : contents) {
            file_contents.append(line + "\n");
        }
    }

    std::string error_prefix("Import region failed: "), message;
    try {
        switch (file_type) {
            case CARTA::FileType::CRTF: {
                // use casa RegionTextList to import file and create annotation file lines
                casa::RegionTextList region_list;
                bool require_region(false); // import regions outside image
                if (!filename.empty()) {
                    region_list = casa::RegionTextList(
                        filename, coord_sys, _image_shape, "", "", "", casa::RegionTextParser::CURRENT_VERSION, true, require_region);
                } else {
                    region_list = casa::RegionTextList(coord_sys, file_contents, _image_shape, "", "", "", true, require_region);
                }

                // iterate through annotations to create regions if valid and add to ack message
                for (unsigned int iline = 0; iline < region_list.nLines(); ++iline) {
                    casa::AsciiAnnotationFileLine file_line = region_list.lineAt(iline);
                    ImportAnnotationFileLine(file_line, coord_sys, file_type, import_ack, message);
                }
                break;
            }
            case CARTA::FileType::REG: {
                carta::Ds9Parser parser;
                if (!filename.empty()) {
                    parser = carta::Ds9Parser(filename, coord_sys, _image_shape);
                } else {
                    parser = carta::Ds9Parser(coord_sys, file_contents, _image_shape);
                }

                // check for failed regions
                message = parser.GetImportErrors();

                // iterate through annotations to create regions if valid and add to ack message
                for (unsigned int iline = 0; iline < parser.NumLines(); ++iline) {
                    casa::AsciiAnnotationFileLine file_line = parser.LineAt(iline);
                    ImportAnnotationFileLine(file_line, coord_sys, file_type, import_ack, message);
                }
                break;
            }
            default: {
                message = error_prefix + "file type not supported.";
                break;
            }
        }
    } catch (casacore::AipsError& err) {
        casacore::String error_message(err.getMesg());
        if (_verbose) {
            std::cerr << error_prefix << error_message << std::endl;
        }

        // shorten error message to user
        error_message = error_message.before("... thrown by"); // remove casacode file
        error_message = error_message.before(" at File");      // remove casacode file
        if (!filename.empty()) {
            casacore::Path full_path(filename);
            error_message.gsub(filename, full_path.baseName()); // remove filename path
        }
        std::ostringstream oss;
        oss << error_prefix << error_message;
        message = oss.str();
    }

    // determine success
    bool success(import_ack.regions_size() > 0);
    if (!success) {
        if (message.empty()) {
            message = error_prefix + "zero regions set";
        }
        import_ack.add_regions();
    }

    // complete message
    import_ack.set_success(success); // true if at least one region was set
    import_ack.set_message(message);
}

void Frame::ImportAnnotationFileLine(casa::AsciiAnnotationFileLine& file_line, const casacore::CoordinateSystem& coord_sys,
    CARTA::FileType file_type, CARTA::ImportRegionAck& import_ack, std::string message) {
    // Process a single CRTF annotation file line to set region; adds region to frame regions.
    // Completes ack message with region properties or appends to message if failed.
    switch (file_line.getType()) {
        case casa::AsciiAnnotationFileLine::ANNOTATION: {
            auto annotation_base = file_line.getAnnotationBase();
            const casa::AnnotationBase::Type annotation_type = annotation_base->getType();
            casacore::String region_type_str;
            if (file_type == CARTA::FileType::CRTF) {
                region_type_str = casa::AnnotationBase::typeToString(annotation_type);
            } else {
                region_type_str = AnnTypeToDs9String(annotation_type);
            }
            switch (annotation_type) {
                case casa::AnnotationBase::VECTOR:
                case casa::AnnotationBase::TEXT: {
                    break;
                }
                case casa::AnnotationBase::LINE:
                case casa::AnnotationBase::POLYLINE:
                case casa::AnnotationBase::ANNULUS: {
                    message += " Region type " + region_type_str + " is not supported yet.";
                    break;
                }
                case casa::AnnotationBase::SYMBOL:
                case casa::AnnotationBase::RECT_BOX:
                case casa::AnnotationBase::CENTER_BOX:
                case casa::AnnotationBase::ROTATED_BOX:
                case casa::AnnotationBase::POLYGON:
                case casa::AnnotationBase::CIRCLE:
                case casa::AnnotationBase::ELLIPSE: {
                    if ((annotation_type == casa::AnnotationBase::SYMBOL) || // symbol is a carta point region
                        !annotation_base->isAnnotationOnly()) {              // shape could be annotation-layer only
                        auto region = std::unique_ptr<carta::Region>(
                            new carta::Region(annotation_base, _image_shape, _spectral_axis, _stokes_axis, coord_sys));
                        if (region && region->IsValid()) {
                            // add to frame's regions
                            auto region_id = GetMaxRegionId() + 1;
                            _regions[region_id] = move(region);

                            // get region info parameters
                            std::string name(_regions[region_id]->Name());
                            CARTA::RegionType type(_regions[region_id]->Type());
                            std::vector<CARTA::Point> points = _regions[region_id]->GetControlPoints();
                            float rotation(_regions[region_id]->Rotation());

                            // add to ack
                            auto region_properties = import_ack.add_regions();
                            region_properties->set_region_id(region_id);
                            auto region_info = region_properties->mutable_region_info();
                            region_info->set_region_name(name);
                            region_info->set_region_type(type);
                            *region_info->mutable_control_points() = {points.begin(), points.end()};
                            region_info->set_rotation(rotation);
                        } else {
                            message += " Region " + region_type_str + " was not validated.";
                        }
                    }
                }
            }
            break;
        }
        case casa::AsciiAnnotationFileLine::GLOBAL:
        case casa::AsciiAnnotationFileLine::COMMENT:
        case casa::AsciiAnnotationFileLine::UNKNOWN_TYPE: {
            break;
        }
    }
}

casacore::String Frame::AnnTypeToDs9String(casa::AnnotationBase::Type annotation_type) {
    casacore::String ds9_type;
    switch (annotation_type) {
        case casa::AnnotationBase::LINE:
        case casa::AnnotationBase::CIRCLE:
        case casa::AnnotationBase::ELLIPSE:
        case casa::AnnotationBase::ANNULUS:
            ds9_type = casa::AnnotationBase::typeToString(annotation_type);
            break;
        case casa::AnnotationBase::TEXT:
            ds9_type = "text";
            break;
        case casa::AnnotationBase::SYMBOL:
            ds9_type = "point";
            break;
        case casa::AnnotationBase::RECT_BOX:
        case casa::AnnotationBase::CENTER_BOX:
        case casa::AnnotationBase::ROTATED_BOX:
            ds9_type = "box";
            break;
        case casa::AnnotationBase::POLYGON:
            ds9_type = "polygon";
            break;
        case casa::AnnotationBase::POLYLINE:
        case casa::AnnotationBase::VECTOR:
            break; // no equivalent
    }
    return ds9_type;
}

void Frame::ExportRegion(CARTA::FileType file_type, CARTA::CoordinateType coord_type, std::vector<int>& region_ids, std::string& filename,
    CARTA::ExportRegionAck& export_ack) {
    // Export regions to file with filename; if no filename, add contents to ack message for client-side export.
    // Check if regions to export
    if (region_ids.empty()) {
        export_ack.set_success(false);
        export_ack.set_message("Export failed: no regions requested.");
        export_ack.add_contents();
        return;
    }

    // Check export file before creating file contents
    if (!filename.empty()) {
        casacore::File export_file(filename);
        if (!export_file.canCreate()) {
            export_ack.set_success(false);
            export_ack.set_message("Export region failed: cannot create file.");
            export_ack.add_contents();
            return;
        }
    }

    casacore::CoordinateSystem coord_sys;
    if (!_loader->GetCoordinateSystem(coord_sys)) {
        export_ack.set_success(false);
        export_ack.set_message("Export region failed: image has no coordinate system.");
        export_ack.add_contents();
        return;
    }
    if (!coord_sys.hasDirectionCoordinate()) {
        export_ack.set_success(false);
        export_ack.set_message("Export region failed: image coordinate system has no direction coordinate.");
        export_ack.add_contents();
        return;
    }

    // export according to type
    switch (file_type) {
        case CARTA::FileType::CRTF:
            ExportCrtfRegions(region_ids, coord_type, coord_sys, filename, export_ack);
            break;
        case CARTA::FileType::REG:
            ExportDs9Regions(region_ids, coord_type, coord_sys, filename, export_ack);
            break;
        default: {
            export_ack.set_success(false);
            export_ack.set_message("Export region failed: file type not supported.");
            export_ack.add_contents();
        }
    }
}

void Frame::ExportCrtfRegions(std::vector<int>& region_ids, CARTA::CoordinateType coord_type, const casacore::CoordinateSystem& coord_sys,
    std::string& crtf_filename, CARTA::ExportRegionAck& export_ack) {
    // Create RegionTextList for all requested regions and export to file or put in ack contents[]
    std::string message;
    bool pixel_coord(coord_type == CARTA::CoordinateType::PIXEL);
    casa::RegionTextList region_list = casa::RegionTextList(coord_sys, _image_shape);

    // create file line for each region
    for (int region_id : region_ids) {
        if (_regions.count(region_id)) {
            auto& region = _regions[region_id];
            if (region->IsValid()) {
                try {
                    casacore::CountedPtr<const casa::AnnotationBase> annotation_region = region->AnnotationRegion(pixel_coord);
                    if (!annotation_region.null()) {
                        casa::AsciiAnnotationFileLine file_line = casa::AsciiAnnotationFileLine(annotation_region);
                        region_list.addLine(file_line);
                    }
                } catch (casacore::AipsError& err) {
                    std::ostringstream oss;
                    oss << " Region " << region_id << " export failed: ";
                    if (err.getMesg().contains("no direction coordinate")) {
                        oss << "image coordinate system has no direction coordinate.";
                    } else {
                        oss << err.getMesg();
                    }
                    message += oss.str();
                }
            } else {
                std::ostringstream oss;
                oss << " Region " << region_id << " export failed: region is not valid for this image.";
                message += oss.str();
            }
        } else {
            std::ostringstream oss;
            oss << " Region " << region_id << " export failed: no longer exists.";
            message += oss.str();
        }
    }

    // check if file lines created
    if (region_list.nLines() == 0) {
        export_ack.set_success(false);
        if (message.empty()) {
            message = "Export region failed: no regions to export.";
        }
        export_ack.set_message(message);
        export_ack.add_contents();
        return;
    }

    if (crtf_filename.empty()) {
        // fill contents[] of ack message
        std::vector<std::string> contents;
        for (unsigned int i = 0; i < region_list.nLines(); ++i) {
            casa::AsciiAnnotationFileLine file_line = region_list.lineAt(i);
            std::ostringstream export_stream;
            file_line.print(export_stream);
            contents.push_back(export_stream.str());
        }
        // complete ack message
        export_ack.set_success(true);
        export_ack.set_message(message);
        *export_ack.mutable_contents() = {contents.begin(), contents.end()};
    } else {
        // export to file; empty contents[] returned
        std::ofstream export_file(crtf_filename);
        region_list.print(export_file);
        export_file.close();

        // complete ack message
        export_ack.set_success(true);
        export_ack.set_message(message);
        export_ack.add_contents();
    }
}

void Frame::ExportDs9Regions(std::vector<int>& region_ids, CARTA::CoordinateType coord_type, const casacore::CoordinateSystem& coord_sys,
    std::string& ds9_filename, CARTA::ExportRegionAck& export_ack) {
    std::string message;
    bool pixel_coord(coord_type == CARTA::CoordinateType::PIXEL);
    carta::Ds9Parser parser(coord_sys, pixel_coord);

    // create file line for each region
    for (int region_id : region_ids) {
        if (_regions.count(region_id)) {
            auto& region = _regions[region_id];
            if (pixel_coord) {
                RegionState region_state = region->GetRegionState();
                std::vector<CARTA::Point> carta_points(region_state.control_points);
                std::vector<casacore::Quantity> points_quantities;
                for (auto& point : carta_points) {
                    points_quantities.push_back(casacore::Quantity(point.x(), "pix"));
                    points_quantities.push_back(casacore::Quantity(point.y(), "pix"));
                }
                parser.AddRegion(region_state.name, region_state.type, points_quantities, region_state.rotation);
            } else {
                std::string name(region->Name());
                CARTA::RegionType type(region->Type());
                std::vector<casacore::Quantity> control_points(region->GetControlPointsWcs());
                float rotation(region->Rotation());
                parser.AddRegion(name, type, control_points, rotation);
            }
        }
    }

    // check if file lines created
    if (parser.NumRegions() == 0) {
        export_ack.set_success(false);
        export_ack.set_message("Export region failed: no regions to export.");
        export_ack.add_contents();
        return;
    }

    if (ds9_filename.empty()) {
        // fill contents[] of ack message
        std::vector<std::string> contents;
        for (unsigned int i = 0; i < parser.NumRegions(); ++i) {
            std::ostringstream export_stream;
            parser.PrintRegion(i, export_stream);
            contents.push_back(export_stream.str());
        }
        // complete ack message
        export_ack.set_success(true);
        export_ack.set_message(message);
        *export_ack.mutable_contents() = {contents.begin(), contents.end()};
    } else {
        // export to file; empty contents[] returned
        std::ofstream export_file(ds9_filename);
        parser.PrintRegionsToFile(export_file);
        export_file.close();

        // complete ack message
        export_ack.set_success(true);
        export_ack.set_message(message);
        export_ack.add_contents();
    }
}

// ********************************************************************
// Image region parameters: view, channel/stokes, slicers

bool Frame::SetImageView(
    const CARTA::ImageBounds& image_bounds, int new_mip, CARTA::CompressionType compression, float quality, int num_subsets) {
    // set image bounds and compression settings
    if (!_valid) {
        return false;
    }
    const int x_min = image_bounds.x_min();
    const int x_max = image_bounds.x_max();
    const int y_min = image_bounds.y_min();
    const int y_max = image_bounds.y_max();
    const int req_height = y_max - y_min;
    const int req_width = x_max - x_min;

    // out of bounds check
    if ((req_height < 0) || (req_width < 0)) {
        return false;
    }
    if ((_image_shape(1) < y_min + req_height) || (_image_shape(0) < x_min + req_width)) {
        return false;
    }
    if (new_mip <= 0) {
        return false;
    }

    // changed check
    ViewSettings current_view_settings = GetViewSettings();
    CARTA::ImageBounds current_view_bounds = current_view_settings.image_bounds;
    if ((current_view_bounds.x_min() == x_min) && (current_view_bounds.x_max() == x_max) && (current_view_bounds.y_min() == y_min) &&
        (current_view_bounds.y_max() == y_max) && (current_view_settings.mip == new_mip) &&
        (current_view_settings.compression_type == compression) && (current_view_settings.quality == quality) &&
        (current_view_settings.num_subsets == num_subsets)) {
        return false;
    }

    SetViewSettings(image_bounds, new_mip, compression, quality, num_subsets);
    return true;
}

void Frame::SetViewSettings(
    const CARTA::ImageBounds& new_bounds, int new_mip, CARTA::CompressionType new_compression, float new_quality, int new_subsets) {
    // save new view settings in atomic operation
    ViewSettings settings;
    settings.image_bounds = new_bounds;
    settings.mip = new_mip;
    settings.compression_type = new_compression;
    settings.quality = new_quality;
    settings.num_subsets = new_subsets;
    _view_settings = settings;
}

bool Frame::SetImageChannels(int new_channel, int new_stokes, std::string& message) {
    bool updated(false);

    if (!_valid || (_regions.count(IMAGE_REGION_ID) == 0)) {
        message = "No file loaded";
    } else {
        if ((new_channel != _channel_index) || (new_stokes != _stokes_index)) {
            auto& region = _regions[IMAGE_REGION_ID];
            bool chan_ok(CheckChannel(new_channel));
            bool stokes_ok(CheckStokes(new_stokes));
            if (chan_ok && stokes_ok) {
                _channel_index = new_channel;
                _stokes_index = new_stokes;
                SetImageCache();
                updated = true;
                for (auto& region : _regions) {
                    // force sending new profiles for new chan/stokes
                    region.second->SetAllProfilesUnsent();
                }
            } else {
                message = fmt::format("Channel {} or Stokes {} is invalid in image", new_channel, new_stokes);
            }
        }
    }
    return updated;
}

void Frame::SetImageCache() {
    // get image data for channel, stokes
    bool write_lock(true);
    tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, write_lock);
    _image_cache.resize(_image_shape(0) * _image_shape(1));
    casacore::Slicer section = GetChannelMatrixSlicer(_channel_index, _stokes_index);
    casacore::Array<float> tmp(section.length(), _image_cache.data(), casacore::StorageInitPolicy::SHARE);
    std::lock_guard<std::mutex> guard(_image_mutex);
    _loader->GetSlice(tmp, section);
}

void Frame::GetChannelMatrix(std::vector<float>& chan_matrix, size_t channel, size_t stokes) {
    // fill matrix for given channel and stokes
    casacore::Slicer section = GetChannelMatrixSlicer(channel, stokes);
    chan_matrix.resize(_image_shape(0) * _image_shape(1));
    casacore::Array<float> tmp(section.length(), chan_matrix.data(), casacore::StorageInitPolicy::SHARE);
    // slice image data
    std::lock_guard<std::mutex> guard(_image_mutex);
    _loader->GetSlice(tmp, section);
}

casacore::Slicer Frame::GetChannelMatrixSlicer(size_t channel, size_t stokes) {
    // slicer for spectral and stokes axes to select channel, stokes
    casacore::IPosition count(_image_shape);
    casacore::IPosition start(_image_shape.size());
    start = 0;

    if (_spectral_axis >= 0) {
        start(_spectral_axis) = channel;
        count(_spectral_axis) = 1;
    }
    if (_stokes_axis >= 0) {
        start(_stokes_axis) = stokes;
        count(_stokes_axis) = 1;
    }
    // slicer for image data
    casacore::Slicer section(start, count);
    return section;
}

void Frame::GetImageSlicer(casacore::Slicer& image_slicer, int x, int y, int channel, int stokes) {
    // to slice image data along axes (full axis indicated with -1)
    // Start with entire image:
    casacore::IPosition count(_image_shape);
    casacore::IPosition start(_image_shape.size());
    start = 0;

    if (x >= 0) {
        start(0) = x;
        count(0) = 1;
    }

    if (y >= 0) {
        start(1) = y;
        count(1) = 1;
    }

    if ((channel >= 0) && (_spectral_axis >= 0)) {
        start(_spectral_axis) = channel;
        count(_spectral_axis) = 1;
    }

    if ((stokes >= 0) && (_stokes_axis >= 0)) {
        start(_stokes_axis) = stokes;
        count(_stokes_axis) = 1;
    }

    casacore::Slicer section(start, count);
    image_slicer = section;
}

bool Frame::GetRegionSubImage(int region_id, casacore::SubImage<float>& sub_image, int stokes, ChannelRange channel_range) {
    // Apply ImageRegion to image and return SubImage.
    // channel could be ALL_CHANNELS in region channel range (default) or
    //     a given channel (e.g. current channel).
    // Returns false if image region is invalid and cannot make subimage.
    bool sub_image_ok(false);
    if (CheckStokes(stokes) && (_regions.count(region_id))) {
        auto& region = _regions[region_id];
        if (region->IsValid()) {
            casacore::ImageRegion image_region;
            if (region->GetRegion(image_region, stokes, channel_range)) {
                try {
                    casacore::ImageInterface<float>* image = _loader->GetImage();
                    if (image) {
                        sub_image = casacore::SubImage<float>(*image, image_region);
                        sub_image_ok = true;
                    }
                } catch (casacore::AipsError& err) {
                    Log(_session_id, "Region creation for {} failed: {}", region->Name(), err.getMesg());
                }
            }
        }
    }
    return sub_image_ok;
}

// ****************************************************
// Region requirements

bool Frame::SetRegionHistogramRequirements(int region_id, const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histograms) {
    // set channel and num_bins for required histograms
    bool region_ok(false);
    if ((region_id == CUBE_REGION_ID) && (!_regions.count(region_id))) {
        SetImageRegion(CUBE_REGION_ID);
    } // create this region
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        region_ok = region->SetHistogramRequirements(histograms);
    }
    return region_ok;
}

bool Frame::SetRegionSpatialRequirements(int region_id, const std::vector<std::string>& profiles) {
    // set requested spatial profiles e.g. ["Qx", "Uy"] or just ["x","y"] to use current stokes
    bool region_ok(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        region_ok = region->SetSpatialRequirements(profiles, NumStokes());
    }
    return region_ok;
}

bool Frame::SetRegionSpectralRequirements(int region_id, const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles) {
    // set requested spectral profiles e.g. ["Qz", "Uz"] or just ["z"] to use current stokes
    bool region_ok(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        region_ok = region->SetSpectralRequirements(profiles, NumStokes());
    }
    return region_ok;
}

bool Frame::SetRegionStatsRequirements(int region_id, const std::vector<int>& stats_types) {
    bool region_ok(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        region->SetStatsRequirements(stats_types);
        region_ok = true;
    }
    return region_ok;
}

// ****************************************************
// Data for Image region

bool Frame::FillRasterImageData(CARTA::RasterImageData& raster_image_data, std::string& message) {
    // fill data message with compressed channel cache data
    bool raster_data_ok(false);
    // retrieve settings
    ViewSettings view_settings = GetViewSettings();
    // get downsampled raster data for message
    std::vector<float> image_data;
    CARTA::ImageBounds bounds_setting(view_settings.image_bounds);
    int mip_setting(view_settings.mip);
    if (GetRasterData(image_data, bounds_setting, mip_setting)) {
        // set common message fields
        raster_image_data.mutable_image_bounds()->set_x_min(bounds_setting.x_min());
        raster_image_data.mutable_image_bounds()->set_x_max(bounds_setting.x_max());
        raster_image_data.mutable_image_bounds()->set_y_min(bounds_setting.y_min());
        raster_image_data.mutable_image_bounds()->set_y_max(bounds_setting.y_max());
        raster_image_data.set_channel(_channel_index);
        raster_image_data.set_stokes(_stokes_index);
        raster_image_data.set_mip(mip_setting);
        CARTA::CompressionType compression_setting = view_settings.compression_type;
        raster_image_data.set_compression_type(compression_setting);

        // add data
        if (compression_setting == CARTA::CompressionType::NONE) {
            raster_image_data.set_compression_quality(0);
            raster_image_data.add_image_data(image_data.data(), image_data.size() * sizeof(float));
            raster_data_ok = true;
        } else if (compression_setting == CARTA::CompressionType::ZFP) {
            // compression settings
            float quality_setting(view_settings.quality);
            int num_subsets_setting(view_settings.num_subsets);

            int precision = lround(quality_setting);
            raster_image_data.set_compression_quality(precision);

            auto row_length = std::ceil((float)(bounds_setting.x_max() - bounds_setting.x_min()) / mip_setting);
            auto num_rows = std::ceil((float)(bounds_setting.y_max() - bounds_setting.y_min()) / mip_setting);
            std::vector<std::vector<char>> compression_buffers(num_subsets_setting);
            std::vector<size_t> compressed_sizes(num_subsets_setting);
            std::vector<std::vector<int32_t>> nan_encodings(num_subsets_setting);

            auto num_subsets = std::min(num_subsets_setting, MAX_SUBSETS);

#pragma omp parallel for
            for (int i = 0; i < num_subsets; ++i) {
                int subset_row_start = i * (num_rows / num_subsets);
                int subset_row_end = (i + 1) * (num_rows / num_subsets);
                if (i == num_subsets - 1) {
                    subset_row_end = num_rows;
                }
                int subset_element_start = subset_row_start * row_length;
                int subset_element_end = subset_row_end * row_length;
                nan_encodings[i] = GetNanEncodingsBlock(image_data, subset_element_start, row_length, subset_row_end - subset_row_start);
                Compress(image_data, subset_element_start, compression_buffers[i], compressed_sizes[i], row_length,
                    subset_row_end - subset_row_start, precision);
            }

            // Complete message
            for (auto i = 0; i < num_subsets_setting; i++) {
                raster_image_data.add_image_data(compression_buffers[i].data(), compressed_sizes[i]);
                raster_image_data.add_nan_encodings((char*)nan_encodings[i].data(), nan_encodings[i].size() * sizeof(int));
            }
            raster_data_ok = true;
        } else {
            message = "SZ compression not implemented";
        }
    } else {
        message = "Raster image data failed to load";
    }
    return raster_data_ok;
}

bool Frame::GetRasterData(std::vector<float>& image_data, CARTA::ImageBounds& bounds, int mip, bool mean_filter) {
    // apply bounds and downsample image cache
    if (!_valid || _image_cache.empty()) {
        return false;
    }

    const int x = bounds.x_min();
    const int y = bounds.y_min();
    const int req_height = bounds.y_max() - y;
    const int req_width = bounds.x_max() - x;

    // check bounds
    if ((req_height < 0) || (req_width < 0)) {
        return false;
    }
    if (_image_shape(1) < y + req_height || _image_shape(0) < x + req_width) {
        return false;
    }
    // check mip; cannot divide by zero
    if (mip <= 0) {
        return false;
    }

    // size returned vector
    size_t num_rows_region = std::ceil((float)req_height / mip);
    size_t row_length_region = std::ceil((float)req_width / mip);
    image_data.resize(num_rows_region * row_length_region);
    int num_image_columns = _image_shape(0);

    // read lock imageCache
    bool write_lock(false);
    tbb::queuing_rw_mutex::scoped_lock lock(_cache_mutex, write_lock);

    if (mean_filter && mip > 1) {
        // Perform down-sampling by calculating the mean for each MIPxMIP block
#pragma omp parallel for
        for (size_t j = 0; j < num_rows_region; ++j) {
            for (size_t i = 0; i != row_length_region; ++i) {
                float pixel_sum = 0;
                int pixel_count = 0;
                size_t image_row = y + (j * mip);
                for (size_t pixel_y = 0; pixel_y < mip; pixel_y++) {
                    if (image_row >= _image_shape(1)) {
                        continue;
                    }
                    size_t image_col = x + (i * mip);
                    for (size_t pixel_x = 0; pixel_x < mip; pixel_x++) {
                        if (image_col >= _image_shape(0)) {
                            continue;
                        }
                        float pix_val = _image_cache[(image_row * num_image_columns) + image_col];
                        if (std::isfinite(pix_val)) {
                            pixel_count++;
                            pixel_sum += pix_val;
                        }
                        image_col++;
                    }
                    image_row++;
                }
                image_data[j * row_length_region + i] = pixel_count ? pixel_sum / pixel_count : NAN;
            }
        }

    } else {
        // Nearest neighbour filtering
#pragma omp parallel for
        for (size_t j = 0; j < num_rows_region; ++j) {
            for (auto i = 0; i < row_length_region; i++) {
                auto image_row = y + j * mip;
                auto image_col = x + i * mip;
                image_data[j * row_length_region + i] = _image_cache[(image_row * num_image_columns) + image_col];
            }
        }
    }
    return true;
}

// Tile data
bool Frame::FillRasterTileData(CARTA::RasterTileData& raster_tile_data, const Tile& tile, int channel, int stokes,
    CARTA::CompressionType compression_type, float compression_quality) {
    // Early exit if channel has changed
    if (ChannelsChanged(channel, stokes)) {
        return false;
    }
    raster_tile_data.set_channel(channel);
    raster_tile_data.set_stokes(stokes);
    raster_tile_data.set_compression_type(compression_type);
    raster_tile_data.set_compression_quality(compression_quality);

    if (raster_tile_data.tiles_size()) {
        raster_tile_data.clear_tiles();
    }

    CARTA::TileData* tile_ptr = raster_tile_data.add_tiles();
    tile_ptr->set_layer(tile.layer);
    tile_ptr->set_x(tile.x);
    tile_ptr->set_y(tile.y);

    std::vector<float> tile_image_data;
    int tile_width;
    int tile_height;
    if (GetRasterTileData(tile_image_data, tile, tile_width, tile_height)) {
        if (ChannelsChanged(channel, stokes)) {
            return false;
        }
        tile_ptr->set_width(tile_width);
        tile_ptr->set_height(tile_height);
        if (compression_type == CARTA::CompressionType::NONE) {
            tile_ptr->set_image_data(tile_image_data.data(), sizeof(float) * tile_image_data.size());
            return true;
        } else if (compression_type == CARTA::CompressionType::ZFP) {
            auto nan_encodings = GetNanEncodingsBlock(tile_image_data, 0, tile_width, tile_height);
            tile_ptr->set_nan_encodings(nan_encodings.data(), sizeof(int32_t) * nan_encodings.size());

            if (ChannelsChanged(channel, stokes)) {
                return false;
            }

            std::vector<char> compression_buffer;
            size_t compressed_size;
            int precision = lround(compression_quality);
            Compress(tile_image_data, 0, compression_buffer, compressed_size, tile_width, tile_height, precision);
            tile_ptr->set_image_data(compression_buffer.data(), compressed_size);

            return !(ChannelsChanged(channel, stokes));
        }
    }
    return false;
}

bool Frame::GetRasterTileData(std::vector<float>& tile_data, const Tile& tile, int& width, int& height) {
    int tile_size = 256;
    int mip = Tile::LayerToMip(tile.layer, _image_shape(0), _image_shape(1), tile_size, tile_size);
    int tile_size_original = tile_size * mip;
    CARTA::ImageBounds bounds;
    // crop to image size
    bounds.set_x_min(std::max(0, tile.x * tile_size_original));
    bounds.set_x_max(std::min((int)_image_shape(0), (tile.x + 1) * tile_size_original));
    bounds.set_y_min(std::max(0, tile.y * tile_size_original));
    bounds.set_y_max(std::min((int)_image_shape(1), (tile.y + 1) * tile_size_original));

    const int req_height = bounds.y_max() - bounds.y_min();
    const int req_width = bounds.x_max() - bounds.x_min();
    width = std::ceil((float)req_width / mip);
    height = std::ceil((float)req_height / mip);
    return GetRasterData(tile_data, bounds, mip, true);
}

// ****************************************************
// Region histograms, profiles, stats

bool Frame::FillRegionHistogramData(int region_id, CARTA::RegionHistogramData* histogram_data, bool channel_changed) {
    // Fill histogram message with histograms for requested channel/num bins.
    // Do not send cube histogram if channel changed.
    bool histogram_ok(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        size_t num_histograms(region->NumHistogramConfigs());
        if (num_histograms == 0) {
            return false;
        } // not requested

        int curr_stokes(CurrentStokes());
        histogram_data->set_stokes(curr_stokes);
        histogram_data->set_progress(1.0); // send entire histogram
        for (size_t i = 0; i < num_histograms; ++i) {
            // get histogram requirements for this index
            CARTA::SetHistogramRequirements_HistogramConfig config = region->GetHistogramConfig(i);
            int config_channel(config.channel()), config_num_bins(config.num_bins());

            if ((config_channel == ALL_CHANNELS) && channel_changed) {
                continue; // do not send
            }
            if (config_channel == CURRENT_CHANNEL) {
                config_channel = _channel_index;
            }

            auto new_histogram = histogram_data->add_histograms();
            new_histogram->set_channel(config_channel);
            // get stored histograms or fill new histograms

            bool have_histogram(false);
            // Check if read from image file (HDF5 only)
            if (region_id == IMAGE_REGION_ID || region_id == CUBE_REGION_ID) {
                have_histogram = GetImageHistogram(config_channel, curr_stokes, config_num_bins, *new_histogram);
            }

            if (!have_histogram) {
                // Retrieve histogram if stored
                int num_bins = (config_num_bins == AUTO_BIN_SIZE ? CalcAutoNumBins(region_id) : config_num_bins);
                if (!GetRegionHistogram(region_id, config_channel, curr_stokes, num_bins, *new_histogram)) {
                    // Calculate histogram
                    BasicStats<float> stats;
                    if (region_id == IMAGE_REGION_ID) {
                        if (config_channel == _channel_index) { // use imageCache
                            if (!GetRegionBasicStats(region_id, config_channel, curr_stokes, stats)) {
                                CalcRegionBasicStats(region_id, config_channel, curr_stokes, stats);
                            }
                            CalcRegionHistogram(region_id, config_channel, curr_stokes, num_bins, stats, *new_histogram);
                        } else { // use matrix slicer on image
                            std::vector<float> data;
                            GetChannelMatrix(data, config_channel, curr_stokes); // slice image once
                            if (!GetRegionBasicStats(region_id, config_channel, curr_stokes, stats)) {
                                region->CalcBasicStats(config_channel, curr_stokes, data, stats);
                            }
                            region->CalcHistogram(config_channel, curr_stokes, num_bins, stats, data, *new_histogram);
                        }
                    } else {
                        casacore::SubImage<float> sub_image;
                        std::unique_lock<std::mutex> ulock(_image_mutex);
                        bool has_subimage = GetRegionSubImage(region_id, sub_image, curr_stokes, ChannelRange(config_channel));
                        ulock.unlock();
                        if (has_subimage) {
                            std::vector<float> region_data;
                            std::unique_lock<std::mutex> ulock2(_image_mutex);
                            bool has_region_data = region->GetData(region_data, sub_image); // get subimage data once
                            ulock2.unlock();
                            if (has_region_data) {
                                if (!GetRegionBasicStats(region_id, config_channel, curr_stokes, stats)) {
                                    region->CalcBasicStats(config_channel, curr_stokes, region_data, stats);
                                }
                                region->CalcHistogram(config_channel, curr_stokes, num_bins, stats, region_data, *new_histogram);
                            }
                        }
                    }
                }
            }
        }
        histogram_ok = (histogram_data->histograms_size() > 0); // do not send if no histograms
    }
    return histogram_ok;
}

bool Frame::FillSpatialProfileData(int region_id, CARTA::SpatialProfileData& profile_data, bool stokes_changed) {
    // Fill spatial profile message with requested x/y profiles (for a point region).
    // Do not send spatial profile for fixed stokes when stokes changed.
    bool profile_ok(false);
    if ((region_id == CURSOR_REGION_ID) && !IsCursorSet()) {
        return profile_ok; // no profile if frontend has not set cursor
    }

    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        if (!region->IsValid() || !region->IsPoint()) {
            return profile_ok;
        }

        // set spatial profile fields
        std::vector<CARTA::Point> control_points = region->GetControlPoints();
        int x(static_cast<int>(std::round(control_points[0].x()))), y(static_cast<int>(std::round(control_points[0].y())));
        // check that control points in image
        bool point_in_image((x >= 0) && (x < _image_shape(0)) && (y >= 0) && (y < _image_shape(1)));
        ssize_t num_image_cols(_image_shape(0)), num_image_rows(_image_shape(1));
        float value(0.0);
        if (!_image_cache.empty()) {
            bool write_lock(false);
            tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, write_lock);
            value = _image_cache[(y * num_image_cols) + x];
            cache_lock.release();
        }
        profile_data.set_x(x);
        profile_data.set_y(y);
        profile_data.set_channel(_channel_index);
        profile_data.set_stokes(_stokes_index);
        profile_data.set_value(value);

        if (point_in_image) {
            // set profiles
            size_t nprofiles(region->NumSpatialProfiles());
            for (size_t i = 0; i < nprofiles; ++i) {
                if (!region->GetSpatialProfileSent(i)) {
                    // get <axis, stokes> for slicing image data
                    std::pair<int, int> axis_stokes = region->GetSpatialProfileAxes(i);
                    if (axis_stokes.first < 0) { // invalid index
                        return profile_ok;
                    }

                    if (stokes_changed && (axis_stokes.second != CURRENT_STOKES)) {
                        // Do not send fixed stokes profile when stokes changes.
                        // When chan/stokes changes, all messages are set to unsent to force new profiles;
                        // put fixed stokes profile back to sent
                        region->SetSpatialProfileSent(i, true);
                        continue;
                    }
                    int profile_stokes = (axis_stokes.second < 0 ? _stokes_index : axis_stokes.second);

                    std::vector<float> profile;
                    int end(0);
                    if ((profile_stokes == _stokes_index) && !_image_cache.empty()) {
                        // use stored channel cache
                        bool write_lock(false);
                        switch (axis_stokes.first) {
                            case 0: { // x
                                tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, write_lock);
                                auto x_start = y * num_image_cols;
                                profile.reserve(_image_shape(0));
                                for (unsigned int j = 0; j < _image_shape(0); ++j) {
                                    auto idx = x_start + j;
                                    profile.push_back(_image_cache[idx]);
                                }
                                cache_lock.release();
                                end = _image_shape(0);
                                break;
                            }
                            case 1: { // y
                                tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, write_lock);
                                profile.reserve(_image_shape(1));
                                for (unsigned int j = 0; j < _image_shape(1); ++j) {
                                    auto idx = (j * num_image_cols) + x;
                                    profile.push_back(_image_cache[idx]);
                                }
                                cache_lock.release();
                                end = _image_shape(1);
                                break;
                            }
                        }
                    } else {
                        // slice image data
                        casacore::Slicer section;
                        switch (axis_stokes.first) {
                            case 0: { // x
                                GetImageSlicer(section, -1, y, _channel_index, profile_stokes);
                                end = _image_shape(0);
                                break;
                            }
                            case 1: { // y
                                GetImageSlicer(section, x, -1, _channel_index, profile_stokes);
                                end = _image_shape(1);
                                break;
                            }
                        }
                        profile.resize(end);
                        casacore::Array<float> tmp(section.length(), profile.data(), casacore::StorageInitPolicy::SHARE);
                        std::lock_guard<std::mutex> guard(_image_mutex);
                        _loader->GetSlice(tmp, section);
                    }
                    // SpatialProfile
                    auto new_profile = profile_data.add_profiles();
                    new_profile->set_coordinate(region->GetSpatialCoordinate(i));
                    new_profile->set_start(0);
                    new_profile->set_end(end);
                    new_profile->set_raw_values_fp32(profile.data(), profile.size() * sizeof(float));
                    region->SetSpatialProfileSent(i, true);
                }
            }
            // send if no profiles requested (for cursor value), but not if requested profiles do not need to be sent
            if ((nprofiles > 0) && (profile_data.profiles_size() == 0)) {
                profile_ok = false;
            } else {
                profile_ok = true;
            }
        }
    }
    return profile_ok;
}

bool Frame::FillSpectralProfileData(
    std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, bool channel_changed, bool stokes_changed) {
    // fill spectral profile message with requested statistics (or values for a point region)
    bool profile_ok(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        if (!region->IsValid()) {
            return false;
        }
        size_t num_profiles(region->NumSpectralProfiles());
        if (num_profiles == 0) {
            return false; // not requested
        }
        // set profile parameters
        int curr_stokes(CurrentStokes());
        std::vector<SpectralProfile> profiles = region->GetSpectralProfiles();
        for (auto& profile : profiles) {
            SpectralConfig spectral_config = profile.config;
            int config_stokes(spectral_config.stokes_index);
            if (region->GetSpectralProfileAllStatsSent(config_stokes)) {
                continue; // already loaded or no longer needed
            }
            if (config_stokes >= CURRENT_STOKES) {
                // When chan/stokes changes, all messages are set to unsent to force new profiles;
                // put fixed stokes profile back to sent for the following:
                if (channel_changed && !stokes_changed) {
                    region->SetSpectralProfileAllStatsSent(config_stokes, true);
                    continue; // do not send spectral profile when only channel changes
                }
                if ((channel_changed || stokes_changed) && (config_stokes != CURRENT_STOKES)) {
                    // Do not send fixed stokes profile when stokes changes.
                    region->SetSpectralProfileAllStatsSent(config_stokes, true);
                    continue;
                }

                // Return NaNs if the region is entirely outside the image
                std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> mask;
                std::unique_lock<std::mutex> guard(_image_mutex);
                try {
                    // check is the region mask valid (outside the lattice or not)
                    mask = region->XyMask();
                } catch (casacore::AipsError& err) {
                }
                guard.unlock();
                if (!mask) {
                    // if region mask not valid, send a NaN to the frontend
                    CARTA::SpectralProfileData profile_message;
                    profile_message.set_stokes(curr_stokes);
                    profile_message.set_progress(1.0);
                    region->FillNaNSpectralProfileDataMessage(profile_message, config_stokes);
                    // send empty (NaN) result to Session
                    cb(profile_message);
                    profile_ok = true;
                    return profile_ok;
                }

                // config_stokes is the stokes index in config, which could be CURRENT_STOKES = -1
                // profile_stokes is a valid stokes index used to slice image
                int profile_stokes = (config_stokes == CURRENT_STOKES ? curr_stokes : config_stokes);

                // fill SpectralProfiles for this config
                if (region->IsPoint()) {
                    // profile is values in image slice
                    std::vector<float> spectral_data;
                    auto cursor_point = region->GetControlPoints()[0];
                    // try use the loader's optimized cursor profile reader first
                    bool have_spectral_data = _loader->GetCursorSpectralData(
                        spectral_data, profile_stokes, cursor_point.x() + 0.5, 1, cursor_point.y() + 0.5, 1, _image_mutex);
                    if (have_spectral_data) {
                        CARTA::SpectralProfileData profile_message;
                        profile_message.set_stokes(curr_stokes);
                        profile_message.set_progress(1.0);
                        region->FillPointSpectralProfileDataMessage(profile_message, config_stokes, spectral_data);
                        // send result to Session
                        cb(profile_message);
                        profile_ok = true;
                    } else {
                        casacore::SubImage<float> sub_image;
                        std::unique_lock<std::mutex> guard(_image_mutex);
                        bool has_subimage = GetRegionSubImage(region_id, sub_image, profile_stokes, ChannelRange());
                        guard.unlock();
                        if (has_subimage) {
                            profile_ok =
                                GetPointSpectralData(region_id, sub_image, [&](std::vector<float> tmp_spectral_data, float progress) {
                                    CARTA::SpectralProfileData profile_message;
                                    profile_message.set_stokes(curr_stokes);
                                    profile_message.set_progress(progress);
                                    region->FillPointSpectralProfileDataMessage(profile_message, config_stokes, tmp_spectral_data);
                                    // send (partial) result to Session
                                    cb(profile_message);
                                });
                        }
                    }
                } else {
                    // profile is statistics for image region (SubImage)
                    if (_image_shape.size() < 3) { // no spectral axis
                        // send empty (NaN) result
                        CARTA::SpectralProfileData profile_message;
                        profile_message.set_stokes(curr_stokes);
                        profile_message.set_progress(1.0);
                        region->FillNaNSpectralProfileDataMessage(profile_message, config_stokes);
                        cb(profile_message);
                        profile_ok = true;
                        return profile_ok;
                    }

                    // if region mask is valid, then check is swizzled data available
                    if (_loader->UseRegionSpectralData(mask, _image_mutex)) {
                        profile_ok = _loader->GetRegionSpectralData(region_id, config_stokes, profile_stokes, mask, region->XyOrigin(),
                            _image_mutex, [&](std::map<CARTA::StatsType, std::vector<double>>* stats_values_map, float progress) {
                                CARTA::SpectralProfileData profile_message;
                                profile_message.set_stokes(curr_stokes);
                                profile_message.set_progress(progress);
                                region->FillSpectralProfileDataMessage(profile_message, config_stokes, *stats_values_map);
                                // send (partial) result to Session
                                cb(profile_message);
                            });
                    } else {
                        profile_ok = GetRegionSpectralData(region_id, config_stokes, profile_stokes,
                            [&](std::map<CARTA::StatsType, std::vector<double>> results, float progress) {
                                CARTA::SpectralProfileData profile_message;
                                profile_message.set_stokes(curr_stokes);
                                profile_message.set_progress(progress);
                                region->FillSpectralProfileDataMessage(profile_message, config_stokes, results);
                                // send (partial) result to Session
                                cb(profile_message);
                            });
                    }
                }
            }
        }
    }
    return profile_ok;
}

bool Frame::FillRegionStatsData(int region_id, CARTA::RegionStatsData& stats_data) {
    // fill stats data message with requested statistics for the region with current channel and stokes
    bool stats_ok(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        if (!region->IsValid()) {
            return false;
        }
        if (region->NumStats() == 0) {
            return false;
        } // not requested

        // If we're using the whole image, try to use loader image stats
        if (region_id == IMAGE_REGION_ID || region_id == CUBE_REGION_ID) {
            int stats_channel = (region_id == CUBE_REGION_ID) ? ALL_CHANNELS : _channel_index;
            auto& image_stats = _loader->GetImageStats(_stokes_index, stats_channel);
            if (image_stats.full) {
                stats_data.set_channel(stats_channel);
                stats_data.set_stokes(_stokes_index);
                region->FillStatsData(stats_data, image_stats.basic_stats);
                stats_ok = true;
            }
        }

        if (!stats_ok) {
            stats_data.set_channel(_channel_index);
            stats_data.set_stokes(_stokes_index);
            casacore::SubImage<float> sub_image;
            std::unique_lock<std::mutex> ulock(_image_mutex);
            bool have_subimage = GetRegionSubImage(region_id, sub_image, _stokes_index, ChannelRange(_channel_index));
            ulock.unlock();
            if (have_subimage) {
                std::lock_guard<std::mutex> guard(_image_mutex);
                region->FillStatsData(stats_data, sub_image, _channel_index, _stokes_index);
            } else {
                region->FillNaNStatsData(stats_data);
            }
            stats_ok = true;
        }
    }
    return stats_ok;
}

// ****************************************************
// Region histograms only (not full data message)

int Frame::CalcAutoNumBins(int region_id) {
    // automatic bin size for histogram when num_bins == AUTO_BIN_SIZE
    int auto_num_bins = int(std::max(sqrt(_image_shape(0) * _image_shape(1)), 2.0)); // default: use image plane
    if ((region_id != IMAGE_REGION_ID) && (region_id != CUBE_REGION_ID)) {
        if (_regions.count(region_id)) {
            auto& region = _regions[region_id];
            casacore::IPosition region_shape(region->XyShape()); // bounding box
            if (region_shape.size() > 0) {
                auto_num_bins = (int(std::max(sqrt(region_shape(0) * region_shape(1)), 2.0)));
            }
        }
    }
    return auto_num_bins;
}

bool Frame::GetRegionBasicStats(int region_id, int channel, int stokes, BasicStats<float>& stats) {
    // Return stored min and max value; false if not stored
    bool have_stats(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        have_stats = region->GetBasicStats(channel, stokes, stats);
    }
    return have_stats;
}

bool Frame::CalcRegionBasicStats(int region_id, int channel, int stokes, BasicStats<float>& stats) {
    // Calculate min/max for region data; primarily for cube histogram
    bool stats_ok(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        if (region_id == IMAGE_REGION_ID) {
            if (channel == _channel_index) { // use channel cache
                bool write_lock(false);
                tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, write_lock);
                region->CalcBasicStats(channel, stokes, _image_cache, stats);
            } else {
                std::vector<float> data;
                GetChannelMatrix(data, channel, stokes);
                region->CalcBasicStats(channel, stokes, data, stats);
            }
            stats_ok = true;
        } else {
            casacore::SubImage<float> sub_image;
            std::unique_lock<std::mutex> ulock(_image_mutex);
            bool has_subimage = GetRegionSubImage(region_id, sub_image, stokes, ChannelRange(channel));
            ulock.unlock();
            bool has_data(false);
            if (has_subimage) {
                std::vector<float> region_data;
                std::unique_lock<std::mutex> ulock2(_image_mutex);
                has_data = region->GetData(region_data, sub_image);
                ulock2.unlock();
                if (has_data) {
                    region->CalcBasicStats(channel, stokes, region_data, stats);
                }
            }
            stats_ok = has_data;
        }
    }
    return stats_ok;
}

bool Frame::GetImageHistogram(int channel, int stokes, int num_bins, CARTA::Histogram& histogram) {
    // Return image histogram in histogram parameter
    bool have_histogram(false);

    auto& current_stats = _loader->GetImageStats(stokes, channel);

    if (current_stats.valid) {
        int image_num_bins(current_stats.histogram_bins.size());

        if ((num_bins == AUTO_BIN_SIZE) || (num_bins == image_num_bins)) {
            double min_val(current_stats.basic_stats[CARTA::StatsType::Min]);
            double max_val(current_stats.basic_stats[CARTA::StatsType::Max]);
            double mean(current_stats.basic_stats[CARTA::StatsType::Mean]);
            double std_dev(current_stats.basic_stats[CARTA::StatsType::Sigma]);

            histogram.set_num_bins(image_num_bins);
            histogram.set_bin_width((max_val - min_val) / image_num_bins);
            histogram.set_first_bin_center(min_val + (histogram.bin_width() / 2.0));
            *histogram.mutable_bins() = {current_stats.histogram_bins.begin(), current_stats.histogram_bins.end()};
            histogram.set_mean(mean);
            histogram.set_std_dev(std_dev);
            have_histogram = true;
        }
    }

    return have_histogram;
}

bool Frame::GetRegionHistogram(int region_id, int channel, int stokes, int num_bins, CARTA::Histogram& histogram) {
    // Return stored histogram in histogram parameter
    bool have_histogram(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        num_bins = (num_bins == AUTO_BIN_SIZE ? CalcAutoNumBins(region_id) : num_bins);
        have_histogram = region->GetHistogram(channel, stokes, num_bins, histogram);
    }
    return have_histogram;
}

bool Frame::CalcRegionHistogram(
    int region_id, int channel, int stokes, int num_bins, const BasicStats<float>& stats, CARTA::Histogram& histogram) {
    // Return calculated histogram in histogram parameter; primarily for cube histogram
    bool histogram_ok(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        num_bins = (num_bins == AUTO_BIN_SIZE ? CalcAutoNumBins(region_id) : num_bins);
        if (region_id == IMAGE_REGION_ID) {
            if (channel == _channel_index) { // use channel cache
                bool write_lock(false);
                tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, write_lock);
                region->CalcHistogram(channel, stokes, num_bins, stats, _image_cache, histogram);
            } else {
                std::vector<float> data;
                GetChannelMatrix(data, channel, stokes);
                region->CalcHistogram(channel, stokes, num_bins, stats, data, histogram);
            }
            histogram_ok = true;
        } else {
            bool has_data(false);
            casacore::SubImage<float> sub_image;
            std::unique_lock<std::mutex> ulock(_image_mutex);
            bool has_subimage = GetRegionSubImage(region_id, sub_image, stokes, ChannelRange(channel));
            ulock.unlock();
            if (has_subimage) {
                std::vector<float> region_data;
                std::unique_lock<std::mutex> ulock2(_image_mutex);
                has_data = region->GetData(region_data, sub_image);
                ulock2.unlock();
                if (has_data) {
                    region->CalcHistogram(channel, stokes, num_bins, stats, region_data, histogram);
                }
            }
            histogram_ok = has_data;
        }
    }
    return histogram_ok;
}

// store cube histogram calculations
void Frame::SetRegionBasicStats(int region_id, int channel, int stokes, const BasicStats<float>& stats) {
    // Store cube min/max calculated in Session
    if (!_regions.count(region_id) && (region_id == CUBE_REGION_ID)) {
        SetImageRegion(CUBE_REGION_ID);
    }

    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        region->SetBasicStats(channel, stokes, stats);
    }
}

void Frame::SetRegionHistogram(int region_id, int channel, int stokes, CARTA::Histogram& histogram) {
    // Store cube histogram calculated in Session
    if (!_regions.count(region_id) && (region_id == CUBE_REGION_ID)) {
        SetImageRegion(CUBE_REGION_ID);
    }

    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        region->SetHistogram(channel, stokes, histogram);
    }
}

bool Frame::GetSubImageXy(casacore::SubImage<float>& sub_image, CursorXy& cursor_xy) {
    bool result(false);
    casacore::IPosition subimage_shape = sub_image.shape();
    casacore::IPosition start(subimage_shape.size(), 0);
    casacore::IPosition count(subimage_shape);
    if (count(0) == 1 && count(1) == 1) { // make sure the subimage is a point region in x-y plane
        casacore::IPosition parent_position = sub_image.getRegionPtr()->convert(start);
        cursor_xy = CursorXy(parent_position(0), parent_position(1));
        result = true;
    }
    return result;
}

bool Frame::GetPointSpectralData(
    int region_id, casacore::SubImage<float>& sub_image, const std::function<void(std::vector<float>, float)>& partial_results_callback) {
    // slice image data for point region (including cursor)
    bool data_ok(false);
    casacore::IPosition sub_image_shape = sub_image.shape();
    std::vector<float> data;
    data.resize(sub_image_shape.product(), std::numeric_limits<double>::quiet_NaN());

    try {
        if ((sub_image_shape.size() > 2) && (_spectral_axis >= 0)) {
            // stoppable spectral profile process
            size_t delta_channels = INIT_DELTA_CHANNEL; // the increment of channels for each step
            size_t dt_target = TARGET_DELTA_TIME;       // the target time elapse for each step, in the unit of milliseconds
            size_t profile_size = NumChannels();        // profile vector size
            casacore::IPosition start(sub_image_shape.size(), 0);
            casacore::IPosition count(sub_image_shape);
            float progress = 0.0;
            // get cursor's x-y coordinate from subimage
            CursorXy subimage_cursor;
            if (GetSubImageXy(sub_image, subimage_cursor)) {
                // get spectral profile for the cursor
                auto t_partial_profile_start = std::chrono::high_resolution_clock::now();
                while (start(_spectral_axis) < profile_size) {
                    // start the timer
                    auto t_start = std::chrono::high_resolution_clock::now();

                    // check if cursor/region point changed from subimage point
                    if ((region_id == CURSOR_REGION_ID) && (Interrupt(region_id, _cursor_xy, subimage_cursor))) {
                        return false; // cursor moved
                    }
                    if (region_id > CURSOR_REGION_ID) {
                        if (_regions.count(region_id)) {
                            std::vector<CARTA::Point> region_points = _regions[region_id]->GetControlPoints();
                            // round the region cursor float values since subimage cursor comes from IPosition
                            CursorXy region_cursor(round(region_points[0].x()), round(region_points[0].y()));
                            if (Interrupt(region_id, region_cursor, subimage_cursor)) { // point region moved
                                return false;
                            }
                        } else { // region closed
                            return false;
                        }
                    }

                    // Slice subimage for this step; modify the count and update progress
                    count(_spectral_axis) =
                        (start(_spectral_axis) + delta_channels < profile_size ? delta_channels : profile_size - start(_spectral_axis));
                    casacore::Slicer slicer(start, count);
                    casacore::Array<float> buffer;
                    std::unique_lock<std::mutex> guard(_image_mutex);
                    sub_image.doGetSlice(buffer, slicer);
                    guard.unlock();
                    memcpy(&data[start(_spectral_axis)], buffer.data(), count(_spectral_axis) * sizeof(float));
                    start(_spectral_axis) += count(_spectral_axis);
                    progress = (float)start(_spectral_axis) / profile_size;

                    // get the time elapse for this step
                    auto t_end = std::chrono::high_resolution_clock::now();
                    auto dt = std::chrono::duration<double, std::milli>(t_end - t_start).count();
                    auto dt_partial_profile = std::chrono::duration<double, std::milli>(t_end - t_partial_profile_start).count();

                    // adjust the increment of channels according to the time elapse
                    delta_channels *= dt_target / dt;
                    if (delta_channels < 1) {
                        delta_channels = 1;
                    }
                    if (delta_channels > profile_size) {
                        delta_channels = profile_size;
                    }

                    if ((dt_partial_profile > TARGET_PARTIAL_CURSOR_TIME) || (progress >= 1.0f)) {
                        // send partial result by the callback function
                        t_partial_profile_start = std::chrono::high_resolution_clock::now();
                        partial_results_callback(data, progress);
                    }
                }
                data_ok = true;
            }
        } else {
            // non-stoppable spectral profile process
            casacore::Array<float> tmp(sub_image_shape, data.data(), casacore::StorageInitPolicy::SHARE);
            std::unique_lock<std::mutex> guard(_image_mutex);
            sub_image.doGetSlice(tmp, casacore::Slicer(casacore::IPosition(sub_image_shape.size(), 0), sub_image_shape));
            guard.unlock();
            data_ok = true;
        }
    } catch (casacore::AipsError& err) {
        std::cerr << "Point region spectral profile error: " << err.getMesg() << std::endl;
    }
    return data_ok;
}

bool Frame::GetRegionSpectralData(int region_id, int config_stokes, int profile_stokes,
    const std::function<void(std::map<CARTA::StatsType, std::vector<double>>, float)>& partial_results_callback) {
    // get starting spectral config
    SpectralConfig start_config_stats;
    if (!GetRegionSpectralConfig(region_id, config_stokes, start_config_stats)) {
        return false;
    }
    // get starting region state
    RegionState start_region_state;
    if (!GetRegionState(region_id, start_region_state)) {
        return false;
    }

    bool data_ok(false);
    if (_regions.count(region_id)) {
        auto& region = _regions[region_id];
        std::map<CARTA::StatsType, std::vector<double>> results;
        size_t start;
        size_t profile_size = NumChannels(); // total number of channels

        // Try to get the cache or initialize spectral profiles to NaN
        region->InitSpectralData(profile_stokes, profile_size, results, start);

        // If stats cache is available and completed, don't need to recalculate spectral profiles.
        if (start == profile_size) {
            partial_results_callback(results, 1.0f);
            return true;
        }

        // Initialize variables and calculate spectral profiles
        size_t count, end;
        float progress = 0;
        casacore::SubImage<float> sub_image;
        int delta_channels = INIT_DELTA_CHANNEL; // the increment of channels for each step
        int dt_target = TARGET_DELTA_TIME;       // the target time elapse for each step, in the unit of milliseconds
        auto t_partial_profile_start = std::chrono::high_resolution_clock::now();

        // get stats data
        while (start < profile_size) {
            // start the timer
            auto t_start = std::chrono::high_resolution_clock::now();

            // check if region, current stokes, spectral requirements changed
            if (Interrupt(region_id, profile_stokes, start_region_state, start_config_stats)) {
                // Save stats cache if region state is not changed
                if (IsSameRegionState(region_id, start_region_state)) {
                    region->SetStatsCache(profile_stokes, results, start);
                }
                return data_ok; // false until complete
            }

            end = (start + delta_channels > profile_size ? profile_size - 1 : start + delta_channels - 1);
            count = end - start + 1;

            // try to get sub-image and its stats data
            std::unique_lock<std::mutex> ulock(_image_mutex);
            bool has_subimage = GetRegionSubImage(region_id, sub_image, profile_stokes, ChannelRange(start, end));
            ulock.unlock();
            if (has_subimage) {
                std::map<CARTA::StatsType, std::vector<double>> buffers;
                std::unique_lock<std::mutex> ulock2(_image_mutex);
                bool has_data = region->GetSpectralProfileData(buffers, sub_image);
                ulock2.unlock();
                if (has_data) {
                    for (const auto& buffer : buffers) {
                        auto stats_type = buffer.first;
                        if (results.count(stats_type)) {
                            const std::vector<double>& stats_data = buffer.second;
                            memcpy(&results[stats_type][start], &stats_data[0], stats_data.size() * sizeof(double));
                        }
                    }
                } else {
                    if (_verbose) {
                        std::cerr << "Can not get zprofile, region id: " << region_id << ", channel range: [" << start << "," << end << "]"
                                  << std::endl;
                    }
                    return data_ok;
                }
            }

            start += count;
            progress = (float)start / profile_size;

            // get the time elapse for this step
            auto t_end = std::chrono::high_resolution_clock::now();
            auto dt = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            auto dt_partial_profile = std::chrono::duration<double, std::milli>(t_end - t_partial_profile_start).count();

            // adjust the increment of channels according to the time elapse
            delta_channels *= dt_target / dt;
            if (delta_channels < 1) {
                delta_channels = 1;
            }
            if (delta_channels > profile_size) {
                delta_channels = profile_size;
            }

            // send partial result by the callback function
            if (dt_partial_profile > TARGET_PARTIAL_REGION_TIME || progress >= 1.0f) {
                t_partial_profile_start = std::chrono::high_resolution_clock::now();
                partial_results_callback(results, progress);
            }

            // Save stats cache while spectral profiles are completed and region state is not changed
            if (progress == 1.0f && IsSameRegionState(region_id, start_region_state)) {
                region->SetStatsCache(profile_stokes, results, start);
            }
        }
        data_ok = true;
    }
    return data_ok;
}

bool Frame::ContourImage(ContourCallback& partial_contour_callback) {
    double scale = 1.0;
    double offset = 0;
    bool smooth_successful = false;
    std::vector<std::vector<float>> vertex_data;
    std::vector<std::vector<int>> index_data;
    tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, false);

    if (_contour_settings.smoothing_mode == CARTA::SmoothingMode::NoSmoothing || _contour_settings.smoothing_factor <= 1) {
        TraceContours(_image_cache.data(), _image_shape(0), _image_shape(1), scale, offset, _contour_settings.levels, vertex_data,
            index_data, _contour_settings.chunk_size, partial_contour_callback, _verbose);
        return true;
    } else if (_contour_settings.smoothing_mode == CARTA::SmoothingMode::GaussianBlur) {
        // Smooth the image from cache
        int mask_size = (_contour_settings.smoothing_factor - 1) * 2 + 1;
        int64_t kernel_width = (mask_size - 1) / 2;

        int64_t source_width = _image_shape(0);
        int64_t source_height = _image_shape(1);
        int64_t dest_width = _image_shape(0) - 2 * kernel_width;
        int64_t dest_height = _image_shape(1) - 2 * kernel_width;
        std::unique_ptr<float[]> dest_array(new float[dest_width * dest_height]);
        smooth_successful = GaussianSmooth(_image_cache.data(), dest_array.get(), source_width, source_height, dest_width, dest_height,
            _contour_settings.smoothing_factor, _verbose);
        // Can release lock early, as we're no longer using the image cache
        cache_lock.release();
        if (smooth_successful) {
            // Perform contouring with an offset based on the Gaussian smoothing apron size
            offset = _contour_settings.smoothing_factor - 1;
            TraceContours(dest_array.get(), dest_width, dest_height, scale, offset, _contour_settings.levels, vertex_data, index_data,
                _contour_settings.chunk_size, partial_contour_callback, _verbose);
            return true;
        }
    } else {
        // Block averaging
        CARTA::ImageBounds image_bounds;
        image_bounds.set_x_min(0);
        image_bounds.set_y_min(0);
        image_bounds.set_x_max(_image_shape(0));
        image_bounds.set_y_max(_image_shape(1));
        std::vector<float> dest_vector;
        smooth_successful = GetRasterData(dest_vector, image_bounds, _contour_settings.smoothing_factor, true);
        cache_lock.release();
        if (smooth_successful) {
            // Perform contouring with an offset based on the block size, and a scale factor equal to block size
            offset = 0;
            scale = _contour_settings.smoothing_factor;
            size_t dest_width = image_bounds.x_max() / _contour_settings.smoothing_factor;
            size_t dest_height = image_bounds.y_max() / _contour_settings.smoothing_factor;
            TraceContours(dest_vector.data(), dest_width, dest_height, scale, offset, _contour_settings.levels, vertex_data, index_data,
                _contour_settings.chunk_size, partial_contour_callback, _verbose);
            return true;
        }
        fmt::print("Smoothing mode not implemented yet!\n");
        return false;
    }
}

bool Frame::Interrupt(int region_id, const CursorXy& cursor1, const CursorXy& cursor2) {
    // check if point moved, spectral requirements changed
    if (!IsConnected(region_id)) {
        if (_verbose) {
            std::cerr << "Closing image/region, exit zprofile before complete" << std::endl;
        }
        return true;
    }
    if (!(cursor1 == cursor2)) {
        if (_verbose) {
            std::cerr << "Cursor/Point changed, exit zprofile before complete" << std::endl;
        }
        return true;
    }
    // check is the Cursor/Point shown on the frontend spectral widget
    if (_regions.count(region_id) && _regions[region_id]->NumSpectralProfiles() == 0) {
        if (_verbose) {
            std::cerr << "Cursor/Point profile not needed, exit zprofile before complete" << std::endl;
        }
        return true;
    }
    return false;
}

bool Frame::Interrupt(
    int region_id, int profile_stokes, const RegionState& start_region_state, const SpectralConfig& start_config_stats, bool is_HDF5) {
    // check if region, current stokes, spectral requirements changed
    bool interrupt(true);
    if (!IsConnected(region_id)) {
        if (_verbose) {
            std::cerr << "[Region " << region_id << "] closing image/region, exit zprofile before complete" << std::endl;
        }
        return interrupt;
    }
    if (!IsSameRegionState(region_id, start_region_state)) {
        if (_verbose) {
            std::cerr << "[Region " << region_id << "] region state changed, exit zprofile before complete" << std::endl;
        }
        return interrupt;
    }
    if (!IsSameRegionSpectralConfig(region_id, profile_stokes, start_config_stats, is_HDF5)) {
        if (_verbose) {
            std::cerr << "[Region " << region_id << "] region requirement changed, exit zprofile before complete" << std::endl;
        }
        return interrupt;
    }
    interrupt = false;
    return interrupt;
}

bool Frame::IsConnected(int region_id) {
    if (_regions.count(region_id)) {
        return (_connected && _regions[region_id]->IsConnected());
    }
    return _connected;
}

bool Frame::IsSameRegionState(int region_id, const RegionState& region_state) {
    if (_regions.count(region_id)) {
        return (_regions[region_id]->GetRegionState() == region_state);
    }
    return false;
}

bool Frame::IsSameRegionSpectralConfig(int region_id, int profile_stokes, const SpectralConfig& start_config_stats, bool is_HDF5) {
    // compare stokes, spectral requirements
    if (!_regions.count(region_id)) { // region closed
        return false;
    }

    // Check CURRENT_STOKES changed
    if ((start_config_stats.stokes_index == CURRENT_STOKES) && (profile_stokes != CurrentStokes())) {
        return false;
    }

    auto& region = _regions[region_id];

    // region has no spectral widgets
    if (region->NumSpectralProfiles() == 0) {
        return false;
    }

    // config removed or changed
    if (!is_HDF5 && !region->IsValidSpectralConfig(start_config_stats)) {
        return false;
    }

    return true; // passed all checks
}

void Frame::SetConnectionFlag(bool connected) {
    _connected = connected;
}

void Frame::SetCursorXy(float x, float y) {
    _cursor_xy = CursorXy(x, y);
}

bool Frame::GetRegionState(int region_id, RegionState& region_state) {
    if (_regions.count(region_id)) {
        region_state = _regions[region_id]->GetRegionState();
        return true;
    }
    return false;
}

bool Frame::GetRegionSpectralConfig(int region_id, int config_stokes, SpectralConfig& config_stats) {
    if (_regions.count(region_id)) {
        return _regions[region_id]->GetSpectralConfig(config_stokes, config_stats);
    }
    return false;
}
bool Frame::SetContourParameters(const CARTA::SetContourParameters& message) {
    ContourSettings new_settings = {std::vector<double>(message.levels().begin(), message.levels().end()), message.smoothing_mode(),
        message.smoothing_factor(), message.decimation_factor(), message.compression_level(), message.contour_chunk_size(),
        message.reference_file_id()};

    if (_contour_settings != new_settings) {
        _contour_settings = new_settings;
        return true;
    }
    return false;
}
