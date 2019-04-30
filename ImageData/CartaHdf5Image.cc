//# CartaHdf5Image.cc : specialized Image implementation for IDIA HDF5 schema

#include "CartaHdf5Image.h"

#include <casacore/coordinates/Coordinates/Projection.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/fits/FITS/fits.h>  // keyword list
#include <casacore/fits/FITS/FITSKeywordUtil.h>  // convert Record to keyword list
#include <casacore/images/Images/ImageFITSConverter.h>  // get coord system
#include <casacore/lattices/Lattices/HDF5Lattice.h>

#include "HDF5Attributes.h"

using namespace carta;

CartaHdf5Image::CartaHdf5Image (const std::string& filename, const std::string& array_name,
        const std::string& hdu, casacore::MaskSpecifier spec) :
    casacore::ImageInterface<float>(casacore::RegionHandlerHDF5(GetHdf5File, this)), 
    valid_(false),
    region_(nullptr)
{
    lattice_ = casacore::HDF5Lattice<float>(filename, array_name, hdu);
    shape_ = lattice_.shape();
    valid_ = Setup(filename, hdu);
}

CartaHdf5Image::CartaHdf5Image(const CartaHdf5Image& other) :
    casacore::ImageInterface<float>(other),
    valid_(other.valid_),
    region_(nullptr),
    lattice_(other.lattice_)
{
    if (other.region_ != nullptr) {
        region_ = new casacore::LatticeRegion(*other.region_);
    }
}

// Image interface

casacore::String CartaHdf5Image::name(bool stripPath) const {
    return lattice_.name(stripPath);
}

casacore::Bool CartaHdf5Image::ok() const {
    return (lattice_.ndim() == coordinates().nPixelAxes());
}

casacore::Bool CartaHdf5Image::doGetSlice(casacore::Array<float>& buffer,
                          const casacore::Slicer& section) {
    return lattice_.doGetSlice(buffer, section);
}

void CartaHdf5Image::doPutSlice(const casacore::Array<float>& buffer,
    const casacore::IPosition& where, const casacore::IPosition& stride) {
    lattice_.doPutSlice(buffer, where, stride);
}

const casacore::LatticeRegion* CartaHdf5Image::getRegionPtr() const {
    return region_;
}

casacore::ImageInterface<float>* CartaHdf5Image::cloneII() const {
    return new CartaHdf5Image(*this);
}

void CartaHdf5Image::resize(const casacore::TiledShape& newShape) {
    throw(casacore::AipsError("CartaHdf5Image::resize - an HDF5 image cannot be resized"));
}


// Set up image manually

bool CartaHdf5Image::Setup(const std::string& filename, const std::string& hdu) {
  // Setup coordinate system, image info
    bool valid(false);
    casacore::HDF5File hdf5_file(filename);
    casacore::HDF5Group hdf5_group(hdf5_file, hdu, true);
    casacore::Record attributes;
    try {
        attributes = HDF5Attributes::doReadAttributes(hdf5_group.getHid());
        hdf5_group.close();
        hdf5_file.close();
	HDF5Attributes::convertToFits(attributes);
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
      return false;  // should not have gotten past the file browser

    bool coordsys_set(false);
    try {
        // convert attributes to FITS keyword strings
        casacore::FitsKeywordList fkwlist = casacore::FITSKeywordUtil::makeKeywordList();
        casacore::FITSKeywordUtil::addKeywords(fkwlist, attributes);
	fkwlist.end(); // add end card to end of list
        if (fkwlist.isempty())
            return false;
        // put kw strings into Vector of Strings
        casacore::Vector<casacore::String> header;
        fkwlist.first();
        casacore::FitsKeyword* x = fkwlist.next();
        while (x != 0) {
	    std::string headerItem(80, ' ');
	    char* card = &(headerItem[0]);
	    casacore::FitsKeyCardTranslator::fmtcard(card, *x);
            size_t hsize(header.size());
            header.resize(hsize + 1, true);
            header(hsize) = headerItem;
            x = fkwlist.next();
        }
        // convert to coordinate system
        int stokesFITSValue(1);
        casacore::Record headerRec;
	casacore::LogSink sink; // null sink; hide confusing FITS log messages
        casacore::LogIO log(sink);
        unsigned int whichRep(0);
        casacore::IPosition imageShape(shape());
        bool dropStokes(true);
        casacore::CoordinateSystem coordsys =
            casacore::ImageFITSConverter::getCoordinateSystem(stokesFITSValue,
            headerRec, header, log, whichRep, imageShape, dropStokes);
        setUnits(casacore::ImageFITSConverter::getBrightnessUnit(headerRec, log));

        // Set coord system in Image
        setCoordinateInfo(coordsys);
        coordsys_set = true;
    } catch (casacore::AipsError& err) {
        std::cerr << "ERROR setting up hdf5 coordinate system: "
                  << err.getMesg() << std::endl;
    }
    return coordsys_set;
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
        if (HDF5Attributes::getDoubleAttribute(bmaj, attributes, "BMAJ")) {
            bmaj_quant = casacore::Quantity(bmaj, "deg");
        }
        if (HDF5Attributes::getDoubleAttribute(bmin, attributes, "BMIN")) {
            bmin_quant = casacore::Quantity(bmin, "deg");
        }
        if (HDF5Attributes::getDoubleAttribute(bpa, attributes, "BPA")) {
            pa_quant = casacore::Quantity(bpa, "deg");
        }
        image_info.setRestoringBeam(bmaj_quant, bmin_quant, pa_quant);

        // Set image info in Image
        setImageInfo(image_info);
    } catch (casacore::AipsError& err) {
    }
}

