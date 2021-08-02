/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_SRC_SESSIONMANAGER_PROGRAMSETTINGSHELPERS_H_
#define CARTA_BACKEND_SRC_SESSIONMANAGER_PROGRAMSETTINGSHELPERS_H_

#include <string>

namespace carta {
// Helpers
namespace ProgramSettingsHelpers {

void SaveLastDirectory(const std::string&, const std::string&);
void GetLastDirectory(const std::string&, std::string&);

} // namespace ProgramSettingsHelpers
} // namespace carta
#endif // CARTA_BACKEND_SRC_SESSIONMANAGER_PROGRAMSETTINGSHELPERS_H_
