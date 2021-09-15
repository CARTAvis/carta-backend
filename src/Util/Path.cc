#include "Path.h"

bool CheckFolderPaths(string& top_level_string, string& starting_string) {
    // TODO: is this code needed at all? Was it a weird workaround?
    {
        if (top_level_string == "base" && starting_string == "root") {
            spdlog::critical("Must set top level or starting directory. Exiting carta.");
            return false;
        }
        if (top_level_string == "base")
            top_level_string = starting_string;
        if (starting_string == "root")
            starting_string = top_level_string;
    }
    // TODO: Migrate to std::filesystem
    // check top level
    casacore::File top_level_folder(top_level_string);
    if (!(top_level_folder.exists() && top_level_folder.isDirectory(true) && top_level_folder.isReadable() &&
            top_level_folder.isExecutable())) {
        spdlog::critical("Invalid top level directory, does not exist or is not a readable directory. Exiting carta.");
        return false;
    }
    // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
    try {
        top_level_string = top_level_folder.path().resolvedName(); // fails on top level folder /
    } catch (casacore::AipsError& err) {
        try {
            top_level_string = top_level_folder.path().absoluteName();
        } catch (casacore::AipsError& err) {
            spdlog::error(err.getMesg());
        }
        if (top_level_string.empty())
            top_level_string = "/";
    }
    // check starting folder
    casacore::File starting_folder(starting_string);
    if (!(starting_folder.exists() && starting_folder.isDirectory(true) && starting_folder.isReadable() &&
            starting_folder.isExecutable())) {
        spdlog::warn("Invalid starting directory, using the provided top level directory instead.");
        starting_string = top_level_string;
    } else {
        // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
        try {
            starting_string = starting_folder.path().resolvedName(); // fails on top level folder /
        } catch (casacore::AipsError& err) {
            try {
                starting_string = starting_folder.path().absoluteName();
            } catch (casacore::AipsError& err) {
                spdlog::error(err.getMesg());
            }
            if (starting_string.empty())
                starting_string = "/";
        }
    }
    bool is_subdirectory = IsSubdirectory(starting_string, top_level_string);
    if (!is_subdirectory) {
        spdlog::critical("Starting {} must be a subdirectory of top level {}. Exiting carta.", starting_string, top_level_string);
        return false;
    }
    return true;
}

bool IsSubdirectory(string folder, string top_folder) {
    folder = casacore::Path(folder).absoluteName();
    top_folder = casacore::Path(top_folder).absoluteName();
    if (top_folder.empty()) {
        return true;
    }
    if (folder == top_folder) {
        return true;
    }
    casacore::Path folder_path(folder);
    string parent_string(folder_path.dirName());
    if (parent_string == top_folder) {
        return true;
    }
    while (parent_string != top_folder) { // navigate up directory tree
        folder_path = casacore::Path(parent_string);
        parent_string = folder_path.dirName();
        if (parent_string == top_folder) {
            return true;
        } else if (parent_string == "/") {
            break;
        }
    }
    return false;
}

casacore::String GetResolvedFilename(const string& root_dir, const string& directory, const string& file) {
    // Given directory (relative to root folder) and file, return resolved path and filename
    // (absolute pathname with symlinks resolved)
    casacore::String resolved_filename;
    casacore::Path root_path(root_dir);
    root_path.append(directory);
    root_path.append(file);
    casacore::File cc_file(root_path);
    if (cc_file.exists()) {
        try {
            resolved_filename = cc_file.path().resolvedName();
        } catch (casacore::AipsError& err) {
            // return empty string
        }
    }
    return resolved_filename;
}

bool FindExecutablePath(string& path) {
    char path_buffer[PATH_MAX + 1];
#ifdef __APPLE__
    uint32_t len = sizeof(path_buffer);

    if (_NSGetExecutablePath(path_buffer, &len) != 0) {
        return false;
    }
#else
    const int len = int(readlink("/proc/self/exe", path_buffer, PATH_MAX));

    if (len == -1) {
        return false;
    }

    path_buffer[len] = 0;
#endif
    path = path_buffer;
    return true;
}

int GetNumItems(const string& path) {
    try {
        int counter = 0;
        auto it = fs::directory_iterator(path);
        for (const auto f : it) {
            counter++;
        }
        return counter;
    } catch (exception) {
        return -1;
    }
}

// quick alternative to bp::search_path that allows us to remove
// boost:filesystem dependency
fs::path SearchPath(std::string filename) {
    std::string path(std::getenv("PATH"));
    std::vector<std::string> path_strings;
    SplitString(path, ':', path_strings);
    for (auto& p : path_strings) {
        fs::path base_path(p);
        base_path /= filename;
        if (fs::exists(base_path)) {
            return base_path;
        }
    }
    return fs::path();
}
