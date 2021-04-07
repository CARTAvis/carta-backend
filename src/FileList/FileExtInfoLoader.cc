/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# FileExtInfoLoader.cc: fill FileInfoExtended for all supported file types

#include "FileExtInfoLoader.h"

#include <spdlog/fmt/fmt.h>
// not needed here but *must* include before miriad.h
#include <spdlog/fmt/ostr.h>

#include <casacore/casa/OS/File.h>
#include <casacore/casa/Quanta/MVAngle.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/fits/FITS/hdu.h>
#include <casacore/images/Images/ImageFITSConverter.h>
#include <casacore/measures/Measures/MDirection.h>
#include <casacore/measures/Measures/MFrequency.h>
#include <casacore/mirlib/miriad.h>

#include "../ImageData/CartaMiriadImage.h"
#include "../ImageData/FileLoader.h"

using namespace carta;

FileExtInfoLoader::FileExtInfoLoader(carta::FileLoader* loader) : _loader(loader) {}

bool FileExtInfoLoader::FillFileExtInfo(
    CARTA::FileInfoExtended& extended_info, const std::string& filename, const std::string& hdu, std::string& message) {
    // set name from filename
    auto entry = extended_info.add_computed_entries();
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

bool FileExtInfoLoader::FillFileInfoFromImage(CARTA::FileInfoExtended& extended_info, const std::string& hdu, std::string& message) {
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

                casacore::CoordinateSystem coord_sys(image->coordinates());
                casacore::ImageFITSHeaderInfo fhi;

                bool use_fits_header(false);
                if (coord_sys.linearAxesNumbers().size() == 2) {
                    casacore::String filename(image->name());
                    if (CasacoreImageType(filename) == casacore::ImageOpener::FITS) {
                        // dummy linear system when there is a wcslib error, get original headers
                        casacore::FitsInput fits_input(filename.c_str(), casacore::FITS::Disk);
                        if (!fits_input.err()) {
                            unsigned int hdu_num(FileInfo::GetFitsHdu(hdu));
                            for (unsigned int ihdu = 0; ihdu < hdu_num; ++ihdu) {
                                fits_input.skip_hdu();
                                if (fits_input.err()) {
                                    message = "Error advancing to requested hdu.";
                                    return false;
                                }
                            }

                            casacore::FitsKeywordList kwlist;
                            if (GetFitsKwList(fits_input, hdu_num, kwlist)) {
                                fhi.kw = kwlist;
                                use_fits_header = true;
                            }
                        }
                    }
                }

                if (!use_fits_header) {
                    bool prefer_velocity, optical_velocity, prefer_wavelength, air_wavelength;
                    GetSpectralCoordPreferences(image, prefer_velocity, optical_velocity, prefer_wavelength, air_wavelength);

                    // Get image headers in FITS format
                    casacore::String error_string, origin_string;
                    bool stokes_last(false), degenerate_last(false), verbose(false), prim_head(true), allow_append(false), history(false);
                    int bit_pix(-32);
                    float min_pix(1.0), max_pix(-1.0);
                    if (!casacore::ImageFITSConverter::ImageHeaderToFITS(error_string, fhi, *image, prefer_velocity, optical_velocity,
                            bit_pix, min_pix, max_pix, degenerate_last, verbose, stokes_last, prefer_wavelength, air_wavelength, prim_head,
                            allow_append, origin_string, history)) {
                        message = error_string;
                        return file_ok;
                    }
                }

                // axis or coord number to append to name
                int naxis(0), ntype(1), nval(1), ndelt(1), npix(1);

                casacore::String extname, radesys; // for computed entries

                // Create header entry for each FitsKeyword
                fhi.kw.first(); // go to first card
                casacore::FitsKeyword* fkw = fhi.kw.next();
                std::set<casacore::String> name_set; // used to check the repetition of name
                casacore::String stokes_coord_type_num;
                while (fkw) {
                    casacore::String name(fkw->name());
                    casacore::String comment(fkw->comm());
                    name_set.insert(name);
                    bool fill(true);

                    // Strangely, the FitsKeyword does not append axis/coord number
                    if ((name == "NAXIS")) {
                        if (naxis > 0) {
                            name += casacore::String::toString(naxis++);
                        } else {
                            naxis++;
                        }
                    }

                    // Modify names
                    if (name == "CTYPE") { // append type number
                        name += casacore::String::toString(ntype++);
                    } else if (name == "CRVAL") { // append val number
                        name += casacore::String::toString(nval++);
                    } else if (name == "CDELT") { // append delt number
                        name += casacore::String::toString(ndelt++);
                    } else if (name == "CRPIX") { // append pix number
                        name += casacore::String::toString(npix++);
                    } else if (name == "H5SCHEMA") { // was shortened to FITS length 8
                        name = "SCHEMA_VERSION";
                    } else if (name == "H5CNVRTR") { // was shortened to FITS length 8
                        name = "HDF5_CONVERTER";
                    } else if (name == "H5CONVSN") { // was shortened to FITS length 8
                        name = "HDF5_CONVERTER_VERSION";
                    } else if (name == "H5DATE") { // was shortened to FITS length 8
                        name = "HDF5_DATE";
                    }

                    // Don't fill the name which is repeated after removing the underscore suffix
                    if (!name.empty() && (name.back() == '_') && name_set.count(name.substr(0, name.size() - 1))) {
                        fill = false;
                    }

                    // Get the stokes coordinate number
                    casacore::String key_world = fkw->asString();
                    if (casacore::String(key_world).find("STOKES") != casacore::String::npos) {
                        stokes_coord_type_num = name.back();
                    }

                    // Fill the first stokes type and the delta value for the stokes index. Set the first stokes type index as 0
                    if (!stokes_coord_type_num.empty()) {
                        if (name == ("CRVAL" + stokes_coord_type_num)) {
                            _loader->SetFirstStokesType((int)fkw->asDouble());
                        } else if (name == ("CDELT" + stokes_coord_type_num)) {
                            _loader->SetDeltaStokesIndex((int)fkw->asDouble());
                        }
                    }

                    if (name != "END" && fill) {
                        switch (fkw->type()) {
                            case casacore::FITS::LOGICAL: {
                                bool value(fkw->asBool());
                                std::string bool_string(value ? "T" : "F");

                                auto header_entry = extended_info.add_header_entries();
                                header_entry->set_name(name);
                                *header_entry->mutable_value() = bool_string;
                                header_entry->set_entry_type(CARTA::EntryType::INT);
                                header_entry->set_numeric_value(value);
                                header_entry->set_comment(comment);
                                break;
                            }
                            case casacore::FITS::LONG: {
                                int value(fkw->asInt());
                                std::string string_value = fmt::format("{:d}", value);

                                auto header_entry = extended_info.add_header_entries();
                                header_entry->set_name(name);
                                *header_entry->mutable_value() = string_value;
                                header_entry->set_entry_type(CARTA::EntryType::INT);
                                header_entry->set_numeric_value(value);
                                header_entry->set_comment(comment);
                                break;
                            }
                            case casacore::FITS::BYTE:
                            case casacore::FITS::SHORT:
                            case casacore::FITS::FLOAT:
                            case casacore::FITS::DOUBLE:
                            case casacore::FITS::REAL: {
                                double value(fkw->asDouble());
                                std::string header_string;
                                if ((name.find("PIX") != std::string::npos) || (name.find("EQUINOX") != std::string::npos) ||
                                    (name.find("EPOCH") != std::string::npos)) {
                                    header_string = fmt::format("{}", value);
                                } else {
                                    header_string = fmt::format("{:.12E}", value);
                                }

                                auto header_entry = extended_info.add_header_entries();
                                header_entry->set_name(name);
                                *header_entry->mutable_value() = header_string;
                                header_entry->set_entry_type(CARTA::EntryType::FLOAT);
                                header_entry->set_numeric_value(value);
                                header_entry->set_comment(comment);
                                break;
                            }
                            case casacore::FITS::STRING:
                            case casacore::FITS::FSTRING: {
                                // Do not include ORIGIN (casacore) or DATE (current) added by ImageHeaderToFITS
                                if (use_fits_header || (!use_fits_header && ((name != "DATE") && (name != "ORIGIN")))) {
                                    casacore::String header_string = fkw->asString();
                                    header_string.trim();

                                    // save for computed_entries
                                    if (name == "RADESYS") {
                                        radesys = header_string;
                                    } else if (name == "EXTNAME") {
                                        extname = header_string;
                                    }

                                    if (name.contains("CTYPE") && header_string.contains("FREQ")) {
                                        // Fix header with "FREQUENCY"
                                        header_string = "FREQ";
                                    }

                                    auto header_entry = extended_info.add_header_entries();
                                    header_entry->set_name(name);
                                    *header_entry->mutable_value() = header_string;
                                    header_entry->set_entry_type(CARTA::EntryType::STRING);
                                    header_entry->set_comment(comment);
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

                // Add hdu number and extension name entries if set
                if (!hdu.empty()) {
                    auto entry = extended_info.add_computed_entries();
                    entry->set_name("HDU");
                    entry->set_value(hdu);
                    entry->set_entry_type(CARTA::EntryType::STRING);
                }
                if (!extname.empty()) {
                    auto entry = extended_info.add_computed_entries();
                    entry->set_name("Extension name");
                    entry->set_value(extname);
                    entry->set_entry_type(CARTA::EntryType::STRING);
                }

                int spectral_axis, depth_axis, stokes_axis;
                if (_loader->FindCoordinateAxes(image_shape, spectral_axis, depth_axis, stokes_axis, message)) {
                    std::vector<int> render_axes;
                    _loader->GetRenderAxes(render_axes);
                    AddShapeEntries(extended_info, image_shape, spectral_axis, depth_axis, stokes_axis, render_axes);
                    AddComputedEntries(extended_info, image, render_axes, radesys, use_fits_header);
                    file_ok = true;
                }
            } else { // image failed
                message = "Image could not be opened.";
            }
        } catch (casacore::AipsError& err) {
            message = err.getMesg();
            if (message.find("diagonal") != std::string::npos) { // "ArrayBase::diagonal() - diagonal out of range"
                message = "Failed to open image at specified HDU.";
            } else if (message.find("No image at specified location") != std::string::npos) {
                message = "No image at specified HDU.";
            }
        }
    } else { // loader failed
        message = "Image type not supported.";
    }
    return file_ok;
}

// ***** Computed entries *****

void FileExtInfoLoader::AddShapeEntries(CARTA::FileInfoExtended& extended_info, const casacore::IPosition& shape, int chan_axis,
    int depth_axis, int stokes_axis, const std::vector<int>& render_axes) {
    // Set fields/header entries for shape: dimensions, width, height, depth, stokes
    int num_dims(shape.size());
    int width(shape(render_axes[0]));
    int height(shape(render_axes[1]));
    int depth(depth_axis >= 0 ? shape(depth_axis) : 1);
    int stokes(stokes_axis >= 0 ? shape(stokes_axis) : 1);

    extended_info.set_dimensions(num_dims);
    extended_info.set_width(width);
    extended_info.set_height(height);
    extended_info.set_depth(depth);
    extended_info.set_stokes(stokes);

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
    auto shape_entry = extended_info.add_computed_entries();
    shape_entry->set_name("Shape");
    shape_entry->set_value(shape_string);
    shape_entry->set_entry_type(CARTA::EntryType::STRING);

    if (chan_axis >= 0) {
        // header entry for number of channels
        unsigned int nchan = shape(chan_axis);
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Number of channels");
        entry->set_value(casacore::String::toString(nchan));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(nchan);
    }
    if (stokes_axis >= 0) {
        // header entry for number of stokes
        unsigned int nstokes = shape(stokes_axis);
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Number of stokes");
        entry->set_value(casacore::String::toString(nstokes));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(nstokes);
    }
}

void FileExtInfoLoader::AddComputedEntries(CARTA::FileInfoExtended& extended_info, casacore::ImageInterface<float>* image,
    const std::vector<int>& display_axes, casacore::String& radesys, bool use_fits_header) {
    // Add computed entries to extended file info
    casacore::CoordinateSystem coord_system(image->coordinates());

    if (use_fits_header) {
        AddComputedEntriesFromHeaders(extended_info, display_axes, radesys);
    } else {
        // Use image coordinate system
        int display_axis0(display_axes[0]), display_axis1(display_axes[1]);

        // add computed_entries to extended info (ensures the proper order in file browser)
        casacore::Vector<casacore::String> axis_names = coord_system.worldAxisNames();
        casacore::Vector<casacore::String> axis_units = coord_system.worldAxisUnits();
        casacore::Vector<casacore::Double> reference_pixels = coord_system.referencePixel();
        casacore::Vector<casacore::Double> reference_values = coord_system.referenceValue();
        casacore::Vector<casacore::Double> increment = coord_system.increment();

        if (!axis_names.empty()) {
            std::string coord_type = fmt::format("{}, {}", axis_names(display_axis0), axis_names(display_axis1));
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Coordinate type");
            entry->set_value(coord_type);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }

        if (coord_system.hasDirectionCoordinate()) {
            casacore::DirectionCoordinate dir_coord(coord_system.directionCoordinate());
            std::string projection(dir_coord.projection().name());
            if ((projection == "SIN") && (dir_coord.isNCP())) {
                projection = "SIN / NCP";
            }
            if (!projection.empty()) {
                auto entry = extended_info.add_computed_entries();
                entry->set_name("Projection");
                entry->set_value(projection);
                entry->set_entry_type(CARTA::EntryType::STRING);
            }
        }

        if (!reference_pixels.empty()) {
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Image reference pixels");
            std::string ref_pix = fmt::format("[{}, {}]", reference_pixels(display_axis0) + 1.0, reference_pixels(display_axis1) + 1.0);
            entry->set_value(ref_pix);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }

        if (!reference_values.empty() && !axis_units.empty() && !axis_names.empty()) {
            // Computed entries for reference coordinates
            casacore::Quantity coord0(reference_values(display_axis0), axis_units(display_axis0));
            casacore::Quantity coord1(reference_values(display_axis1), axis_units(display_axis1));

            // Add direction coord(s) converted to angle string (RA/Dec or Lat/Long)
            // Returns Quantity string if not angle type
            std::string coord1angle =
                MakeAngleString(axis_names(display_axis0), reference_values(display_axis0), axis_units(display_axis0));
            std::string coord2angle =
                MakeAngleString(axis_names(display_axis1), reference_values(display_axis1), axis_units(display_axis1));
            std::string format_coords = fmt::format("[{}, {}]", coord1angle, coord2angle);
            // Add reference coords (angle format if possible)
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Image reference coords");
            entry->set_value(format_coords);
            entry->set_entry_type(CARTA::EntryType::STRING);

            bool coord0IsDir(coord0.isConform("deg")), coord1IsDir(coord1.isConform("deg"));
            if (coord0IsDir || coord1IsDir) {
                // Reference coord(s) converted to deg
                std::string ref_coords_deg = ConvertCoordsToDeg(coord0, coord1);
                // Add ref coords in deg
                entry = extended_info.add_computed_entries();
                entry->set_name("Image ref coords (deg)");
                entry->set_value(ref_coords_deg);
                entry->set_entry_type(CARTA::EntryType::STRING);
            }
        }

        if (!increment.empty() && !axis_units.empty()) {
            casacore::Quantity inc0(increment(display_axis0), axis_units(display_axis0));
            casacore::Quantity inc1(increment(display_axis1), axis_units(display_axis1));
            std::string pixel_inc = ConvertIncrementToArcsec(inc0, inc1);
            // Add increment entry
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Pixel increment");
            entry->set_value(pixel_inc);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }

        if (coord_system.hasDirectionCoordinate()) {
            // add RADESYS
            casacore::String direction_frame = casacore::MDirection::showType(coord_system.directionCoordinate().directionType());
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

            auto entry = extended_info.add_computed_entries();
            entry->set_name("Celestial frame");
            entry->set_value(direction_frame);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }
    }

    if (coord_system.hasSpectralAxis()) {
        casacore::String spectral_frame = casacore::MFrequency::showType(coord_system.spectralCoordinate().frequencySystem(true));
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Spectral frame");
        entry->set_value(spectral_frame);
        entry->set_entry_type(CARTA::EntryType::STRING);
        casacore::String vel_doppler = casacore::MDoppler::showType(coord_system.spectralCoordinate().velocityDoppler());
        entry = extended_info.add_computed_entries();
        entry->set_name("Velocity definition");
        entry->set_value(vel_doppler);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    casacore::String brightness_unit(image->units().getName());
    if (!brightness_unit.empty()) {
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Pixel unit");
        entry->set_value(brightness_unit);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    casacore::ImageInfo image_info(image->imageInfo());
    if (image_info.hasBeam()) {
        auto entry = extended_info.add_computed_entries();
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

void FileExtInfoLoader::AddComputedEntriesFromHeaders(
    CARTA::FileInfoExtended& extended_info, const std::vector<int>& display_axes, std::string& radesys) {
    // Convert display axis1 and axis2 header_entries into computed_entries;
    // For images with missing headers or headers which casacore/wcslib cannot process.
    // Axes are 1-based for header names (ctype, cunit, etc.), 0-based for display axes
    casacore::String suffix1(std::to_string(display_axes[0] + 1));
    casacore::String suffix2(std::to_string(display_axes[1] + 1));

    casacore::String ctype1, ctype2, cunit1, cunit2, frame;
    float min_float(std::numeric_limits<float>::min());
    float crval1(min_float), crval2(min_float), crpix1(min_float), crpix2(min_float), cdelt1(min_float), cdelt2(min_float);

    // Quit looking for key when have needed values
    bool need_ctype(true), need_crpix(true), need_crval(true), need_cunit(true), need_cdelt(true), need_frame(true);
    bool need_radesys(radesys.empty());

    for (int i = 0; i < extended_info.header_entries_size(); ++i) {
        auto entry = extended_info.header_entries(i);
        auto entry_name = entry.name();

        // coordinate types
        if (need_ctype && (entry_name.find("CTYPE") != std::string::npos)) {
            if (entry_name.find("CTYPE" + suffix1) != std::string::npos) {
                ctype1 = entry.value();
                if (ctype1.contains("/")) {
                    ctype1 = ctype1.before("/");
                }
                ctype1.trim();
            } else if (entry_name.find("CTYPE" + suffix2) != std::string::npos) {
                ctype2 = entry.value();
                if (ctype2.contains("/")) {
                    ctype2 = ctype2.before("/");
                }
                ctype2.trim();
            }
            if (!ctype1.empty() && !ctype2.empty()) {
                need_ctype = false;
            }
        }

        // reference pixels
        if (need_crpix && (entry_name.find("CRPIX") != std::string::npos)) {
            if (entry_name.find("CRPIX" + suffix1) != std::string::npos) {
                crpix1 = entry.numeric_value();
            } else if (entry_name.find("CRPIX" + suffix2) != std::string::npos) {
                crpix2 = entry.numeric_value();
            }
            if ((crpix1 != min_float) && (crpix2 != min_float)) {
                need_crpix = false;
            }
        }

        // reference values
        if (need_crval && (entry_name.find("CRVAL") != std::string::npos)) {
            if (entry_name.find("CRVAL" + suffix1) != std::string::npos) {
                crval1 = entry.numeric_value();
            } else if (entry_name.find("CRVAL" + suffix2) != std::string::npos) {
                crval2 = entry.numeric_value();
            }
            if ((crval1 != min_float) && (crval2 != min_float)) {
                need_crval = false;
            }
        }

        // coordinate units
        if (need_cunit && (entry_name.find("CUNIT") != std::string::npos)) {
            if (entry_name.find("CUNIT" + suffix1) != std::string::npos) {
                cunit1 = entry.value();
                if (cunit1.contains("/")) {
                    cunit1 = cunit1.before("/");
                }
                cunit1.trim();
                if (cunit1 == "Degrees") { // nonstandard FITS value
                    cunit1 = "deg";
                }
            } else if (entry_name.find("CUNIT" + suffix2) != std::string::npos) {
                cunit2 = entry.value();
                if (cunit2.contains("/")) {
                    cunit2 = cunit2.before("/");
                }
                cunit2.trim();
                if (cunit2 == "Degrees") { // nonstandard FITS value
                    cunit2 = "deg";
                }
            }
            if (!cunit1.empty() && !cunit2.empty()) {
                need_cunit = false;
            }
        }

        // pixel increment
        if (need_cdelt && (entry_name.find("CDELT") != std::string::npos)) {
            if (entry_name.find("CDELT" + suffix1) != std::string::npos) {
                cdelt1 = entry.numeric_value();
            } else if (entry_name.find("CDELT" + suffix2) != std::string::npos) {
                cdelt2 = entry.numeric_value();
            }
            if ((cdelt1 != min_float) && (cdelt2 != min_float)) {
                need_cdelt = false;
            }
        }

        // Celestial frame
        if (need_frame && ((entry_name.find("EQUINOX") != std::string::npos) || (entry_name.find("EPOCH") != std::string::npos))) {
            need_frame = false;
            frame = entry.value();
            if (frame.contains("2000")) {
                frame = "J2000";
            } else if (frame.contains("1950")) {
                frame = "B1950";
            }
        }
    }

    // Set computed entries
    std::string coord_name1, coord_name2, projection;
    if (!need_ctype) {
        GetCoordNames(ctype1, ctype2, radesys, coord_name1, coord_name2, projection);
        std::string coord_type = fmt::format("{}, {}", coord_name1, coord_name2);
        auto comp_entry = extended_info.add_computed_entries();
        comp_entry->set_name("Coordinate type");
        comp_entry->set_value(coord_type);
        comp_entry->set_entry_type(CARTA::EntryType::STRING);
        if (!projection.empty()) {
            auto comp_entry = extended_info.add_computed_entries();
            comp_entry->set_name("Projection");
            comp_entry->set_value(projection);
            comp_entry->set_entry_type(CARTA::EntryType::STRING);
        }
    }
    if (!need_crpix) {
        std::string ref_pix = fmt::format("[{}, {}]", crpix1, crpix2);
        auto comp_entry = extended_info.add_computed_entries();
        comp_entry->set_name("Image reference pixels");
        comp_entry->set_value(ref_pix);
        comp_entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!need_crval) {
        // reference coordinates
        std::string format_coord1 = MakeAngleString(coord_name1, crval1, cunit1);
        std::string format_coord2 = MakeAngleString(coord_name2, crval2, cunit2);
        std::string ref_coords = fmt::format("[{}, {}]", format_coord1, format_coord2);
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Image reference coords");
        entry->set_value(ref_coords);
        entry->set_entry_type(CARTA::EntryType::STRING);

        // reference coordinates in deg
        if (!need_cunit) {
            casacore::Quantity q1(crval1, cunit1);
            casacore::Quantity q2(crval2, cunit2);
            bool q1IsDir(q1.isConform("deg")), q2IsDir(q2.isConform("deg"));
            if (q1IsDir || q2IsDir) {
                // Reference coord(s) converted to deg
                std::string ref_coords_deg = ConvertCoordsToDeg(q1, q2);
                auto comp_entry = extended_info.add_computed_entries();
                comp_entry->set_name("Image ref coords (deg)");
                comp_entry->set_value(ref_coords_deg);
                comp_entry->set_entry_type(CARTA::EntryType::STRING);
            }
        }
    }

    if (!need_radesys || !need_frame) {
        std::string direction_frame;
        if (need_radesys) {
            if (frame == "J2000") {
                direction_frame = "FK5, J2000";
            } else if (frame == "B1950") {
                direction_frame = "FK4, B1950";
            } else if (frame == "ICRS") {
                direction_frame = "ICRS, J2000";
            } else {
                direction_frame = frame;
            }
        } else if (need_frame) {
            direction_frame = radesys;
        } else {
            direction_frame = fmt::format("{}, {}", radesys, frame);
        }
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Celestial frame");
        entry->set_value(direction_frame);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!need_cdelt && !need_cunit) {
        // Increment in arcsec
        casacore::Quantity inc1(cdelt1, cunit1);
        casacore::Quantity inc2(cdelt2, cunit2);
        std::string pixel_inc = ConvertIncrementToArcsec(inc1, inc2);
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Pixel increment");
        entry->set_value(pixel_inc);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }
}

// ***** FITS keyword conversion *****

bool FileExtInfoLoader::GetFitsKwList(casacore::FitsInput& fits_input, unsigned int hdu, casacore::FitsKeywordList& kwlist) {
    // Use casacore HeaderDataUnit to get keyword list
    if (hdu == 0) {
        switch (fits_input.datatype()) {
            case casacore::FITS::FLOAT: {
                casacore::PrimaryArray<casacore::Float> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::DOUBLE: {
                casacore::PrimaryArray<casacore::Double> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::SHORT: {
                casacore::PrimaryArray<casacore::Short> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::LONG: {
                casacore::PrimaryArray<casacore::Int> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::BYTE: {
                casacore::PrimaryArray<casacore::uChar> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            default:
                return false;
        }
    } else {
        switch (fits_input.datatype()) {
            case casacore::FITS::FLOAT: {
                casacore::ImageExtension<casacore::Float> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::DOUBLE: {
                casacore::ImageExtension<casacore::Double> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::SHORT: {
                casacore::ImageExtension<casacore::Short> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::LONG: {
                casacore::ImageExtension<casacore::Int> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::BYTE: {
                casacore::ImageExtension<casacore::uChar> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            default:
                return false;
        }
    }
    return true;
}

std::string FileExtInfoLoader::MakeAngleString(const std::string& type, double val, const std::string& unit) {
    // make coordinate angle string for RA, DEC, GLON, GLAT; else just return "{val} {unit}"
    if (unit.empty()) {
        return fmt::format("{:.6g}", val);
    }

    casacore::MVAngle::formatTypes format;
    if (type == "Right Ascension") {
        format = casacore::MVAngle::TIME;
    } else if ((type == "Declination") || (type.find("Longitude") != std::string::npos) || (type.find("Latitude") != std::string::npos)) {
        format = casacore::MVAngle::ANGLE;
    } else {
        return fmt::format("{:.6g} {}", val, unit);
    }

    casacore::Quantity quant1(val, unit);
    casacore::MVAngle mva(quant1);
    return mva.string(format, 10);
}

std::string FileExtInfoLoader::ConvertCoordsToDeg(const casacore::Quantity& coord0, const casacore::Quantity& coord1) {
    // If possible, convert quantities to degrees. Return formatted string
    casacore::Quantity coord0_deg(coord0), coord1_deg(coord1);
    if (coord0.isConform("deg")) {
        coord0_deg = coord0.get("deg");
    }
    if (coord1.isConform("deg")) {
        coord1_deg = coord1.get("deg");
    }

    return fmt::format("[{}, {}]", coord0_deg, coord1_deg);
}

std::string FileExtInfoLoader::ConvertIncrementToArcsec(const casacore::Quantity& inc0, const casacore::Quantity& inc1) {
    // Convert to arcsec, use unit symbol "
    casacore::String unit0(inc0.getUnit()), unit1(inc1.getUnit());
    casacore::Quantity inc0_arcsec(inc0), inc1_arcsec(inc1);

    if (inc0.isConform("arcsec")) {
        inc0_arcsec = inc0.get("arcsec");
        unit0 = "\"";
    } else {
        unit0 = " " + unit0;
    }

    if (inc1.isConform("arcsec")) {
        inc1_arcsec = inc1.get("arcsec");
        unit1 = "\"";
    } else {
        unit1 = " " + unit1;
    }

    std::string inc_format = "{:.6g}" + unit0 + ", {:.6g}" + unit1;
    return fmt::format(inc_format, inc0_arcsec.getValue(), inc1_arcsec.getValue());
}

void FileExtInfoLoader::GetCoordNames(std::string& ctype1, std::string& ctype2, std::string& radesys, std::string& coord_name1,
    std::string& coord_name2, std::string& projection) {
    // split ctype1 and ctype2 into type and projection
    std::unordered_map<std::string, std::string> names = {
        {"RA", "Right Ascension"}, {"DEC", "Declination"}, {"GLON", "Longitude"}, {"GLAT", "Latitude"}};
    // split ctype1
    std::vector<std::string> type_proj;
    SplitString(ctype1, '-', type_proj);
    coord_name1 = type_proj[0];
    if (radesys.empty() && (coord_name1 == "GLON")) {
        radesys = "GALACTIC";
    }
    if (names.count(coord_name1)) {
        coord_name1 = names[coord_name1];
    }
    size_t split_size(type_proj.size());
    if (split_size > 1) {
        projection = type_proj[split_size - 1];
    }

    // split ctype2
    type_proj.clear();
    SplitString(ctype2, '-', type_proj);
    coord_name2 = type_proj[0];
    if (names.count(coord_name2)) {
        coord_name2 = names[coord_name2];
    }
}
