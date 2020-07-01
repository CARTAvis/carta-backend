#include "CasacRegionManager.h"

#include <imageanalysis/IO/ParameterParser.h>
#include <imageanalysis/ImageAnalysis/ImageMetaData.h>

#include <memory>

using namespace casacore;

namespace carta {

CasacRegionManager::CasacRegionManager(const CoordinateSystem& csys) : _csys(new CoordinateSystem(csys)) {}

const CoordinateSystem& CasacRegionManager::GetCoordSys() const {
    return *_csys;
}

vector<uInt> CasacRegionManager::SetPolarizationRanges(String& specification) const {
    vector<uInt> results(0);
    CoordinateSystem csys = GetCoordSys();
    if (!csys.hasPolarizationCoordinate()) {
        return results;
    }

    specification.trim();
    specification.ltrim('[');
    specification.rtrim(']');
    specification.upcase();

    // Split on commas and semicolons in the past for polarization specification.
    Vector<String> stokes_range = stringToVector(specification, Regex("[,;]"));

    // Get all defined stokes names
    Vector<String> all_stokes_names = Stokes::allNames(false);
    uInt all_stokes_names_size = all_stokes_names.size();
    Vector<uInt> stokes_name_lengths(all_stokes_names_size);
    for (uInt i = 0; i < all_stokes_names_size; i++) {
        stokes_name_lengths[i] = all_stokes_names[i].length();
    }

    uInt* stokes_name_lengths_ptr = stokes_name_lengths.data();
    Vector<uInt> index(all_stokes_names_size);
    Sort sorter;
    sorter.sortKey(stokes_name_lengths_ptr, TpUInt, 0, Sort::Descending);
    sorter.sort(index, all_stokes_names_size);

    Vector<String> sorted_names(all_stokes_names_size);
    for (uInt i = 0; i < all_stokes_names_size; i++) {
        sorted_names[i] = all_stokes_names[index[i]];
        sorted_names[i].upcase();
    }

    // Check are the stokes names from users requirements match the stokes names in the pocket
    for (uInt i = 0; i < stokes_range.size(); i++) {
        String stokes_part = stokes_range[i];
        stokes_part.trim();
        Vector<String>::iterator iter = sorted_names.begin();

        while (iter != sorted_names.end() && !stokes_part.empty()) {
            if (stokes_part.startsWith(*iter)) {
                Int stokes_pix = csys.stokesPixelNumber(*iter);
                results.push_back(stokes_pix);
                results.push_back(stokes_pix);
                stokes_part = stokes_part.substr(iter->length());

                if (!stokes_part.empty()) {
                    // Reset the iterator to start over at the beginning of the list for the next specified polarization
                    iter = sorted_names.begin();
                }
            } else {
                iter++;
            }
        }

        if (!stokes_part.empty()) {
            std::cerr << "Sub string " << stokes_part << " in stokes specification part " << stokes_range[i]
                      << " does not match a known polarization!" << std::endl;
        }
    }

    uInt selected_num;
    return casa::ParameterParser::consolidateAndOrderRanges(selected_num, results);
}

Bool CasacRegionManager::Supports2DBox() const {
    Bool ok = true;
    const CoordinateSystem& csys = GetCoordSys();
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

Record CasacRegionManager::MakeRegion(String& stokes, const String& channels, const IPosition& image_shape) {
    uInt selected_channels_num;
    vector<uInt> channels_range = SetSpectralRanges(channels, selected_channels_num, image_shape);
    vector<uInt> stokes_range = SetPolarizationRanges(stokes);

    vector<Double> box_corners;
    const CoordinateSystem& csys = GetCoordSys();
    if (Supports2DBox()) {
        if (csys.hasDirectionCoordinate() || csys.hasLinearCoordinate()) {
            Vector<Int> direction_axis;
            if (csys.hasDirectionCoordinate()) {
                direction_axis = csys.directionAxesNumbers();
            } else {
                direction_axis = csys.linearAxesNumbers();
            }

            Vector<Int> direction_shape(2);
            direction_shape[0] = image_shape[direction_axis[0]];
            direction_shape[1] = image_shape[direction_axis[1]];

            box_corners.resize(4);
            box_corners[0] = 0;                      // bl: x
            box_corners[1] = 0;                      // bl: y
            box_corners[2] = direction_shape[0] - 1; // tr: x
            box_corners[3] = direction_shape[1] - 1; // tr: y
        }
    } else {
        std::cerr << "Can not make a 2D box region!" << std::endl;
    }

    return MakeRegion(box_corners, channels_range, stokes_range, image_shape).toRecord("");
}

ImageRegion CasacRegionManager::MakeRegion(
    const vector<Double>& box_corners, const vector<uInt>& channels_range, const vector<uInt>& stokes_range, IPosition image_shape) const {
    Vector<Double> blc(image_shape.nelements(), 0);
    Vector<Double> trc(image_shape.nelements(), 0);

    const CoordinateSystem csys = GetCoordSys();
    Vector<Int> direction_axis = csys.directionAxesNumbers();
    vector<Int> linear_axis = csys.linearAxesNumbers().tovector();

    // Stupidly, sometimes the values returned by linearAxesNumbers can be less than 0
    // This needs to be fixed in the implementation of that method
    vector<Int>::iterator iter = linear_axis.begin();
    vector<Int>::iterator end = linear_axis.end();
    while (iter != end) {
        if (*iter < 0) {
            iter = linear_axis.erase(iter);
        }
        ++iter;
    }

    Int spectral_axis = csys.spectralAxisNumber();
    Int polarization_axis = csys.polarizationAxisNumber();

    size_t corners_size = 2;
    Vector<Double> x_corners(corners_size);
    Vector<Double> y_corners(corners_size);

    for (uInt i = 0; i < corners_size; i++) {
        Double x = box_corners[2 * i];
        Double y = box_corners[2 * i + 1];
        x_corners[i] = x;
        y_corners[i] = y;
    }

    Vector<Double> x_corners_ext(corners_size, 0);
    Vector<Double> y_corners_ext(corners_size, 0);
    Vector<Double> stokes_corners_ext(corners_size, 0);
    Vector<Double> channels_corners_ext(corners_size, 0);

    if (csys.hasDirectionCoordinate() || csys.hasLinearCoordinate()) {
        x_corners_ext[0] = x_corners[0];
        x_corners_ext[1] = x_corners[1];
        y_corners_ext[0] = y_corners[0];
        y_corners_ext[1] = y_corners[1];
    }

    if (csys.hasPolarizationCoordinate()) {
        stokes_corners_ext[0] = (Double)stokes_range[0];
        stokes_corners_ext[1] = (Double)stokes_range[1];
    }

    if (csys.hasSpectralAxis()) {
        channels_corners_ext[0] = (Double)channels_range[0];
        channels_corners_ext[1] = (Double)channels_range[1];
    }

    map<uInt, Vector<Double> > axis_corner_map; // axis index v.s. its range as a vector
    for (uInt axis = 0; axis < csys.nPixelAxes(); axis++) {
        if ((direction_axis.size() > 1 && (Int)axis == direction_axis[0]) ||
            (!csys.hasDirectionCoordinate() && linear_axis.size() > 1 && (Int)axis == linear_axis[0])) {
            axis_corner_map[axis] = x_corners_ext;

        } else if ((direction_axis.size() > 1 && (Int)axis == direction_axis[1]) ||
                   (!csys.hasDirectionCoordinate() && linear_axis.size() > 1 && (Int)axis == linear_axis[1])) {
            axis_corner_map[axis] = y_corners_ext;

        } else if ((Int)axis == spectral_axis) {
            axis_corner_map[axis] = channels_corners_ext;

        } else if ((Int)axis == polarization_axis) {
            axis_corner_map[axis] = stokes_corners_ext;

        } else {
            Vector<Double> range(corners_size, 0);
            range[1] = image_shape[axis] - 1;
            axis_corner_map[axis] = range;
        }
    }

    for (uInt axis = 0; axis < csys.nPixelAxes(); axis++) {
        blc(axis) = axis_corner_map[axis][0];
        trc(axis) = axis_corner_map[axis][1];
    }

    LCBox lc_box(blc, trc, image_shape);
    WCBox wc_box(lc_box, csys);
    ImageRegion image_region(wc_box);

    return image_region;
}

vector<uInt> CasacRegionManager::InitSpectralRanges(uInt& selected_channels_num, const IPosition& image_shape) const {
    vector<uInt> ranges(0);
    if (!GetCoordSys().hasSpectralAxis()) {
        selected_channels_num = 0;
        return ranges;
    }

    uInt channels_num = image_shape[GetCoordSys().spectralAxisNumber()];
    ranges.push_back(0);
    ranges.push_back(channels_num - 1);
    selected_channels_num = channels_num;

    return ranges;
}

vector<uInt> CasacRegionManager::SetSpectralRanges(String specification, uInt& selected_channels_num, const IPosition& image_shape) const {
    specification.trim();
    String chans = specification;
    chans.upcase();

    if (chans.empty()) {
        return InitSpectralRanges(selected_channels_num, image_shape);

    } else if (!GetCoordSys().hasSpectralAxis()) {
        std::cerr << "Channel specification is not empty but the coordinate system has no spectral axis!"
                  << "Channel specification will be ignored." << std::endl;
        selected_channels_num = 0;
        return vector<uInt>(0);

    } else {
        uInt channels_num = image_shape[GetCoordSys().spectralAxisNumber()];
        return casa::ParameterParser::spectralRangesFromChans(selected_channels_num, specification, channels_num);
    }
}

} // namespace carta
