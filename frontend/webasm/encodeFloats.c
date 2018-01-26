#include <stdio.h>
#include <emscripten/emscripten.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zfp.h"

// Compile with:
// emcc -o encodeFloats.html webasm/encodeFloats.c webasm/zfp/src/*.c -lm -I webasm/zfp/include -O3 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s NO_EXIT_RUNTIME=1 -s EXPORTED_FUNCTIONS='["_encodeFloats", "_zfpDecompress", "_malloc", "_free"]' -s EXTRA_EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]'

int main(int argc, char **argv) {
  printf("Loaded encodeFloats\n");
}

#ifdef __cplusplus
extern "C" {
#endif

int EMSCRIPTEN_KEEPALIVEencodeFloats(float *arr, char *rgba, int N) {
  unsigned int *u32arr = arr;
  for (int i = 0, offset = 0; i < N; i++, offset += 4) {
    unsigned int xi = u32arr[i];
    int v = (xi >> 31 & 1) | ((xi & 0x7fffff) << 1);
    rgba[offset] = v/0x10000;
    rgba[offset + 1] = (v%0x10000)/0x100;
    rgba[offset + 2] = v%0x100;
    rgba[offset + 3] = xi >> 23 & 0xff;
  }
  return 0;
}

int EMSCRIPTEN_KEEPALIVEzfpDecompress(int precision, float *array, int nx, int ny, unsigned char *buffer, int compressedSize)
{
  int status = 0;    /* return value: 0 = success */
  zfp_type type;     /* array scalar type */
  zfp_field *field;  /* array meta data */
  zfp_stream *zfp;   /* compressed stream */
  bitstream *stream; /* bit stream to write to or read from */
  type = zfp_type_float;
  field = zfp_field_2d(array, type, nx, ny);
  zfp = zfp_stream_open(NULL);

  zfp_stream_set_precision(zfp, precision);
  stream = stream_open(buffer, compressedSize);
  zfp_stream_set_bit_stream(zfp, stream);
  zfp_stream_rewind(zfp);

  if (!zfp_decompress(zfp, field)) {
    printf("Decompression failed!\n");
    status = 1;
  }

  zfp_field_free(field);
  zfp_stream_close(zfp);
  stream_close(stream);

  return status;
}

#ifdef __cplusplus
}
#endif