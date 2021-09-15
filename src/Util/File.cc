#include "File.h"

uint32_t GetMagicNumber(const string& filename) {
    uint32_t magic_number = 0;

    ifstream input_file(filename);
    if (input_file) {
        input_file.read((char*)&magic_number, sizeof(magic_number));
        input_file.close();
    }

    return magic_number;
}

bool IsCompressedFits(const std::string& filename) {
    // Check if gzip file, then check .fits extension
    bool is_fits(false);
    auto magic_number = GetMagicNumber(filename);
    if (magic_number == GZ_MAGIC_NUMBER) {
        fs::path gz_path(filename);
        is_fits = (gz_path.stem().extension().string() == ".fits");
    }

    return is_fits;
}

void GetSpectralCoordPreferences(
    casacore::ImageInterface<float>* image, bool& prefer_velocity, bool& optical_velocity, bool& prefer_wavelength, bool& air_wavelength) {
    prefer_velocity = optical_velocity = prefer_wavelength = air_wavelength = false;
    casacore::CoordinateSystem coord_sys(image->coordinates());
    if (coord_sys.hasSpectralAxis()) { // prefer spectral axis native type
        casacore::SpectralCoordinate::SpecType native_type;
        if (image->imageType() == "CartaMiriadImage") { // workaround to get correct native type
            carta::CartaMiriadImage* miriad_image = static_cast<carta::CartaMiriadImage*>(image);
            native_type = miriad_image->NativeType();
        } else {
            native_type = coord_sys.spectralCoordinate().nativeType();
        }
        switch (native_type) {
            case casacore::SpectralCoordinate::FREQ: {
                break;
            }
            case casacore::SpectralCoordinate::VRAD:
            case casacore::SpectralCoordinate::BETA: {
                prefer_velocity = true;
                break;
            }
            case casacore::SpectralCoordinate::VOPT: {
                prefer_velocity = true;

                // Check doppler type; oddly, native type can be VOPT but doppler is RADIO--?
                casacore::MDoppler::Types vel_doppler(coord_sys.spectralCoordinate().velocityDoppler());
                if ((vel_doppler == casacore::MDoppler::Z) || (vel_doppler == casacore::MDoppler::OPTICAL)) {
                    optical_velocity = true;
                }
                break;
            }
            case casacore::SpectralCoordinate::WAVE: {
                prefer_wavelength = true;
                break;
            }
            case casacore::SpectralCoordinate::AWAV: {
                prefer_wavelength = true;
                air_wavelength = true;
                break;
            }
        }
    }
}

int GetStokesValue(const CARTA::StokesType& stokes_type) {
    int stokes_value(-1);
    switch (stokes_type) {
        case CARTA::StokesType::I:
            stokes_value = 1;
            break;
        case CARTA::StokesType::Q:
            stokes_value = 2;
            break;
        case CARTA::StokesType::U:
            stokes_value = 3;
            break;
        case CARTA::StokesType::V:
            stokes_value = 4;
            break;
        default:
            break;
    }
    return stokes_value;
}

CARTA::StokesType GetStokesType(int stokes_value) {
    CARTA::StokesType stokes_type = CARTA::StokesType::STOKES_TYPE_NONE;
    switch (stokes_value) {
        case 1:
            stokes_type = CARTA::StokesType::I;
            break;
        case 2:
            stokes_type = CARTA::StokesType::Q;
            break;
        case 3:
            stokes_type = CARTA::StokesType::U;
            break;
        case 4:
            stokes_type = CARTA::StokesType::V;
            break;
        default:
            break;
    }
    return stokes_type;
}
