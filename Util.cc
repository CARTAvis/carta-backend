#include "Util.h"

using namespace std;

void Log(uint32_t id, const string& log_message) {
    time_t time = chrono::system_clock::to_time_t(chrono::system_clock::now());
    string time_string = ctime(&time);
    time_string = time_string.substr(0, time_string.length() - 1);

    fmt::print("Session {} ({}): {}\n", id, time_string, log_message);
}

void ReadPermissions(const string& filename, unordered_map<string, vector<string>>& permissions_map) {
    ifstream permissions_file(filename);
    if (permissions_file.good()) {
        fmt::print("Reading permissions file\n");
        string line;
        regex comment_regex("\\s*#.*");
        regex folder_regex("\\s*(\\S+):\\s*");
        regex key_regex("\\s*(\\S{4,}|\\*)\\s*");
        string current_folder;
        while (getline(permissions_file, line)) {
            smatch matches;
            if (regex_match(line, comment_regex)) {
                continue;
            } else if (regex_match(line, matches, folder_regex) && matches.size() == 2) {
                current_folder = matches[1].str();
            } else if (current_folder.length() && regex_match(line, matches, key_regex) && matches.size() == 2) {
                string key = matches[1].str();
                permissions_map[current_folder].push_back(key);
            }
        }
    } else {
        fmt::print("Missing permissions file\n");
    }
}

bool CheckRootBaseFolders(string& root, string& base) {
    if (root == "base" && base == "root") {
        fmt::print("ERROR: Must set root or base directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }
    if (root == "base")
        root = base;
    if (base == "root")
        base = root;

    // check root
    casacore::File root_folder(root);
    if (!(root_folder.exists() && root_folder.isDirectory(true) && root_folder.isReadable() && root_folder.isExecutable())) {
        fmt::print("ERROR: Invalid root directory, does not exist or is not a readable directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }
    // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
    try {
        root = root_folder.path().resolvedName(); // fails on root folder /
    } catch (casacore::AipsError& err) {
        try {
            root = root_folder.path().absoluteName();
        } catch (casacore::AipsError& err) {
            fmt::print(err.getMesg());
        }
        if (root.empty())
            root = "/";
    }
    // check base
    casacore::File base_folder(base);
    if (!(base_folder.exists() && base_folder.isDirectory(true) && base_folder.isReadable() && base_folder.isExecutable())) {
        fmt::print("ERROR: Invalid base directory, does not exist or is not a readable directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }
    // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
    try {
        base = base_folder.path().resolvedName(); // fails on root folder /
    } catch (casacore::AipsError& err) {
        try {
            base = base_folder.path().absoluteName();
        } catch (casacore::AipsError& err) {
            fmt::print(err.getMesg());
        }
        if (base.empty())
            base = "/";
    }
    // check if base is same as or subdir of root
    if (base != root) {
        bool is_subdirectory(false);
        casacore::Path base_path(base);
        casacore::String parent_string(base_path.dirName()), root_string(root);
        if (parent_string == root_string)
            is_subdirectory = true;
        while (!is_subdirectory && (parent_string != root_string)) { // navigate up directory tree
            base_path = casacore::Path(parent_string);
            parent_string = base_path.dirName();
            if (parent_string == root_string) {
                is_subdirectory = true;
            } else if (parent_string == "/") {
                break;
            }
        }
        if (!is_subdirectory) {
            fmt::print("ERROR: Base {} must be a subdirectory of root {}. Exiting carta.\n", base, root);
            return false;
        }
    }
    return true;
}

void SplitString(std::string& input, char delim, std::vector<std::string>& parts) {
    // util to split input string into parts by delimiter
    parts.clear();
    std::stringstream ss(input);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) {
            parts.push_back(item);
        }
    }
}
