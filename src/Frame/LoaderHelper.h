/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_FRAME_LOADERHELPER_H_
#define CARTA_SRC_FRAME_LOADERHELPER_H_

#include "ImageData/FileLoader.h"
#include "ImageState.h"

#include <memory>
#include <string>

namespace carta {

class LoaderHelper {
public:
    LoaderHelper(std::shared_ptr<FileLoader> loader, std::shared_ptr<ImageState> status, std::mutex& image_mutex);

    StokesSlicer GetImageSlicer(const AxisRange& x_range, const AxisRange& y_range, const AxisRange& z_range, int stokes);
    bool GetSlicerData(const StokesSlicer& stokes_slicer, float* data);
    bool GetStokesTypeIndex(const string& coordinate, int& stokes_index, bool mute_err_msg);
    bool TileCacheAvailable();
    double GetBeamArea();
    bool FillCubeImageCache(std::map<int, std::unique_ptr<float[]>>& stokes_data);
    bool FillChannelImageCache(std::unique_ptr<float[]>& channel_data, int z, int stokes);
    bool FillStokesImageCache(std::unique_ptr<float[]>& stokes_data, int stokes);
    bool IsValid() const;
    void SetImageChannels(int z, int stokes);

    casacore::IPosition OriginalImageShape() const;
    size_t Width() const;
    size_t Height() const;
    size_t Depth() const;
    size_t NumStokes() const;
    int XAxis() const;
    int YAxis() const;
    int ZAxis() const;
    int SpectralAxis() const;
    int StokesAxis() const;
    int CurrentZ() const;
    int CurrentStokes() const;
    bool IsCurrentChannel(int z, int stokes) const;
    bool IsCurrentStokes(int stokes) const;

private:
    bool _valid;
    std::shared_ptr<FileLoader> _loader;
    std::shared_ptr<ImageState> _image_state;
    std::mutex& _image_mutex; // Reference of the image mutex for the file loader
};

} // namespace carta

#endif // CARTA_SRC_FRAME_LOADERHELPER_H_
