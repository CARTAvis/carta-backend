#ifndef CARTA_BACKEND_REMOTEFILES_H
#define CARTA_BACKEND_REMOTEFILES_H

#include <carta-protobuf/remote_file_request.pb.h>
#include <string>

const auto HIPS_BASE_URL = "https://alasky.cds.unistra.fr/hips-image-services/hips2fits";
const auto HIPS_MAX_PIXELS = 50e6;
bool GenerateUrlFromRequest(const CARTA::RemoteFileRequest& request, std::string& url, std::string& error_message);

#endif // CARTA_BACKEND_REMOTEFILES_H
