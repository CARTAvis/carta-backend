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


PolarizationCalculator::PolarizationCalculator(std::shared_ptr<FileLoader> loader) : _loader(loader) {
    auto original_image = _loader->GetImage();
    
    if (original_image->ndim() < 4) {
        spdlog::info("No computed polarizations available for {}d image.", original_image->ndim());
        return computed_images;
    }
    
    // Create slicer for one Stokes cube
    auto stokes_axis = _loader->GetAxes().stokes;
    casacore::IPosition end(_loader->GetShape());
    end -= 1;
    end(stokes_axis) = 0;
    casacore::IPosition start(end.size(), 0);
    casacore::Slicer slicer(start, end, casacore::Slicer::endIsLast);
    
    // Get the components
    std::vector<Pol> components;
    
    // Use mapping from loader
    // TODO make sure that this is properly populated in the loader, instead of repeating the same guessing in multiple places
    for (const auto &[pol, idx]: _loader->GetStokesIndices()) {
        components.push_back(pol);        
        
        slicer(axis) = idx;
        casacore::LCSlicer lc_slicer(slicer);
        casacore::ImageRegion region(lc_slicer);
        _component_images[pol] = std::make_shared<casacore::SubImage<float>>(*original_image, region);
    }
    
    // Get the computed images
    for (auto& computed_type : Stokes::Computable(components)) {
        // Create the image
        auto node = std::invoke(_nodes[computed_type], this);
        casacore::LatticeExpr<float> lattice_expr(node);
        auto computed_image = std::make_shared<casacore::ImageExpr<float>>(lattice_expr, Stokes::Name(computed_type));

        // Set the units
        try {
            computed_image->setUnits(_units.at(computed_type));
        } catch(const std::out_of_range& e) {
            computed_image->setUnits(original_image->units());
        }

        // Copy image info from original image
        auto info = original_image->imageInfo();
        if (info.hasMultipleBeams()) {
            try {
                // Copy beam from specified component image
                auto beam_type = _beam_types.at(computed_type);
                info.setBeams(_component_images[beam_type]->imageInfo().getBeamSet());
            } catch(const std::out_of_range& e) {
                // Multiple beams can vary; don't copy
                info.removeRestoringBeam();
            }
        }
        computed_image->setImageInfo(info);

        // Update Stokes coordinate with computed type
        auto coord_sys = computed_image->coordinates();
        int stokes_index = coord_sys.findCoordinate(casacore::Coordinate::STOKES);
        if (stokes_index > -1) {
            casacore::StokesCoordinate stokes({Stokes::ToCasa(computed_type)});
            coord_sys.replaceCoordinate(stokes, stokes_index);
            computed_image->setCoordinateInfo(coord_sys);
        }
        
        // Store computed image expression
        _computed_images[computed_type] = computed_image;
    }
}

ImagePtr PolarizationCalculator::GetImage(Pol computed_type) {
    try {
        return _computed_images.at(computed_type);
    } catch(const std::out_of_range& e) {
        spdlog::error("No computed polarization available for {}.", Stokes::Description(computed_type));
        return nullptr;
    }
}


Node PolarizationCalculator::PtotalNode() {
    casacore::LatticeExprNode lin_node = casacore::LatticeExprNode(
        casacore::pow(*_component_images[Pol::V], 2) + casacore::pow(*_component_images[Pol::U], 2) + casacore::pow(*_component_images[Pol::Q], 2));
    return casacore::sqrt(lin_node);
}


Node PolarizationCalculator::PlinearNode() {
    casacore::LatticeExprNode lin_node =
        casacore::LatticeExprNode(casacore::pow(*_component_images[Pol::U], 2) + casacore::pow(*_component_images[Pol::Q], 2));
    return casacore::sqrt(lin_node);
}


Node PolarizationCalculator::PFtotalNode() {
    return 100.0 * TotalPolarizedIntensityNode() / (*_component_images[Pol::I]);
}


Node PolarizationCalculator::PFlinearNode() {
    return 100.0 * PolarizedIntensityNode() / (*_component_images[Pol::I]);
}


Node PolarizationCalculator::PangleNode() {
    return casacore::pa(*_component_images[Pol::U], *_component_images[Pol::Q]);
}
