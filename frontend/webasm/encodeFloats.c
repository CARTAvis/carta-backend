#include <stdio.h>
#include <emscripten/emscripten.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zfp.h"

// Compile with:
// emcc -o encodeFloats.html webasm/encodeFloats.c webasm/zfp/src/*.c -lm -I webasm/zfp/include -O3 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1

int main(int argc, char** argv)
{
	printf("Loaded encodeFloats.wasm\n");
}

#ifdef __cplusplus
extern "C" {
#endif

int EMSCRIPTEN_KEEPALIVE encodeFloats(float *arr, char* rgba, int N) {
	unsigned int* u32arr = arr;
	for (int i = 0, offset = 0; i < N; i++, offset+=4)
	{
		unsigned int xi = u32arr[i];
		int v =(xi >> 31 & 1) |((xi & 0x7fffff) << 1);
		rgba[offset] = v / 0x10000;
		rgba[offset+1] = (v % 0x10000) / 0x100;
		rgba[offset+2] = v % 0x100;
		rgba[offset+3] = xi >> 23 & 0xff;
	}
	return 0;
}

int EMSCRIPTEN_KEEPALIVE zfpDecompress(int precision, float* array, int nx, int ny, unsigned char* buffer, int compressedSize)
{
	int status = 0;    /* return value: 0 = success */
	zfp_type type;     /* array scalar type */
	zfp_field* field;  /* array meta data */
	zfp_stream* zfp;   /* compressed stream */
	//void* buffer;      /* storage for compressed stream */
	//size_t bufsize;    /* byte size of compressed buffer */
	bitstream* stream; /* bit stream to write to or read from */
	size_t zfpsize;    /* byte size of compressed stream */
	//printf("Decompressing  %d bytes into %dx%d array of floats using p=%d\n", compressedSize, nx, ny, precision);
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
	//bufsize = zfp_stream_maximum_size(zfp, field);
	//buffer = malloc(bufsize);

	/* associate bit stream with allocated buffer */
	stream = stream_open(buffer, compressedSize);
	zfp_stream_set_bit_stream(zfp, stream);
	zfp_stream_rewind(zfp);

	//zfpsize = fread(buffer, 1, bufsize, stdin);
	if (!zfp_decompress(zfp, field)) {
		printf("Decompression failed!\n");
		status = 1;
	}
	/* clean up */
	zfp_field_free(field);
	zfp_stream_close(zfp);
	stream_close(stream);
	//free(buffer);
	//free(array);

	return status;
}



#ifdef __cplusplus
}
#endif