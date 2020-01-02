//# FileExtInfoLoader.cc: fill FileInfoExtended for all supported file types

#include "FileExtInfoLoader.h"

#include <fmt/format.h>
#include <fmt/ostream.h> // not needed here but *must* include before miriad.h

#include <casacore/casa/OS/File.h>
#include <casacore/casa/Quanta/MVAngle.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/images/Images/ImageFITSConverter.h>
#include <casacore/measures/Measures/MDirection.h>
#include <casacore/measures/Measures/MFrequency.h>
#include <casacore/mirlib/miriad.h>

#include "../ImageData/FileLoader.h"

using namespace carta;

FileExtInfoLoader::FileExtInfoLoader(carta::FileLoader* loader) : _loader(loader) {}

bool FileExtInfoLoader::FillFileExtInfo(
    CARTA::FileInfoExtended* extended_info, const std::string& filename, const std::string& hdu, std::string& message) {
    // set name from filename
    auto entry = extended_info->add_computed_entries();
    entry->set_name("Name");
    entry->set_value(filename);
    entry->set_entry_type(CARTA::EntryType::STRING);

    // fill header_entries, computed_entries
    bool file_ok(false);
    if (_loader->CanOpenFile(message)) {
        file_ok = FillFileInfoFromImage(extended_info, hdu, message);
    }
    return file_ok;
}

bool FileExtInfoLoader::FillFileInfoFromImage(CARTA::FileInfoExtended* extended_info, const std::string& hdu, std::string& message) {
    // add header_entries in FITS format (issue #13) using ImageInterface from FileLoader
    bool file_ok(false);
    if (_loader) {
        try {
            _loader->OpenFile(hdu);
            casacore::ImageInterface<float>* image = _loader->GetImage();
            if (image) {
                casacore::IPosition image_shape(image->shape());
                unsigned int num_dim = image_shape.size();
                if (num_dim < 2 || num_dim > 4) {
                    message = "Image must be 2D, 3D or 4D.";
                    return file_ok;
                }

                bool prefer_velocity(false), optical_velocity(false);
                bool prefer_wavelength(false), air_wavelength(false);
                casacore::CoordinateSystem coord_sys(image->coordinates());
                if (coord_sys.hasSpectralAxis()) {
                    // retain spectral axis native type in headers
                    switch (coord_sys.spectralCoordinate().nativeType()) {
                        case casacore::SpectralCoordinate::FREQ: {
                            break;
                        }
                        case casacore::SpectralCoordinate::VRAD:
                        case casacore::SpectralCoordinate::BETA: {
                            prefer_velocity = true;
                            break;
                        }
                        case casacore::SpectralCoordinate::VOPT: {
                            prefer_velocity = true;
                            // If VELREF is not set in headers, spectral native type (mirlib) is VOPT even when CTYPE is VRAD
                            casacore::MDoppler::Types vel_doppler(coord_sys.spectralCoordinate().velocityDoppler());
                            if ((vel_doppler == casacore::MDoppler::Z) || (vel_doppler == casacore::MDoppler::OPTICAL)) {
                                optical_velocity = true;
                            }
                            break;
                        }
                        case casacore::SpectralCoordinate::WAVE: {
                            prefer_wavelength = true;
                            break;
                        }
                        case casacore::SpectralCoordinate::AWAV: {
                            prefer_wavelength = true;
                            air_wavelength = true;
                            break;
                        }
                    }
                }

                // Add header_entries in FITS format
                casacore::String error_string, origin_string;
                casacore::ImageFITSHeaderInfo fhi;
                bool stokes_last(false), degenerate_last(false), verbose(false), prim_head(true), allow_append(false), history(false);
                int bit_pix(-32);
                float min_pix(1.0), max_pix(-1.0);
                if (!casacore::ImageFITSConverter::ImageHeaderToFITS(error_string, fhi, *image, prefer_velocity, optical_velocity, bit_pix,
                        min_pix, max_pix, degenerate_last, verbose, stokes_last, prefer_wavelength, air_wavelength, prim_head, allow_append,
                        origin_string, history)) {
                    message = error_string;
                    return file_ok;
                }

                // axis or coord number to append to name
                int naxis(0), ncoord(0);
                // save radesys header
                casacore::String radesys;

                fhi.kw.first(); // go to first card
                casacore::FitsKeyword* fkw = fhi.kw.next();
                while (fkw) {
                    // parse each FitsKeyword into header_entries in FileInfoExtended
                    casacore::String name(fkw->name());
                    name.trim();

                    // Strangely, ImageHeaderToFITS does not append axis or coordinate number
                    if (name == "NAXIS") {
                        // append and increment axis number
                        if (naxis > 0) {
                            name += casacore::String::toString(naxis);
                        }
                        naxis++;
                    } else if (name == "CTYPE") {
                        // This assumes that CTYPE starts the block of C*n headers
                        ++ncoord;
                    }

                    // Modify names
                    if ((name == "CTYPE") || (name == "CRVAL") || (name == "CDELT") || (name == "CRPIX")) {
                        // append coordinate number
                        name += casacore::String::toString(ncoord);
                    } else if (name == "H5SCHEMA") { // was shortened to FITS length 8
                        name = "SCHEMA_VERSION";
                    } else if (name == "H5CNVRTR") { // was shortened to FITS length 8
                        name = "HDF5_CONVERTER";
                    } else if (name == "H5CONVSN") { // was shortened to FITS length 8
                        name = "HDF5_CONVERTER_VERSION";
                    } else if (name == "H5DATE") { // was shortened to FITS length 8
                        name = "HDF5_DATE";
                    }

                    if (name != "END") {
                        switch (fkw->type()) {
                            case casacore::FITS::LOGICAL: {
                                bool value(fkw->asBool());
                                std::string bool_string(value ? "T" : "F");
                                std::string comment(fkw->comm());
                                if (!comment.empty()) {
                                    bool_string.append(" / " + comment);
                                }
                                auto header_entry = extended_info->add_header_entries();
                                header_entry->set_name(name);
                                *header_entry->mutable_value() = bool_string;
                                header_entry->set_entry_type(CARTA::EntryType::INT);
                                header_entry->set_numeric_value(value);
                                break;
                            }
                            case casacore::FITS::LONG: {
                                int value(fkw->asInt());
                                std::string string_value = fmt::format("{:d}", value);
                                std::string comment(fkw->comm());
                                if (!comment.empty()) {
                                    string_value.append(" / " + comment);
                                }
                                auto header_entry = extended_info->add_header_entries();
                                header_entry->set_name(name);
                                *header_entry->mutable_value() = string_value;
                                header_entry->set_entry_type(CARTA::EntryType::INT);
                                header_entry->set_numeric_value(value);
                                break;
                            }
                            case casacore::FITS::BYTE:
                            case casacore::FITS::SHORT:
                            case casacore::FITS::FLOAT:
                            case casacore::FITS::DOUBLE:
                            case casacore::FITS::REAL: {
                                double value(fkw->asDouble());
                                std::string string_value;
                                if ((name.find("PIX") != std::string::npos) || (name.find("EQUINOX") != std::string::npos) ||
                                    (name.find("EPOCH") != std::string::npos)) {
                                    string_value = fmt::format("{}", value);
                                } else {
                                    string_value = fmt::format("{:.12E}", value);
                                }
                                std::string comment(fkw->comm());
                                if (!comment.empty()) {
                                    string_value.append(" / " + comment);
                                }
                                auto header_entry = extended_info->add_header_entries();
                                header_entry->set_name(name);
                                *header_entry->mutable_value() = string_value;
                                header_entry->set_entry_type(CARTA::EntryType::FLOAT);
                                header_entry->set_numeric_value(value);
                                break;
                            }
                            case casacore::FITS::STRING:
                            case casacore::FITS::FSTRING: {
                                // Do not include ORIGIN (casacore) or DATE (current) added by ImageHeaderToFITS
                                if ((name != "DATE") && (name != "ORIGIN")) {
                                    casacore::String fkw_string = fkw->asString();
                                    fkw_string.trim(); // remove whitespace
                                    // save without comment
                                    if (name == "RADESYS") {
                                        radesys = fkw_string;
                                    }
                                    // add comment
                                    std::string comment(fkw->comm());
                                    if (!comment.empty()) {
                                        fkw_string.append(" / " + comment);
                                    }

                                    auto header_entry = extended_info->add_header_entries();
                                    header_entry->set_name(name);
                                    *header_entry->mutable_value() = fkw_string;
                                    header_entry->set_entry_type(CARTA::EntryType::STRING);
                                }
                                break;
                            }
                            case casacore::FITS::BIT:
                            case casacore::FITS::CHAR:
                            case casacore::FITS::COMPLEX:
                            case casacore::FITS::ICOMPLEX:
                            case casacore::FITS::DCOMPLEX:
                            case casacore::FITS::VADESC:
                            case casacore::FITS::NOVALUE:
                            default:
                                break;
                        }
                    }
                    fkw = fhi.kw.next(); // get next keyword
                }

                int spectral_axis, stokes_axis;
                _loader->FindCoordinateAxes(image_shape, spectral_axis, stokes_axis, message);
                AddShapeEntries(extended_info, image_shape, spectral_axis, stokes_axis);
                AddComputedEntries(extended_info, image, radesys);
                file_ok = true;
            } else { // image failed
                message = "Image could not be opened.";
            }
        } catch (casacore::AipsError& err) {
            message = err.getMesg();
            if (message.find("diagonal") != std::string::npos) { // "ArrayBase::diagonal() - diagonal out of range"
                message = "Failed to open image at specified HDU.";
            } else if (message.find("No image at specified location") != std::string::npos) {
                message = "No image at specified HDU.";
            } else {
                message = "Failed to open image: " + message;
            }
        }
    } else { // loader failed
        message = "Image type not supported.";
    }
    return file_ok;
}

// ***** Computed entries *****

void FileExtInfoLoader::AddShapeEntries(
    CARTA::FileInfoExtended* extended_info, const casacore::IPosition& shape, int chan_axis, int stokes_axis) {
    // Set fields/header entries for shape: dimensions, width, height, depth, stokes
    int num_dims(shape.size());
    extended_info->set_dimensions(num_dims);
    extended_info->set_width(shape(0));
    extended_info->set_height(shape(1));
    if (num_dims == 2) { // 2D
        extended_info->set_depth(1);
        extended_info->set_stokes(1);
    } else if (num_dims == 3) { // 3D
        extended_info->set_depth(shape(2));
        extended_info->set_stokes(1);
    } else { // 4D
        extended_info->set_depth(shape(chan_axis));
        extended_info->set_stokes(shape(stokes_axis));
    }

    // shape computed_entry
    std::string shape_string;
    switch (num_dims) {
        case 2:
            shape_string = fmt::format("[{}, {}]", shape(0), shape(1));
            break;
        case 3:
            shape_string = fmt::format("[{}, {}, {}]", shape(0), shape(1), shape(2));
            break;
        case 4:
            shape_string = fmt::format("[{}, {}, {}, {}]", shape(0), shape(1), shape(2), shape(3));
            break;
    }
    auto shape_entry = extended_info->add_computed_entries();
    shape_entry->set_name("Shape");
    shape_entry->set_value(shape_string);
    shape_entry->set_entry_type(CARTA::EntryType::STRING);

    if (chan_axis >= 0) {
        // header entry for number of channels
        unsigned int nchan = shape(chan_axis);
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Number of channels");
        entry->set_value(casacore::String::toString(nchan));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(nchan);
    }
    if (stokes_axis >= 0) {
        // header entry for number of stokes
        unsigned int nstokes = shape(stokes_axis);
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Number of stokes");
        entry->set_value(casacore::String::toString(nstokes));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(nstokes);
    }
}

void FileExtInfoLoader::AddComputedEntries(
    CARTA::FileInfoExtended* extended_info, casacore::ImageInterface<float>* image, casacore::String& radesys) {
    // add computed_entries to extended info (ensures the proper order in file browser)
    casacore::CoordinateSystem coord_system(image->coordinates());
    casacore::Vector<casacore::String> axis_names = coord_system.worldAxisNames();
    casacore::Vector<casacore::String> axis_units = coord_system.worldAxisUnits();
    casacore::Vector<casacore::Double> reference_pixels = coord_system.referencePixel();
    casacore::Vector<casacore::Double> reference_values = coord_system.referenceValue();
    casacore::Vector<casacore::Double> increment = coord_system.increment();

    if (!axis_names.empty()) {
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Coordinate type");
        std::string coord_type = fmt::format("{}, {}", axis_names(0), axis_names(1));
        entry->set_value(coord_type);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (coord_system.hasDirectionCoordinate()) {
        std::string projection(coord_system.directionCoordinate().projection().name());
        if (!projection.empty()) {
            auto entry = extended_info->add_computed_entries();
            entry->set_name("Projection");
            entry->set_value(projection);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }
    }

    if (!reference_pixels.empty()) {
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Image reference pixels");
        std::string ref_pix = fmt::format("[{}, {}]", reference_pixels(0) + 1.0, reference_pixels(1) + 1.0);
        entry->set_value(ref_pix);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!axis_names.empty() && !reference_values.empty() && !axis_units.empty()) {
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Image reference coords");
        std::string format_coord1 = MakeAngleString(axis_names(0), reference_values(0), axis_units(0));
        std::string format_coord2 = MakeAngleString(axis_names(1), reference_values(1), axis_units(1));
        std::string format_coords = fmt::format("[{}, {}]", format_coord1, format_coord2);
        entry->set_value(format_coords);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!reference_values.empty() && !axis_units.empty()) {
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Image ref coords (deg)");
        casacore::Quantity coord0(reference_values(0), axis_units(0));
        casacore::Quantity coord1(reference_values(1), axis_units(1));
        std::string ref_coords = fmt::format("[{}, {}]", coord0.get("deg"), coord1.get("deg"));
        entry->set_value(ref_coords);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (coord_system.hasDirectionCoordinate()) {
        casacore::String direction_frame = casacore::MDirection::showType(coord_system.directionCoordinate().directionType());
        // add RADESYS
        if (radesys.empty()) {
            if (direction_frame.contains("J2000")) {
                radesys = "FK5";
            } else if (direction_frame.contains("B1950")) {
                radesys = "FK4";
            }
        }
        if (!radesys.empty() && (radesys != "ICRS")) {
            direction_frame = radesys + ", " + direction_frame;
        }

        auto entry = extended_info->add_computed_entries();
        entry->set_name("Celestial frame");
        entry->set_value(direction_frame);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (coord_system.hasSpectralAxis()) {
        casacore::String spectral_frame = casacore::MFrequency::showType(coord_system.spectralCoordinate().frequencySystem(true));
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Spectral frame");
        entry->set_value(spectral_frame);
        entry->set_entry_type(CARTA::EntryType::STRING);
        casacore::String vel_doppler = casacore::MDoppler::showType(coord_system.spectralCoordinate().velocityDoppler());
        entry = extended_info->add_computed_entries();
        entry->set_name("Velocity definition");
        entry->set_value(vel_doppler);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    casacore::String brightness_unit(image->units().getName());
    if (!brightness_unit.empty()) {
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Pixel unit");
        entry->set_value(brightness_unit);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!increment.empty() && !axis_units.empty()) {
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Pixel increment");
        casacore::Quantity inc0(increment(0), axis_units(0));
        casacore::Quantity inc1(increment(1), axis_units(1));
        std::string pixel_inc = fmt::format("{:.3f}\", {:.3f}\"", inc0.getValue("arcsec"), inc1.getValue("arcsec"));
        entry->set_value(pixel_inc);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    casacore::ImageInfo image_info(image->imageInfo());
    if (image_info.hasBeam()) {
        auto entry = extended_info->add_computed_entries();
        entry->set_entry_type(CARTA::EntryType::STRING);
        casacore::GaussianBeam gaussian_beam;
        if (image_info.hasSingleBeam()) {
            gaussian_beam = image_info.restoringBeam();
            entry->set_name("Restoring beam");
        } else if (image_info.hasMultipleBeams()) {
            gaussian_beam = image_info.getBeamSet().getMedianAreaBeam();
            entry->set_name("Median area beam");
        }
        std::string beam_info = fmt::format("{:g}\" X {:g}\", {:g} deg", gaussian_beam.getMajor("arcsec"), gaussian_beam.getMinor("arcsec"),
            gaussian_beam.getPA("deg").getValue());
        entry->set_value(beam_info);
    }
}

// ***** FITS keyword conversion *****

std::string FileExtInfoLoader::MakeAngleString(const std::string& type, double val, const std::string& unit) {
    // make coordinate angle string for RA, DEC, GLON, GLAT; else just return "{val} {unit}"
    if (unit.empty()) {
        return fmt::format("{:g}", val);
    }

    casacore::MVAngle::formatTypes format;
    if (type == "Right Ascension") {
        format = casacore::MVAngle::TIME;
    } else if ((type == "Declination") || (type.find("Longitude") != std::string::npos) || (type.find("Latitude") != std::string::npos)) {
        format = casacore::MVAngle::ANGLE;
    } else {
        return fmt::format("{:g} {}", val, unit);
    }

    casacore::Quantity quant1(val, unit);
    casacore::MVAngle mva(quant1);
    return mva.string(format, 10);
}
