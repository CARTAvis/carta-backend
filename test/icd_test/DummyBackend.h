/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_ICD_TEST_DUMMYBACKEND_H_
#define CARTA_BACKEND_ICD_TEST_DUMMYBACKEND_H_

#include "Session.h"

class DummyBackend {
public:
    DummyBackend();
    ~DummyBackend();

    void ReceiveMessage(CARTA::RegisterViewer message);
    void ReceiveMessage(CARTA::ResumeSession message);
    void ReceiveMessage(CARTA::SetImageChannels message);
    void ReceiveMessage(CARTA::SetCursor message);
    void ReceiveMessage(CARTA::SetHistogramRequirements message);
    void ReceiveMessage(CARTA::CloseFile message);
    void ReceiveMessage(CARTA::StartAnimation message);
    void ReceiveMessage(CARTA::StopAnimation message);
    void ReceiveMessage(CARTA::AnimationFlowControl message);
    void ReceiveMessage(CARTA::FileInfoRequest message);
    void ReceiveMessage(CARTA::OpenFile message);
    void ReceiveMessage(CARTA::AddRequiredTiles message);
    void ReceiveMessage(CARTA::RegionFileInfoRequest message);
    void ReceiveMessage(CARTA::ImportRegion message);
    void ReceiveMessage(CARTA::ExportRegion message);
    void ReceiveMessage(CARTA::SetContourParameters message);
    void ReceiveMessage(CARTA::ScriptingResponse message);
    void ReceiveMessage(CARTA::SetRegion message);
    void ReceiveMessage(CARTA::RemoveRegion message);
    void ReceiveMessage(CARTA::SetSpectralRequirements message);
    void ReceiveMessage(CARTA::CatalogFileInfoRequest message);
    void ReceiveMessage(CARTA::OpenCatalogFile message);
    void ReceiveMessage(CARTA::CloseCatalogFile message);
    void ReceiveMessage(CARTA::CatalogFilterRequest message);
    void ReceiveMessage(CARTA::StopMomentCalc message);
    void ReceiveMessage(CARTA::SaveFile message);
    void ReceiveMessage(CARTA::SplataloguePing message);
    void ReceiveMessage(CARTA::SpectralLineRequest message);
    void ReceiveMessage(CARTA::ConcatStokesFiles message);
    void ReceiveMessage(CARTA::StopFileList message);
    void ReceiveMessage(CARTA::SetSpatialRequirements message);
    void ReceiveMessage(CARTA::SetStatsRequirements message);
    void ReceiveMessage(CARTA::MomentRequest message);
    void ReceiveMessage(CARTA::FileListRequest message);
    void ReceiveMessage(CARTA::RegionListRequest message);
    void ReceiveMessage(CARTA::CatalogListRequest message);

    bool TryPopMessagesQueue(std::pair<std::vector<char>, bool>& message);
    void ClearMessagesQueue();

private:
    FileListHandler* _file_list_handler;
    Session* _session;
};

#endif // CARTA_BACKEND_ICD_TEST_DUMMYBACKEND_H_
