#include "compression.h"
#include <zfp.h>
#include <cmath>

using namespace std;

int compress(vector<float>& array, size_t offset, vector<char>& compressionBuffer, size_t& compressedSize, uint32_t nx, uint32_t ny, uint32_t precision) {
    int status = 0;    /* return value: 0 = success */
    zfp_type type;     /* array scalar type */
    zfp_field* field;  /* array meta data */
    zfp_stream* zfp;   /* compressed stream */
    size_t bufsize;    /* byte size of compressed buffer */
    bitstream* stream; /* bit stream to write to or read from */

    type = zfp_type_float;
    field = zfp_field_2d(array.data() + offset, type, nx, ny);

    /* allocate meta data for a compressed stream */
    zfp = zfp_stream_open(nullptr);

    /* set compression mode and parameters via one of three functions */
    zfp_stream_set_precision(zfp, precision);

    /* allocate buffer for compressed data */
    bufsize = zfp_stream_maximum_size(zfp, field);
    if (compressionBuffer.size() < bufsize) {
        compressionBuffer.resize(bufsize);
    }
    stream = stream_open(compressionBuffer.data(), bufsize);
    zfp_stream_set_bit_stream(zfp, stream);
    zfp_stream_rewind(zfp);

    compressedSize = zfp_compress(zfp, field);
    if (!compressedSize) {
        status = 1;
    }

    /* clean up */
    zfp_field_free(field);
    zfp_stream_close(zfp);
    stream_close(stream);

    return status;
}

int decompress(vector<float>& array, vector<char>& compressionBuffer, size_t& compressedSize, uint32_t nx, uint32_t ny, uint32_t precision) {
    int status = 0;    /* return value: 0 = success */
    zfp_type type;     /* array scalar type */
    zfp_field* field;  /* array meta data */
    zfp_stream* zfp;   /* compressed stream */
    bitstream* stream; /* bit stream to write to or read from */

    /* allocate meta data for the 3D array a[nz][ny][nx] */
    type = zfp_type_float;
    field = zfp_field_2d(array.data(), type, nx, ny);

    /* allocate meta data for a compressed stream */
    zfp = zfp_stream_open(nullptr);
    zfp_stream_set_precision(zfp, precision);

    stream = stream_open(compressionBuffer.data(), compressedSize);
    zfp_stream_set_bit_stream(zfp, stream);
    zfp_stream_rewind(zfp);

    if (!zfp_decompress(zfp, field)) {
        //fmt::print("decompression failed\n");
        status = 1;
    }
    /* clean up */
    zfp_field_free(field);
    zfp_stream_close(zfp);
    stream_close(stream);

    return status;
}

// Removes NaNs from an array and returns run-length encoded list of NaNs
vector<int32_t> getNanEncodingsSimple(vector<float>& array, int offset, int length) {
    int32_t prevIndex = offset;
    bool prev = false;
    vector<int32_t> encodedArray;
    // Find first non-NaN number in the array
    float prevValidNum = 0;
    for (auto i = offset; i < offset + length; i++) {
        if (!isnan(array[i])) {
            prevValidNum = array[i];
            break;
        }
    }

    // Generate RLE list and replace NaNs with neighbouring valid values. Ideally, this should take into account
    // the width and height of the image, and look for neighbouring values in vertical and horizontal directions,
    // but this is only an issue with NaNs right at the edge of images.
    for (auto i = offset; i < offset + length; i++) {
        bool current = isnan(array[i]);
        if (current != prev) {
            encodedArray.push_back(i - prevIndex);
            prevIndex = i;
            prev = current;
        }
        if (current) {
            array[i] = prevValidNum;
        } else {
            prevValidNum = array[i];
        }
    }
    encodedArray.push_back(offset + length - prevIndex);
    return encodedArray;
}

vector<int32_t> getNanEncodingsBlock(vector<float>& array, int offset, int w, int h) {
    // Generate RLE NaN list
    int length = w * h;
    int32_t prevIndex = offset;
    bool prev = false;
    vector<int32_t> encodedArray;

    for (auto i = offset; i < offset + length; i++) {
        bool current = isnan(array[i]);
        if (current != prev) {
            encodedArray.push_back(i - prevIndex);
            prevIndex = i;
            prev = current;
        }
    }
    encodedArray.push_back(offset + length - prevIndex);

    // Skip all-NaN images and NaN-free images
    if (encodedArray.size() > 1) {
        // Calculate average of 4x4 blocks (matching blocks used in ZFP), and replace NaNs with block average
        for (auto i = 0; i < w; i += 4) {
            for (auto j = 0; j < h; j += 4) {
                int blockStart = offset + j * w + i;
                int validCount = 0;
                float sum = 0;
                // Limit the block size when at the edges of the image
                int blockWidth = min(4, w - i);
                int blockHeight = min(4, h - j);
                for (int x = 0; x < blockWidth; x++) {
                    for (int y = 0; y < blockHeight; y++) {
                        float v = array[blockStart + (y * w) + x];
                        if (!isnan(v)) {
                            validCount++;
                            sum += v;
                        }
                    }
                }

                // Only process blocks which have at least one valid value AND at least one NaN. All-NaN blocks won't affect ZFP compression
                if (validCount && validCount != blockWidth * blockHeight) {
                    float average = sum / validCount;
                    for (int x = 0; x < blockWidth; x++) {
                        for (int y = 0; y < blockHeight; y++) {
                            float v = array[blockStart + (y * w) + x];
                            if (isnan(v)) {
                                array[blockStart + (y * w) + x] = average;
                            }
                        }
                    }
                }
            }
        }
    }
    return encodedArray;
}
