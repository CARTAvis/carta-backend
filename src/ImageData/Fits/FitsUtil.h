/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_FITSUTIL_H_
#define CARTA_SRC_IMAGEDATA_FITSUTIL_H_

#include <string>
#include <vector>

/**
 * @brief Parses an AIPS-style beam header to extract beam parameters with regular expression.
 *
 * @param[in] header A string reference to the history beam header string to be parsed.
 * @param[out] bmaj A string reference to the extracted major axis value with its unit.
 * @param[out] bmin A string reference to the extracted minor axis value with its unit.
 * @param[out] bpa A string reference to the extracted position angle value with its unit.
 *
 * @return `true` if the header was successfully parsed and values were extracted,
 *         otherwise `false`.
 */
bool ParseHistoryBeamHeader(std::string& header, std::string& bmaj, std::string& bmin, std::string& bpa);

#endif // CARTA_SRC_IMAGEDATA_FITSUTIL_H_
