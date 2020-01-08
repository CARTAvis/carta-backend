//# CartaMiriadImage.h : MIRIAD Image class to support masks

#ifndef CARTA_BACKEND_IMAGEDATA_CARTAMIRIADIMAGE_H_
#define CARTA_BACKEND_IMAGEDATA_CARTAMIRIADIMAGE_H_

#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/images/Images/MIRIADImage.h>
#include <casacore/images/Images/MaskSpecifier.h>

namespace carta {

class CartaMiriadImage : public casacore::MIRIADImage {
public:
    // Construct an image from a pre-existing file.
    CartaMiriadImage(const std::string& filename, casacore::MaskSpecifier = casacore::MaskSpecifier());
    // Copy constructor
    CartaMiriadImage(const CartaMiriadImage& other);
    ~CartaMiriadImage();

    inline bool Valid() {
        return _valid;
    };

    inline casacore::SpectralCoordinate::SpecType NativeType() {
        return _native_type;
    }

    // implement casacore ImageInterface
    casacore::String imageType() const override;
    casacore::String name(bool stripPath = false) const override;
    casacore::ImageInterface<float>* cloneII() const override;

    // implement functions in other casacore Image classes
    casacore::Bool isMasked() const override;
    casacore::Bool hasPixelMask() const override;
    const casacore::Lattice<bool>& pixelMask() const override;
    casacore::Lattice<bool>& pixelMask() override;
    casacore::Bool doGetMaskSlice(casacore::Array<bool>& buffer, const casacore::Slicer& section) override;

private:
    void SetUp();
    void OpenImage();
    void CloseImage();
    void SetMask();
    void SetNativeType();

    // for doGetMaskSlice, read flag rows from mask file using mirlib
    void GetPlaneFlags(casacore::Array<bool>& buffer, const casacore::Slicer& section, int z = -1, int w = -1);

    casacore::String _filename;
    casacore::MaskSpecifier _mask_spec;
    bool _valid;
    bool _is_open;
    int _file_handle;
    casacore::SpectralCoordinate::SpecType _native_type;

    bool _has_mask;
    casacore::String _mask_name; // full path to mask file
    casacore::Lattice<casacore::Bool>* _pixel_mask;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CARTAMIRIADIMAGE_H_
