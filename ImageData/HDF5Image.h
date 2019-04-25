//# HDF5Image.h : CARTA HDF5Image derived from casacore::HDF5Image

#ifndef CARTA_HDF5IMAGE_H
#define CARTA_HDF5IMAGE_H

#include <casacore/images/Images/HDF5Image.h>
#include <casacore/images/Images/MaskSpecifier.h>
#include <casacore/casa/Containers/Record.h>

namespace carta {

class HDF5Image : public casacore::ImageInterface<float> {
    public:
      // Construct an image from a pre-existing file.
      HDF5Image(const std::string& filename, const std::string& array_name,
          const std::string& hdu, casacore::MaskSpecifier = casacore::MaskSpecifier());
      // Copy constructor
      HDF5Image(const carta::HDF5Image& other);

      inline bool valid() { return valid_; };

      inline const casacore::CountedPtr<casacore::HDF5Group> group() const {
          return lattice_.group(); };
      inline const casacore::HDF5Lattice<float> lattice() { return lattice_; };

      // implement casacore ImageInterface
      virtual inline casacore::String imageType() const { return "HDF5Image"; };
      virtual casacore::String name(bool stripPath=false) const;
      virtual casacore::IPosition shape() const;
      virtual casacore::Bool ok() const;
      virtual casacore::Bool doGetSlice(casacore::Array<float>& buffer,
          const casacore::Slicer& section);
      virtual void doPutSlice(const casacore::Array<float>& buffer,
          const casacore::IPosition& where, const casacore::IPosition& stride);
      virtual const casacore::LatticeRegion* getRegionPtr() const;
      virtual casacore::ImageInterface<float>* cloneII() const;
      virtual void resize(const casacore::TiledShape& newShape);

  private:
      // Function to return the internal HDF5File object to the RegionHandlerHDF5
      inline static const casacore::CountedPtr<casacore::HDF5File>& GetHdf5File(void* image) {
          carta::HDF5Image* im = static_cast<carta::HDF5Image*>(image);
          return im->lattice().file();
      }


      bool Setup(const std::string& filename, const std::string& hdu);
      bool SetupCoordSys(casacore::Record& attributes);
      void SetupImageInfo(casacore::Record& attributes);

      bool valid_;
      casacore::HDF5Lattice<float> lattice_;
      casacore::LatticeRegion* region_;
};

} // namespace carta

#endif
