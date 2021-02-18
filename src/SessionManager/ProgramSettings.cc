/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ProgramSettings.h"

#include <iostream>

#include <cxxopts.hpp>

#include <images/Images/ImageOpener.h>

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
    vector<string> positional_arguments;

    cxxopts::Options options("CARTA", "CARTA Backend");
    // clang-format off
    // clang-format doesn't like this chain of calls
    options.add_options()("h,help", "Print usage.")
        ("v,version", "Print version.")
        ("verbosity",
         "Display verbose logging from this level (0: off, 1: critical, 2: error, 3: warning, 4: info, 5: debug, 6: trace).",
         cxxopts::value<int>()->default_value(to_string(verbosity)), "<level>")
        ("no_log", "Do not log output to a log file. By default output is logged both to the terminal and to a log file, '~/.carta/log/carta.log'.", cxxopts::value<bool>())
        ("no_http", "Disable CARTA frontend HTTP server. By default the backend serves the frontend files.", cxxopts::value<bool>())
        ("no_browser", "Don't open the frontend URL in a browser on startup. By default the backend prints the frontend URL in the terminal and also opens it in the default browser.", cxxopts::value<bool>())
        ("host", "Only listen on the specified interface (IP address or hostname). If this is unset, the backend listens on all available interfaces.", cxxopts::value<string>()->default_value(host), "<interface>")
        ("p,port", "Manually set the port on which to host frontend files and accept WebSocket connections. If this is unset, the backend will automatically find an unused port, starting from the default.", cxxopts::value<int>()->default_value(to_string(DEFAULT_SOCKET_PORT)), "<port number>")
        ("g,grpc_port", "Set gRPC server port. If this is unset, the gRPC service is disabled.", cxxopts::value<int>(), "<port number>")
        ("t,omp_threads", "Manually set OpenMP thread pool count. By default this is automatically set to the number of cores detected by the backend.", cxxopts::value<int>(), "<thread count>")
        ("top_level_folder", "Set top-level folder for data files. Files outside of this directory will not be accessible.", cxxopts::value<string>()->default_value(top_level_folder), "<path>")
        ("frontend_folder", "Set folder from which frontend files are served. By default a path relative to the location of the backend executable is used: '../share/carta/frontend'.", cxxopts::value<string>(), "<path>")
        ("exit_after", "Number of seconds to stay alive after last session exits. By default the backend will not shut down automatically after all sessions exit.", cxxopts::value<int>(), "<duration>")
        ("init_exit_after", "Number of seconds to stay alive at start if no clients connect. By default the backend will not shut down automatically if no clients connect.", cxxopts::value<int>(), "<duration>")
        ("files", "Files to load", cxxopts::value<vector<string>>(positional_arguments));

    options.add_options("Deprecated and debug")
        ("debug_no_auth", "Accept all incoming WebSocket connections on the specified port (not secure; use with caution!)", cxxopts::value<bool>())
        ("threads", "[Deprecated] No longer supported.", cxxopts::value<int>(), "<thread count>")
        ("base", "[Deprecated] Set starting folder for data files. Use the positional parameter instead.", cxxopts::value<string>(), "<path>")
        ("root", "[Deprecated] Use 'top_level_folder' instead.", cxxopts::value<string>(), "<path>");
    // clang-format on

    options.positional_help("<file or folder to open>");
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

    applyOptionalArgument(frontend_folder, "frontend_folder", result);
    applyOptionalArgument(host, "host", result);
    applyOptionalArgument(port, "port", result);
    applyOptionalArgument(grpc_port, "grpc_port", result);

    applyOptionalArgument(omp_thread_count, "omp_threads", result);
    applyOptionalArgument(wait_time, "exit_after", result);
    applyOptionalArgument(init_wait_time, "init_exit_after", result);

    // base will be overridden by the positional argument if it exists and is a folder
    applyOptionalArgument(starting_folder, "base", result);
    vector<fs::path> file_paths;

    for (const auto& arg : positional_arguments) {
        fs::path p(arg);
        if (fs::exists(p)) {
            if (fs::is_directory(p)) {
                auto image_type = casacore::ImageOpener::imageType(p.string());
                if (image_type == casacore::ImageOpener::AIPSPP || image_type == casacore::ImageOpener::MIRIAD ||
                    image_type == casacore::ImageOpener::IMAGECONCAT || image_type == casacore::ImageOpener::IMAGEEXPR ||
                    image_type == casacore::ImageOpener::COMPLISTIMAGE) {
                    file_paths.push_back(p);
                } else {
                    starting_folder = p.string();
                    // Exit loop after first folder has been found and remove all existing files
                    file_paths.clear();
                    break;
                }
            } else if (!fs::is_regular_file(p)) {
                // Ignore invalid files
                file_paths.clear();
                break;
            } else {
                file_paths.push_back(p);
            }
        } else {
            // Ignore invalid files
            file_paths.clear();
        }
    }
    if (file_paths.size()) {
        // Calculate paths relative to top level folder
        auto top_level_path = fs::absolute(top_level_folder).lexically_normal();
        for (const auto& p : file_paths) {
            auto relative_path = fs::absolute(p).lexically_normal().lexically_relative(top_level_path);
            files.push_back(relative_path.string());
        }
    }
}

bool ProgramSettings::operator!=(const ProgramSettings& rhs) const {
    return GetTuple() != rhs.GetTuple();
}

bool ProgramSettings::operator==(const ProgramSettings& rhs) const {
    return GetTuple() == rhs.GetTuple();
}

} // namespace carta
