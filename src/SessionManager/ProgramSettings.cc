/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ProgramSettings.h"

#include <iostream>

#include <cxxopts.hpp>

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>

namespace fs = std::filesystem;
#endif

using namespace std;

namespace carta {

template <class T>
void applyOptionalArgument(T& val, const string& argument_name, const cxxopts::ParseResult& results) {
    if (results.count(argument_name)) {
        val = results[argument_name].as<T>();
    }
}

ProgramSettings::ProgramSettings(int argc, char** argv) {
    cxxopts::Options options("CARTA", "CARTA Backend");
    // clang-format off
    // clang-format doesn't like this chain of calls
    options.add_options()("h,help", "Print usage")
        ("v,version", "Print version")
        ("verbosity",
         "Display verbose logging from level (0: off, 1: critical, 2: error, 3: warning, 4: info, 5: debug, 6: trace)",
         cxxopts::value<int>()->default_value(to_string(verbosity)), "<level>")
        ("no_log", "Do not output to a log file", cxxopts::value<bool>())
        ("no_http", "Disable CARTA frontend HTTP server", cxxopts::value<bool>())
        ("no_browser", "Prevent the frontend from automatically opening in the default browser on startup", cxxopts::value<bool>())
        ("host", "Only listen on the specified interface (IP address or hostname)", cxxopts::value<string>()->default_value(host), "<interface>")
        ("p,port", "Manually set port on which to host frontend files and accept WebSocket connections", cxxopts::value<int>(), "<port number>")
        ("g,grpc_port", "Set grpc server port", cxxopts::value<int>(), "<port number>")
        ("t,threads", "Manually set OpenMP thread pool count", cxxopts::value<int>(), "<thread count>")
        ("top_level_folder", "Set top-level folder for data files. Files outside of this directory will not be accessible", cxxopts::value<string>(), "<path>")
        ("frontend_folder", "Set folder to serve frontend files from", cxxopts::value<string>(), "<path>")
        ("exit_after", "Number of seconds to stay alive after last sessions exists", cxxopts::value<int>(), "<duration>")
        ("init_exit_after", "Number of seconds to stay alive at start if no clients connect", cxxopts::value<int>(), "<duration>")
        ("files", "Files to load", cxxopts::value<vector<string>>(files));

    options.add_options("deprecated and debug")
        ("debug_no_auth", "Accept all incoming WebSocket connections (insecure, use with caution!)", cxxopts::value<bool>())
        ("base", "[Deprecated] Set starting folder for data files", cxxopts::value<string>(), "<path>")
        ("root", "[Deprecated] Use 'top_level_folder' instead", cxxopts::value<string>(), "<path>");
    // clang-format on

    options.positional_help("<file(s) or folder to open>");
    options.parse_positional("files");
    auto result = options.parse(argc, argv);

    if (result.count("version")) {
        cout << VERSION_ID << endl;
        version = true;
        return;
    } else if (result.count("help")) {
        cout << options.help() << endl;
        help = true;
        return;
    }

    verbosity = result["verbosity"].as<int>();
    no_log = result["no_log"].as<bool>();

    no_http = result["no_http"].as<bool>();
    debug_no_auth = result["debug_no_auth"].as<bool>();
    no_browser = result["no_browser"].as<bool>();

    applyOptionalArgument(top_level_folder, "root", result);
    // Override deprecated "root" argument
    applyOptionalArgument(top_level_folder, "top_level_folder", result);

    applyOptionalArgument(starting_folder, "base", result);
    // Override deprecated "base" argument if there is exactly one "files" argument supplied
    // TODO: support multiple files (once frontend supports this)
    if (files.size()) {
        fs::path p(files[0]);
        if (fs::exists(p)) {
            // TODO: check if the folder is a CASA or Miriad image
            if (fs::is_directory(p)) {
                starting_folder = p.string();
                files.clear();
            } else if (!fs::is_regular_file(p)) {
                files.clear();
            } else {
                // TODO: Convert to path relative to root directory
            }
        } else {
            files.clear();
        }
    }

    applyOptionalArgument(frontend_folder, "frontend_folder", result);

    applyOptionalArgument(port, "port", result);
    applyOptionalArgument(grpc_port, "grpc_port", result);
    applyOptionalArgument(omp_thread_count, "omp_threads", result);
}

bool ProgramSettings::operator!=(const ProgramSettings& rhs) const {
    return GetTuple() != rhs.GetTuple();
}

bool ProgramSettings::operator==(const ProgramSettings& rhs) const {
    return GetTuple() == rhs.GetTuple();
}

} // namespace carta
