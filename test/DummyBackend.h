/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__DUMMYBACKEND_H_
#define CARTA_BACKEND__DUMMYBACKEND_H_

#include "Session.h"

class DummyBackend {
public:
    DummyBackend();
    ~DummyBackend();

    void ReceiveMessage(CARTA::RegisterViewer message);
    void ReceiveMessage(CARTA::CloseFile message);
    void ReceiveMessage(CARTA::OpenFile message);
    void ReceiveMessage(CARTA::SetImageChannels message);

    void CheckMessagesQueue(std::function<void(tbb::concurrent_queue<std::pair<std::vector<char>, bool>> out_msgs)> callback);

private:
    FileListHandler* _file_list_handler;
    Session* _session;
};

#endif // CARTA_BACKEND__DUMMYBACKEND_H_
