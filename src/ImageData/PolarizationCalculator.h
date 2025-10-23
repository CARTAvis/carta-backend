/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_POLARIZATIONCALCULATOR_H_
#define CARTA_SRC_IMAGEDATA_POLARIZATIONCALCULATOR_H_

#include <casacore/images/Images/ImageExpr.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/LRegions/LCSlicer.h>
#include <casacore/lattices/LRegions/RegionType.h>
#include <casacore/lattices/LatticeMath.h>
#include <casacore/lattices/LatticeMath/LatticeStatistics.h>

#include "Util/Image.h"
#include "Util/Stokes.h"

namespace carta {

class FileLoader;

class PolarizationCalculator {
public:
    using ImagePtr = std::shared_ptr<casacore::ImageInterface<float>>;
    using Pol = CARTA::PolarizationType;
    using ImageMap = std::map<Pol, ImagePtr>;
    using CasaPol = casacore::Stokes::StokesTypes;
    using Node = casacore::LatticeExprNode;
    using CoordSysPtr = std::shared_ptr<casacore::CoordinateSystem>;

    typedef Node (PolarizationCalculator::*NodeFunc)();

    PolarizationCalculator(std::weak_ptr<FileLoader> loader_w);
    ImagePtr GetImage(Pol computed_type);
    CoordSysPtr GetCoordSys(Pol computed_type);
    const std::unordered_set<Pol>& AvailablePolarizations() {
        return _available_polarizations;
    }

private:
    Node PtotalNode();
    Node PlinearNode();
    Node PFtotalNode();
    Node PFlinearNode();
    Node PangleNode();

    static std::unordered_map<Pol, NodeFunc> _nodes;
    static std::unordered_map<Pol, casacore::Unit> _units;
    static std::unordered_map<Pol, Pol> _beam_types;

    ImageMap _component_images;
    ImageMap _computed_images;
    std::map<Pol, CoordSysPtr> _coord_sys;
    std::unordered_set<Pol> _available_polarizations;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_POLARIZATIONCALCULATOR_H_
