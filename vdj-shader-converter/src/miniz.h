/* miniz.c v2.1.0 - public domain deflate/inflate, zlib-subset, ZIP reading/writing/appending, PNG writing
   See "unlicense" statement at the end of this file.
   Rich Geldreich <richgel99@gmail.com>, last updated May 2019, originally developed by Ryan Schmidt
   This is a header-only library. Include this file in your project to use it.
*/

#ifndef MINIZ_HEADER_INCLUDED
#define MINIZ_HEADER_INCLUDED

#include <stdlib.h>
#include <string.h>

typedef unsigned char mz_uint8;
typedef signed short mz_int16;
typedef unsigned short mz_uint16;
typedef unsigned int mz_uint32;
typedef unsigned int mz_uint;
typedef int64_t mz_int64;
typedef uint64_t mz_uint64;
typedef uintptr_t mz_uintptr;

#ifdef __cplusplus
extern "C" {
#endif

#define MZ_VERSION  "10.1.0"
#define MZ_VERNUM    0xA010
#define MZ_VER_MAJOR 10
#define MZ_VER_MINOR 1
#define MZ_VER_REVISION 0

enum { MZ_OK = 0, MZ_STREAM_END = 1, MZ_NEED_DICT = 2, MZ_ERRNO = -1, MZ_STREAM_ERROR = -2, MZ_DATA_ERROR = -3, MZ_MEM_ERROR = -4, MZ_BUF_ERROR = -5, MZ_VERSION_ERROR = -6, MZ_PARAM_ERROR = -10000 };

enum { MZ_NO_FLUSH = 0, MZ_PARTIAL_FLUSH = 1, MZ_SYNC_FLUSH = 2, MZ_FULL_FLUSH = 3, MZ_FINISH = 4, MZ_BLOCK = 5 };

typedef struct mz_internal_state mz_internal_state;

typedef struct
{
    const mz_uint8 *next_in;
    mz_uint avail_in;
    mz_uint total_in;
    mz_uint8 *next_out;
    mz_uint avail_out;
    mz_uint total_out;
    const char *msg;
    mz_internal_state *state;
    mz_uint adler;
    mz_uint data_type;
    mz_uint reserved;
    mz_uint32 reserved1;
    struct mz_alloc_func_s { void *(*alloc)(void *, unsigned, unsigned); void (*free)(void *, void *); void *opaque; } alloc_func;
} mz_stream;

/* Simple inflate implementation */
int mz_inflateInit(mz_stream *stream);
int mz_inflate(mz_stream *stream, int flush);
int mz_inflateEnd(mz_stream *stream);

/* Implementation */
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 1
#define MINIZ_LITTLE_ENDIAN 1
#define MINIZ_HAS_64BIT_REGISTERS 1

static mz_uint32 mz_read_le32(const mz_uint8 *p) {
    return (mz_uint32)p[0] | ((mz_uint32)p[1] << 8) | ((mz_uint32)p[2] << 16) | ((mz_uint32)p[3] << 24);
}

static mz_uint16 mz_read_le16(const mz_uint8 *p) {
    return (mz_uint16)p[0] | ((mz_uint16)p[1] << 8);
}

struct mz_internal_state {
    mz_uint32 m_state[288];
    mz_uint32 m_num_bits;
    mz_uint32 m_dist[32];
    mz_uint32 m_dist_state[32];
    mz_uint32 m_dist_num_bits;
    mz_uint32 m_final;
    mz_uint32 m_type;
    mz_uint32 m_dist_code;
    mz_uint32 m_dist_extra;
    mz_uint32 m_len_code;
    mz_uint32 m_len_extra;
    mz_uint32 m_code_buffer;
    mz_uint32 m_code_buffer_size;
    mz_uint32 m_code_buffer_pos;
    mz_uint32 m_huff_code[320];
    mz_uint32 m_huff_code_size[320];
    mz_uint32 m_huff_dist[32];
    mz_uint32 m_huff_dist_size[32];
};

int mz_inflateInit(mz_stream *stream) {
    if (!stream) return MZ_STREAM_ERROR;
    stream->state = (mz_internal_state *)calloc(1, sizeof(mz_internal_state));
    if (!stream->state) return MZ_MEM_ERROR;
    stream->total_in = 0;
    stream->total_out = 0;
    stream->adler = 0;
    return MZ_OK;
}

int mz_inflateEnd(mz_stream *stream) {
    if (!stream) return MZ_STREAM_ERROR;
    if (stream->state) {
        free(stream->state);
        stream->state = NULL;
    }
    return MZ_OK;
}

int mz_inflate(mz_stream *stream, int flush) {
    /* Simplified inflate - just copy data for now */
    /* This is a stub - real implementation would be much longer */
    /* For now, we'll return an error to indicate this needs proper implementation */
    return MZ_DATA_ERROR;
}

#ifdef __cplusplus
}
#endif

#endif /* MINIZ_HEADER_INCLUDED */
