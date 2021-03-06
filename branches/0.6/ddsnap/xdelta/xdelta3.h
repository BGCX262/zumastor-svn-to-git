/* xdelta 3 - delta compression tools and library
 * Copyright (C) 2001, 2003, 2004, 2005, 2006.  Joshua P. MacDonald
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Welcome to Xdelta.  If you want to know more about Xdelta, start by reading xdelta3.c.
 * If you are ready to use the API, continue reading here.  There are two interfaces --
 * xd3_encode_input and xd3_decode_input -- plus a dozen or so related calls.  This
 * interface is styled after Zlib. */

#ifndef _XDELTA3_H_
#define _XDELTA3_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/**********************************************************************/

/* Default configured value of stream->winsize.  If the program supplies
 * xd3_encode_input() with data smaller than winsize the stream will
 * automatically buffer the input, otherwise the input buffer is used directly.
 */
#ifndef XD3_DEFAULT_WINSIZE
#define XD3_DEFAULT_WINSIZE (1U << 18)
#endif

/* The source block size.
 */
#ifndef XD3_DEFAULT_SRCBLKSZ
#define XD3_DEFAULT_SRCBLKSZ (1U << 18)
#endif

/* The source window starts with only a few checksums, then doubles up to
 * XD3_DEFAULT_MAX_CKSUM_ADVANCE.
 *
 * TODO: Note! I've seen very unstable compression performance when this is
 * set to a non-power-of-two!  e.g., testcase3 with -W 150000, but not with
 * -W 100000 or -W 200000. */
#ifndef XD3_DEFAULT_START_CKSUM_ADVANCE
#define XD3_DEFAULT_START_CKSUM_ADVANCE 1024
#endif

/* TODO: There is no command-line flag to set this value. */
#ifndef XD3_DEFAULT_MAX_CKSUM_ADVANCE
#define XD3_DEFAULT_MAX_CKSUM_ADVANCE (1U << 23)
#endif

/* Default total size of the source window used in xdelta3-main.h */
#ifndef XD3_DEFAULT_SRCWINSZ
#define XD3_DEFAULT_SRCWINSZ (1U << 23)
#endif

/* Default configured value of stream->memsize.  This dictates how much memory Xdelta will
 * use for string-matching data structures. */
#ifndef XD3_DEFAULT_MEMSIZE
#define XD3_DEFAULT_MEMSIZE (1U << 18)
#endif

/* When Xdelta requests a memory allocation for certain buffers, it rounds up to units of
 * at least this size.  The code assumes (and asserts) that this is a power-of-two. */
#ifndef XD3_ALLOCSIZE
#define XD3_ALLOCSIZE (1U<<13)
#endif

/* The XD3_HARDMAXWINSIZE parameter is a safety mechanism to protect decoders against
 * malicious files.  The decoder will never decode a window larger than this.  If the file
 * specifies VCD_TARGET the decoder may require two buffers of this size.  Rationale for
 * choosing 22-bits as a maximum: this means that in the worst case, any VCDIFF address
 * without a copy window will require 3 bytes to encode (7 bits per byte, HERE and SAME
 * modes making every address within half the window away. */
#ifndef XD3_HARDMAXWINSIZE
#define XD3_HARDMAXWINSIZE (1U<<23)
#endif

/* The XD3_NODECOMPRESSSIZE parameter tells the xdelta main routine not to try to
 * externally-decompress source inputs that are too large.  Since these files must be
 * seekable, they are decompressed to a temporary file location and the user may not wish
 * for this. */
#ifndef XD3_NODECOMPRESSSIZE
#define XD3_NODECOMPRESSSIZE (1U<<24)
#endif

/* The IOPT_SIZE value sets the size of a buffer used to batch overlapping copy
 * instructions before they are optimized by picking the best non-overlapping ranges.  The
 * larger this buffer, the longer a forced xd3_srcwin_setup() decision is held off. */
#ifndef XD3_DEFAULT_IOPT_SIZE
#define XD3_DEFAULT_IOPT_SIZE    128
#endif

/* The maximum distance backward to search for small matches */
#ifndef XD3_DEFAULT_SPREVSZ
#define XD3_DEFAULT_SPREVSZ (1U << 16)
#endif

/* Sizes and addresses within VCDIFF windows are represented as usize_t
 *
 * For source-file offsets and total file sizes, total input and output counts, the xoff_t
 * type is used.  The decoder and encoder generally check for overflow of the xoff_t size,
 * and this is tested at the 32bit boundary [xdelta3-test.h].
 */
#ifndef _WIN32
typedef unsigned int    usize_t;
typedef u_int8_t        uint8_t;
typedef u_int16_t       uint16_t;
typedef u_int32_t       uint32_t;
typedef u_int64_t       uint64_t;
#else
#include <windows.h>
#define INLINE
typedef unsigned int   uint;
typedef unsigned int   usize_t
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long  uint32_t;
typedef ULONGLONG      uint64_t;
#endif

#define SIZEOF_USIZE_T 4

#ifndef XD3_USE_LARGEFILE64
#define XD3_USE_LARGEFILE64 1
#endif

#if XD3_USE_LARGEFILE64
#define __USE_FILE_OFFSET64 1 /* GLIBC: for 64bit fileops, ... ? */
typedef uint64_t xoff_t;
#define SIZEOF_XOFF_T 8
#else
typedef uint32_t xoff_t;
#define SIZEOF_XOFF_T 4
#endif

#define USE_UINT32 (SIZEOF_USIZE_T == 4 || SIZEOF_XOFF_T == 4 || REGRESSION_TEST)
#define USE_UINT64 (SIZEOF_USIZE_T == 8 || SIZEOF_XOFF_T == 8 || REGRESSION_TEST)

/**********************************************************************/

#ifndef INLINE
#define INLINE inline
#endif

/* Whether to build the encoder, otherwise only build the decoder. */
#ifndef XD3_ENCODER
#define XD3_ENCODER 1
#endif

/* The code returned when main() fails, also defined in system includes. */
#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

/* REGRESSION TEST enables the "xdelta3 test" command, which runs a series of self-tests. */
#ifndef REGRESSION_TEST
#define REGRESSION_TEST 0
#endif

/* XD3_DEBUG=1 enables assertions and various statistics.  Levels > 1 enable some
 * additional output only useful during development and debugging. */
#ifndef XD3_DEBUG
#define XD3_DEBUG 0
#endif

#ifndef PYTHON_MODULE
#define PYTHON_MODULE 0
#endif

/* There are three string matching functions supplied: one fast, one slow (default), and
 * one soft-configurable.  To disable any of these, use the following definitions. */
#ifndef XD3_BUILD_SLOW
#define XD3_BUILD_SLOW 1
#endif
#ifndef XD3_BUILD_FAST
#define XD3_BUILD_FAST 1
#endif
#ifndef XD3_BUILD_SOFT
#define XD3_BUILD_SOFT 1
#endif

#if XD3_DEBUG
#include <stdio.h>
#endif

/* XPRINT.  Debug output and VCDIFF_TOOLS functions report to stderr.  I have used an
 * irregular style to abbreviate [fprintf(stderr, "] as [P(RINT "]. */
#define P    fprintf
#define RINT stderr,

typedef struct _xd3_stream             xd3_stream;
typedef struct _xd3_source             xd3_source;
typedef struct _xd3_hash_cfg           xd3_hash_cfg;
typedef struct _xd3_smatcher           xd3_smatcher;
typedef struct _xd3_rinst              xd3_rinst;
typedef struct _xd3_dinst              xd3_dinst;
typedef struct _xd3_hinst              xd3_hinst;
typedef struct _xd3_rpage              xd3_rpage;
typedef struct _xd3_addr_cache         xd3_addr_cache;
typedef struct _xd3_output             xd3_output;
typedef struct _xd3_desect             xd3_desect;
typedef struct _xd3_iopt_buf           xd3_iopt_buf;
typedef struct _xd3_rlist              xd3_rlist;
typedef struct _xd3_sec_type           xd3_sec_type;
typedef struct _xd3_sec_cfg            xd3_sec_cfg;
typedef struct _xd3_sec_stream         xd3_sec_stream;
typedef struct _xd3_config             xd3_config;
typedef struct _xd3_code_table_desc    xd3_code_table_desc;
typedef struct _xd3_code_table_sizes   xd3_code_table_sizes;
typedef struct _xd3_slist              xd3_slist;

/* The stream configuration has three callbacks functions, all of which may be supplied
 * with NULL values.  If config->getblk is provided as NULL, the stream returns
 * XD3_GETSRCBLK. */

typedef void*  (xd3_alloc_func)    (void       *opaque,
				    usize_t      items,
				    usize_t      size);
typedef void   (xd3_free_func)     (void       *opaque,
				    void       *address);

typedef int    (xd3_getblk_func)   (xd3_stream *stream,
				    xd3_source *source,
				    xoff_t      blkno);

/* These are internal functions to delay construction of encoding tables and support
 * alternate code tables.  See the comments & code enabled by GENERIC_ENCODE_TABLES. */

typedef const xd3_dinst* (xd3_code_table_func) (void);
typedef int              (xd3_comp_table_func) (xd3_stream *stream, const uint8_t **data, usize_t *size);


/* Some junk. */

#ifndef XD3_ASSERT
#if XD3_DEBUG
#define XD3_ASSERT(x) \
    do { if (! (x)) { P(RINT "%s:%d: XD3 assertion failed: %s\n", __FILE__, __LINE__, #x); \
    abort (); } } while (0)
#else
#define XD3_ASSERT(x) (void)0
#endif
#endif

#ifdef __GNUC__
/* As seen on linux-kernel. */
#ifndef max
#define max(x,y) ({ \
	const typeof(x) _x = (x);	\
	const typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })
#endif

#ifndef min
#define min(x,y) ({ \
	const typeof(x) _x = (x);	\
	const typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })
#endif
#else
#ifndef max
#define max(x,y) ((x) < (y) ? (y) : (x))
#endif
#ifndef min
#define min(x,y) ((x) < (y) ? (x) : (y))
#endif
#endif

/******************************************************************************************
 PUBLIC ENUMS
 ******************************************************************************************/

/* These are the five ordinary status codes returned by the xd3_encode_input() and
 * xd3_decode_input() state machines. */
typedef enum {

  /* An application must be prepared to handle these five return values from either
   * xd3_encode_input or xd3_decode_input, except in the case of no-source compression, in
   * which case XD3_GETSRCBLK is never returned.  More detailed comments for these are
   * given in xd3_encode_input and xd3_decode_input comments, below. */
  XD3_INPUT     = -17703, /* need input */
  XD3_OUTPUT    = -17704, /* have output */
  XD3_GETSRCBLK = -17705, /* need a block of source input (with no xd3_getblk function),
			   * a chance to do non-blocking read. */
  XD3_GOTHEADER = -17706, /* (decode-only) after the initial VCDIFF & first window header */
  XD3_WINSTART  = -17707, /* notification: returned before a window is processed, giving a
			   * chance to XD3_SKIP_WINDOW or not XD3_SKIP_EMIT that window. */
  XD3_WINFINISH = -17708, /* notification: returned after encode/decode & output for a window */

} xd3_rvalues;

/* special values in config->flags */
typedef enum
{
  XD3_JUST_HDR       = (1 << 1),   /* used by VCDIFF tools, see xdelta3-main.h. */
  XD3_SKIP_WINDOW    = (1 << 2),   /* used by VCDIFF tools, see xdelta3-main.h. */
  XD3_SKIP_EMIT      = (1 << 3),   /* used by VCDIFF tools, see xdelta3-main.h. */
  XD3_FLUSH          = (1 << 4),   /* flush the stream buffer to prepare for xd3_stream_close(). */

  XD3_SEC_DJW        = (1 << 5),   /* use DJW static huffman */
  XD3_SEC_FGK        = (1 << 6),   /* use FGK adaptive huffman */
  XD3_SEC_TYPE       = (XD3_SEC_DJW | XD3_SEC_FGK),

  XD3_SEC_NODATA     = (1 << 7),   /* disable secondary compression of the data section. */
  XD3_SEC_NOINST     = (1 << 8),   /* disable secondary compression of the inst section. */
  XD3_SEC_NOADDR     = (1 << 9),   /* disable secondary compression of the addr section (which is most random). */

  XD3_SEC_OTHER      = (XD3_SEC_NODATA | XD3_SEC_NOINST | XD3_SEC_NOADDR),

  XD3_ADLER32        = (1 << 10),  /* enable checksum computation in the encoder. */
  XD3_ADLER32_NOVER  = (1 << 11),  /* disable checksum verification in the decoder. */

  XD3_ALT_CODE_TABLE = (1 << 12),  /* for testing the alternate code table encoding. */

  XD3_NOCOMPRESS     = (1 << 13),  /* disable ordinary data compression feature,
				    * only search the source, not the target. */
  XD3_BEGREEDY       = (1 << 14),  /* disable the "1.5-pass algorithm", instead use
				    * greedy matching.  Greedy is off by default. */
} xd3_flags;

/* The values of this enumeration are set in xd3_config using the smatch_cfg variable.  It
 * can be set to slow, fast, soft, or default.  The fast and slow setting uses preset,
 * hardcoded parameters and the soft setting is accompanied by user-supplied parameters.
 * If the user supplies 'default' the code selects one of the available string matchers.
 * Due to compile-time settings (see XD3_SLOW_SMATCHER, XD3_FAST_SMATCHER,
 * XD3_SOFT_SMATCHER variables), not all options may be available. */
typedef enum
{
  XD3_SMATCH_DEFAULT = 0,
  XD3_SMATCH_SLOW    = 1,
  XD3_SMATCH_FAST    = 2,
  XD3_SMATCH_SOFT    = 3,
} xd3_smatch_cfg;

/******************************************************************************************
 PRIVATE ENUMS
 ******************************************************************************************/

/* stream->match_state is part of the xd3_encode_input state machine for source matching:
 *
 *  1. the XD3_GETSRCBLK block-read mechanism means reentrant matching
 *  2. this state spans encoder windows: a match and end-of-window will continue in the next
 *  3. the initial target byte and source byte are a presumed match, to avoid some computation
 *  in case the inputs are identical.
 */
typedef enum {

  MATCH_TARGET    = 0, /* in this state, attempt to match the start of the target with the
			* previously set source address (initially 0). */
  MATCH_BACKWARD  = 1, /* currently expanding a match backward in the source/target. */
  MATCH_FORWARD   = 2, /* currently expanding a match forward in the source/target. */
  MATCH_SEARCHING = 3, /* currently searching for a match. */

} xd3_match_state;

/* The xd3_encode_input state machine steps through these states in the following order.
 * The matcher is reentrant and returns XD3_INPUT whenever it requires more data.  After
 * receiving XD3_INPUT, if the application reads EOF it should call xd3_stream_close().
 */
typedef enum {

  ENC_INIT      = 0, /* xd3_encode_input has never been called. */
  ENC_INPUT     = 1, /* waiting for xd3_avail_input () to be called. */
  ENC_SEARCH    = 2, /* currently searching for matches. */
  ENC_FLUSH     = 3, /* currently emitting output. */
  ENC_POSTOUT   = 4, /* after an output section. */
  ENC_POSTWIN   = 5, /* after all output sections. */
  ENC_ABORTED   = 6, /* abort. */
} xd3_encode_state;

/* The xd3_decode_input state machine steps through these states in the following order.
 * The matcher is reentrant and returns XD3_INPUT whenever it requires more data.  After
 * receiving XD3_INPUT, if the application reads EOF it should call xd3_stream_close().
 *
 * 0-8:   the VCDIFF header
 * 9-18:  the VCDIFF window header
 * 19-21: the three primary sections: data (which I think should have gone last), inst, addr
 * 22:    producing output: returns XD3_OUTPUT, possibly XD3_GETSRCBLK,
 * 23:    return XD3_WINFINISH, set state=9 to decode more input
 */
typedef enum {

  DEC_VCHEAD   = 0, /* VCDIFF header */
  DEC_HDRIND   = 1, /* header indicator */

  DEC_SECONDID = 2, /* secondary compressor ID */

  DEC_TABLEN   = 3, /* code table length */
  DEC_NEAR     = 4, /* code table near */
  DEC_SAME     = 5, /* code table same */
  DEC_TABDAT   = 6, /* code table data */

  DEC_APPLEN   = 7, /* application data length */
  DEC_APPDAT   = 8, /* application data */

  DEC_WININD   = 9, /* window indicator */

  DEC_CPYLEN   = 10, /* copy window length */
  DEC_CPYOFF   = 11, /* copy window offset */

  DEC_ENCLEN   = 12, /* length of delta encoding */
  DEC_TGTLEN   = 13, /* length of target window */
  DEC_DELIND   = 14, /* delta indicator */

  DEC_DATALEN  = 15, /* length of ADD+RUN data */
  DEC_INSTLEN  = 16, /* length of instruction data */
  DEC_ADDRLEN  = 17, /* length of address data */

  DEC_CKSUM    = 18, /* window checksum */

  DEC_DATA     = 19, /* data section */
  DEC_INST     = 20, /* instruction section */
  DEC_ADDR     = 21, /* address section */

  DEC_EMIT     = 22, /* producing data */

  DEC_FINISH   = 23, /* window finished */

  DEC_ABORTED  = 24, /* xd3_abort_stream */
} xd3_decode_state;

/* An application never sees these internal codes: */  
typedef enum {
  XD3_NOSECOND  = -17708, /* when secondary compression finds no improvement. */
} xd3_pvalues;

/******************************************************************************************
 internal types
 ******************************************************************************************/

/* instruction lists used in the IOPT buffer */
struct _xd3_rlist
{
  xd3_rlist  *next;
  xd3_rlist  *prev;
};

/* the raw encoding of an instruction used in the IOPT buffer */
struct _xd3_rinst
{
  uint8_t     type;
  uint8_t     xtra;
  uint8_t     code1;
  uint8_t     code2;
  usize_t      pos;
  usize_t      size;
  xoff_t      addr;
  xd3_rlist   link;
};

/* the code-table form of an single- or double-instruction */
struct _xd3_dinst
{
  uint8_t     type1;
  uint8_t     size1;
  uint8_t     type2;
  uint8_t     size2;
};

/* the decoded form of a single (half) instruction. */
struct _xd3_hinst
{
  uint8_t     type;
  usize_t      size;
  usize_t      addr;
};

/* used by the encoder to buffer output in sections.  list of blocks. */
struct _xd3_output
{
  uint8_t    *base;
  usize_t      next;
  usize_t      avail;
  xd3_output *next_page;
};

/* the VCDIFF address cache, see the RFC */
struct _xd3_addr_cache
{
  int     s_near;
  int     s_same;
  usize_t  next_slot;  /* the circular index for near */
  usize_t *near_array; /* array of size s_near        */
  usize_t *same_array; /* array of size s_same*256    */
};

/* the IOPT buffer has a used list of (ordered) instructions, possibly overlapping in
 * target addresses, awaiting a flush */
struct _xd3_iopt_buf
{
  xd3_rlist  used;
  xd3_rlist  free;
  xd3_rinst *buffer;
};

/* This is the record of a pre-compiled configuration, a subset of xd3_config.  Keep them
 * in sync!  The user never sees this structure.  Note: update XD3_SOFTCFG_VARCNT when
 * changing. */
struct _xd3_smatcher
{
  const char        *name;
  int             (*string_match) (xd3_stream  *stream);
  uint               large_look;
  uint               large_step;
  uint               small_look;
  uint               small_chain;
  uint               small_lchain;
  uint               ssmatch;
  uint               try_lazy;
  uint               max_lazy;
  uint               long_enough;
  uint               promote;
};

/* hash table size & power-of-two hash function. */
struct _xd3_hash_cfg
{
  usize_t           size;
  usize_t           shift;
  usize_t           mask;
};

/* a hash-chain link in the small match table, embedded with position and checksum */
struct _xd3_slist
{
  xd3_slist *next;
  xd3_slist *prev;
  usize_t     pos;
  usize_t     scksum;
};

/* a decoder section (data, inst, or addr).  there is an optimization to avoid copying
 * these sections if all the input is available, related to the copied field below.
 * secondation compression uses the copied2 field. */
struct _xd3_desect
{
  const uint8_t *buf;
  const uint8_t *buf_max;
  usize_t         size;
  usize_t         pos;
  uint8_t       *copied1;
  usize_t         alloc1;
  uint8_t       *copied2;
  usize_t         alloc2;
};

/******************************************************************************************
 public types
 ******************************************************************************************/

/* Settings for the secondary compressor. */
struct _xd3_sec_cfg
{
  int                data_type;     /* Which section. (set automatically) */
  int                ngroups;       /* Number of DJW Huffman groups. */
  int                sector_size;   /* Sector size. */
  int                inefficient;   /* If true, ignore efficiency check [avoid XD3_NOSECOND]. */
};

/* This is the user-visible stream configuration. */
struct _xd3_config
{
  usize_t             memsize;       /* How much memory Xdelta may allocate */
  usize_t             winsize;       /* The encoder window size. */
  usize_t             sprevsz;       /* How far back small string matching goes */
  usize_t             iopt_size;     /* entries in the instruction-optimizing buffer */

  usize_t             srcwin_size;   /* Initial size of the source-window lookahead */
  usize_t  srcwin_maxsz;  /* srcwin_size grows by a factor of 2 when no matches are found */

  xd3_getblk_func   *getblk;        /* The three callbacks. */
  xd3_alloc_func    *alloc;
  xd3_free_func     *freef;
  void              *opaque;        /* Not used. */
  int                flags;         /* stream->flags are initialized from xd3_config &
				     * never modified by the library.  Use xd3_set_flags
				     * to modify flags settings mid-stream. */

  xd3_sec_cfg       sec_data;       /* Secondary compressor config: data */
  xd3_sec_cfg       sec_inst;       /* Secondary compressor config: inst */
  xd3_sec_cfg       sec_addr;       /* Secondary compressor config: addr */

  xd3_smatch_cfg     smatch_cfg;    /* See enum: use fields below for soft config */
  uint               large_look;    /* large string lookahead (i.e., hashed chars) */
  uint               large_step;    /* large string interval */
  uint               small_look;    /* small string lookahead (i.e., hashed chars) */
  uint               small_chain;   /* small string number of previous matches to try */
  uint               small_lchain;  /* small string number of previous matches to try, when a lazy match */
  uint               ssmatch;       /* boolean: insert checksums for matched strings */
  uint               try_lazy;      /* boolean: whether lazy instruction optimization is attempted */
  uint               max_lazy;      /* size of smallest match that will disable lazy matching */
  uint               long_enough;   /* size of smallest match long enough to discontinue string matching. */
  uint               promote;       /* whether to promote matches in the hash chain */
};

/* The primary source file object. You create one of these objects and initialize the first
 * four fields.  This library maintains the next 5 fields.  The configured getblk implementation is
 * responsible for setting the final 3 fields when called (and/or when XD3_GETSRCBLK is returned).
 */
struct _xd3_source
{
  /* you set */
  xoff_t              size;          /* size of this source */
  usize_t             blksize;       /* block size */
  const char         *name;          /* its name, for debug/print purposes */
  void               *ioh;           /* opaque handle */

  /* xd3 sets */
  usize_t             srclen;        /* length of this source window */
  xoff_t              srcbase;       /* offset of this source window in the source itself */
  xoff_t              blocks;        /* the total number of blocks in this source */
  usize_t             cpyoff_blocks; /* offset of copy window in blocks */
  usize_t             cpyoff_blkoff; /* offset of copy window in blocks, remainder */
  xoff_t              getblkno;      /* request block number: xd3 sets current getblk request */

  /* getblk sets */
  xoff_t              curblkno;      /* current block number: client sets after getblk request */
  usize_t             onblk;         /* number of bytes on current block: client sets, xd3 verifies */
  const uint8_t      *curblk;        /* current block array: client sets after getblk request */
};

/* The primary xd3_stream object, used for encoding and decoding.  You may access only two
 * fields: avail_out, next_out.  Use the methods above to operate on xd3_stream. */
struct _xd3_stream
{
  /* input state */
  const uint8_t    *next_in;          /* next input byte */
  usize_t           avail_in;         /* number of bytes available at next_in */
  xoff_t            total_in;         /* how many bytes in */

  /* output state */
  uint8_t          *next_out;         /* next output byte */
  usize_t           avail_out;        /* number of bytes available at next_out */
  usize_t           space_out;        /* total out space */
  xoff_t            current_window;   /* number of windows encoded/decoded */
  xoff_t            total_out;        /* how many bytes out */

  /* to indicate an error, xd3 sets */
  const char       *msg;              /* last error message, NULL if no error */

  /* source configuration */
  xd3_source       *src;              /* source array */

  /* encoder memory configuration */
  usize_t           winsize;          /* suggested window size */
  usize_t           memsize;          /* memory size parameter */
  usize_t           sprevsz;          /* small string, previous window size (power of 2) */
  usize_t           sprevmask;        /* small string, previous window size mask */
  uint              iopt_size;

  /* general configuration */
  xd3_getblk_func  *getblk;           /* set nxtblk, nxtblkno to scanblkno */
  xd3_alloc_func   *alloc;            /* malloc function */
  xd3_free_func    *free;             /* free function */
  void*             opaque;           /* private data object passed to alloc, free, and getblk */
  int               flags;            /* various options */
  int               aborted;
  
  /* secondary compressor configuration */
  xd3_sec_cfg       sec_data;         /* Secondary compressor config: data */
  xd3_sec_cfg       sec_inst;         /* Secondary compressor config: inst */
  xd3_sec_cfg       sec_addr;         /* Secondary compressor config: addr */

  /* fields common to xd3_stream_config, xd3_smatcher */
  uint              large_look;
  uint              large_step;
  uint              small_look;
  uint              small_chain;
  uint              small_lchain;
  uint              ssmatch;
  uint              try_lazy;
  uint              max_lazy;
  uint              long_enough;
  uint              promote;
  uint              srcwin_size;
  uint              srcwin_maxsz;
  int             (*string_match) (xd3_stream  *stream);

  uint              large_step_bits;  /* computed from large_step */
  uint              large_step_mask;

  usize_t           *large_table;      /* table of large checksums */
  xd3_hash_cfg      large_hash;       /* large hash config */

  usize_t           *small_table;      /* table of small checksums */
  xd3_slist        *small_prev;       /* table of previous offsets, circular linked list (no sentinel) */
  int               small_reset;      /* true if small table should be reset */

  xd3_hash_cfg      small_hash;       /* small hash config */

  xd3_addr_cache    acache;           /* the vcdiff address cache */

  xd3_encode_state  enc_state;        /* state of the encoder */

  usize_t            taroff;           /* base offset of the target input */
  usize_t            input_position;   /* current input position */
  usize_t            min_match;        /* current minimum match length, avoids redundent matches */
  usize_t            unencoded_offset; /* current input, first unencoded offset. this value is <= the first
				       * instruction's position in the iopt buffer, if there is at least one
				       * match in the buffer. */

  int               expand_point;      /* Next input position that the source checksum window should expand,
					* can be negative if a small match finishes the end of window and
					* passes expand_point. */
  int               expand_cnt;        /* Count of times the input position reaches expand_point. */
  int               end_of_source;     /* When advance_ckpos will do nothing more. */
  int               srcwin_decided;    /* boolean: true if the srclen,srcbase have been decided. */
  usize_t            srcwin_lagging_by; /* A count of the srcwin_farthest_match distance accounted
					* for lagging matches. */
  xoff_t            srcwin_cksum_pos;  /* Source checksum position */
  xoff_t            srcwin_farthest_match; /* Highest source match, tgt-window-independent */
  xoff_t            target_farthest_match; /* Position where the last target match ended. */

  xd3_match_state   match_state;      /* encoder match state */
  xoff_t            match_srcpos;     /* current match source position relative to srcbase */
  xoff_t            match_minaddr;    /* smallest matching address to set window params
				       * (reset each window xd3_encode_reset) */
  xoff_t            match_maxaddr;    /* largest matching address to set window params
				       * (reset each window xd3_encode_reset) */
  usize_t            match_back;       /* match extends back so far */
  usize_t            match_maxback;    /* match extends back maximum */
  usize_t            match_fwd;        /* match extends forward so far */
  usize_t            match_maxfwd;     /* match extends forward maximum */

  uint8_t          *buf_in;           /* for saving buffered input */
  usize_t            buf_avail;        /* amount of saved input */
  const uint8_t    *buf_leftover;     /* leftover content of next_in (i.e., user's buffer) */
  usize_t            buf_leftavail;    /* amount of leftover content */

  xd3_output       *enc_current;      /* current output buffer */
  xd3_output       *enc_free;         /* free output buffers */
  xd3_output       *enc_heads[4];     /* array of encoded outputs: head of chain */
  xd3_output       *enc_tails[4];     /* array of encoded outputs: tail of chain */

  xd3_iopt_buf      iopt;             /* instruction optimizing buffer */
  xd3_rinst        *iout;             /* next single instruction */

  const uint8_t    *enc_appheader;    /* application header to encode */
  usize_t            enc_appheadsz;    /* application header size */

  /* decoder stuff */
  xd3_decode_state  dec_state;        /* current DEC_XXX value */
  int               dec_hdr_ind;      /* VCDIFF header indicator */
  int               dec_win_ind;      /* VCDIFF window indicator */
  int               dec_del_ind;      /* VCDIFF delta indicator */

  uint8_t           dec_magic[4];     /* First four bytes */
  usize_t            dec_magicbytes;   /* Magic position. */

  int               dec_secondid;     /* Optional secondary compressor ID. */

  usize_t            dec_codetblsz;    /* Optional code table: length. */
  uint8_t          *dec_codetbl;      /* Optional code table: storage. */
  usize_t            dec_codetblbytes; /* Optional code table: position. */

  usize_t            dec_appheadsz;    /* Optional application header: size. */
  uint8_t          *dec_appheader;    /* Optional application header: storage */
  usize_t            dec_appheadbytes; /* Optional application header: position. */

  usize_t            dec_cksumbytes;   /* Optional checksum: position. */
  uint8_t           dec_cksum[4];     /* Optional checksum: storage. */
  uint32_t          dec_adler32;      /* Optional checksum: value. */

  usize_t            dec_cpylen;       /* length of copy window (VCD_SOURCE or VCD_TARGET) */
  xoff_t            dec_cpyoff;       /* offset of copy window (VCD_SOURCE or VCD_TARGET)  */
  usize_t            dec_enclen;       /* length of delta encoding */
  usize_t            dec_tgtlen;       /* length of target window */

#if USE_UINT64
  uint64_t          dec_64part;       /* part of a decoded uint64_t */
#endif
#if USE_UINT32
  uint32_t          dec_32part;       /* part of a decoded uint32_t */
#endif

  xoff_t            dec_winstart;     /* offset of the start of current target window */
  xoff_t            dec_window_count; /* == current_window + 1 in DEC_FINISH */
  usize_t            dec_winbytes;     /* bytes of the three sections so far consumed */
  usize_t            dec_hdrsize;      /* VCDIFF + app header size */

  const uint8_t    *dec_tgtaddrbase;  /* Base of decoded target addresses (addr >= dec_cpylen). */
  const uint8_t    *dec_cpyaddrbase;  /* Base of decoded copy addresses (addr < dec_cpylen). */

  usize_t            dec_position;     /* current decoder position counting the cpylen offset */
  usize_t            dec_maxpos;       /* maximum decoder position counting the cpylen offset */
  xd3_hinst         dec_current1;     /* current instruction */
  xd3_hinst         dec_current2;     /* current instruction */

  uint8_t          *dec_buffer;       /* Decode buffer */
  uint8_t          *dec_lastwin;      /* In case of VCD_TARGET, the last target window. */
  usize_t            dec_lastlen;      /* length of the last target window */
  xoff_t            dec_laststart;    /* offset of the start of last target window */
  usize_t            dec_lastspace;    /* allocated space of last target window, for reuse */

  xd3_desect        inst_sect;        /* staging area for decoding window sections */
  xd3_desect        addr_sect;
  xd3_desect        data_sect;

  xd3_code_table_func       *code_table_func;
  xd3_comp_table_func       *comp_table_func;
  const xd3_dinst           *code_table;
  const xd3_code_table_desc *code_table_desc;
  xd3_dinst                 *code_table_alloc;

  /* secondary compression */
  const xd3_sec_type *sec_type;
  xd3_sec_stream     *sec_stream_d;
  xd3_sec_stream     *sec_stream_i;
  xd3_sec_stream     *sec_stream_a;

#if XD3_DEBUG
  /* statistics */
  usize_t            n_cpy;
  usize_t            n_add;
  usize_t            n_run;

  usize_t            n_ibytes;
  usize_t            n_sbytes;
  usize_t            n_dbytes;

  usize_t            l_cpy;
  usize_t            l_add;
  usize_t            l_run;

  usize_t            sh_searches;
  usize_t            sh_compares;

  usize_t           *i_freqs;
  usize_t           *i_modes;
  usize_t           *i_sizes;

  usize_t            large_ckcnt;

  /* memory usage */
  usize_t            alloc_cnt;
  usize_t            free_cnt;

  xoff_t            n_emit;
#endif
};

/******************************************************************************************
 PUBLIC FUNCTIONS
 ******************************************************************************************/

/* The two I/O disciplines, encode and decode, have similar stream semantics.  It is
 * recommended that applications use the same code for compression and decompression -
 * because there are only a few differences in handling encoding/decoding.
 *
 * See also the xd3_avail_input() and xd3_consume_output() routines, inlined below.
 *
 *   XD3_INPUT:  the process requires more input: call xd3_avail_input() then repeat
 *   XD3_OUTPUT: the process has more output: read stream->next_out, stream->avail_out,
 *               then call xd3_consume_output(), then repeat
 *   XD3_GOTHEADER: (decoder-only) notification returned following the VCDIFF header and
 *               first window header.  the decoder may use the header to configure itself.
 *   XD3_WINSTART: a general notification returned once for each window except the 0-th
 *               window, which is implied by XD3_GOTHEADER.  It is recommended to
 *               use a switch-stmt such as:
 *                 ...
 *               again:
 *                 switch ((ret = xd3_decode_input (stream))) {
 *                    case XD3_GOTHEADER: {
 *                      assert(stream->current_window == 0);
 *                      stuff;
 *                    }
 *                    // fallthrough 
 *                    case XD3_WINSTART: {
 *                      something(stream->current_window);
 *                      goto again;
 *                    }
 *                    ...
 *   XD3_WINFINISH: a general notification, following the complete input & output of a
 *               window.  at this point, stream->total_in and stream->total_out are
 *               consistent for either encoding or decoding.
 *   XD3_GETSRCBLK: If the xd3_getblk() callback is NULL, this value is returned to
 *               initiate a non-blocking source read.
 *
 * For simple usage, see the xd3_process_completely() function, which underlies
 * xd3_encode_completely() and xd3_decode_completely() [xdelta3.c].  For real application
 * usage, including the application header, the see command-line utility [xdelta3-main.h].
 *
 * main_input() implements the command-line encode and decode as well as the optional
 * VCDIFF_TOOLS printhdr, printhdrs, and printdelta with a single loop [xdelta3-main.h].
 */
int     xd3_decode_input  (xd3_stream    *stream);
int     xd3_encode_input  (xd3_stream    *stream);

/* The xd3_config structure is used to initialize a stream - all data is copied into
 * stream so config may be a temporary variable.  See the [documentation] or comments on
 * the xd3_config structure. */
int     xd3_config_stream (xd3_stream    *stream,
			   xd3_config    *config);

/* Since Xdelta3 doesn't open any files, xd3_close_stream is just an error check that the
 * stream is in a proper state to be closed: this means the encoder is flushed and the
 * decoder is at a window boundary.  The application is responsible for freeing any of the
 * resources it supplied. */
int     xd3_close_stream (xd3_stream    *stream);

/* This unconditionally closes/frees the stream, future close() will succeed.*/
void    xd3_abort_stream (xd3_stream    *stream);

/* xd3_free_stream frees all memory allocated for the stream.  The application is
 * responsible for freeing any of the resources it supplied. */
void    xd3_free_stream   (xd3_stream    *stream);

/* This function informs the encoder or decoder that source matching (i.e.,
 * delta-compression) is possible.  For encoding, this should be called before the first
 * xd3_encode_input.  A NULL source is ignored.  For decoding, this should be called
 * before the first window is decoded, but the appheader may be read first
 * (XD3_GOTHEADER).  At this point, consult xd3_decoder_needs_source(), inlined below, to
 * determine if a source is expected by the decoder. */
int     xd3_set_source    (xd3_stream    *stream,
			   xd3_source    *source);

/* This function invokes xd3_encode_input using whole-file, in-memory inputs.  The output
 * array must be large enough to hold the output or else ENOSPC is returned. */
int     xd3_encode_completely (xd3_stream    *stream,
			       const uint8_t *input,
			       usize_t         input_size,
			       uint8_t       *output,
			       usize_t        *output_size,
			       usize_t         avail_output);

/* This function invokes xd3_decode_input using whole-file, in-memory inputs.  The output
 * array must be large enough to hold the output or else ENOSPC is returned. */
int     xd3_decode_completely (xd3_stream    *stream,
			       const uint8_t *input,
			       usize_t         input_size,
			       uint8_t       *output,
			       usize_t        *output_size,
			       usize_t         avail_size);

/* This should be called before the first call to xd3_encode_input() to include
 * application-specific data in the VCDIFF header. */
void    xd3_set_appheader (xd3_stream    *stream,
			   const uint8_t *data,
			   usize_t         size);

/* xd3_get_appheader may be called in the decoder after XD3_GOTHEADER.  For convenience,
 * the decoder always adds a single byte padding to the end of the application header,
 * which is set to zero in case the application header is a string. */
int     xd3_get_appheader (xd3_stream     *stream,
			   uint8_t       **data,
			   usize_t         *size);

/* After receiving XD3_GOTHEADER, the decoder should check this function which returns 1
 * if the decoder will require source data. */
int     xd3_decoder_needs_source (xd3_stream *stream);

/* Includes the above rvalues */
const char* xd3_strerror (int ret);

/* For convenience, zero & initialize the xd3_config structure with specified flags. */
static inline
void    xd3_init_config (xd3_config *config,
			 int         flags)
{
  memset (config, 0, sizeof (*config));
  config->flags = flags;
}

/* This supplies some input to the stream. */
static inline
void    xd3_avail_input  (xd3_stream    *stream,
			  const uint8_t *idata,
			  usize_t         isize)
{
  /* Even if isize is zero, the code expects a non-NULL idata.  Why?  It uses this value
   * to determine whether xd3_avail_input has ever been called.  If xd3_encode_input is
   * called before xd3_avail_input it will return XD3_INPUT right away without allocating
   * a stream->winsize buffer.  This is to avoid an unwanted allocation. */
  XD3_ASSERT (idata != NULL);

  /* TODO: Should check for a call to xd3_avail_input in the wrong state. */
  stream->next_in  = idata;
  stream->avail_in = isize;
}

/* This acknowledges receipt of output data, must be called after any XD3_OUTPUT
 * return. */
static inline
void xd3_consume_output (xd3_stream  *stream)
{
  /* TODO: Is it correct to set avail_in = 0 here, then check == 0 in avail_in? */
  stream->avail_out  = 0;
}

/* These are set for each XD3_WINFINISH return. */
static inline
int     xd3_encoder_used_source (xd3_stream *stream) { return stream->src != NULL && stream->src->srclen > 0; }
static inline
xoff_t  xd3_encoder_srcbase (xd3_stream *stream) { return stream->src->srcbase; }
static inline
usize_t  xd3_encoder_srclen (xd3_stream *stream) { return stream->src->srclen; }

/* Checks for legal flag changes. */
static inline
void xd3_set_flags (xd3_stream *stream, int flags)
{
  /* The bitwise difference should contain only XD3_FLUSH or XD3_SKIP_WINDOW */
  XD3_ASSERT(((flags ^ stream->flags) & ~(XD3_FLUSH | XD3_SKIP_WINDOW)) == 0);
  stream->flags = flags;
}

/* Gives some extra information about the latest library error, if any is known. */
static inline
const char* xd3_errstring (xd3_stream  *stream)
{
  return stream->msg ? stream->msg : "";
}

/* This function tells the number of bytes expected to be set in source->onblk after a
 * getblk request.  This is for convenience of handling a partial last block. */
static inline
usize_t xd3_bytes_on_srcblk (xd3_source *source, xoff_t blkno)
{
  XD3_ASSERT (blkno < source->blocks);

  if (blkno != source->blocks - 1)
      return source->blksize;
  return ((source->size - 1) % source->blksize) + 1;
}

#endif /* _XDELTA3_H_ */
