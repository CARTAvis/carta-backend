//
// From the original file: "casa/code/imageanalysis/Regions/CasacRegionManager.cc"
//
#include "CasacRegionManager.h"

#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/OS/File.h>
#include <casacore/casa/namespace.h>
#include <casacore/images/Regions/ImageRegion.h>
#include <casacore/images/Regions/WCBox.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCSlicer.h>
#include <casacore/measures/Measures/Stokes.h>
#include <imageanalysis/IO/ParameterParser.h>
#include <imageanalysis/ImageAnalysis/ImageMetaData.h>

#include <memory>

using namespace casacore;

namespace carta { //# name space carta begins

const String CasacRegionManager::ALL = "ALL";

CasacRegionManager::CasacRegionManager(const CoordinateSystem& csys, bool verbose) : RegionManager(csys), _verbose(verbose) {}

CasacRegionManager::~CasacRegionManager() {}

vector<uInt> CasacRegionManager::SetPolarizationRanges(String& specification) const {
    vector<uInt> ranges(0);
    CoordinateSystem csys = getcoordsys();
    if (!csys.hasPolarizationCoordinate()) {
        return ranges;
    }

    specification.trim();
    specification.ltrim('[');
    specification.rtrim(']');
    specification.upcase();

    // Split on commas and semicolons in the past for polarization specification.
    Vector<String> parts = stringToVector(specification, Regex("[,;]"));

    // Get all defined stokes names
    Vector<String> pol_names = Stokes::allNames(false);
    uInt names_size = pol_names.size();
    Vector<uInt> name_lengths(names_size);
    for (uInt i = 0; i < names_size; i++) {
        name_lengths[i] = pol_names[i].length();
    }

    uInt* length_data = name_lengths.data();
    Vector<uInt> index(names_size);
    Sort sorter;
    sorter.sortKey(length_data, TpUInt, 0, Sort::Descending);
    sorter.sort(index, names_size);

    Vector<String> sorted_names(names_size);
    for (uInt i = 0; i < names_size; i++) {
        sorted_names[i] = pol_names[index[i]];
        sorted_names[i].upcase();
    }

    // Check are the stokes names from users requirements match the stokes names in the pocket
    for (uInt i = 0; i < parts.size(); i++) {
        String part = parts[i];
        part.trim();
        Vector<String>::iterator iter = sorted_names.begin();

        while (iter != sorted_names.end() && !part.empty()) {
            if (part.startsWith(*iter)) {
                if (_verbose) {
                    std::cout << "Use the stoke: " << *iter << std::endl;
                }
                Int stokes_pix = csys.stokesPixelNumber(*iter);
                ranges.push_back(stokes_pix);
                ranges.push_back(stokes_pix);
                part = part.substr(iter->length());

                if (!part.empty()) {
                    // Reset the iterator to start over at the beginning of the list for the next specified polarization
                    iter = sorted_names.begin();
                }
            } else {
                iter++;
            }
        }

        if (!part.empty()) {
            std::cerr << "Sub string " << part << " in stokes specification part " << parts[i] << " does not match a known polarization!"
                      << std::endl;
        }
    }

    uInt selected_num;
    return casa::ParameterParser::consolidateAndOrderRanges(selected_num, ranges);
}

Bool CasacRegionManager::Supports2DBox() const {
    Bool ok = true;
    const CoordinateSystem& csys = getcoordsys();
    Vector<Int> axes;
    if (csys.hasDirectionCoordinate()) {
        axes = csys.directionAxesNumbers();
    } else if (csys.hasLinearCoordinate()) {
        axes = csys.linearAxesNumbers();
    } else {
        ok = false;
    }

    if (ok) {
        uInt good_num = 0;
        for (uInt i = 0; i < axes.size(); i++) {
            if (axes[i] >= 0) {
                good_num++;
            }
        }
        if (good_num != 2) {
            ok = false;
        }
    } else {
        std::cerr << "This image does not have a 2-D direction or linear coordinate!" << std::endl;
    }

    return ok;
}

Record CasacRegionManager::fromBCS(String& diagnostics, uInt& selected_channels_num, String& stokes, const Record* const& region_ptr,
    const String& region_name, const String& chans, const StokesControl stokes_control, const String& box, const IPosition& image_shape,
    const String& imageName, Bool verbose) {
    Record region_record;
    region_record = fromBCS(diagnostics, selected_channels_num, stokes, chans, stokes_control, box, image_shape).toRecord("");

    return region_record;
}

ImageRegion CasacRegionManager::fromBCS(String& diagnostics, uInt& selected_channels_num, String& stokes, const String& chans,
    const StokesControl stokes_control, const String& box, const IPosition& image_shape) const {
    vector<uInt> chan_end_pts = SetSpectralRanges(chans, selected_channels_num, image_shape);

    const CoordinateSystem& csys = getcoordsys();
    Int pol_axis_num = csys.polarizationAxisNumber();
    uInt total_polarizations_num = pol_axis_num >= 0 ? image_shape[pol_axis_num] : 0;
    String first_stokes = pol_axis_num >= 0 ? csys.stokesAtPixel(0) : "";

    vector<uInt> pol_end_pts = SetPolarizationRanges(stokes);

    vector<Double> box_corners;
    if (Supports2DBox()) {
        if (csys.hasDirectionCoordinate() || csys.hasLinearCoordinate()) {
            Vector<Int> dir_axis_numbers;
            if (csys.hasDirectionCoordinate()) {
                dir_axis_numbers = csys.directionAxesNumbers();
            } else {
                dir_axis_numbers = csys.linearAxesNumbers();
            }

            Vector<Int> dir_shape(2);
            dir_shape[0] = image_shape[dir_axis_numbers[0]];
            dir_shape[1] = image_shape[dir_axis_numbers[1]];

            box_corners.resize(4);
            box_corners[0] = 0;                // bl: x
            box_corners[1] = 0;                // bl: y
            box_corners[2] = dir_shape[0] - 1; // tr: x
            box_corners[3] = dir_shape[1] - 1; // tr: y
        }
    }

    return _fromBCS(diagnostics, box_corners, chan_end_pts, pol_end_pts, image_shape);
}

ImageRegion CasacRegionManager::_fromBCS(String& diagnostics, const vector<Double>& box_corners, const vector<uInt>& chan_end_pts,
    const vector<uInt>& pol_end_pts, const IPosition image_shape) const {
    Vector<Double> blc(image_shape.nelements(), 0);
    Vector<Double> trc(image_shape.nelements(), 0);

    const CoordinateSystem csys = getcoordsys();
    Vector<Int> direction_axis_numbers = csys.directionAxesNumbers();
    vector<Int> linear_axis_numbers = csys.linearAxesNumbers().tovector();

    // Stupidly, sometimes the values returned by linearAxesNumbers can be less than 0
    // This needs to be fixed in the implementation of that method
    vector<Int>::iterator iter = linear_axis_numbers.begin();
    vector<Int>::iterator end = linear_axis_numbers.end();
    while (iter != end) {
        if (*iter < 0) {
            iter = linear_axis_numbers.erase(iter);
        }
        ++iter;
    }

    Int spectral_axis_number = csys.spectralAxisNumber();
    Int polarization_axis_number = csys.polarizationAxisNumber();

    Vector<Double> x_corners(box_corners.size() / 2);
    Vector<Double> y_corners(x_corners.size());

    for (uInt i = 0; i < x_corners.size(); i++) {
        Double x = box_corners[2 * i];
        Double y = box_corners[2 * i + 1];

        // if (x < 0 || y < 0) {
        //    *_getLog() << "blc in box spec is less than 0" << LogIO::EXCEPTION;
        //}

        // if (csys.hasDirectionCoordinate()) {
        //    if (x >= imShape[direction_axis_numbers[0]] || y >= imShape[direction_axis_numbers[1]]) {
        //        *_getLog() << "dAxisNum0=" << direction_axis_numbers[0] << " dAxisNum1=" << direction_axis_numbers[1];
        //        *_getLog() << "x=" << x << " imShape[0]=" << imShape[direction_axis_numbers[0]] << " y=" << y
        //                   << " imShape[1]=" << imShape[direction_axis_numbers[1]] << LogIO::POST;
        //        *_getLog() << "trc in box spec is greater than or equal to number "
        //                   << "of direction coordinate pixels in the image" << LogIO::EXCEPTION;
        //    }
        //} else if (csys.hasLinearCoordinate() && (x >= imShape[linear_axis_numbers[0]] || y >= imShape[linear_axis_numbers[1]])) {
        //    *_getLog() << "trc in box spec is greater than or equal to number "
        //               << "of linear coordinate pixels in the image" << LogIO::EXCEPTION;
        //}

        x_corners[i] = x;
        y_corners[i] = y;
    }

    Vector<Double> pol_end_pts_double(pol_end_pts.size());
    for (uInt i = 0; i < pol_end_pts.size(); ++i) {
        pol_end_pts_double[i] = (Double)pol_end_pts[i];
    }

    Bool csys_supports_2d_box = Supports2DBox();
    uInt regions_num = 1;
    if (csys_supports_2d_box) {
        if (csys.hasDirectionCoordinate()) {
            regions_num *= box_corners.size() / 4;
        }
        if (csys.hasLinearCoordinate()) {
            regions_num *= box_corners.size() / 4;
        }
    }
    if (csys.hasPolarizationCoordinate()) {
        regions_num *= pol_end_pts.size() / 2;
    }
    if (csys.hasSpectralAxis()) {
        regions_num *= chan_end_pts.size() / 2;
    }

    Vector<Double> ext_x_corners(2 * regions_num, 0);
    Vector<Double> ext_y_corners(2 * regions_num, 0);
    Vector<Double> ext_pol_end_pts(2 * regions_num, 0);
    Vector<Double> ext_chan_end_pts(2 * regions_num, 0);

    uInt count = 0;

    if (csys_supports_2d_box) {
        for (uInt i = 0; i < max(uInt(1), x_corners.size() / 2); i++) {
            for (uInt j = 0; j < max((uInt)1, pol_end_pts.size() / 2); j++) {
                for (uInt k = 0; k < max(uInt(1), chan_end_pts.size() / 2); k++) {
                    if (csys.hasDirectionCoordinate() || csys.hasLinearCoordinate()) {
                        ext_x_corners[2 * count] = x_corners[2 * i];
                        ext_x_corners[2 * count + 1] = x_corners[2 * i + 1];
                        ext_y_corners[2 * count] = y_corners[2 * i];
                        ext_y_corners[2 * count + 1] = y_corners[2 * i + 1];
                    }
                    if (csys.hasPolarizationCoordinate()) {
                        ext_pol_end_pts[2 * count] = pol_end_pts_double[2 * j];
                        ext_pol_end_pts[2 * count + 1] = pol_end_pts_double[2 * j + 1];
                    }
                    if (csys.hasSpectralAxis()) {
                        ext_chan_end_pts[2 * count] = chan_end_pts[2 * k];
                        ext_chan_end_pts[2 * count + 1] = chan_end_pts[2 * k + 1];
                    }
                    count++;
                }
            }
        }
    } else {
        // here we have neither a direction nor linear coordinate with two
        // pixel axes which are greater than 0
        for (uInt j = 0; j < max((uInt)1, pol_end_pts.size() / 2); j++) {
            for (uInt k = 0; k < max(uInt(1), chan_end_pts.size() / 2); k++) {
                if (csys.hasPolarizationCoordinate()) {
                    ext_pol_end_pts[2 * count] = pol_end_pts_double[2 * j];
                    ext_pol_end_pts[2 * count + 1] = pol_end_pts_double[2 * j + 1];
                }
                if (csys.hasSpectralAxis()) {
                    ext_chan_end_pts[2 * count] = chan_end_pts[2 * k];
                    ext_chan_end_pts[2 * count + 1] = chan_end_pts[2 * k + 1];
                }
                count++;
            }
        }
    }

    map<uInt, Vector<Double> > axis_corner_map;

    for (uInt i = 0; i < regions_num; i++) {
        for (uInt axis_num = 0; axis_num < csys.nPixelAxes(); axis_num++) {
            if ((direction_axis_numbers.size() > 1 && (Int)axis_num == direction_axis_numbers[0]) ||
                (!csys.hasDirectionCoordinate() && linear_axis_numbers.size() > 1 && (Int)axis_num == linear_axis_numbers[0])) {
                axis_corner_map[axis_num] = ext_x_corners;

            } else if ((direction_axis_numbers.size() > 1 && (Int)axis_num == direction_axis_numbers[1]) ||
                       (!csys.hasDirectionCoordinate() && linear_axis_numbers.size() > 1 && (Int)axis_num == linear_axis_numbers[1])) {
                axis_corner_map[axis_num] = ext_y_corners;

            } else if ((Int)axis_num == spectral_axis_number) {
                axis_corner_map[axis_num] = ext_chan_end_pts;

            } else if ((Int)axis_num == polarization_axis_number) {
                axis_corner_map[axis_num] = ext_pol_end_pts;

            } else {
                Vector<Double> range(2, 0);
                range[1] = image_shape[axis_num] - 1;
                axis_corner_map[axis_num] = range;
            }
        }
    }

    // Set results
    ImageRegion image_region;
    for (uInt i = 0; i < regions_num; i++) {
        for (uInt axis = 0; axis < csys.nPixelAxes(); axis++) {
            blc(axis) = axis_corner_map[axis][2 * i];
            trc(axis) = axis_corner_map[axis][2 * i + 1];
        }

        LCBox lc_box(blc, trc, image_shape);
        WCBox wc_box(lc_box, csys);
        ImageRegion this_region(wc_box);
        image_region = (i == 0) ? this_region : image_region = *(doUnion(image_region, this_region));
    }

    return image_region;
}

vector<uInt> CasacRegionManager::InitSpectralRanges(uInt& selected_channels_num, const IPosition& image_shape) const {
    vector<uInt> ranges(0);
    if (!getcoordsys().hasSpectralAxis()) {
        selected_channels_num = 0;
        return ranges;
    }

    uInt channels_num = image_shape[getcoordsys().spectralAxisNumber()];
    ranges.push_back(0);
    ranges.push_back(channels_num - 1);
    selected_channels_num = channels_num;

    return ranges;
}

vector<uInt> CasacRegionManager::SetSpectralRanges(String specification, uInt& selected_channels_num, const IPosition& image_shape) const {
    specification.trim();
    String chans = specification;
    chans.upcase();

    if (chans.empty() || chans == ALL) {
        return InitSpectralRanges(selected_channels_num, image_shape);

    } else if (!getcoordsys().hasSpectralAxis()) {
        std::cerr << "Channel specification is not empty but the coordinate system has no spectral axis!"
                  << "Channel specification will be ignored." << std::endl;
        selected_channels_num = 0;
        return vector<uInt>(0);

    } else {
        uInt channels_num = image_shape[getcoordsys().spectralAxisNumber()];
        return casa::ParameterParser::spectralRangesFromChans(selected_channels_num, specification, channels_num);
    }
}

} // namespace carta
