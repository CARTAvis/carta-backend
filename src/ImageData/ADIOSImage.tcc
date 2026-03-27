/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ADIOSIMAGE_TCC_
#define CARTA_SRC_IMAGEDATA_ADIOSIMAGE_TCC_

#include <casacore/casa/Logging/LogIO.h>
#include <casacore/casa/Logging/LogMessage.h>
#include <casacore/images/Images/ImageInfo.h>
#include <casacore/images/Regions/ImageRegion.h>
#include <casacore/images/Regions/RegionHandlerTable.h>
#include <casacore/lattices/LEL/LatticeExpr.h>
#include <casacore/lattices/LEL/LatticeExprNode.h>
#include <casacore/lattices/LRegions/LatticeRegion.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>
#include <casacore/lattices/Lattices/LatticeIterator.h>
#include <casacore/lattices/Lattices/LatticeNavigator.h>
#include <casacore/lattices/Lattices/LatticeStepper.h>
#include <casacore/lattices/Lattices/PagedArrIter.h>
#include "ADIOSImage.h"

#include <casacore/casa/Arrays/Array.h>
#include <casacore/casa/Arrays/ArrayLogical.h>
#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/casa/Arrays/LogiArray.h>
#include <casacore/casa/Arrays/Slicer.h>
#include <casacore/casa/BasicSL/String.h>
#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/OS/File.h>
#include <casacore/casa/OS/Path.h>
#include <casacore/casa/Quanta/UnitMap.h>
#include <casacore/casa/Utilities/Assert.h>
#include <casacore/tables/DataMan/Adios2StMan.h>
#include <casacore/tables/Tables/SetupNewTab.h>
#include <casacore/tables/Tables/Table.h>
#include <casacore/tables/Tables/TableColumn.h>
#include <casacore/tables/Tables/TableDesc.h>
#include <casacore/tables/Tables/TableInfo.h>
#include <casacore/tables/Tables/TableLock.h>
#include <casacore/tables/Tables/TableRecord.h>

#include <casacore/casa/iostream.h>
#include <casacore/casa/sstream.h>

// ASKAP_LOGGER(ADIOSImageLogger, ".ADIOSImage");

template <class T>
ADIOSImage<T>::ADIOSImage() : casacore::ImageInterface<T>(casacore::RegionHandlerTable(getTable, this)), regionPtr_p(0) {}

template <class T>
ADIOSImage<T>::ADIOSImage(const casacore::TiledShape& shape, const casacore::CoordinateSystem& coordinateInfo,
    const casacore::String& filename, casacore::String configname, casacore::uInt rowNumber)
    : casacore::ImageInterface<T>(casacore::RegionHandlerTable(getTable, this)), regionPtr_p(0) {
    config = configname;
    row_p = rowNumber;
#ifdef ADIOS2_USE_MPI
    // A Hack to force the use of MPI_COMM_WORLD
    MPI_Comm comm = MPI_COMM_WORLD;
    int size;
    MPI_Comm_size(comm, &size);
    if (size > 1) {
        adios_comm = comm;
    }
#endif
    makeNewTable(shape, rowNumber, filename);
    attach_logtable();
    AlwaysAssert(setCoordinateInfo(coordinateInfo), casacore::AipsError);
    setTableType();
}

#ifdef ADIOS2_USE_MPI
template <class T>
ADIOSImage<T>::ADIOSImage(askapparallel::AskapParallel& comms, size_t comm_index, const casacore::TiledShape& shape,
    const casacore::CoordinateSystem& coordinateInfo, const casacore::String& filename, casacore::String configname,
    casacore::uInt rowNumber)
    : casacore::ImageInterface<T>(casacore::RegionHandlerTable(getTable, this)), regionPtr_p(0) {
    adios_comm = comms.getComm(comm_index);
    int size;
    int result = MPI_Comm_size(adios_comm, &size);
    ASKAPDEBUGASSERT(result == MPI_SUCCESS);
    int rank;
    result = MPI_Comm_rank(adios_comm, &rank);
    ASKAPDEBUGASSERT(result == MPI_SUCCESS);
    ASKAPLOG_INFO_STR(ADIOSImageLogger, "ADIOS received MPI Comm with size " << size << " and rank " << rank);
    config = configname;
    row_p = rowNumber;
    makeNewTable(shape, rowNumber, filename);
    if (rank == 0) {
        attach_logtable();
        setTableType();
    }
    AlwaysAssert(setCoordinateInfo(coordinateInfo), casacore::AipsError);
}
#endif

template <class T>
ADIOSImage<T>::ADIOSImage(
    const casacore::String& filename, casacore::String configname, casacore::MaskSpecifier spec, casacore::uInt rowNumber)
    : casacore::ImageInterface<T>(casacore::RegionHandlerTable(getTable, this)), regionPtr_p(0) {
    tab_p = casacore::Table(filename, casacore::Table::TableOption::Old);
    map_p = casacore::ArrayColumn<T>(tab_p, "map");
    row_p = rowNumber;
    config = configname;
    attach_logtable();
    restoreAll(tab_p.keywordSet());
    applyMaskSpecifier(spec);
}

#ifdef ADIOS2_USE_MPI
template <class T>
ADIOSImage<T>::ADIOSImage(askapparallel::AskapParallel& comms, const casacore::String& filename, casacore::String configname,
    casacore::MaskSpecifier spec, casacore::uInt rowNumber)
    : casacore::ImageInterface<T>(casacore::RegionHandlerTable(getTable, this)), regionPtr_p(0) {
    adios_comm = comms.interGroupCommIndex();
    tab_p = casacore::Table(filename, casacore::Table::TableOption::Old);
    map_p = casacore::ArrayColumn<T>(tab_p, "map");
    row_p = rowNumber;
    config = configname;
    restoreAll(tab_p.keywordSet());
    applyMaskSpecifier(spec);
}
#endif

template <class T>
ADIOSImage<T>::ADIOSImage(const ADIOSImage<T>& other) : casacore::ImageInterface<T>(other), map_p(other.map_p), regionPtr_p(0) {
    if (other.regionPtr_p != 0) {
        regionPtr_p = new casacore::LatticeRegion(*other.regionPtr_p);
    }
}

template <class T>
void ADIOSImage<T>::makeNewTable(const casacore::TiledShape& shape, casacore::uInt rowNumber, casacore::String filename) {
    casacore::IPosition latShape = shape.shape();

    const casacore::uInt ndim = latShape.nelements();

    casacore::TableDesc description;
    // PJE - why are we adding a hard-coded string???
    description.addColumn(casacore::ArrayColumnDesc<T>("map", casacore::String("version 4.0"), latShape, casacore::ColumnDesc::FixedShape));

    casacore::SetupNewTable newtab(filename, description, casacore::Table::New);

    // now invoke the Adios2 storage manager
    // PJE - the issue here is that the constructors are only valid if ADIOS has not be compiled
    // with MPI. Not obvious how to best catch that and whether ADIOS can be passed a null
    // communicator if compiled with ADIOS but don't want parallel accessors.

    // might want a unique pointer locally
    // std::unique_ptr<casacore::Adios2StMan> stmanptr;

    if (config == "") {
        // invoke Adios2StMan(engineType, engineParams, transportParams, operatorParams)
        // with default engine type and parameters
#ifdef ADIOS2_USE_MPI
        // if mpi then using MPI_COMM_SELF for writing
        casacore::Adios2StMan stman(adios_comm, "", {}, {{}}, {{{"Variable", "map"}, {}, {}}});
        newtab.bindColumn("map", stman);
#else
        casacore::Adios2StMan stman("", {}, {{}}, {{{"Variable", "map"}, {}, {}}});
        newtab.bindColumn("map", stman);
#endif
    } else {
        // invoke configuration based call
        // PJE - question on whether we also need to include the operatorParams {{{"Varaible", "map"}}}
        casacore::Adios2StMan::from_config_t from_config{};
#ifdef ADIOS2_USE_MPI
        casacore::Adios2StMan stman(adios_comm, config, from_config);
        newtab.bindColumn("map", stman);
#else
        casacore::Adios2StMan stman(config, from_config);
        newtab.bindColumn("map", stman);
#endif
    }

    // this call would be ideal but requires maybe use of pointer
    // newtab.bindColumn("map", stman);
#ifdef ADIOS2_USE_MPI
    casacore::Table tab(adios_comm, newtab);
#else
    casacore::Table tab(newtab);
#endif
    tab_p = tab;
    casacore::ArrayColumn<T> arrayCol(tab_p, "map");
    const casacore::uInt rows = tab_p.nrow();
    if ((rowNumber + 1) > rows) {
        tab_p.addRow(rowNumber - rows + 1);
        for (casacore::rownr_t row = rows; row <= rowNumber - rows; row++) {
            arrayCol.setShape(row, latShape);
        }
    }
    map_p = arrayCol;
}

template <class T>
casacore::Bool ADIOSImage<T>::setUnits(const casacore::Unit& newUnits) {
    setUnitMember(newUnits);
    casacore::Table& tab = table();
    if (tab.keywordSet().isDefined("units")) {
        tab.rwKeywordSet().removeField("units");
    }
    if (!tab.isWritable()) {
        tab.reopenRW();
    }
    tab.rwKeywordSet().define("units", newUnits.getName());
    return casacore::True;
}

template <class T>
casacore::Bool ADIOSImage<T>::setImageInfo(const casacore::ImageInfo& info) {
    // Set imageinfo in base class.
    casacore::Bool ok = casacore::ImageInterface<T>::setImageInfo(info);
    if (ok) {
        // Make persistent in table keywords.
        casacore::Table& tab = table();
        // Delete existing one if there.
        if (tab.keywordSet().isDefined("imageinfo")) {
            tab.rwKeywordSet().removeField("imageinfo");
        }
        // Convert info to a record and save as keyword.
        casacore::TableRecord rec;
        casacore::String error;
        if (imageInfo().toRecord(error, rec)) {
            tab.rwKeywordSet().defineRecord("imageinfo", rec);
        } else {
            // Could not convert to record.
            casacore::LogIO os;
            os << casacore::LogIO::SEVERE << "Error saving ImageInfo in image " << name() << "; " << error << casacore::LogIO::POST;
            ok = casacore::False;
        }
    }
    return ok;
}

template <class T>
casacore::Bool ADIOSImage<T>::setMiscInfo(const casacore::RecordInterface& newInfo) {
    setMiscInfoMember(newInfo);
    casacore::Table& tab = table();
    if (tab.keywordSet().isDefined("miscinfo")) {
        tab.rwKeywordSet().removeField("miscinfo");
    }
    tab.rwKeywordSet().defineRecord("miscinfo", newInfo);
    return casacore::True;
}

template <class T>
void ADIOSImage<T>::attach_logtable() {
    // Open logtable as readonly if main table is not writable.
    casacore::Table& tab = table();
    setLogMember(casacore::LoggerHolder(name() + "/logtable", tab.isWritable()));
    // Insert the keyword if possible and if it does not exist yet.
    if (tab.isWritable() && !tab.keywordSet().isDefined("logtable")) {
        tab.rwKeywordSet().defineTable("logtable", casacore::Table(name() + "/logtable"));
    }
}

template <class T>
void ADIOSImage<T>::setTableType() {
    casacore::TableInfo& info(table().tableInfo());
    const casacore::String reqdType = info.type(casacore::TableInfo::PAGEDIMAGE);
    if (info.type() != reqdType) {
        info.setType(reqdType);
    }
    const casacore::String reqdSubType = info.subType(casacore::TableInfo::PAGEDIMAGE);
    if (info.subType() != reqdSubType) {
        info.setSubType(reqdSubType);
    }
}

template <class T>
void ADIOSImage<T>::restoreAll(const casacore::TableRecord& rec) {
    // Restore the coordinates.
    casacore::CoordinateSystem* restoredCoords = casacore::CoordinateSystem::restore(rec, "coords");
    AlwaysAssert(restoredCoords != 0, casacore::AipsError);
    setCoordsMember(*restoredCoords);
    delete restoredCoords;
    // Restore the image info.
    restoreImageInfo(rec);
    // Restore the units.
    restoreUnits(rec);
    // Restore the miscinfo.
    restoreMiscInfo(rec);
}

template <class T>
void ADIOSImage<T>::applyMaskSpecifier(const casacore::MaskSpecifier& spec) {
    // Use default mask if told to do so.
    // If it does not exist, use no mask.
    casacore::String name = spec.name();
    if (spec.useDefault()) {
        name = getDefaultMask();
        if (!hasRegion(name, casacore::RegionHandler::Masks)) {
            name = casacore::String();
        }
    }
    applyMask(name);
}

template <class T>
void ADIOSImage<T>::restoreImageInfo(const casacore::TableRecord& rec) {
    if (rec.isDefined("imageinfo")) {
        casacore::String error;
        casacore::ImageInfo info;
        casacore::Bool ok = info.fromRecord(error, rec.asRecord("imageinfo"));
        if (ok) {
            setImageInfoMember(info);
        } else {
            casacore::LogIO os;
            os << casacore::LogIO::WARN << "Failed to restore the ImageInfo in image " << name() << "; " << error << casacore::LogIO::POST;
        }
    }
}

template <class T>
void ADIOSImage<T>::restoreUnits(const casacore::TableRecord& rec) {
    casacore::Unit retval;
    casacore::String unitName;
    if (rec.isDefined("units")) {
        if (rec.dataType("units") != casacore::TpString) {
            casacore::LogIO os;
            os << casacore::LogOrigin("ADIOSImage<T>", "units()", WHERE)
               << "'units' keyword in image table is not a string! Units not restored." << casacore::LogIO::SEVERE << casacore::LogIO::POST;
        } else {
            rec.get("units", unitName);
        }
    }
    if (!unitName.empty()) {
        // OK, non-empty unit, see if it's valid, if not try some known things to
        // make a valid unit out of it.
        if (!casacore::UnitVal::check(unitName)) {
            // Beam and Pixel are the most common undefined units
            casacore::UnitMap::putUser("Pixel", casacore::UnitVal(1.0), "Pixel unit");
            casacore::UnitMap::putUser("Beam", casacore::UnitVal(1.0), "Beam area");
        }
        if (!casacore::UnitVal::check(unitName)) {
            // OK, maybe we need FITS
            casacore::UnitMap::addFITS();
        }
        if (!casacore::UnitVal::check(unitName)) {
            // I give up!
            casacore::LogIO os;
            casacore::UnitMap::putUser(unitName, casacore::UnitVal(1.0, casacore::UnitDim::Dnon), unitName);
            os << casacore::LogIO::WARN << "FITS unit \"" << unitName << "\" unknown to CASA - will treat it as non-dimensional."
               << casacore::LogIO::POST;
            retval.setName(unitName);
            retval.setValue(casacore::UnitVal(1.0, casacore::UnitDim::Dnon));
        } else {
            retval = casacore::Unit(unitName);
        }
    }
    setUnitMember(retval);
}

template <class T>
void ADIOSImage<T>::restoreMiscInfo(const casacore::TableRecord& rec) {
    if (rec.isDefined("miscinfo") && rec.dataType("miscinfo") == casacore::TpRecord) {
        setMiscInfoMember(rec.asRecord("miscinfo"));
    }
}

template <class T>
void ADIOSImage<T>::applyMask(const casacore::String& maskName) {
    // No region if no mask name is given.
    if (maskName.empty()) {
        delete regionPtr_p;
        regionPtr_p = 0;
        return;
    }
    // Reconstruct the ImageRegion object.
    // Turn the region into lattice coordinates.
    casacore::ImageRegion* regPtr = getImageRegionPtr(maskName, casacore::RegionHandler::Masks);
    casacore::LatticeRegion* latReg = new casacore::LatticeRegion(regPtr->toLatticeRegion(coordinates(), shape()));
    delete regPtr;
    // The mask has to cover the entire image.
    if (latReg->shape() != shape()) {
        delete latReg;
        throw(casacore::AipsError("PagedImage::setDefaultMask - region " + maskName + " does not cover the full image"));
    }
    // Replace current by new mask.
    delete regionPtr_p;
    regionPtr_p = latReg;
}

template <class T>
casacore::Table& ADIOSImage<T>::getTable(void* imagePtr, casacore::Bool writable) {
    ADIOSImage<T>* im = static_cast<ADIOSImage<T>*>(imagePtr);
    return im->tab_p;
}

template <class T>
casacore::Bool ADIOSImage<T>::setCoordinateInfo(const casacore::CoordinateSystem& coords) {
    casacore::Bool ok = casacore::ImageInterface<T>::setCoordinateInfo(coords);
    if (ok) {
        casacore::Table& tab = table();
        // Update the coordinates
        if (tab.keywordSet().isDefined("coords")) {
            tab.rwKeywordSet().removeField("coords");
        }
        if (!(coordinates().save(tab.rwKeywordSet(), "coords"))) {
            casacore::LogIO os;
            os << casacore::LogIO::SEVERE << "Error saving coordinates in image " << name() << casacore::LogIO::POST;
            ok = casacore::False;
        }
    }
    return ok;
}

template <class T>
casacore::IPosition ADIOSImage<T>::shape() const {
    return map_p.shape(row_p);
}

template <class T>
casacore::String ADIOSImage<T>::name(casacore::Bool stripPath) const {
    return tab_p.tableName();
}

template <class T>
casacore::Bool ADIOSImage<T>::ok() const {
    casacore::Int okay = (map_p.ndim(row_p) == coordinates().nPixelAxes());
    return okay ? casacore::True : casacore::False;
}

template <class T>
casacore::Bool ADIOSImage<T>::doGetSlice(casacore::Array<T>& buffer, const casacore::Slicer& theSlice) {
    map_p.getSlice(row_p, theSlice, buffer, casacore::True);
    return casacore::False;
}

template <class T>
void ADIOSImage<T>::doPutSlice(
    const casacore::Array<T>& sourceBuffer, const casacore::IPosition& where, const casacore::IPosition& stride) {
    const casacore::uInt arrDim = sourceBuffer.ndim();
    const casacore::uInt latDim = ndim();
    AlwaysAssert(arrDim <= latDim, casacore::AipsError);
    if (arrDim == latDim) {
        casacore::Slicer section(where, sourceBuffer.shape(), stride, casacore::Slicer::endIsLength);
        map_p.putSlice(row_p, section, sourceBuffer);
    } else {
        casacore::Array<T> degenerateArr(sourceBuffer.addDegenerate(latDim - arrDim));
        casacore::Slicer section(where, degenerateArr.shape(), stride, casacore::Slicer::endIsLength);
        map_p.putSlice(row_p, section, degenerateArr);
    }
}

template <class T>
const casacore::LatticeRegion* ADIOSImage<T>::getRegionPtr() const {
    return regionPtr_p;
}

template <class T>
casacore::ImageInterface<T>* ADIOSImage<T>::cloneII() const {
    return new ADIOSImage<T>(*this);
}

template <class T>
void ADIOSImage<T>::resize(const casacore::TiledShape& newShape) {
    if (newShape.shape().nelements() != coordinates().nPixelAxes()) {
        throw(
            casacore::AipsError("ADIOSImage<T>::resize: coordinate info is "
                                "the incorrect shape."));
    }
    casacore::IPosition tileShape = newShape.tileShape();
    map_p.setShape(row_p, newShape.shape(), tileShape);
}

template <class T>
casacore::String ADIOSImage<T>::imageType() const {
    return className();
}

template <class T>
void ADIOSImage<T>::reopenRW() {
    if (!tab_p.isWritable()) {
        tab_p.reopenRW();
        map_p = casacore::ArrayColumn<T>(tab_p, "map");
    }
}

template <class T>
void ADIOSImage<T>::reopenColumn() {
    map_p = casacore::ArrayColumn<T>(tab_p, "map");
}

template <class T>
casacore::Bool ADIOSImage<T>::isPaged() const {
    return casacore::True;
}

template <class T>
void ADIOSImage<T>::setDefaultMask(const casacore::String& maskName) {
    // No region if no mask name is given.
    if (maskName.empty()) {
        delete regionPtr_p;
        regionPtr_p = 0;
        return;
    }
    // Reconstruct the ImageRegion object.
    // Turn the region into lattice coordinates.
    casacore::ImageRegion* regPtr = getImageRegionPtr(maskName, casacore::RegionHandler::Masks);
    casacore::LatticeRegion* latReg = new casacore::LatticeRegion(regPtr->toLatticeRegion(coordinates(), shape()));
    delete regPtr;
    // The mask has to cover the entire image.
    if (latReg->shape() != shape()) {
        delete latReg;
        throw(casacore::AipsError("ADIOSImage::setDefaultMask - region " + maskName + " does not cover the full image"));
    }
    // Replace current by new mask.
    delete regionPtr_p;
    regionPtr_p = latReg;

    casacore::ImageInterface<T>::setDefaultMask(maskName);
}

template <class T>
casacore::Lattice<casacore::Bool>& ADIOSImage<T>::pixelMask() {
    if (regionPtr_p == 0) {
        throw(casacore::AipsError("ADIOSImage::pixelMask - no pixelmask used"));
    }
    return *regionPtr_p;
}
#endif // CARTA_SRC_IMAGEDATA_ADIOSIMAGE_TCC_
