//# CartaHdf5Image.cc : specialized Image implementation for IDIA HDF5 schema

#include "CartaHdf5Image.h"

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/Projection.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/lattices/Lattices/HDF5Lattice.h>

#include "HDF5Attributes.h"

using namespace carta;

CartaHdf5Image::CartaHdf5Image(
    const std::string& filename, const std::string& array_name, const std::string& hdu, casacore::MaskSpecifier spec)
    : casacore::ImageInterface<float>(casacore::RegionHandlerHDF5(GetHdf5File, this)), valid_(false), region_(nullptr) {
    lattice_ = casacore::HDF5Lattice<float>(filename, array_name, hdu);
    valid_ = Setup(filename, hdu);
}

CartaHdf5Image::CartaHdf5Image(const CartaHdf5Image& other)
    : casacore::ImageInterface<float>(other), valid_(other.valid_), region_(nullptr), lattice_(other.lattice_) {
    if (other.region_ != nullptr) {
        region_ = new casacore::LatticeRegion(*other.region_);
    }
}

// Image interface

casacore::String CartaHdf5Image::name(bool stripPath) const {
    return lattice_.name(stripPath);
}

casacore::IPosition CartaHdf5Image::shape() const {
    return lattice_.shape();
}

casacore::Bool CartaHdf5Image::ok() const {
    return (lattice_.ndim() == coordinates().nPixelAxes());
}

casacore::Bool CartaHdf5Image::doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) {
    return lattice_.doGetSlice(buffer, section);
}

void CartaHdf5Image::doPutSlice(const casacore::Array<float>& buffer, const casacore::IPosition& where, const casacore::IPosition& stride) {
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
        return false; // should not have gotten past the file browser in this case

    bool coordsys_set(false);
    try {
        casacore::CoordinateSystem coordsys = casacore::CoordinateSystem();
        casacore::Int64 naxis;
        HDF5Attributes::getIntAttribute(naxis, attributes, "NAXIS");

        // add Direction coordinate
        casacore::MDirection::Types direction_type(casacore::MDirection::DEFAULT);
        if (attributes.isDefined("RADESYS")) {
            std::string radesys(attributes.asString("RADESYS"));
            casacore::MDirection::getType(direction_type, radesys);
        }
        std::string ctype_long(attributes.asString("CTYPE1"));
        std::string ctype_lat(attributes.asString("CTYPE2"));
        std::string long_unit(attributes.asString("CUNIT1"));
        std::string lat_unit(attributes.asString("CUNIT2"));
        // Projection
        std::vector<double> parameters;
        casacore::Projection projection = casacore::Projection(ctype_long, ctype_lat, parameters);
        double ref_longitude(0.0), ref_latitude(0.0), inc_longitude(0.0), inc_latitude(0.0), ref_x(0.0), ref_y(0.0);
        HDF5Attributes::getDoubleAttribute(ref_longitude, attributes, "CRVAL1");
        HDF5Attributes::getDoubleAttribute(ref_latitude, attributes, "CRVAL2");
        HDF5Attributes::getDoubleAttribute(inc_longitude, attributes, "CDELT1");
        HDF5Attributes::getDoubleAttribute(inc_latitude, attributes, "CDELT2");
        HDF5Attributes::getDoubleAttribute(ref_x, attributes, "CRPIX1");
        HDF5Attributes::getDoubleAttribute(ref_y, attributes, "CRPIX2");
        casacore::Matrix<casacore::Double> xform;
        if (projection.type() == casacore::Projection::SIN) {
            xform.resize(casacore::IPosition(2, 2, 0));
            xform.diagonal() = 1;
        }
        casacore::DirectionCoordinate direction_coord(direction_type, projection, casacore::Quantity(ref_longitude, long_unit),
            casacore::Quantity(ref_latitude, lat_unit), casacore::Quantity(inc_longitude, long_unit),
            casacore::Quantity(inc_latitude, lat_unit), xform, ref_x, ref_y);
        coordsys.addCoordinate(direction_coord);

        if (naxis > 2) {
            // add Spectral coordinate
            casacore::MFrequency::Types freq_type(casacore::MFrequency::DEFAULT);
            if (attributes.isDefined("SPECSYS")) {
                std::string specsys(attributes.asString("SPECSYS"));
                casacore::MFrequency::getType(freq_type, specsys);
            }
            double crval3(0.0), cdelt3(0.0), crpix3(0.0), restfrq(0.0);
            HDF5Attributes::getDoubleAttribute(crval3, attributes, "CRVAL3");
            HDF5Attributes::getDoubleAttribute(cdelt3, attributes, "CDELT3");
            HDF5Attributes::getDoubleAttribute(crpix3, attributes, "CRPIX3");
            HDF5Attributes::getDoubleAttribute(restfrq, attributes, "RESTFRQ");
            // convert val and delt to Hz
            std::string freq_unit = (attributes.isDefined("CUNIT3") ? attributes.asString("CUNIT3") : "Hz");
            casacore::Quantity ref_freq(crval3, freq_unit);
            ref_freq.convert("Hz");
            casacore::Quantity inc_freq(cdelt3, freq_unit);
            inc_freq.convert("Hz");
            casacore::SpectralCoordinate spectral_coord(freq_type, ref_freq.getValue(), inc_freq.getValue(), crpix3, restfrq);
            coordsys.addCoordinate(spectral_coord);
        }

        if (naxis > 3) {
            // add Stokes coordinate
            double nstokes(1);
            HDF5Attributes::getDoubleAttribute(nstokes, attributes, "NAXIS4");
            casacore::Vector<casacore::Int> whichStokes(nstokes);
            casacore::indgen(whichStokes); // generate vector starting at 0
            casacore::StokesCoordinate stokes_coord(whichStokes);
            coordsys.addCoordinate(stokes_coord);
        }

        // ObsInfo
        casacore::ObsInfo obs_info;
        if (attributes.isDefined("TELE")) {
            obs_info.setTelescope(attributes.asString("TELE"));
        }
        if (attributes.isDefined("OBSERVER")) {
            obs_info.setObserver(attributes.asString("OBSERVER"));
        }
        coordsys.setObsInfo(obs_info);

        // Set coord system in Image
        setCoordinateInfo(coordsys);
        coordsys_set = true;
    } catch (casacore::AipsError& err) {
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
