#ifndef CARTA_BACKEND_FILELIST_FITSHDULIST_H_
#define CARTA_BACKEND_FILELIST_FITSHDULIST_H_

#include <casacore/fits/FITS/FITSError.h>
#include <casacore/fits/FITS/hdu.h>

#include <carta-protobuf/defs.pb.h>

inline void FitsInfoErrHandler(const char* err_message, casacore::FITSError::ErrorLevel severity) {
    if (severity > casacore::FITSError::WARN)
        std::cout << err_message << std::endl;
}

class FitsHduList {
public:
    FitsHduList(const std::string& filename);
    bool GetHduList(CARTA::FileInfo* file_info);

private:
    bool IsImageHdu(casacore::FITS::HDUType hdu_type);
    void GetFitsHduInfo(casacore::FitsInput& fits_input, int& ndim, std::string& ext_name);

    std::string _filename;
};

#endif // CARTA_BACKEND_FILELIST_FITSHDULIST_H_
