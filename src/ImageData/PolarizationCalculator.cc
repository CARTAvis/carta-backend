/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PolarizationCalculator.h"
#include "ImageData/FileLoader.h"
#include "Logger/Logger.h"

using namespace carta;
using PolCalc = PolarizationCalculator;

std::unordered_map<PolCalc::Pol, PolCalc::NodeFunc> PolCalc::_nodes = {{Pol::Ptotal, &PolCalc::PtotalNode},
    {Pol::Plinear, &PolCalc::PlinearNode}, {Pol::PFtotal, &PolCalc::PFtotalNode}, {Pol::PFlinear, &PolCalc::PFlinearNode},
    {Pol::Pangle, &PolCalc::PangleNode}};

std::unordered_map<PolCalc::Pol, casacore::Unit> PolCalc::_units = {
    {Pol::PFtotal, casacore::Unit("%")}, {Pol::PFlinear, casacore::Unit("%")}, {Pol::Pangle, casacore::Unit("deg")}};

std::unordered_map<PolCalc::Pol, PolCalc::Pol> PolCalc::_beam_types = {
    {Pol::Ptotal, Pol::Q}, {Pol::Plinear, Pol::Q}, {Pol::PFtotal, Pol::I}, {Pol::PFlinear, Pol::I}};

PolCalc::PolarizationCalculator(std::shared_ptr<FileLoader> loader) {
    auto original_image = loader->GetImage();

    if (original_image->ndim() < 4) {
        spdlog::info("No computed polarizations available for {}d image.", original_image->ndim());
        return;
    }

    if (loader->GetDims().num_stokes < 2) {
        spdlog::info("No computed polarizations available for image with {} polarizations.", loader->GetDims().num_stokes);
        return;
    }

    // Create slicer for one Stokes cube
    auto stokes_axis = loader->GetAxes().stokes;
    casacore::IPosition end(loader->GetShape());
    end -= 1;
    end(stokes_axis) = 0;
    casacore::IPosition start(end.size(), 0);

    // Get the components
    std::vector<Pol> components;

    // Use mapping from loader or deduce indices in order
    std::map<Pol, int> indices = loader->GetStokesIndices();
    if (indices.empty()) {
        indices = loader->GetDeducedStokesIndices();
    }

    for (const auto& [pol, idx] : indices) {
        components.push_back(pol);
        start(stokes_axis) = idx;
        end(stokes_axis) = idx;
        casacore::Slicer slicer(start, end, casacore::Slicer::endIsLast);
        _component_images[pol] = std::make_shared<casacore::SubImage<float>>(*original_image, slicer);
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
        } catch (const std::out_of_range& e) {
            computed_image->setUnits(original_image->units());
        }

        // Copy image info from original image
        auto info = original_image->imageInfo();
        if (info.hasMultipleBeams()) {
            try {
                // Copy beam from specified component image
                auto beam_type = _beam_types.at(computed_type);
                info.setBeams(_component_images[beam_type]->imageInfo().getBeamSet());
            } catch (const std::out_of_range& e) {
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
        _coord_sys[computed_type] =
            std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(computed_image->coordinates().clone()));
        _available_polarizations.insert(computed_type);
    }
}

PolCalc::ImagePtr PolCalc::GetImage(Pol computed_type) {
    try {
        return _computed_images.at(computed_type);
    } catch (const std::out_of_range& e) {
        spdlog::error("No computed polarization image available for {}.", Stokes::Name(computed_type));
        return nullptr;
    }
}

PolCalc::CoordSysPtr PolCalc::GetCoordSys(Pol computed_type) {
    try {
        return _coord_sys.at(computed_type);
    } catch (const std::out_of_range& e) {
        spdlog::error("No coordinate system available for {}.", Stokes::Name(computed_type));
        return nullptr;
    }
}

PolCalc::Node PolCalc::PtotalNode() {
    casacore::LatticeExprNode lin_node =
        casacore::LatticeExprNode(casacore::pow(*_component_images[Pol::V], 2) + casacore::pow(*_component_images[Pol::U], 2) +
                                  casacore::pow(*_component_images[Pol::Q], 2));
    return casacore::sqrt(lin_node);
}

PolCalc::Node PolCalc::PlinearNode() {
    casacore::LatticeExprNode lin_node =
        casacore::LatticeExprNode(casacore::pow(*_component_images[Pol::U], 2) + casacore::pow(*_component_images[Pol::Q], 2));
    return casacore::sqrt(lin_node);
}

PolCalc::Node PolCalc::PFtotalNode() {
    return 100.0 * PtotalNode() / (*_component_images[Pol::I]);
}

PolCalc::Node PolCalc::PFlinearNode() {
    return 100.0 * PlinearNode() / (*_component_images[Pol::I]);
}

PolCalc::Node PolCalc::PangleNode() {
    return casacore::pa(*_component_images[Pol::U], *_component_images[Pol::Q]);
}
