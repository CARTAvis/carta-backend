//# CartaHdf5Image.cc : specialized Image implementation for IDIA HDF5 schema

#include "CartaHdf5Image.h"

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/Projection.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/fits/FITS/FITSKeywordUtil.h>        // convert Record to keyword list
#include <casacore/fits/FITS/fits.h>                   // keyword list
#include <casacore/images/Images/ImageFITSConverter.h> // get coord system
#include <casacore/lattices/Lattices/HDF5Lattice.h>

#include "Hdf5Attributes.h"

using namespace carta;

CartaHdf5Image::CartaHdf5Image(
    const std::string& filename, const std::string& array_name, const std::string& hdu, casacore::MaskSpecifier mask_spec)
    : casacore::ImageInterface<float>(casacore::RegionHandlerHDF5(GetHdf5File, this)),
      _valid(false),
      _pixel_mask(nullptr),
      _mask_spec(mask_spec) {
    _lattice = casacore::HDF5Lattice<float>(filename, array_name, hdu);
    _pixel_mask = new casacore::ArrayLattice<bool>(_lattice.shape());
    _pixel_mask->set(true);
    _shape = _lattice.shape();
    _valid = Setup(filename, hdu);
}

CartaHdf5Image::CartaHdf5Image(const CartaHdf5Image& other)
    : casacore::ImageInterface<float>(other),
      _valid(other._valid),
      _mask_spec(other._mask_spec),
      _lattice(other._lattice),
      _pixel_mask(nullptr),
      _shape(other._shape) {
    if (other._pixel_mask != nullptr)
        _pixel_mask = other._pixel_mask->clone();
}

CartaHdf5Image::~CartaHdf5Image() {
    delete _pixel_mask;
}

// Image interface

casacore::String CartaHdf5Image::imageType() const {
    return "CartaHdf5Image";
}

casacore::String CartaHdf5Image::name(bool stripPath) const {
    return _lattice.name(stripPath);
}

casacore::IPosition CartaHdf5Image::shape() const {
    return _shape;
}

casacore::Bool CartaHdf5Image::ok() const {
    return (_lattice.ndim() == coordinates().nPixelAxes());
}

casacore::Bool CartaHdf5Image::doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) {
    return _lattice.doGetSlice(buffer, section);
}

void CartaHdf5Image::doPutSlice(const casacore::Array<float>& buffer, const casacore::IPosition& where, const casacore::IPosition& stride) {
    _lattice.doPutSlice(buffer, where, stride);
}

const casacore::LatticeRegion* CartaHdf5Image::getRegionPtr() const {
    return nullptr; // full lattice
}

casacore::ImageInterface<float>* CartaHdf5Image::cloneII() const {
    return new CartaHdf5Image(*this);
}

void CartaHdf5Image::resize(const casacore::TiledShape& newShape) {
    throw(casacore::AipsError("CartaHdf5Image::resize - an HDF5 image cannot be resized"));
}

casacore::Bool CartaHdf5Image::isMasked() const {
    return (_pixel_mask != nullptr);
}

casacore::Bool CartaHdf5Image::hasPixelMask() const {
    return (_pixel_mask != nullptr);
}

const casacore::Lattice<bool>& CartaHdf5Image::pixelMask() const {
    if (!hasPixelMask()) {
        throw(casacore::AipsError("CartaHdf5Image::pixelMask - no pixelmask used"));
    }
    return *_pixel_mask;
}

casacore::Lattice<bool>& CartaHdf5Image::pixelMask() {
    if (!hasPixelMask()) {
        throw(casacore::AipsError("CartaHdf5Image::pixelMask - no pixelmask used"));
    }
    return *_pixel_mask;
}

casacore::Bool CartaHdf5Image::doGetMaskSlice(casacore::Array<bool>& buffer, const casacore::Slicer& section) {
    // set buffer to mask for section of image
    if (!hasPixelMask()) {
        buffer.resize(section.length()); // section shape
        buffer = true;                   // use entire section
        return false;
    }
    // Get section of data
    casacore::SubLattice<float> sublattice(_lattice, section);
    casacore::ArrayLattice<bool> mask_lattice(sublattice.shape());
    // Set up iterators
    unsigned int max_pix(sublattice.advisedMaxPixels());
    casacore::IPosition cursor_shape(sublattice.doNiceCursorShape(max_pix));
    casacore::RO_LatticeIterator<float> lattice_iter(sublattice, cursor_shape);
    casacore::LatticeIterator<bool> mask_iter(mask_lattice, cursor_shape);
    mask_iter.reset();
    // Retrieve each iteration and set mask
    casacore::Array<bool> mask_cursor_data; // cursor array for each iteration
    for (lattice_iter.reset(); !lattice_iter.atEnd(); lattice_iter++) {
        mask_cursor_data.resize();
        mask_cursor_data = isFinite(lattice_iter.cursor());
        // make sure mask data is same shape as mask cursor
        casacore::IPosition mask_cursor_shape(mask_iter.rwCursor().shape());
        if (mask_cursor_data.shape() != mask_cursor_shape)
            mask_cursor_data.resize(mask_cursor_shape, true);
        mask_iter.rwCursor() = mask_cursor_data;
        mask_iter++;
    }
    buffer = mask_lattice.asArray();
    return true;
}

// Set up image CoordinateSystem, ImageInfo

bool CartaHdf5Image::Setup(const std::string& filename, const std::string& hdu) {
    // Setup coordinate system, image info
    bool valid(false);
    casacore::HDF5File hdf5_file(filename);
    casacore::HDF5Group hdf5_group(hdf5_file, hdu, true);
    casacore::Record attributes;
    try {
        attributes = Hdf5Attributes::ReadAttributes(hdf5_group.getHid());
        hdf5_group.close();
        hdf5_file.close();
        Hdf5Attributes::ConvertToFits(attributes);
        valid = SetupCoordSys(attributes);
        SetupImageInfo(attributes);
    } catch (casacore::HDF5Error& err) {
        hdf5_group.close();
        hdf5_file.close();
    }
    return valid;
}

bool CartaHdf5Image::SetupCoordSys(casacore::Record& attributes) {
    // Use header attributes to set up Image CoordinateSystem
    if (attributes.empty())
        return false; // should not have gotten past the file browser

    bool coord_sys_set(false);
    try {
        // convert attributes to FITS keyword strings
        casacore::FitsKeywordList fits_kw_list = casacore::FITSKeywordUtil::makeKeywordList();
        casacore::FITSKeywordUtil::addKeywords(fits_kw_list, attributes);
        fits_kw_list.end(); // add end card to end of list
        if (fits_kw_list.isempty())
            return false;
        // put kw strings into Vector of Strings
        casacore::Vector<casacore::String> header;
        fits_kw_list.first();
        casacore::FitsKeyword* x = fits_kw_list.next();
        while (x != 0) {
            std::string header_item(80, ' ');
            char* card = &(header_item[0]);
            casacore::FitsKeyCardTranslator::fmtcard(card, *x);
            size_t header_size(header.size());
            header.resize(header_size + 1, true);
            header(header_size) = header_item;
            x = fits_kw_list.next();
        }
        // convert to coordinate system
        int stokes_fits_value(1);
        casacore::Record header_rec;
        casacore::LogSink sink; // null sink; hide confusing FITS log messages
        casacore::LogIO log(sink);
        unsigned int which_rep(0);
        casacore::IPosition image_shape(shape());
        bool drop_stokes(true);
        casacore::CoordinateSystem coordinate_system = casacore::ImageFITSConverter::getCoordinateSystem(
            stokes_fits_value, header_rec, header, log, which_rep, image_shape, drop_stokes);
        setUnits(casacore::ImageFITSConverter::getBrightnessUnit(header_rec, log));

        // Set coord system in Image
        setCoordinateInfo(coordinate_system);
        coord_sys_set = true;
    } catch (casacore::AipsError& err) {
        std::cerr << "ERROR setting up hdf5 coordinate system: " << err.getMesg() << std::endl;
    }
    return coord_sys_set;
}

void CartaHdf5Image::SetupImageInfo(casacore::Record& attributes) {
    // Holds object name and restoring beam
    casacore::ImageInfo image_info;

    try {
        // object
        if (attributes.isDefined("OBJECT")) {
            image_info.setObjectName(attributes.asString("OBJECT"));
        }

        // restoring beam
        casacore::Quantity bmaj_quant, bmin_quant, pa_quant;
        double bmaj(0.0), bmin(0.0), bpa(0.0);
        if (Hdf5Attributes::GetDoubleAttribute(bmaj, attributes, "BMAJ")) {
            bmaj_quant = casacore::Quantity(bmaj, "deg");
        }
        if (Hdf5Attributes::GetDoubleAttribute(bmin, attributes, "BMIN")) {
            bmin_quant = casacore::Quantity(bmin, "deg");
        }
        if (Hdf5Attributes::GetDoubleAttribute(bpa, attributes, "BPA")) {
            pa_quant = casacore::Quantity(bpa, "deg");
        }
        image_info.setRestoringBeam(bmaj_quant, bmin_quant, pa_quant);

        // Set image info in Image
        setImageInfo(image_info);
    } catch (casacore::AipsError& err) {
    }
}
