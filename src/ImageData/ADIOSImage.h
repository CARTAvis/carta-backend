/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifdef USE_ADIOS // to enable ADIOS support in CMakeLists.txt
#ifndef CARTA_SRC_IMAGEDATA_ADIOSIMAGE_H_
#define CARTA_SRC_IMAGEDATA_ADIOSIMAGE_H_

//# Includes
#include <casacore/casa/Utilities/DataType.h>
#include <casacore/casa/aips.h>
#include <casacore/images/Images/ImageAttrHandlerCasa.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/PagedImage.h>
#include <casacore/tables/Tables/Table.h>
#include <casacore/tables/Tables/TableRecord.h>

//# Forward Declarations
#include <casacore/casa/iosfwd.h>

#ifdef ADIOS2_USE_MPI
#include <askap/askapparallel/AskapParallel.h>
#include <askap/askapparallel/MPIComms.h>
#include <mpi.h>
#endif

template <class T>
class ADIOSImage : public casacore::ImageInterface<T> {
public:
    ADIOSImage();
    /*  ~ADIOSImage() {
         // temporary just to see when it is called ?
         printf("In ~ADIOSImage ???\n");fflush(stdout);
      }*/
    ADIOSImage(const casacore::TiledShape& mapShape, const casacore::CoordinateSystem& coordinateInfo,
        const casacore::String& nameOfNewFile, casacore::String configname = "", casacore::uInt rowNumber = 0);

#ifdef ADIOS2_USE_MPI
    ADIOSImage(askapparallel::AskapParallel& comms, size_t comm_index, const casacore::TiledShape& mapShape,
        const casacore::CoordinateSystem& coordinateInfo, const casacore::String& nameOfNewFile, casacore::String configname = "",
        casacore::uInt rowNumber = 0);
#endif

    explicit ADIOSImage(const casacore::String& filename, casacore::String configname = "",
        casacore::MaskSpecifier spec = casacore::MaskSpecifier(), casacore::uInt rowNumber = 0);

#ifdef ADIOS2_USE_MPI
    explicit ADIOSImage(askapparallel::AskapParallel& comms, const casacore::String& filename, casacore::String configname = "",
        casacore::MaskSpecifier spec = casacore::MaskSpecifier(), casacore::uInt rowNumber = 0);
#endif

    ADIOSImage(const ADIOSImage<T>& other);

    casacore::Table& table() {
        return tab_p;
    }

    static casacore::String className() {
        return "ADIOSImage";
    }

    virtual casacore::String imageType() const;

    void makeNewTable(const casacore::TiledShape& shape, casacore::uInt rowNumber, casacore::String filename);

    virtual casacore::Bool setUnits(const casacore::Unit& newUnits);

    virtual casacore::Bool setImageInfo(const casacore::ImageInfo& info);

    virtual casacore::Bool setMiscInfo(const casacore::RecordInterface& newInfo);

    virtual casacore::Bool setCoordinateInfo(const casacore::CoordinateSystem& coords);

    virtual casacore::IPosition shape() const;

    virtual casacore::String name(casacore::Bool stripPath = casacore::False) const;

    virtual casacore::Bool ok() const;

    virtual casacore::Bool doGetSlice(casacore::Array<T>& buffer, const casacore::Slicer& theSlice);

    virtual void doPutSlice(const casacore::Array<T>& sourceBuffer, const casacore::IPosition& where, const casacore::IPosition& stride);

    virtual const casacore::LatticeRegion* getRegionPtr() const;

    virtual casacore::ImageInterface<T>* cloneII() const;

    virtual void resize(const casacore::TiledShape& newShape);

    virtual void reopenColumn();

    virtual casacore::Bool isPaged() const;

    virtual void setDefaultMask(const casacore::String& maskName);

    virtual casacore::Lattice<casacore::Bool>& pixelMask();

    // rewritten PagedImage private functions
    void attach_logtable();
    void setTableType();
    void restoreAll(const casacore::TableRecord& rec);
    void applyMaskSpecifier(const casacore::MaskSpecifier&);
    void restoreImageInfo(const casacore::TableRecord& rec);
    void restoreUnits(const casacore::TableRecord& rec);
    void restoreMiscInfo(const casacore::TableRecord& rec);
    void applyMask(const casacore::String& maskName);
    static casacore::Table& getTable(void* imagePtr, casacore::Bool writable);

private:
    void reopenRW();

    casacore::ArrayColumn<T> map_p;
    casacore::Table tab_p;
    casacore::LatticeRegion* regionPtr_p;
    casacore::uInt row_p;
    casacore::String config;
    // PJE - not certain how best to store the adios configuration besides
    // a string containing the xml file of adios configuration
    //

    // for MPI lets store a default MPI_COMM_SELF communicator
#ifdef ADIOS2_USE_MPI
    MPI_Comm adios_comm = MPI_COMM_SELF;
#endif

public:
    using casacore::ImageInterface<T>::setMiscInfoMember;
    using casacore::ImageInterface<T>::setUnitMember;
    using casacore::ImageInterface<T>::imageInfo;
    using casacore::ImageInterface<T>::setLogMember;
    using casacore::ImageInterface<T>::setCoordsMember;
    using casacore::ImageInterface<T>::setImageInfoMember;
    using casacore::ImageInterface<T>::getDefaultMask;
    using casacore::ImageInterface<T>::hasRegion;
    using casacore::ImageInterface<T>::getImageRegionPtr;
    using casacore::ImageInterface<T>::coordinates;

    using casacore::Lattice<T>::ndim;
};

#include "ADIOSImage.tcc"

#endif // CARTA_SRC_IMAGEDATA_ADIOSIMAGE_H_
#endif // CARTA_SRC_IMAGEDATA_ADIOSIMAGE_H_
