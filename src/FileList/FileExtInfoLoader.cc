/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# FileExtInfoLoader.cc: fill FileInfoExtended for all supported file types

#include "FileExtInfoLoader.h"

#include <fmt/format.h>
#include <fmt/ostream.h> // not needed here but *must* include before miriad.h

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
                    bool prefer_velocity(false), optical_velocity(false);
                    bool prefer_wavelength(false), air_wavelength(false);
                    if (coord_sys.hasSpectralAxis()) { // prefer spectral axis native type
                        casacore::SpectralCoordinate::SpecType native_type;
                        if (image->imageType() == "CartaMiriadImage") { // workaround to get correct native type
                            CartaMiriadImage* miriad_image = static_cast<CartaMiriadImage*>(image);
                            native_type = miriad_image->NativeType();
                        } else {
                            native_type = coord_sys.spectralCoordinate().nativeType();
                        }
                        switch (native_type) {
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

                                // Check doppler type; oddly, native type can be VOPT but doppler is RADIO--?
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

                casacore::String radesys; // for computed entry

                // Create header entry for each FitsKeyword
                fhi.kw.first(); // go to first card
                casacore::FitsKeyword* fkw = fhi.kw.next();
                while (fkw) {
                    casacore::String name(fkw->name());
                    casacore::String comment(fkw->comm());

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

                    if (name != "END") {
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

                                    if (name == "RADESYS") {
                                        radesys = header_string; // save for computed_entries
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

                int spectral_axis, stokes_axis;
                if (_loader->FindCoordinateAxes(image_shape, spectral_axis, stokes_axis, message)) {
                    AddShapeEntries(extended_info, image_shape, spectral_axis, stokes_axis);
                    AddComputedEntries(extended_info, image, radesys, use_fits_header);
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

void FileExtInfoLoader::AddShapeEntries(
    CARTA::FileInfoExtended& extended_info, const casacore::IPosition& shape, int chan_axis, int stokes_axis) {
    // Set fields/header entries for shape: dimensions, width, height, depth, stokes
    int num_dims(shape.size());
    extended_info.set_dimensions(num_dims);
    extended_info.set_width(shape(0));
    extended_info.set_height(shape(1));
    if (num_dims == 2) { // 2D
        extended_info.set_depth(1);
        extended_info.set_stokes(1);
    } else if (num_dims == 3) { // 3D
        extended_info.set_depth(shape(2));
        extended_info.set_stokes(1);
    } else { // 4D
        extended_info.set_depth(shape(chan_axis));
        extended_info.set_stokes(shape(stokes_axis));
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

void FileExtInfoLoader::AddComputedEntries(
    CARTA::FileInfoExtended& extended_info, casacore::ImageInterface<float>* image, casacore::String& radesys, bool use_fits_header) {
    casacore::CoordinateSystem coord_system(image->coordinates());
    if (use_fits_header) { // image has dummy coordinate system; use header_entries for frontend use
        AddComputedEntriesFromHeaders(extended_info, radesys);
    } else { // use image coordinate system
        // add computed_entries to extended info (ensures the proper order in file browser)
        casacore::Vector<casacore::String> axis_names = coord_system.worldAxisNames();
        casacore::Vector<casacore::String> axis_units = coord_system.worldAxisUnits();
        casacore::Vector<casacore::Double> reference_pixels = coord_system.referencePixel();
        casacore::Vector<casacore::Double> reference_values = coord_system.referenceValue();
        casacore::Vector<casacore::Double> increment = coord_system.increment();

        if (!axis_names.empty()) {
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Coordinate type");
            std::string coord_type = fmt::format("{}, {}", axis_names(0), axis_names(1));
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
            std::string ref_pix = fmt::format("[{}, {}]", reference_pixels(0) + 1.0, reference_pixels(1) + 1.0);
            entry->set_value(ref_pix);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }

        if (!axis_names.empty() && !reference_values.empty() && !axis_units.empty()) {
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Image reference coords");
            std::string format_coord1 = MakeAngleString(axis_names(0), reference_values(0), axis_units(0));
            std::string format_coord2 = MakeAngleString(axis_names(1), reference_values(1), axis_units(1));
            std::string format_coords = fmt::format("[{}, {}]", format_coord1, format_coord2);
            entry->set_value(format_coords);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }

        if (!reference_values.empty() && !axis_units.empty()) {
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Image ref coords (deg)");
            casacore::Quantity coord0(reference_values(0), axis_units(0));
            casacore::Quantity coord1(reference_values(1), axis_units(1));
            std::string ref_coords = fmt::format("[{}, {}]", coord0.get("deg"), coord1.get("deg"));
            entry->set_value(ref_coords);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }

        if (!increment.empty() && !axis_units.empty()) {
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Pixel increment");
            casacore::Quantity inc0(increment(0), axis_units(0));
            casacore::Quantity inc1(increment(1), axis_units(1));
            std::string pixel_inc = fmt::format("{:g}\", {:g}\"", inc0.getValue("arcsec"), inc1.getValue("arcsec"));
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

void FileExtInfoLoader::AddComputedEntriesFromHeaders(CARTA::FileInfoExtended& extended_info, std::string& radesys) {
    // Convert axis1 and axis2 header_entries into computed_entries;
    // kludge for missing headers or headers which casacore/wcslib cannot process
    casacore::String ctype1, ctype2, cunit1, cunit2, frame;
    float crval1(0.0), crval2(0.0), crpix1(0.0), crpix2(0.0), cdelt1(0.0), cdelt2(0.0);
    bool need_ctype(true), need_crpix(true), need_crval(true), need_cunit(true), need_cdelt(true), need_frame(true);
    bool need_radesys(radesys.empty());
    for (int i = 0; i < extended_info.header_entries_size(); ++i) {
        auto entry = extended_info.header_entries(i);
        // entry.name(), entry.value() (string), entry.numeric_value() (double), entry.entry_type() (CARTA::EntryType)
        auto entry_name = entry.name();

        // coordinate types
        if (need_ctype && (entry_name.find("CTYPE") != std::string::npos)) {
            if (entry_name == "CTYPE1") {
                ctype1 = entry.value();
                if (ctype1.contains("/")) {
                    ctype1 = ctype1.before("/");
                }
                ctype1.trim();
            } else if (entry_name == "CTYPE2") {
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
            if (entry_name.find("CRPIX1") != std::string::npos) {
                crpix1 = entry.numeric_value();
            } else if (entry_name.find("CRPIX2") != std::string::npos) {
                crpix2 = entry.numeric_value();
            }
            if ((crpix1 != 0.0) && (crpix2 != 0.0)) {
                need_crpix = false;
            }
        }

        // reference values
        if (need_crval && (entry_name.find("CRVAL") != std::string::npos)) {
            if (entry_name == "CRVAL1") {
                crval1 = entry.numeric_value();
            } else if (entry_name == "CRVAL2") {
                crval2 = entry.numeric_value();
            }
            if ((crval1 != 0.0) && (crval2 != 0.0)) {
                need_crval = false;
            }
        }

        // coordinate units
        if (need_cunit && (entry_name.find("CUNIT") != std::string::npos)) {
            if (entry_name.find("CUNIT1") != std::string::npos) {
                cunit1 = entry.value();
                if (cunit1.contains("/")) {
                    cunit1 = cunit1.before("/");
                }
                cunit1.trim();
                if (cunit1 == "Degrees") { // nonstandard FITS value
                    cunit1 = "deg";
                }
            } else if (entry_name.find("CUNIT2") != std::string::npos) {
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
            if (entry_name == "CDELT1") {
                cdelt1 = entry.numeric_value();
            } else if (entry_name == "CDELT2") {
                cdelt2 = entry.numeric_value();
            }
            if ((cdelt1 != 0.0) && (cdelt2 != 0.0)) {
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
        entry->set_name("Image reference coordinates");
        entry->set_value(ref_coords);
        entry->set_entry_type(CARTA::EntryType::STRING);

        // reference coordinates in deg
        if (!need_cunit) {
            casacore::Quantity q1(crval1, cunit1);
            casacore::Quantity q2(crval2, cunit2);
            std::string ref_coords_deg = fmt::format("[{}, {}]", q1.getValue("deg"), q2.getValue("deg"));
            auto comp_entry = extended_info.add_computed_entries();
            comp_entry->set_name("Image reference coords (deg)");
            comp_entry->set_value(ref_coords_deg);
            comp_entry->set_entry_type(CARTA::EntryType::STRING);
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
        casacore::Quantity inc1(cdelt1, cunit1);
        casacore::Quantity inc2(cdelt2, cunit2);
        std::string pixel_inc = fmt::format("{:.3f}\", {:.3f}\"", inc1.getValue("arcsec"), inc2.getValue("arcsec"));
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
