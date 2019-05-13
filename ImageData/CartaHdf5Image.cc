//# CartaHdf5Image.cc : specialized Image implementation for IDIA HDF5 schema

#include "CartaHdf5Image.h"

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/Projection.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/fits/FITS/fits.h>                   // keyword list
#include <casacore/fits/FITS/FITSKeywordUtil.h>        // convert Record to keyword list
#include <casacore/fits/FITS/FITSDateUtil.h>           // convert date string to fits format
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

bool CartaHdf5Image::Setup(const std::string& filename, const std::string& hdu, const CARTA::FileInfoExtended* info) {
    // Setup coordinate system, image info
    bool valid(false);
    try {
        casacore::Record header = ConvertInfoToCasacoreRecord(info);
        valid = SetupCoordSys(header);
        SetupImageInfo(header);
    } catch (casacore::AipsError& err) {
        std::cerr << "Error opening HDF5 image: " << err.getMesg() << std::endl;
    }
    return valid;
}

casacore::Record CartaHdf5Image::ConvertInfoToCasacoreRecord(const CARTA::FileInfoExtended* info) {
    // convert header_entries to Record
    casacore::Record header_record;
    for (int i = 0; i < info->header_entries_size(); ++i) {
        const CARTA::HeaderEntry header_entry = info->header_entries(i);
        const std::string entry_name = header_entry.name();
	if (entry_name == "SIMPLE") {
            continue; // added in FITS util
        }
        switch (header_entry.entry_type()) {
            case CARTA::EntryType::STRING: {
                if ((entry_name == "EXTEND") || (entry_name == "BLOCKED")) { // bool
                    header_record.define(entry_name, (header_entry.value() == "T" ? true : false));
		} else if ((entry_name == "BITPIX") || (entry_name == "NAXIS")) { // int
                    header_record.define(entry_name, std::stoi(header_entry.value()));
		} else if (ShouldBeFloat(entry_name)) { // float
                    header_record.define(entry_name, std::stof(header_entry.value()));
                } else { // string
                    casacore::String entry_value = header_entry.value();
		    entry_value = (entry_value == "Kelvin" ? "K" : entry_value);
		    if (entry_name == "DATE_OBS") { // date
			casacore::String fits_date;
                        casacore::FITSDateUtil::convertDateString(fits_date, entry_value);
                        header_record.define(entry_name, fits_date);
                    } else {
                        header_record.define(entry_name, entry_value);
                    }
                }
                break;
            }
            case CARTA::EntryType::FLOAT:
                header_record.define(header_entry.name(), static_cast<float>(header_entry.numeric_value()));
                break;
            case CARTA::EntryType::INT:
                header_record.define(header_entry.name(), static_cast<int>(header_entry.numeric_value()));
                break;
            default:
                header_record.define(header_entry.name(), header_entry.value());
        }
    }
    return header_record;
}

bool CartaHdf5Image::ShouldBeFloat(const std::string& entry_name) {
    casacore::String name(entry_name);
    return ((name == "EQUINOX") || (name == "EPOCH") || (name == "LONPOLE") || (name == "LATPOLE") || (name == "RESTFRQ") ||
            (name == "OBSFREQ") || (name == "MJD-OBS") || (name == "DATAMIN") || (name == "DATAMAX") || (name.startsWith("CRVAL")) ||
            (name.startsWith("CRPIX")) || (name.startsWith("CDELT")) || (name.startsWith("CROTA")));
}

bool CartaHdf5Image::SetupCoordSys(casacore::Record& header) {
    // Use header to set up Image CoordinateSystem
    if (header.empty())
        return false; // should not have gotten past the file browser

    bool coord_sys_set(false);
    try {
        // convert header to FITS keyword strings
        casacore::FitsKeywordList fits_kw_list = casacore::FITSKeywordUtil::makeKeywordList();
        casacore::FITSKeywordUtil::addKeywords(fits_kw_list, header);
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

void CartaHdf5Image::SetupImageInfo(casacore::Record& header) {
    // Holds object name and restoring beam
    casacore::ImageInfo image_info;

    try {
        // object
        if (header.isDefined("OBJECT")) {
            image_info.setObjectName(header.asString("OBJECT"));
        }

        // restoring beam
        casacore::Quantity bmaj_quant, bmin_quant, pa_quant;
        double bmaj(0.0), bmin(0.0), bpa(0.0);
        if (Hdf5Attributes::GetDoubleAttribute(bmaj, header, "BMAJ")) {
            bmaj_quant = casacore::Quantity(bmaj, "deg");
        }
        if (Hdf5Attributes::GetDoubleAttribute(bmin, header, "BMIN")) {
            bmin_quant = casacore::Quantity(bmin, "deg");
        }
        if (Hdf5Attributes::GetDoubleAttribute(bpa, header, "BPA")) {
            pa_quant = casacore::Quantity(bpa, "deg");
        }
        image_info.setRestoringBeam(bmaj_quant, bmin_quant, pa_quant);

        // Set image info in Image
        setImageInfo(image_info);
    } catch (casacore::AipsError& err) {
    }
}
