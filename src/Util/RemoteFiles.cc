#include "RemoteFiles.h"
#include <spdlog/fmt/fmt.h>
#include <cmath>
#include "String.h"

bool GenerateUrlFromRequest(const CARTA::RemoteFileRequest& request, std::string& url, std::string& error_message) {
    // Check for required fields
    if (request.hips().empty()) {
        error_message = "hips ID or keyword is required";
        return false;
    }
    if (!std::isfinite(request.width()) || !std::isfinite(request.height()) || request.width() <= 0 || request.height() <= 0) {
        error_message = "width and height are required";
        return false;
    }
    if (request.width() * request.height() > HIPS_MAX_PIXELS) {
        error_message = "requested image size exceeds maximum pixel count";
        return false;
    }
    if (!request.coordsys().empty() && request.coordsys() != "icrs" && request.coordsys() != "galactic") {
        error_message = "invalid coordinate system";
        return false;
    }
    if (request.wcs().empty()) {
        if (request.coordsys().empty()) {
            error_message = "coordsys is required if wcs is not provided";
            return false;
        } else if (request.projection().empty()) {
            error_message = "projection is required if wcs is not provided";
            return false;
        } else if (!std::isfinite(request.fov()) || request.fov() <= 0) {
            error_message = "fov is required if wcs is not provided";
            return false;
        } else if (request.object().empty() && (!std::isfinite(request.ra()) || !std::isfinite(request.dec()))) {
            error_message = "object or (ra, dec) are required if wcs is not provided";
            return false;
        }
    }

    url = fmt::format("{}?hips={}&format=fits&width={}&height={}", HIPS_BASE_URL, SafeStringEscape(request.hips()), request.width(), request.height());

    if (!request.wcs().empty()) {
        url += fmt::format("&wcs={}", SafeStringEscape(request.wcs()));
    }
    if (!request.projection().empty()) {
        url += fmt::format("&projection={}", SafeStringEscape(request.projection()));
    }
    if (std::isfinite(request.fov()) && request.fov() > 0) {
        url += fmt::format("&fov={}", request.fov());
    }
    if (std::isfinite(request.ra()) && std::isfinite(request.dec())) {
        url += fmt::format("&ra={}&dec={}", request.ra(), request.dec());
    }
    if (!request.coordsys().empty()) {
        url += fmt::format("&coordsys={}", SafeStringEscape(request.coordsys()));
    }
    if (std::isfinite(request.rotation_angle())) {
        url += fmt::format("&rotation_angle={}", request.rotation_angle());
    }
    if (!request.object().empty()) {
        url += fmt::format("&object={}", SafeStringEscape(request.object()));
    }

    return true;
}
