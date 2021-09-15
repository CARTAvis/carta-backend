#ifndef CARTA_BACKEND__UTIL_FILE_H_
#define CARTA_BACKEND__UTIL_FILE_H_

// Valid for little-endian only
#define FITS_MAGIC_NUMBER 0x504D4953
#define GZ_MAGIC_NUMBER 0x08088B1F
#define HDF5_MAGIC_NUMBER 0x46444889
#define XML_MAGIC_NUMBER 0x6D783F3C

uint32_t GetMagicNumber(const std::string& filename);
bool IsCompressedFits(const std::string& filename);

// Determine image type from filename
inline casacore::ImageOpener::ImageTypes CasacoreImageType(const std::string& filename) {
    return casacore::ImageOpener::imageType(filename);
}

// stokes types and value conversion
int GetStokesValue(const CARTA::StokesType& stokes_type);
CARTA::StokesType GetStokesType(int stokes_value);

void GetSpectralCoordPreferences(
    casacore::ImageInterface<float>* image, bool& prefer_velocity, bool& optical_velocity, bool& prefer_wavelength, bool& air_wavelength);

#endif // CARTA_BACKEND__UTIL_FILE_H_
