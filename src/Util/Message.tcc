/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_MESSAGE_TCC_
#define CARTA_SRC_UTIL_MESSAGE_TCC_


/**
 * @details This function extracts the payload from a given message buffer, ignoring the event header,
 * and deserializes it into an object of template type `T` using `ParseFromArray`.
 */
template <typename T>
T Message::DecodeMessage(std::vector<char>& message) {
    T decoded_message;
    char* event_buf = message.data() + sizeof(carta::EventHeader);
    int event_length = message.size() - sizeof(carta::EventHeader);
    decoded_message.ParseFromArray(event_buf, event_length);
    return decoded_message;
}

#endif // CARTA_SRC_UTIL_MESSAGE_TCC_
