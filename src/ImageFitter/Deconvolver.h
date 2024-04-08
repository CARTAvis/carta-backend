/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_
#define CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_

#include "Util/Message.h"

#include <casacore/casa/Quanta.h>
#include <casacore/coordinates/Coordinates.h>
#include <casacore/scimath/Mathematics/GaussianBeam.h>

namespace carta {

struct DeconvolutionResult {
    casacore::Quantity center_x;
    casacore::Quantity center_y;
    casacore::Quantity major;
    casacore::Quantity minor;
    casacore::Quantity pa;
    casacore::Quantity major_err;
    casacore::Quantity minor_err;
    casacore::Quantity pa_err;
};

struct GaussianShape {
    casacore::Quantity fwhm_major;
    casacore::Quantity fwhm_minor;
    casacore::Quantity pa;
};

class Deconvolver {
public:
    Deconvolver() = delete;
    Deconvolver(casacore::CoordinateSystem coord_sys, casacore::GaussianBeam beam, double residue_rms);

    ~Deconvolver() = default;

    std::string GetDeconvolutionLog(const std::vector<CARTA::GaussianComponent>& in_gauss_vec);
    bool DoDeconvolution(const CARTA::GaussianComponent& in_gauss, DeconvolutionResult& result);
    bool GetWorldWidthToPixel(const DeconvolutionResult& world_coords, DeconvolutionResult& pixel_coords);
    bool WorldWidthToPixel(casacore::Vector<casacore::Double>& pixel_params, const casacore::Vector<casacore::Quantity>& world_params);
    GaussianShape PixelToWorld(
        casacore::Double cemter_x, casacore::Double center_y, casacore::Double fwhm_x, casacore::Double fwhm_y, casacore::Double pa);

private:
    double CorrelatedOverallSNR(double peak_intensities, casacore::Quantity major, casacore::Quantity minor, double a, double b);
    casacore::MDirection DirectionFromCartesian(casacore::Double center_x, casacore::Double center_y, casacore::Double width,
        casacore::Double pa, const casacore::DirectionCoordinate& dir_coord);
    // This is copied from the CASA code function casa::GaussianDeconvolver::deconvolve
    bool Deconvolve(
        casacore::GaussianBeam& deconvolved_size, const casacore::GaussianBeam& convolved_size, const casacore::GaussianBeam& beam);
    // Get pixel params [major, minor, pa (rad)] from the world params [x, y, major, minor, pa]
    void CalcWorldWidthToPixel(casacore::Vector<casacore::Double>& pixel_params, const casacore::Vector<casacore::Quantity>& world_params,
        const casacore::IPosition& dir_axes);
    // Convert a length and position angle in world units (for a non-coupled coordinate) to pixels. The length is in some 2D plane in the
    // casacore::CoordinateSystem specified by pixel axes.
    casacore::Double CalcAltWorldWidthToPixel(
        const casacore::Double& pa, const casacore::Quantity& length, const casacore::IPosition& pixel_axes);
    casacore::Vector<casacore::Double> WidthToCartesian(const casacore::Quantity& width, const casacore::Quantity& pa,
        const casacore::MDirection& dir_ref, const casacore::Vector<casacore::Double>& pixel_center);
    casacore::Vector<casacore::Double> ToPixel(
        casacore::MDirection md_world, casacore::Quantity major_world, casacore::Quantity minor_world, casacore::Quantity pa_major);

    casacore::CoordinateSystem _coord_sys;
    casacore::GaussianBeam _beam;
    double _residue_rms;
    casacore::Quantity _noise_FWHM;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_
