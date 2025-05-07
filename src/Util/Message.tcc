/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_MESSAGE_TCC_
#define CARTA_SRC_UTIL_MESSAGE_TCC_

struct message_parsing_exception : std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <typename T>
/**
 * @note This function uses a static_assert to ensure that T has a member function `ParseFromArray`.
 *       If T does not have this member function, a compilation error will occur.
 *       The function also throws a runtime error if the parsing fails, providing information about
 *       the session ID and the type of the message.
 */
T Message::DecodeMessage(const char* event_buffer, int event_length) {
    static_assert(std::is_member_function_pointer<decltype(&T::ParseFromArray)>::value,
        "T must have a member function ParseFromArray(const void*, int)");
    T decoded_message;
    if (!decoded_message.ParseFromArray(event_buffer, event_length)) {
        throw message_parsing_exception("Failed to parse message");
    }
    return decoded_message;
}

#endif // CARTA_SRC_UTIL_MESSAGE_TCC_
