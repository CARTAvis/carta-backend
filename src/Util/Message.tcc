/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_MESSAGE_TCC_
#define CARTA_BACKEND__UTIL_MESSAGE_TCC_

template <typename T>
T Message::DecodeMessage(std::vector<char>& message) {
    T decoded_message;
    char* event_buf = message.data() + sizeof(carta::EventHeader);
    int event_length = message.size() - sizeof(carta::EventHeader);
    decoded_message.ParseFromArray(event_buf, event_length);
    return decoded_message;
}

#endif // CARTA_BACKEND__UTIL_MESSAGE_TCC_
