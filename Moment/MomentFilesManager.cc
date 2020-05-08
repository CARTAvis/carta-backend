#include "MomentFilesManager.h"

using namespace carta;

MomentFilesManager::MomentFilesManager(std::string root_folder) : _root_folder(root_folder) {}

MomentFilesManager::~MomentFilesManager() {
    // Remove the moment files while deleted
    for (auto& moment_file_directory : _moment_file_directories) {
        const std::string& directory = moment_file_directory.first;
        const std::vector<std::string>& filenames = moment_file_directory.second;
        for (const auto& filename : filenames) {
            std::string full_filename = _root_folder + directory + "/" + filename;
            std::string remove_file = "rm -rf " + full_filename;
            const char* command = remove_file.c_str();
            system(command);
        }
    }
}

void MomentFilesManager::CacheMomentFiles(CARTA::MomentResponse message) {
    std::string directory(message.directory());
    for (int i = 0; i < message.output_files_size(); ++i) {
        std::string filename(message.output_files(i).file_name());
        _moment_file_directories[directory].push_back(filename);
    }
}