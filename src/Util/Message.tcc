/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_MESSAGE_TCC_
#define CARTA_SRC_UTIL_MESSAGE_TCC_

// #include <spdlog/fmt/fmt.h>
// #include <typeinfo>
// #include "Session/Session.h"

template <typename T>
/**
 * Decodes a message from a buffer of characters into an object of type T.
 *
 * @tparam T The type of the object to decode the message into. T must have a member function
 *           `ParseFromArray(const void*, int)` to parse the data.
 * @param session A vector of characters containing the serialized message. The message is expected
 *                to start with a header of type `carta::EventHeader`, followed by the payload.
 * @param message
 * @return The decoded message of type T.
 */
T Message::DecodeMessage(uint32_t session_id, const char* event_buffer, int event_length, const carta::EventHeader& head) {
    static_assert(std::is_member_function_pointer<decltype(&T::ParseFromArray)>::value,
        "T must have a member function ParseFromArray(const void*, int)");
    T decoded_message;
    if (!decoded_message.ParseFromArray(event_buffer, event_length)) {
        throw std::runtime_error(
            fmt::format("Error parsing message for event type {} in session {}: Failed to parse message.", session_id, typeid(T).name()));
    }
    return decoded_message;
}

#endif // CARTA_SRC_UTIL_MESSAGE_TCC_
