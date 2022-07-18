/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_TEST_MOCKFILELOADER_H
#define CARTA_TEST_MOCKFILELOADER_H

#include "gmock/gmock.h"

#include "ImageData/FileLoader.h"

namespace carta {

class MockFileLoader : public FileLoader {
public:
    MOCK_METHOD(bool, CanOpenFile, (std::string & error), (override));
    MOCK_METHOD(void, OpenFile, (const std::string& hdu), (override));
    MOCK_METHOD(bool, HasData, (FileInfo::Data ds), (const, override));
    MOCK_METHOD(void, CloseImageIfUpdated, (), (override));
    MOCK_METHOD(ImageRef, GetImage, (bool check_data_type), (override));
    MOCK_METHOD(casacore::DataType, GetDataType, (), (override));
    MOCK_METHOD(bool, IsComplexDataType, (), (override));
    MOCK_METHOD(ImageRef, GetStokesImage, (const StokesSource& stokes_source), (override));
    MOCK_METHOD(bool, GetBeams, (std::vector<CARTA::Beam> & beams, std::string& error), (override));
    MOCK_METHOD(casacore::IPosition, GetShape, (), (override));
    MOCK_METHOD(std::shared_ptr<casacore::CoordinateSystem>, GetCoordinateSystem, (const StokesSource& stokes_source), (override));
    MOCK_METHOD(bool, FindCoordinateAxes,
        (casacore::IPosition & shape, int& spectral_axis, int& z_axis, int& stokes_axis, std::string& message), (override));
    MOCK_METHOD(std::vector<int>, GetRenderAxes, (), (override));
    MOCK_METHOD(bool, GetSlice, (casacore::Array<float> & data, const StokesSlicer& stokes_slicer), (override));
    MOCK_METHOD(bool, GetSubImage, (const StokesSlicer& stokes_slicer, casacore::SubImage<float>& sub_image), (override));
    MOCK_METHOD(bool, GetSubImage, (const StokesRegion& stokes_region, casacore::SubImage<float>& sub_image), (override));
    MOCK_METHOD(bool, GetSubImage,
        (const casacore::Slicer& slicer, const casacore::LattRegionHolder& region, casacore::SubImage<float>& sub_image), (override));
    MOCK_METHOD(void, LoadImageStats, (bool load_percentiles), (override));
    MOCK_METHOD(FileInfo::ImageStats&, GetImageStats, (int current_stokes, int channel), (override));
    MOCK_METHOD(bool, GetCursorSpectralData,
        (std::vector<float> & data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex), (override));
    MOCK_METHOD(bool, UseRegionSpectralData, (const casacore::IPosition& region_shape, std::mutex& image_mutex), (override));
    MOCK_METHOD(bool, GetRegionSpectralData,
        (int region_id, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin,
            std::mutex& image_mutex, (std::map<CARTA::StatsType, std::vector<double>>)&results, float& progress),
        (override));
    MOCK_METHOD(bool, GetDownsampledRasterData,
        (std::vector<float> & data, int z, int stokes, CARTA::ImageBounds& bounds, int mip, std::mutex& image_mutex), (override));
    MOCK_METHOD(bool, GetChunk,
        (std::vector<float> & data, int& data_width, int& data_height, int min_x, int min_y, int z, int stokes, std::mutex& image_mutex),
        (override));
    MOCK_METHOD(bool, HasMip, (int mip), (const, override));
    MOCK_METHOD(bool, UseTileCache, (), (const, override));
    MOCK_METHOD(std::string, GetFileName, (), (override));
    MOCK_METHOD((std::unordered_map<CARTA::PolarizationType, int>), GetStokesIndices, (), (override));
    MOCK_METHOD(bool, ImageUpdated, (), (override));
    MOCK_METHOD(void, SetStokesCrval, (float stokes_crval), (override));
    MOCK_METHOD(void, SetStokesCrpix, (float stokes_crpix), (override));
    MOCK_METHOD(void, SetStokesCdelt, (int stokes_cdelt), (override));
    MOCK_METHOD(bool, GetStokesTypeIndex, (const CARTA::PolarizationType& stokes_type, int& stokes_index), (override));
    MOCK_METHOD(bool, SaveFile, (const CARTA::FileType type, const std::string& output_filename, std::string& message), (override));
    MOCK_METHOD(const casacore::IPosition, GetStatsDataShape, (FileInfo::Data ds), (override));
    MOCK_METHOD(std::unique_ptr<casacore::ArrayBase>, GetStatsData, (FileInfo::Data ds), (override));
    MOCK_METHOD(void, LoadStats2DBasic, (FileInfo::Data ds), (override));
    MOCK_METHOD(void, LoadStats2DHist, (), (override));
    MOCK_METHOD(void, LoadStats2DPercent, (), (override));
    MOCK_METHOD(void, LoadStats3DBasic, (FileInfo::Data ds), (override));
    MOCK_METHOD(void, LoadStats3DHist, (), (override));
    MOCK_METHOD(void, LoadStats3DPercent, (), (override));
    MOCK_METHOD(double, CalculateBeamArea, (), (override));
};

} // namespace carta

#endif // CARTA_TEST_MOCKFILELOADER_H
