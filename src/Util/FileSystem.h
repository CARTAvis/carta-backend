#ifndef CARTA_BACKEND__UTIL_FILESYSTEM_H_
#define CARTA_BACKEND__UTIL_FILESYSTEM_H_

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

#endif // CARTA_BACKEND__UTIL_FILESYSTEM_H_
