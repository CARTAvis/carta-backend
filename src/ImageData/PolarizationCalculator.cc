/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PolarizationCalculator.h"
#include "Logger/Logger.h"

using namespace carta;

std::unordered_map<Pol, CalculatorFunc> PolarizationCalculator::_nodes = {
    {Pol::Ptotal, &PolarizationCalculator::PtotalNode},
    {Pol::Plinear, &PolarizationCalculator::PlinearNode},
    {Pol::PFtotal, &PolarizationCalculator::PFtotalNode},
    {Pol::PFlinear, &PolarizationCalculator::PFlinearNode},
    {Pol::Pangle, &PolarizationCalculator::PangleNode}
}

std::unordered_map<Pol, casacore::Unit> PolarizationCalculator::_units = {
    {Pol::PFtotal, casacore::Unit("%")},
    {Pol::PFlinear, casacore::Unit("%")},
    {Pol::Pangle, casacore::Unit("deg")}
}

std::unordered_map<Pol, Pol> PolarizationCalculator::_beam_types = {
    {Pol::Ptotal, Pol::Q},
    {Pol::Plinear, Pol::Q},
    {Pol::PFtotal, Pol::I},
    {Pol::PFlinear, Pol::I}
}

PolarizationCalculator::PolarizationCalculator(std::shared_ptr<FileLoader> loader) : _loader(loader) {}


ImagePtr PolarizationCalculator::GetImage(casacore::Slicer slicer) {
    // TODO: have to make sure slicer passed in here is valid (no placeholders)
    // TODO this slicer should already have been constructed with the appropriate axis order and should be 4D
    
    if (_loader->GetImage()->ndim() < 4) {
        spdlog::error("Invalid image dimensions {}", _loader->GetImage()->ndim());
        return nullptr;
    }
    
    if (slicer.ndim() < 4) {
        spdlog::error("Invalid slicer dimensions {}", slicer.ndim());
        return nullptr;
    }
    
    auto computed_type = Stokes::Get(slicer(_loader->GetAxes().stokes));
    
    if (!Stokes::IsComputed(computed_type)) {
        spdlog::error("Cannot calculate polarization {}", Stokes::Name(computed_type));
        return nullptr;
    }
    
    // Create the image
    auto component_images = GetComponents(computed_type);    
    auto node = std::invoke(_nodes[computed_type], this, component_images);
    auto computed_image = Calculate(node, computed_type);
    
    // Update metadata
    UpdateUnits(computed_image, computed_type);
    UpdateInfo(computed_image, component_images, computed_type);
    UpdateCoordinates(computed_image, computed_type);
    
    return computed_image;
}


ImagePtr PolarizationCalculator::GetComponentImage(casacore::Slicer slicer, int axis, int index) {
    slicer(axis) = index;
    casacore::LCSlicer lc_slicer(slicer);
    casacore::ImageRegion region(lc_slicer);
    // TODO when the calculator call is moved from loader to frame and loader only loads raw data, this should just call a function on the loader
    return std::make_shared<casacore::SubImage<float>>(*(_loader->GetImage()), region);
}


void PolarizationCalculator::GetComponents(Pol computed_type) {
    std::unordered_map<CasaPol, int> stokes_indices;
    
    // Use mapping from loader
    for (const auto &[pol, i]: _loader->GetStokesIndices()) {
        stokes_indices[Stokes::ToCasa(pol)] = i;
    }
    
    // Otherwise assume IQUV (or subset) in order
    if (stokes_indices.empty()) {
        for (int i = 0; i < std::min(_loader.GetDims().num_stokes, 4); i++) {
            stokes_indices[CasaPol::type(i + 1)] = i;
        }
    }
    
    auto stokes_axis = _loader->GetAxes().stokes;
    ImageMap component_images;

    // Get the required components
    for (auto& pol : Stokes::Components(computed_type)) {
        try {
            auto stokes_index = stokes_indices.at(Stokes::ToCasa(pol));
        } catch(const std::out_of_range& e) {
            spdlog::error("This image lacks {}. Cannot compute {}.", Stokes::Name(pol), Stokes::Name(computed_type));
            return nullptr;
        }
        // TODO TODO TODO can this fail?
        component_images[pol] = GetComponentImage(slicer, stokes_axis, stokes_index);
    }
}


ImagePtr PolarizationCalculator::Calculate(Node node, Pol computed_type) {
    // Create the image
    casacore::LatticeExpr<float> lattice_expr(node);
    return std::make_shared<casacore::ImageExpr<float>>(lattice_expr, Stokes::Name(computed_type));
}
    

void PolarizationCalculator::UpdateUnits(ImagePtr computed_image, Pol computed_type) {
    // Set the units
    try {
        computed_image->setUnits(_units.at(computed_type));
    } catch(const std::out_of_range& e) {
        computed_image->setUnits(_loader->GetImage()->units());
    }
}

void PolarizationCalculator::UpdateInfo(ImagePtr computed_image, ImageMap& component_images, Pol computed_type) {
    // Copy image info from original image
    auto info = _loader->GetImage()->imageInfo();
    if (info.hasMultipleBeams()) {
        try {
            // Copy beam from specified component image
            auto beam_type = _beam_types.at(computed_type);
            info.setBeams(component_images[beam_type]->imageInfo().getBeamSet());
        } catch(const std::out_of_range& e) {
            // Multiple beams can vary; don't copy
            info.removeRestoringBeam();
        }
    }
    computed_image->setImageInfo(info);
}

void PolarizationCalculator::UpdateCoordinates(ImagePtr computed_image, Pol computed_type) {
    // Update Stokes coordinate with computed type
    auto coord_sys = computed_image->coordinates();
    int stokes_index = coord_sys.findCoordinate(casacore::Coordinate::STOKES);
    if (stokes_index > -1) {
        casacore::StokesCoordinate stokes({Stokes::ToCasa(computed_type)});
        coord_sys.replaceCoordinate(stokes, stokes_index);
        computed_image->setCoordinateInfo(coord_sys);
    }    
}


Node PolarizationCalculator::PtotalNode(ImageMap& component_images) {
    casacore::LatticeExprNode lin_node = casacore::LatticeExprNode(
        casacore::pow(*component_images[Pol::V], 2) + casacore::pow(*component_images[Pol::U], 2) + casacore::pow(*component_images[Pol::Q], 2));
    return casacore::sqrt(lin_node);
}


Node PolarizationCalculator::PlinearNode(ImageMap& component_images) {
    casacore::LatticeExprNode lin_node =
        casacore::LatticeExprNode(casacore::pow(*component_images[Pol::U], 2) + casacore::pow(*component_images[Pol::Q], 2));
    return casacore::sqrt(lin_node);
}


Node PolarizationCalculator::PFtotalNode(ImageMap& component_images) {
    return 100.0 * TotalPolarizedIntensityNode() / (*component_images[Pol::I]);
}


Node PolarizationCalculator::PFlinearNode(ImageMap& component_images) {
    return 100.0 * PolarizedIntensityNode() / (*component_images[Pol::I]);
}


Node PolarizationCalculator::PangleNode(ImageMap& component_images) {
    return casacore::pa(*component_images[Pol::U], *component_images[Pol::Q]);
}
