#ifndef CARTA_BACKEND__UTIL_PATH_H_
#define CARTA_BACKEND__UTIL_PATH_H_

#include "FileSystem.h"

bool FindExecutablePath(std::string& path);
bool IsSubdirectory(std::string folder, std::string top_folder);
casacore::String GetResolvedFilename(const std::string& root_dir, const std::string& directory, const std::string& file);
bool CheckFolderPaths(std::string& top_level_string, std::string& starting_string);
int GetNumItems(const std::string& path);
fs::path SearchPath(std::string filename);

#endif // CARTA_BACKEND__UTIL_PATH_H_
