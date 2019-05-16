//# CartaHdf5Image.cc : specialized Image implementation for IDIA HDF5 schema

#include "CartaHdf5Image.h"

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/Projection.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/fits/FITS/fits.h>                   // keyword list
#include <casacore/fits/FITS/FITSKeywordUtil.h>        // convert Record to keyword list
#include <casacore/images/Images/ImageFITSConverter.h> // get coord system
#include <casacore/lattices/Lattices/HDF5Lattice.h>

#include "Hdf5Attributes.h"

using namespace carta;

CartaHdf5Image::CartaHdf5Image(const std::string& filename, const std::string& array_name, const std::string& hdu,
                               const CARTA::FileInfoExtended* info, casacore::MaskSpecifier mask_spec)
    : casacore::ImageInterface<float>(casacore::RegionHandlerHDF5(GetHdf5File, this)),
      _valid(false),
      _pixel_mask(nullptr),
      _mask_spec(mask_spec) {
    _lattice = casacore::HDF5Lattice<float>(filename, array_name, hdu);
    _pixel_mask = new casacore::ArrayLattice<bool>(_lattice.shape());
    _pixel_mask->set(true);
    _shape = _lattice.shape();
    _valid = Setup(filename, hdu, info);
}

CartaHdf5Image::CartaHdf5Image(const CartaHdf5Image& other)
    : casacore::ImageInterface<float>(other),
      _valid(other._valid),
      _mask_spec(other._mask_spec),
      _lattice(other._lattice),
      _pixel_mask(nullptr),
      _shape(other._shape) {
    if (other._pixel_mask != nullptr) {
        _pixel_mask = other._pixel_mask->clone();
    }
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

bool CartaHdf5Image::Setup(const std::string& filename, const std::string& hdu, const CARTA::FileInfoExtended* info) {
    // Setup coordinate system, image info, misc info from image header entries
    bool valid(false);
    try {
        // convert FileInfoExtended entries to casacore Record
        casacore::Record info_header = ConvertInfoToCasacoreRecord(info);

        // convert info header record to vector of FITS keyword strings
        casacore::FitsKeywordList fits_kw_list = casacore::FITSKeywordUtil::makeKeywordList();
        casacore::FITSKeywordUtil::addKeywords(fits_kw_list, info_header);
        fits_kw_list.end(); // add end card to end of list
        if (fits_kw_list.isempty())
            return false;
        // put kw strings into Vector of Strings
        casacore::Vector<casacore::String> header_vector;
        fits_kw_list.first();
        casacore::FitsKeyword* x = fits_kw_list.next();
        while (x != nullptr) {
            std::string header_item(80, ' ');
            char* card = &(header_item[0]);
            casacore::FitsKeyCardTranslator::fmtcard(card, *x);
            size_t header_size(header_vector.size());
            header_vector.resize(header_size + 1, true);
            header_vector(header_size) = header_item;
            x = fits_kw_list.next();
        }

        // set coordinate system
        int stokes_fits_value(1);
        casacore::Record header_rec;
        casacore::LogSink sink; // null sink; hide confusing FITS log messages
        casacore::LogIO log(sink);
        unsigned int which_rep(0);
        casacore::IPosition image_shape(shape());
        bool drop_stokes(true);
        casacore::CoordinateSystem coordinate_system = casacore::ImageFITSConverter::getCoordinateSystem(
            stokes_fits_value, header_rec, header_vector, log, which_rep, image_shape, drop_stokes);
        setCoordinateInfo(coordinate_system);

        // set image units
        setUnits(casacore::ImageFITSConverter::getBrightnessUnit(header_rec, log));

        // set image info
        casacore::ImageInfo image_info = casacore::ImageFITSConverter::getImageInfo(header_rec);
        if (stokes_fits_value != -1) {
            casacore::ImageInfo::ImageTypes type = casacore::ImageInfo::imageTypeFromFITS(stokes_fits_value);
            if (type != casacore::ImageInfo::Undefined) {
                image_info.setImageType(type);
            }
        }
        setImageInfo(image_info);

        // set misc info
        casacore::Record misc_info;
        casacore::ImageFITSConverter::extractMiscInfo(misc_info, header_rec);
        setMiscInfo(misc_info);

        valid = true;
    } catch (casacore::AipsError& err) {
        std::cerr << "Error opening HDF5 image: " << err.getMesg() << std::endl;
    }
    return valid;
}

casacore::Record CartaHdf5Image::ConvertInfoToCasacoreRecord(const CARTA::FileInfoExtended* info) {
    // convert header_entries to Record string, int or double field
    casacore::Record header_record;
    for (int i = 0; i < info->header_entries_size(); ++i) {
        const CARTA::HeaderEntry header_entry = info->header_entries(i);
        const std::string entry_name = header_entry.name();

        switch (header_entry.entry_type()) {
            case CARTA::EntryType::STRING: {
                casacore::String entry_value = header_entry.value();
                entry_value.gsub("'", ""); // remove quote : error converting to FITS card if final quote removed
                header_record.define(entry_name, entry_value);
            } break;
            case CARTA::EntryType::INT: {
                if (entry_name == "SIMPLE") { // FITSKeywordUtil adds this
                    continue;
                }
                if ((entry_name == "EXTEND") || (entry_name == "BLOCKED")) { // convert to bool
                    bool val = (header_entry.numeric_value() == 0.0 ? false : true);
                    header_record.define(entry_name, val);
                } else {
                    header_record.define(entry_name, static_cast<int>(header_entry.numeric_value()));
                }
            } break;
            default:
                header_record.define(entry_name, header_entry.numeric_value()); // double
        }
    }
    return header_record;
}

