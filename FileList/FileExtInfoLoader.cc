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

FileExtInfoLoader::FileExtInfoLoader(const std::string& filename) : _filename(filename) {
    _type = GetCartaFileType(filename);
}

bool FileExtInfoLoader::FillFileExtInfo(CARTA::FileInfoExtended* extended_info, std::string& hdu, std::string& message) {
    casacore::File cc_file(_filename);
    if (!cc_file.exists()) {
        return false;
    }

    // set name from filename
    std::string name(cc_file.path().baseName());
    auto entry = extended_info->add_computed_entries();
    entry->set_name("Name");
    entry->set_value(name);
    entry->set_entry_type(CARTA::EntryType::STRING);

    // fill header_entries, computed_entries
    return FillFileInfoFromImage(extended_info, hdu, message);
}

bool FileExtInfoLoader::FillFileInfoFromImage(CARTA::FileInfoExtended* extended_info, std::string& hdu, std::string& message) {
    // add header_entries in FITS format (issue #13) using ImageInterface from FileLoader
    bool file_ok(false);
    if ((_type == CARTA::FileType::MIRIAD) && (!CheckMiriadImage(_filename, message))) { // in Util.h
        // checks if image is valid using mirlib directly before making MIRIADImage which could crash backend
        return file_ok;
    }

    try {
        std::unique_ptr<FileLoader> loader = std::unique_ptr<FileLoader>(FileLoader::GetLoader(_filename));
        if (loader) {
            loader->OpenFile(hdu);
            casacore::ImageInterface<float>* image = loader->LoadData(carta::FileInfo::Data::Image);
            if (image) {
                casacore::IPosition image_shape(image->shape());
                unsigned int num_dim = image_shape.size();
                if (num_dim < 2 || num_dim > 4) {
                    message = "Image must be 2D, 3D or 4D.";
                    return file_ok;
                }

                // Add header_entries
                casacore::String error_string, origin_str;
                casacore::ImageFITSHeaderInfo fhi;
                bool prefer_velocity(false), optical_velocity(false), stokes_last(false), degenerate_last(false), prefer_wavelength(false),
                    air_wavelength(false), verbose(false), prim_head(true), allow_append(false), history(false);
                int bit_pix(-32);
                float min_pix(1.0), max_pix(-1.0);
                bool ok = casacore::ImageFITSConverter::ImageHeaderToFITS(error_string, fhi, *image, prefer_velocity, optical_velocity,
                    bit_pix, min_pix, max_pix, degenerate_last, verbose, stokes_last, prefer_wavelength, air_wavelength, prim_head,
                    allow_append, origin_str, history);
                if (!ok) {
                    message = error_string;
                    return file_ok;
                }

                if (_type == CARTA::FileType::HDF5) {
                    // add schema and converter info
                    carta::CartaHdf5Image* hdf5_image = static_cast<carta::CartaHdf5Image*>(image);
                    if (hdf5_image) {
                        AddHdf5Headers(extended_info, hdf5_image);
                    }
                }

                int naxis(0), ncoord(0);
                fhi.kw.first(); // go to first card
                casacore::FitsKeyword* fkw = fhi.kw.next();
                while (fkw) {
                    // parse each FitsKeyword into header_entries in FileInfoExtended
                    casacore::String full_name(fkw->name());
                    full_name.trim();

                    // Strangely, ImageHeaderToFITS does not append axis or coordinate number
                    if (full_name == "NAXIS") {
                        if (naxis > 0) {
                            full_name += casacore::String::toString(naxis); // append axis number
                        }
                        naxis++;
                    }
                    if (full_name == "CTYPE") {
                        // This assumes that CTYPE starts the block of C*n headers
                        ++ncoord;
                    }
                    if ((full_name == "CTYPE") || (full_name == "CRVAL") || (full_name == "CDELT") || (full_name == "CRPIX")) {
                        full_name += casacore::String::toString(ncoord); // append coordinate number
                    }

                    if (full_name != "END") {
                        auto header_entry = extended_info->add_header_entries();
                        // Left justify and pad to 8 chars
                        // std::string name = fmt::format("{:<8}", full_name);
                        header_entry->set_name(full_name);
                        switch (fkw->type()) {
                            case casacore::FITS::LOGICAL: {
                                bool value(fkw->asBool());
                                header_entry->set_entry_type(CARTA::EntryType::INT);
                                header_entry->set_numeric_value(value);
                                std::string bool_string(value ? "T" : "F");
                                // std::string string_value = fmt::format("{:>30}", bool_string);
                                std::string comment(fkw->comm());
                                if (!comment.empty()) {
                                    bool_string.append(" / " + comment);
                                }
                                *header_entry->mutable_value() = bool_string;
                                break;
                            }
                            case casacore::FITS::LONG: {
                                int value(fkw->asInt());
                                header_entry->set_entry_type(CARTA::EntryType::INT);
                                header_entry->set_numeric_value(value);
                                std::string string_value = std::to_string(value);
                                // std::string string_value = fmt::format("{:>30}", std::to_string(value));
                                std::string comment(fkw->comm());
                                if (!comment.empty()) {
                                    string_value.append(" / " + comment);
                                }
                                *header_entry->mutable_value() = string_value;
                                break;
                            }
                            case casacore::FITS::BYTE:
                            case casacore::FITS::SHORT:
                            case casacore::FITS::FLOAT:
                            case casacore::FITS::DOUBLE:
                            case casacore::FITS::REAL: {
                                double value(fkw->asDouble());
                                header_entry->set_entry_type(CARTA::EntryType::FLOAT);
                                header_entry->set_numeric_value(value);
                                std::string string_value = std::to_string(value);
                                // std::string string_value = fmt::format("{:>30}", std::to_string(value));
                                std::string comment(fkw->comm());
                                if (!comment.empty()) {
                                    string_value.append(" / " + comment);
                                }
                                *header_entry->mutable_value() = string_value;
                                break;
                            }
                            case casacore::FITS::STRING:
                            case casacore::FITS::FSTRING: {
                                casacore::String fkw_string = fkw->asString();
                                fkw_string.trim(); // remove whitespace
                                // std::string string_value = fmt::format("{:>30}", fkw_value);
                                std::string comment(fkw->comm());
                                if (!comment.empty()) {
                                    fkw_string.append(" / " + comment);
                                }
                                header_entry->set_entry_type(CARTA::EntryType::STRING);
                                *header_entry->mutable_value() = fkw_string;
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
                if (loader->FindShape(image_shape, spectral_axis, stokes_axis, message)) {
                    AddShapeEntries(extended_info, image_shape, spectral_axis, stokes_axis);
                    AddComputedEntries(extended_info, image);
                    file_ok = true;
                }
            } else { // image failed
                message = "Image could not be opened.";
            }
        } else { // loader failed
            message = "Image type not supported.";
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

    return file_ok;
}

bool FileExtInfoLoader::CheckMiriadImage(const std::string& filename, std::string& message) {
    // Some MIRIAD images throw an error in the miriad libs which cannot be caught in casacore::MIRIADImage, which crashes the backend.
    // If the following checks pass, it should be safe to open the MiriadImage.
    bool miriad_ok(true);
    int t_handle, i_handle, io_stat, num_dim;
    hopen_c(&t_handle, filename.c_str(), "old", &io_stat);
    if (io_stat != 0) {
        message = "Could not open MIRIAD file";
        miriad_ok = false;
    } else {
        haccess_c(t_handle, &i_handle, "image", "read", &io_stat);
        if (io_stat != 0) {
            message = "Could not open MIRIAD file";
            miriad_ok = false;
        } else {
            rdhdi_c(t_handle, "naxis", &num_dim, 0); // read "naxis" value into ndim, default 0
            hdaccess_c(i_handle, &io_stat);
            hclose_c(t_handle);
            if (num_dim < 2 || num_dim > 4) {
                message = "Image must be 2D, 3D or 4D.";
                miriad_ok = false;
            }
        }
    }
    return miriad_ok;
}

void FileExtInfoLoader::AddHdf5Headers(CARTA::FileInfoExtended* extended_info, carta::CartaHdf5Image* hdf5_image) {
    // add schema and converter info
    std::string schema_version = hdf5_image->SchemaVersion();
    if (!schema_version.empty()) {
        auto header_entry = extended_info->add_header_entries();
        header_entry->set_name("SCHEMA_VERSION");
        header_entry->set_entry_type(CARTA::EntryType::STRING);
        size_t first = schema_version.find("'") + 1;
        size_t count = schema_version.rfind("'") - first;
        *header_entry->mutable_value() = schema_version.substr(first, count);
    }
    std::string hdf5_converter = hdf5_image->Hdf5Converter();
    if (!hdf5_converter.empty()) {
        auto header_entry = extended_info->add_header_entries();
        header_entry->set_name("HDF5_CONVERTER");
        header_entry->set_entry_type(CARTA::EntryType::STRING);
        size_t first = hdf5_converter.find("'") + 1;
        size_t count = hdf5_converter.rfind("'") - first;
        *header_entry->mutable_value() = hdf5_converter.substr(first, count);
    }
    std::string converter_version = hdf5_image->Hdf5ConverterVersion();
    if (!converter_version.empty()) {
        auto header_entry = extended_info->add_header_entries();
        header_entry->set_name("HDF5_CONVERTER_VERSION");
        header_entry->set_entry_type(CARTA::EntryType::STRING);
        size_t first = converter_version.find("'") + 1;
        size_t count = converter_version.rfind("'") - first;
        *header_entry->mutable_value() = converter_version.substr(first, count);
    }
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

void FileExtInfoLoader::AddComputedEntries(CARTA::FileInfoExtended* extended_info, casacore::ImageInterface<float>* image) {
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

    if (!reference_values.empty() && !axis_units.empty()) {
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Image reference coordinates");
        casacore::Quantity coord0(reference_values(0), axis_units(0));
        casacore::Quantity coord1(reference_values(1), axis_units(1));
        std::string ref_coords = fmt::format("[{}, {}]", coord0.get("deg"), coord1.get("deg"));
        entry->set_value(ref_coords);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!axis_names.empty() && !reference_values.empty() && !axis_units.empty()) {
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Image ref coords (coord type)");
        std::string format_coord1 = MakeAngleString(axis_names(0), reference_values(0), axis_units(0));
        std::string format_coord2 = MakeAngleString(axis_names(1), reference_values(1), axis_units(1));
        std::string format_coords = fmt::format("[{}, {}]", format_coord1, format_coord2);
        entry->set_value(format_coords);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (coord_system.hasDirectionCoordinate()) {
        casacore::String direction_frame = casacore::MDirection::showType(coord_system.directionCoordinate().directionType());
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Celestial frame");
        entry->set_value(direction_frame);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (coord_system.hasSpectralAxis()) {
        casacore::String spectral_frame = casacore::MFrequency::showType(coord_system.spectralCoordinate().frequencySystem());
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Spectral frame");
        entry->set_value(spectral_frame);
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
            entry->set_name("Median area beam");
            gaussian_beam = image_info.getBeamSet().getMedianAreaBeam();
        }
        std::string beam_info = fmt::format("{:.2f}\" X {:.2f}\", {:.4f} deg", gaussian_beam.getMajor("arcsec"),
            gaussian_beam.getMinor("arcsec"), gaussian_beam.getPA("deg").getValue());
        entry->set_value(beam_info);
    }
}

// ***** FITS keyword conversion *****

std::string FileExtInfoLoader::MakeAngleString(const std::string& type, double val, const std::string& unit) {
    // make coordinate angle string for RA, DEC, GLON, GLAT; else just return "{val} {unit}"
    if (unit.empty()) {
        return fmt::format("{}", val);
    }

    casacore::MVAngle::formatTypes format;
    if (type == "Right Ascension") {
        format = casacore::MVAngle::TIME;
    } else if ((type == "Declination") || (type.find("Longitude") != std::string::npos) || (type.find("Latitude") != std::string::npos)) {
        format = casacore::MVAngle::ANGLE;
    } else {
        return fmt::format("{} {}", val, unit);
    }

    casacore::Quantity quant1(val, unit);
    casacore::MVAngle mva(quant1);
    return mva.string(format, 10);
}
