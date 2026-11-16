/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "FitsUtil.h"

#include <spdlog/spdlog.h>
#include <iomanip>
#include <regex>
#include <sstream>

/**
 * @details This function uses regular expressions to extract the major axis (BMAJ),
 * minor axis (BMIN), and position angle (BPA) from an AIPS-style history
 * beam header string. It handles two common formats: one using "Beam ="
 * notation and another using "BMAJ=", "BMIN=", and "BPA=" notation.
 *
 * @note Units are normalized to "deg" when "degrees" is found in the header.
 *
 * @warning If the header format does not match expected patterns, the function
 *          logs a debug message and returns `false`.
 */
bool ParseHistoryBeamHeader(std::string& header, std::string& bmaj, std::string& bmin, std::string& bpa) {
    // Parse AIPS beam header using regex_match.
    // Returns false if regex failed, else true with beam value-unit strings.
    std::regex r;
    std::cmatch results;
    bool matched(false);

    if (header.find("Beam") != std::string::npos) {
        // Example:
        // HISTORY RESTOR Beam =  2.000E+00 x  1.800E+00 arcsec, pa =  8.000E+01 degrees
        r = R"/(.*Beam\s*=\s*([\d.Ee+-]+)\s*x\s*([\d.Ee+-]+)\s*([A-Za-z]*)\s*,*\s*pa\s*=\s*([\d.Ee+-]+)\s*([A-Za-z]*).*)/";
        matched = std::regex_match(header.c_str(), results, r);
    } else if (header.find("BMAJ") != std::string::npos) {
        // Examples:
        // HISTORY CONVL BMAJ=  5.0000 BMIN=  5.0000 BPA=   0.0/Output beam
        // HISTORY AIPS   CLEAN BMAJ=  1.3889E-03 BMIN=  1.3889E-03 BPA=   0.00
        r = R"/(.*BMAJ\s*=\s*([\d.Ee+-]+)\s*BMIN\s*=\s*([\d.Ee+-]+)\s*BPA\s*=\s*([\d.Ee+-]+).*)/";
        matched = std::regex_match(header.c_str(), results, r);
    }

    if (matched) {
        if (results.size() == 4) {
            // 0 matched expr, 1 bmaj, 2 bmin, 3 bpa. Use default unit.
            bmaj = results.str(1) + "deg";
            bmin = results.str(2) + "deg";
            bpa = results.str(3) + "deg";
        } else if (results.size() == 6) {
            // 0 matched expr, 1 bmaj, 2 bmin, 3 unit, 4 bpa, 5 unit
            auto unit3 = results.str(3);
            auto unit5 = results.str(5);
            bmaj = results.str(1) + (unit3 == "degrees" ? "deg" : unit3);
            bmin = results.str(2) + (unit3 == "degrees" ? "deg" : unit3);
            bpa = results.str(4) + (unit5 == "degrees" ? "deg" : unit5);
        } else {
            spdlog::debug("Unable to set history beam header {}: unexpected format.", header);
            matched = false;
        }
    }

    return matched;
}
