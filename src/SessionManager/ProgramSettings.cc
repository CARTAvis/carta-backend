/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ProgramSettings.h"

#include <iostream>

#include <spdlog/fmt/fmt.h>
#include <cxxopts/cxxopts.hpp>

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

    cxxopts::Options options("carta", "Cube Analysis and Rendering Tool for Astronomy");
    // clang-format off
    // clang-format doesn't like this chain of calls
    options.add_options()
        ("h,help", "print usage")
        ("v,version", "print version")
        ("verbosity", "display verbose logging from this level",
         cxxopts::value<int>()->default_value(to_string(verbosity)), "<level>")
        ("no_log", "do not log output to a log file", cxxopts::value<bool>())
        ("no_http", "disable frontend HTTP server", cxxopts::value<bool>())
        ("no_browser", "don't open the frontend URL in a browser on startup", cxxopts::value<bool>())
        ("host", "only listen on the specified interface (IP address or hostname)", cxxopts::value<string>(), "<interface>")
        ("p,port", fmt::format("manually set the HTTP and WebSocket port (default: {} or nearest available port)", DEFAULT_SOCKET_PORT), cxxopts::value<int>(), "<port>")
        ("g,grpc_port", "set gRPC service port", cxxopts::value<int>(), "<port>")
        ("t,omp_threads", "manually set OpenMP thread pool count", cxxopts::value<int>(), "<threads>")
        ("top_level_folder", "set top-level folder for data files", cxxopts::value<string>(), "<dir>")
        ("frontend_folder", "set folder from which frontend files are served", cxxopts::value<string>(), "<dir>")
        ("exit_timeout", "number of seconds to stay alive after last session exits", cxxopts::value<int>(), "<sec>")
        ("initial_timeout", "number of seconds to stay alive at start if no clients connect", cxxopts::value<int>(), "<sec>")
        ("idle_timeout", "number of seconds to keep idle sessions alive", cxxopts::value<int>(), "<sec>")
        ("files", "files to load", cxxopts::value<vector<string>>(positional_arguments));

    options.add_options("Deprecated and debug")
        ("debug_no_auth", "accept all incoming WebSocket connections on the specified port (not secure; use with caution!)", cxxopts::value<bool>())
        ("threads", "[deprecated] no longer supported", cxxopts::value<int>(), "<threads>")
        ("base", "[deprecated] set starting folder for data files (use the positional parameter instead)", cxxopts::value<string>(), "<dir>")
        ("root", "[deprecated] use 'top_level_folder' instead", cxxopts::value<string>(), "<dir>");
    // clang-format on

    options.positional_help("<file or folder to open>");
    options.parse_positional("files");
    auto result = options.parse(argc, argv);

    std::string log_levels(R"(
 0   off
 1   critical
 2   error
 3   warning
 4   info
 5   info + performance messages
 6   debug
 7   debug + protocol messages)");

    std::string extra = fmt::format(R"(
By default the CARTA backend uses the current directory as the starting data 
folder, and uses the root of the filesystem (/) as the top-level data folder. If 
a custom top-level folder is set, the backend will be restricted from accessing 
files outside this directory.

Frontend files are served from '../share/carta/frontend' (relative to the 
location of the backend executable). By default the backend listens for HTTP and 
WebSocket connections on all available interfaces, and automatically selects the 
first available port starting from {}.  On startup the backend prints out a URL 
which can be used to launch the frontend, and tries to open this URL in the 
default browser.

The gRPC service is disabled unless a gRPC port is set. By default the number of 
OpenMP threads is automatically set to the detected number of logical cores.

Logs are written both to the terminal and to a log file, '.carta/log/carta.log' 
in the user's home directory. Possible log levels are:{}

Options are provided to shut the backend down automatically if it is idle (if no 
clients are connected), and to kill frontend sessions that are idle (no longer 
sending messages to the backend).
)",
        DEFAULT_SOCKET_PORT, log_levels);

    if (result.count("version")) {
        cout << VERSION_ID << endl;
        version = true;
        return;
    } else if (result.count("help")) {
        cout << options.help() << extra;
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
    applyOptionalArgument(wait_time, "exit_timeout", result);
    applyOptionalArgument(init_wait_time, "initial_timeout", result);

    applyOptionalArgument(idle_session_wait_time, "idle_timeout", result);

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
