//# InterfaceConstants.h: definitions used in the Interface Control Document
//* Others added for implementation
#ifndef CARTA_BACKEND__INTERFACECONSTANTS_H_
#define CARTA_BACKEND__INTERFACECONSTANTS_H_

// version
#define VERSION_ID "1.3"

// thread counts
#define TBB_THREAD_COUNT 2
#define OMP_THREAD_COUNT 4

// file ids
#define ALL_FILES -1

// region ids
#define CUBE_REGION_ID -2
#define IMAGE_REGION_ID -1
#define CURSOR_REGION_ID 0

// channels
#define DEFAULT_CHANNEL 0
#define CURRENT_CHANNEL -1
#define ALL_CHANNELS -2
#define CHANNEL_NOT_SET -3

// stokes
#define DEFAULT_STOKES 0
#define CURRENT_STOKES -1

// raster image data
#define MAX_SUBSETS 8

// histograms
#define AUTO_BIN_SIZE -1
#define HISTOGRAM_START 0.0
#define HISTOGRAM_COMPLETE 1.0
#define HISTOGRAM_CANCEL -1.0

// spectral profile calculation
#define INIT_DELTA_CHANNEL 10
#define TARGET_DELTA_TIME 50 // milliseconds
#define TARGET_PARTIAL_CURSOR_TIME 500
#define TARGET_PARTIAL_REGION_TIME 1000
#define PROFILE_COMPLETE 1.0

#endif // CARTA_BACKEND__INTERFACECONSTANTS_H_
