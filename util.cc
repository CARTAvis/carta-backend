#include "util.h"

void log(const string& uuid, const string& logMessage) {
    // Shorten uuids a bit for brevity
    auto uuidString = uuid;
    auto lastHash = uuidString.find_last_of('-');
    if (lastHash != string::npos) {
        uuidString = uuidString.substr(lastHash + 1);
    }
    time_t time = chrono::system_clock::to_time_t(chrono::system_clock::now());
    string timeString = ctime(&time);
    timeString = timeString.substr(0, timeString.length() - 1);

    fmt::print("Session {} ({}): {}\n", uuidString, timeString, logMessage);
}

void readPermissions(string filename, unordered_map<string, vector<string>>& permissionsMap) {
    ifstream permissionsFile(filename);
    if (permissionsFile.good()) {
        fmt::print("Reading permissions file\n");
        string line;
        regex commentRegex("\\s*#.*");
        regex folderRegex("\\s*(\\S+):\\s*");
        regex keyRegex("\\s*(\\S{4,}|\\*)\\s*");
        string currentFolder;
        while (getline(permissionsFile, line)) {
            smatch matches;
            if (regex_match(line, commentRegex)) {
                continue;
            } else if (regex_match(line, matches, folderRegex) && matches.size() == 2) {
                currentFolder = matches[1].str();
            } else if (currentFolder.length() && regex_match(line, matches, keyRegex) && matches.size() == 2) {
                string key = matches[1].str();
                permissionsMap[currentFolder].push_back(key);
            }
        }
    } else {
        fmt::print("Missing permissions file\n");
    }
}

bool checkRootBaseFolders(string& root, string& base) {
    if (root=="base" && base == "root") {
        fmt::print("ERROR: Must set root or base directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }
    if (root=="base") root = base;
    if (base=="root") base = root;

    // check root
    casacore::File rootFolder(root);
    if (!(rootFolder.exists() && rootFolder.isDirectory(true) &&
          rootFolder.isReadable() && rootFolder.isExecutable())) {
        fmt::print("ERROR: Invalid root directory, does not exist or is not a readable directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }
    // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
    try {
        root = rootFolder.path().resolvedName(); // fails on root folder /
    } catch (casacore::AipsError& err) {
        try {
            root = rootFolder.path().absoluteName();
        } catch (casacore::AipsError& err) {
            fmt::print(err.getMesg());
        }
        if (root.empty()) root = "/";
    }
    // check base
    casacore::File baseFolder(base);
    if (!(baseFolder.exists() && baseFolder.isDirectory(true) &&
          baseFolder.isReadable() && baseFolder.isExecutable())) {
        fmt::print("ERROR: Invalid base directory, does not exist or is not a readable directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }
    // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
    try {
        base = baseFolder.path().resolvedName(); // fails on root folder /
    } catch (casacore::AipsError& err) {
        try {
            base = baseFolder.path().absoluteName();
        } catch (casacore::AipsError& err) {
            fmt::print(err.getMesg());
        }
        if (base.empty()) base = "/";
    }
    // check if base is same as or subdir of root
    if (base != root) {
        bool isSubdirectory(false);
        casacore::Path basePath(base);
        casacore::String parentString(basePath.dirName()), rootString(root);
	if (parentString == rootString)
            isSubdirectory = true;
        while (!isSubdirectory && (parentString != rootString)) {  // navigate up directory tree
            basePath = casacore::Path(parentString);
            parentString = basePath.dirName();
            if (parentString == rootString) {
                isSubdirectory = true;
	    } else if (parentString == "/") {
                break;
            }
        }
        if (!isSubdirectory) {
            fmt::print("ERROR: Base {} must be a subdirectory of root {}. Exiting carta.\n", base, root);
            return false;
        }
    }
    return true;
}
