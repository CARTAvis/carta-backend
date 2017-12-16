/* minimal code example showing how to call the zfp (de)compressor */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zfp.h"
#include <emscripten/emscripten.h>

/* compress or decompress array */
int EMSCRIPTEN_KEEPALIVE compress(float* array, int nx, int ny, float precision, int decompress)
{
  int status = 0;    /* return value: 0 = success */
  zfp_type type;     /* array scalar type */
  zfp_field* field;  /* array meta data */
  zfp_stream* zfp;   /* compressed stream */
  void* buffer;      /* storage for compressed stream */
  size_t bufsize;    /* byte size of compressed buffer */
  bitstream* stream; /* bit stream to write to or read from */
  size_t zfpsize;    /* byte size of compressed stream */

  /* allocate meta data for the 3D array a[nz][ny][nx] */
  type = zfp_type_float;
  field = zfp_field_2d(array, type, nx, ny);

  /* allocate meta data for a compressed stream */
  zfp = zfp_stream_open(NULL);

  /* set compression mode and parameters via one of three functions */
/*  zfp_stream_set_rate(zfp, rate, type, 3, 0); */
  zfp_stream_set_precision(zfp, precision);
//  zfp_stream_set_accuracy(zfp, tolerance);

  /* allocate buffer for compressed data */
  bufsize = zfp_stream_maximum_size(zfp, field);
  buffer = malloc(bufsize);

  /* associate bit stream with allocated buffer */
  stream = stream_open(buffer, bufsize);
  zfp_stream_set_bit_stream(zfp, stream);
  zfp_stream_rewind(zfp);

  /* compress or decompress entire array */
  if (decompress) {
    /* read compressed stream and decompress array */
    zfpsize = fread(buffer, 1, bufsize, stdin);
    if (!zfp_decompress(zfp, field)) {
      fprintf(stderr, "decompression failed\n");
      status = 1;
    }
  }
  else {
    /* compress array and output compressed stream */
    zfpsize = zfp_compress(zfp, field);
    if (!zfpsize) {
      fprintf(stderr, "compression failed\n");
      status = 1;
    }
    else
      fwrite(buffer, 1, zfpsize, stdout);
  }

  /* clean up */
  zfp_field_free(field);
  zfp_stream_close(zfp);
  stream_close(stream);
  free(buffer);
  free(array);

  return status;
}

int main(int argc, char* argv[])
{
  /* use -d to decompress rather than compress data */
  int decompress = (argc == 2 && !strcmp(argv[1], "-d"));

  /* allocate 100x100x100 array of doubles */
  int nx = 100;
  int ny = 100;
  float* array = malloc(nx * ny * sizeof(float));

  if (!decompress) {
    /* initialize array to be compressed */
    int i, j;
      for (j = 0; j < ny; j++)
        for (i = 0; i < nx; i++) {
          float x = 2.0 * i / nx;
          float y = 2.0 * j / ny;
          array[i + nx * j] = exp(-(x * x + y * y));
        }
  }

  /* compress or decompress array */
  return compress(array, nx, ny, 12, decompress);
}

