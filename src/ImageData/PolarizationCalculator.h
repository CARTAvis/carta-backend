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
    
    
// TODO constructor should just take a loader; GetImage should get the calculated type from the slicer
    
class PolarizationCalculator {
public:
    using ImagePtr = std::shared_ptr<casacore::ImageInterface<float>>;
    using Pol = CARTA::PolarizationType;
    using ImageMap = std::unordered_map<Pol, ImagePtr>;
    using CasaPol = casacore::Stokes::StokesTypes;
    using Node = casacore::LatticeExprNode;
    typedef Node (PolarizationCalculator::*NodeFunc)(ImageMap&);
    PolarizationCalculator(std::shared_ptr<FileLoader> loader);
    ImagePtr GetImage(casacore::Slicer slicer);
private:
    ImagePtr GetComponentImage(casacore::Slicer slicer, int axis, int index);
    ImageMap GetComponents(Pol computed_type);
    ImagePtr Calculate(Node node, Pol computed_type);
    void UpdateUnits(ImagePtr computed_image, Pol computed_type);
    void UpdateInfo(ImagePtr computed_image, ImageMap& component_images, Pol computed_type);
    void UpdateCoordinates(ImagePtr computed_image, Pol computed_type);
    Node PtotalNode(ImageMap& component_images);
    Node PlinearNode(ImageMap& component_images);
    Node PFtotalNode(ImageMap& component_images);
    Node PFlinearNode(ImageMap& component_images);
    Node PangleNode(ImageMap& component_images);
    std::shared_ptr<FileLoader> _loader;
    static std::unordered_map<Pol, NodeFunc> _nodes;
    static std::unordered_map<Pol, casacore::Unit> _units;
    static std::unordered_map<Pol, Pol> _beam_types;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_POLARIZATIONCALCULATOR_H_
