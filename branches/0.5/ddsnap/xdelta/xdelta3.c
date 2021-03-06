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

   -------------------------------------------------------------------

			       Xdelta 3

   The goal of this library is to to implement both the (stand-alone)
   data-compression and delta-compression aspects of VCDIFF encoding, and
   to support a programming interface that works like Zlib
   (http://www.gzip.org/zlib.html). See RFC3284: The VCDIFF Generic
   Differencing and Compression Data Format.

   VCDIFF is a unified encoding that combines data-compression and
   delta-encoding ("differencing").

   VCDIFF has a detailed byte-code instruction set with many features.
   The instruction format supports an immediate size operand for small
   COPYs and ADDs (e.g., under 18 bytes).  There are also instruction
   "modes", which are used to compress COPY addresses by using two
   address caches.  An instruction mode refers to slots in the NEAR
   and SAME caches for recent addresses.  NEAR remembers the
   previous 4 (by default) COPY addresses, and SAME catches
   frequent re-uses of the same address using a 3-way (by default)
   256-entry associative cache of [ADDR mod 256], the encoded byte.
   A hit in the NEAR/SAME cache requires 0/1 ADDR bytes.

   VCDIFF has a default instruction table, but an alternate
   instruction tables may themselves be be delta-compressed and
   included in the encoding header.  This allows even more freedom.
   There are 9 instruction modes in the default code table, 4 near, 3
   same, VCD_SELF (absolute encoding) and VCD_HERE (relative to the
   current position).

   ----------------------------------------------------------------------

  			      Algorithms

   Aside from the details of encoding and decoding, there are a bunch
   of algorithms needed.

   1. STRING-MATCH.  A two-level fingerprinting approach is used.  A
   single loop computes the two checksums -- small and large -- at
   successive offsets in the TARGET file.  The large checksum is more
   accurate and is used to discover SOURCE matches, which are
   potentially very long.  The small checksum is used to discover
   copies within the TARGET.  Small matching, which is more expensive,
   usually dominates the large STRING-MATCH costs in this code - the
   more exhaustive the search, the better the results.  Either of the
   two string-matching mechanisms may be disabled.  Currently, large
   checksums are only performed in the source file, if present, and
   small checksums are performed only in the left-over target input.
   However, small matches are possible in the source file too, with a
   range of possibilities.  [I've seen a paper on this subject, but
   I lost it.]

   2. INSTRUCTION SELECTION.  The IOPT buffer here represents a queue
   used to store overlapping copy instructions.  There are two possible
   optimizations that go beyond a greedy search.  Both of these fall
   into the category of "non-greedy matching" optimizations.

   The first optimization stems from backward SOURCE-COPY matching.
   When a new SOURCE-COPY instruction covers a previous instruction in
   the target completely, it is erased from the queue.  Randal Burns
   originally analyzed these algorithms and did a lot of related work
   (\cite the 1.5-pass algorithm).

   The second optimization comes by the encoding of common very-small
   COPY and ADD instructions, for which there are special DOUBLE-code
   instructions, which code two instructions in a single byte.

   The cost of bad instruction-selection overhead is relatively high
   for data-compression, relative to delta-compression, so this second
   optimization is fairly important.  With "lazy" matching (the name
   used in Zlib for a similar optimization), the string-match
   algorithm searches after a match for potential overlapping copy
   instructions.  In Xdelta and by default, VCDIFF, the minimum match
   size is 4 bytes, whereas Zlib searches with a 3-byte minimum.  This
   feature, combined with double instructions, provides a nice
   challenge.  Search in this file for "black magic", a heuristic.

   3. STREAM ALIGNMENT.  Stream alignment is needed to compress large
   inputs in constant space.  This is an open-ended problem but I hope
   to keep it simple.  Source checksums are computed for some length in
   advance of the current source->srcwin_farthest_match, which records
   the highest matching source address.  The stream->expand_point
   maintains an offset relative to the current target window position,
   and every time the expand_point is reached or surpassed, more
   checksums are computed and source->srcwin_cksum_pos is advanced.  If
   the target's expand_point is reached before a source-copy is
   discovered, the stream->srcwin_size is doubled -- and
   source->srcwin_cksum_pos is always kept ahead of
   stream->srcwin_farthest_match by stream->srcwin_size bytes.

   Whenever a source-copy is discovered, the following
   xd3_srcwin_update() considers whether the match ends before the
   srcwin_farthest_match, after the srcwin_farthest_match but before the
   srcwin_cksum_pos, or after the srcwin_cksum_pos.  In any case, the
   srcwin_cksum_pos is advanced by the difference between the
   end-of-target-match and the previous stream->target_farthest_match.

   This implies advancing the srcwin_cksum_pos even when the source
   match position falls short of the source_farthest_match; in this
   case copies are considered "lagging".  By advancing the
   srcwin_cksum_pos in this case, we risk getting too far ahead in the
   source (which reduces match quality, the worst case being complete
   disalignment).  Not advancing the srcwin_cksum_pos in this case has
   the opposite consequence: falling too far behind in the source.

   For example (falling behind), shortly after a large deletion in the
   target window the source will stop matching ahead of the
   srcwin_farthest_match until the srcwin_cksum_pos surpasses the
   deleted region.  If the target begins matching earlier in the source
   file at this point, it is said here to be "lagging".

   To avoid getting too far ahead while avoiding the opposite extreme,
   falling too far behind, a srcwin_lagging_by count is introduced.  In
   the case of a lagging copy, srcwin_lagging_by is incremented by the
   same value as srcwin_cksum_pos (as srcwin_farthest_match is
   unmodified).  The srcwin_lagging_by counts the number of bytes that
   the srcwin_cksum_pos has been advanced in the lagging case, those
   cases where stream->srcwin_fathest_match does not advance.
   [srcwin_lagging_by += end-of-target-match - target_farthest_match]

   The srcwin_lagging_by count is used to avoid getting to far ahead by
   artificially inflating srcwin_farthest_match whenever a non-lagging
   case is seen.  If the source match exceeds the current
   srcwin_cksum_pos, the lagging count is completely reset (the streams
   are assumed to be realigned, again).  In the intermediate cases,
   where the match surpasses the srcwin_farthest_match but not the
   srcwin_cksum_pos, the lagging count is cut in half and the
   srcwin_farthest_match is inflated by that amount.  Thus, when source
   copies are gaining on the srcwin_farthest_match/srcwin_cksum_pos, the
   srcwin_cksum_pos advances more slowly as the srcwin_lagging_by count
   is reduced.

   The stream->srcwin_size is never decreased, although it could be in
   those cases where the stream is re-aligned.

   Finally, the source matcher will never emit a copy further than
   XD3_DEFAULT_SRCBACK behind the latest source match, this can be used
   if the stream is non-seekable, for example, and it also limits the
   ability of spurious matches to confuse the alignment algorithm.  It
   also limits the maximum source-window size, which relates to window
   selection.

   4. WINDOW SELECTION.  When the IOPT buffer flushes, in the first call
   to xd3_iopt_finish_encoding containing any kind of copy instruction,
   the parameters of the source window must be decided: the offset into
   the source and the length of the window.  Since the IOPT buffer is
   finite, the program may be forced to fix these values before knowing
   the best offset/length.  XD3_DEFAULT_SRCBACK limits the length, but a
   smaller length is preferred because all target copies are addressed
   after source copies in the VCDIFF address space.  Picking too large a
   source window means larger address encoding.

   If the IOPT buffer is filling easily, perhaps the target window is
   too large.  In any case, a decision is made (though an alternative is
   to emit the sub-window right away, to reduce the winsize
   automatically - not implemented, another alternative is to grow the
   IOPT buffer, it is after all bounded in size by winsize.)

   The algorithm is in xd3_srcwin_setup.

   5. SECONDARY COMPRESSION.  VCDIFF supports a secondary encoding to
   be applied to the individual sections of the data format, which are
   ADDRess, INSTruction, and DATA.  Several secondary compressor
   variations are implemented here, although none is standardized yet.

   One is an adaptive huffman algorithm -- the FGK algorithm (Faller,
   Gallager, and Knuth, 1985).  This compressor is extremely slow.

   The other is a simple static Huffman routine, which is the base
   case of a semi-adaptive scheme published by D.J. Wheeler and first
   widely used in bzip2 (by Julian Seward).  This is a very
   interesting algorithm, originally published in nearly cryptic form
   by D.J. Wheeler. !!!NOTE!!! Because these are not standardized, the
   -S option (no secondary compression) remains on by default.
     ftp://ftp.cl.cam.ac.uk/users/djw3/bred3.{c,ps}
   --------------------------------------------------------------------

			    Other Features

   1. USER CONVENIENCE

   For user convenience, it is essential to recognize Gzip-compressed
   files and automatically Gzip-decompress them prior to
   delta-compression (or else no delta-compression will be achieved
   unless the user manually decompresses the inputs).  The compressed
   represention competes with Xdelta, and this must be hidden from the
   command-line user interface.  The Xdelta-1.x encoding was simple, not
   compressed itself, so Xdelta-1.x uses Zlib internally to compress the
   representation.

   This implementation supports external compression, which implements
   the necessary fork() and pipe() mechanics.  There is a tricky step
   involved to support automatic detection of a compressed input in a
   non-seekable input.  First you read a bit of the input to detect
   magic headers.  When a compressed format is recognized, exec() the
   external compression program and create a second child process to
   copy the original input stream. [Footnote: There is a difficulty
   related to using Gzip externally. It is not possible to decompress
   and recompress a Gzip file transparently.  If FILE.GZ had a
   cryptographic signature, then, after: (1) Gzip-decompression, (2)
   Xdelta-encoding, (3) Gzip-compression the signature could be
   broken.  The only way to solve this problem is to guess at Gzip's
   compression level or control it by other means.  I recommend that
   specific implementations of any compression scheme store
   information needed to exactly re-compress the input, that way
   external compression is transparent - however, this won't happen
   here until it has stabilized.]

   2. APPLICATION-HEADER

   This feature was introduced in RFC3284.  It allows any application
   to include a header within the VCDIFF file format.  This allows
   general inter-application data exchange with support for
   application-specific extensions to communicate metadata.

   3. VCDIFF CHECKSUM

   An optional checksum value is included with each window, which can
   be used to validate the final result.  This verifies the correct source
   file was used for decompression as well as the obvious advantage:
   checking the implementation (and underlying) correctness.

   4. LIGHT WEIGHT

   The code makes efforts to avoid copying data more than necessary.
   The code delays many initialization tasks until the first use, it
   optimizes for identical (perfectly matching) inputs.  It does not
   compute any checksums until the first lookup misses.  Memory usage
   is reduced.  String-matching is templatized (by slightly gross use
   of CPP) to hard-code alternative compile-time defaults.  The code
   has few outside dependencies.
   ----------------------------------------------------------------------

		The default rfc3284 instruction table:
		    (see RFC for the explanation)

           TYPE      SIZE     MODE    TYPE     SIZE     MODE     INDEX
   --------------------------------------------------------------------
       1.  Run         0        0     Noop       0        0        0
       2.  Add    0, [1,17]     0     Noop       0        0      [1,18]
       3.  Copy   0, [4,18]     0     Noop       0        0     [19,34]
       4.  Copy   0, [4,18]     1     Noop       0        0     [35,50]
       5.  Copy   0, [4,18]     2     Noop       0        0     [51,66]
       6.  Copy   0, [4,18]     3     Noop       0        0     [67,82]
       7.  Copy   0, [4,18]     4     Noop       0        0     [83,98]
       8.  Copy   0, [4,18]     5     Noop       0        0     [99,114]
       9.  Copy   0, [4,18]     6     Noop       0        0    [115,130]
      10.  Copy   0, [4,18]     7     Noop       0        0    [131,146]
      11.  Copy   0, [4,18]     8     Noop       0        0    [147,162]
      12.  Add       [1,4]      0     Copy     [4,6]      0    [163,174]
      13.  Add       [1,4]      0     Copy     [4,6]      1    [175,186]
      14.  Add       [1,4]      0     Copy     [4,6]      2    [187,198]
      15.  Add       [1,4]      0     Copy     [4,6]      3    [199,210]
      16.  Add       [1,4]      0     Copy     [4,6]      4    [211,222]
      17.  Add       [1,4]      0     Copy     [4,6]      5    [223,234]
      18.  Add       [1,4]      0     Copy       4        6    [235,238]
      19.  Add       [1,4]      0     Copy       4        7    [239,242]
      20.  Add       [1,4]      0     Copy       4        8    [243,246]
      21.  Copy        4      [0,8]   Add        1        0    [247,255]
   --------------------------------------------------------------------

  			   Major TODO list:

   [xdelta3-regtest.py]
   0. More testing, testing, testing [more TODOs in xdelta3-test.h]
   1. Do a time/space tuning suite, further optimization, cache/profile.

   2. Finalize RFC issues
   3. Get together an RFC for the static Huffman routine using Phong's
      proposed Huffman file format

   4. Decide what config variables are hard-coded, which are not.
   5. Experiment with hash functions, memory allocation parameters
   --------------------------------------------------------------------

  		       Non-immediate TODO list:

   0. Check for more invalid state transitions
   1. Large checksum duplicates.
   2. Add VCDIFF extensions for multi-source support.
   3. Do VCD_TARGET encoding support. (matching against previous target
      windows, when there is no source.
   4. Consider forcing a window mid-target as a way of handling
      source-matches outside the decided window.
   5. Handle sources larger than 2^32 * LSTEP by either inference
      on srcwin position or xoff_t large_table.
   --------------------------------------------------------------------

		     Reading the source: Overview

   This file includes itself in several passes to macro-expand certain
   sections with variable forms.  Just read ahead, there's only a
   little confusion.  I know this sounds ugly, but hard-coding some of
   the string-matching parameters results in a 10-15% increase in
   string-match performance.  The only time this hurts is when you have
   unbalanced #if/endifs.

   A single compilation unit tames the Makefile.  In short, this is to
   allow the above-described hack without an explodingMakefile.  The
   single compilation unit includes the core library features,
   configurable string-match templates, optional main() command-line
   tool, misc optional features, and a regression test.  Features are
   controled with CPP #defines, see Makefile.am.

   The initial __XDELTA3_C_HEADER_PASS__ starts first, the INLINE and
   TEMPLATE sections follow.  Easy stuff first, hard stuff last.

   Optional features include:

     xdelta3-main.h     The command-line interface, external compression
                        support, POSIX-specific, info & VCDIFF-debug tools.
     xdelta3-second.h   The common secondary compression routines.
     xdelta3-djw.h      The semi-adaptive huffman secondary encoder.
     xdelta3-fgk.h      The adaptive huffman secondary encoder.
     xdelta3-test.h     The unit test covers major algorithms,
                        encoding and decoding.  There are single-bit
                        error decoding tests.  There are 32/64-bit file size
                        boundary tests.  There are command-line tests.
                        There are compression tests.  There are external
                        compression tests.  There are string-matching tests.
			There should be more tests...

   Additional headers include:

     xdelta3.h          The public header file.
     xdelta3-cfgs.h     The default settings for default, built-in
                        encoders.  These are hard-coded at
                        compile-time.  There is also a single
                        soft-coded string matcher for experimenting
                        with arbitrary values.
     xdelta3-list.h     A cyclic list template

   Regression test & performance study:

     xdelta3-regtest.py TODO config: all the "TODO config" labels need
                        doing before good performance comparisons/tuning.

   Misc little debug utilities:

     badcopy.c          Randomly modifies an input file based on two
                        parameters: (1) the probability that a byte in
                        the file is replaced with a pseudo-random value,
                        and (2) the mean change size.  Changes are
                        generated using an expoential distribution
                        which approximates the expected error_prob
			distribution.
     show.c             Prints an offset/length segment from a file.
     testh.c            Checks that xdelta3.h is can be #included
   --------------------------------------------------------------------

   This file itself is unusually large.  I hope to defend this layout
   with lots of comments.  Everything in this file is related to
   encoding and decoding.  I like it all together - the template stuff
   is just a hack. */

#ifndef __XDELTA3_C_HEADER_PASS__
#define __XDELTA3_C_HEADER_PASS__

#include <errno.h>
#include <string.h>

#include "xdelta3.h"

/******************************************************************************************
 STATIC CONFIGURATION
 ******************************************************************************************/

#ifndef XD3_MAIN                  /* the main application */
#define XD3_MAIN 0
#endif

#ifndef VCDIFF_TOOLS
#define VCDIFF_TOOLS XD3_MAIN
#endif

#ifndef SECONDARY_FGK             /* one from the algorithm preservation department: */
#define SECONDARY_FGK 0           /* adaptive Huffman routines */
#endif

#ifndef SECONDARY_DJW             /* semi-adaptive/static Huffman for the eventual */
#define SECONDARY_DJW 0           /* standardization, off by default until such time. */
#endif

#ifndef GENERIC_ENCODE_TABLES    /* These three are the RFC-spec'd app-specific */
#define GENERIC_ENCODE_TABLES 0  /* code features.  This is tested but not recommended */
#endif  			 /* unless there's a real application. */
#ifndef GENERIC_ENCODE_TABLES_COMPUTE
#define GENERIC_ENCODE_TABLES_COMPUTE 0
#endif
#ifndef GENERIC_ENCODE_TABLES_COMPUTE_PRINT
#define GENERIC_ENCODE_TABLES_COMPUTE_PRINT 0
#endif

#if XD3_USE_LARGEFILE64          /* How does everyone else do this? */
#define Q "q"
#else
#define Q
#endif

#if XD3_ENCODER
#define IF_ENCODER(x) x
#else
#define IF_ENCODER(x)
#endif

/******************************************************************************************/

typedef enum {

  /* header indicator bits */
  VCD_SECONDARY  = (1 << 0),  /* uses secondary compressor */
  VCD_CODETABLE  = (1 << 1),  /* supplies code table data */
  VCD_APPHEADER  = (1 << 2),  /* supplies application data */
  VCD_INVHDR     = ~7U,

  /* window indicator bits */
  VCD_SOURCE     = (1 << 0),  /* copy window in source file */
  VCD_TARGET     = (1 << 1),  /* copy window in target file */
  VCD_ADLER32    = (1 << 2),  /* has adler32 checksum */
  VCD_INVWIN     = ~7U,

  VCD_SRCORTGT   = VCD_SOURCE | VCD_TARGET,

  /* delta indicator bits */
  VCD_DATACOMP   = (1 << 0),
  VCD_INSTCOMP   = (1 << 1),
  VCD_ADDRCOMP   = (1 << 2),
  VCD_INVDEL     = ~0x7U,

} xd3_indicator;

typedef enum {
  VCD_DJW_ID    = 1,
  VCD_FGK_ID    = 16, /* !!!Note: these are not a standard IANA-allocated ID!!! */
} xd3_secondary_ids;

typedef enum {
  SEC_NOFLAGS     = 0,
  SEC_COUNT_FREQS = (1 << 0), /* OPT: Not implemented: Could eliminate first pass of Huffman... */
} xd3_secondary_flags;

typedef enum {
  DATA_SECTION, /* These indicate which section to the secondary compressor. */
  INST_SECTION, /* The header section is not compressed, therefore not listed here. */
  ADDR_SECTION,
} xd3_section_type;

typedef enum
{
  XD3_NOOP = 0,
  XD3_ADD  = 1,
  XD3_RUN  = 2,
  XD3_CPY  = 3, /* XD3_CPY rtypes are represented as (XD3_CPY + copy-mode value) */
} xd3_rtype;

/******************************************************************************************/

#include "xdelta3-list.h"

XD3_MAKELIST(xd3_rlist, xd3_rinst, link);

/******************************************************************************************/

#ifndef unlikely              /* The unlikely macro - any good? */
#if defined(__GNUC__) && __GNUC__ >= 3
#define unlikely(x) __builtin_expect((x),0)
#define likely(x)   __builtin_expect((x),1)
#else
#define unlikely(x) (x)
#define likely(x)   (x)
#endif
#endif

#define SECONDARY_MIN_SAVINGS 2  /* Secondary compression has to save at least this many bytes. */
#define SECONDARY_MIN_INPUT   10 /* Secondary compression needs at least this many bytes. */

#define VCDIFF_MAGIC1  0xd6  /* 1st file byte */
#define VCDIFF_MAGIC2  0xc3  /* 2nd file byte */
#define VCDIFF_MAGIC3  0xc4  /* 3rd file byte */
#define VCDIFF_VERSION 0x00  /* 4th file byte */

#define VCD_SELF       0     /* 1st address mode */
#define VCD_HERE       1     /* 2nd address mode */

#define CODE_TABLE_STRING_SIZE (6 * 256) /* Should fit a code table string. */
#define CODE_TABLE_VCDIFF_SIZE (6 * 256) /* Should fit a compressed code table string */

#define SECONDARY_ANY (SECONDARY_DJW || SECONDARY_FGK) /* True if any secondary compressor is used. */

#define ALPHABET_SIZE      256  /* Used in test code--size of the secondary compressor alphabet. */

#define HASH_PRIME         0    /* Old hashing experiments */
#define HASH_PERMUTE       1
#define ARITH_SMALL_CKSUM  1

#define HASH_CKOFFSET      1U   /* Table entries distinguish "no-entry" from offset 0 using this offset. */

#define MIN_SMALL_LOOK    2U    /* Match-optimization stuff. */
#define MIN_LARGE_LOOK    2U
#define MIN_MATCH_OFFSET  1U
#define MAX_MATCH_SPLIT   18U   /* VCDIFF code table: 18 is the default limit for direct-coded ADD sizes */

#define LEAST_MATCH_INCR  0   /* The least number of bytes an overlapping match must beat
			       * the preceding match by.  This is a bias for the lazy
			       * match optimization.  A non-zero value means that an
			       * adjacent match has to be better by more than the step
			       * between them.  0. */

#define MIN_MATCH         4U  /* VCDIFF code table: MIN_MATCH=4 */
#define MIN_ADD           1U  /* 1 */
#define MIN_RUN           8U  /* The shortest run, if it is shorter than this an immediate
			       * add/copy will be just as good.  ADD1/COPY6 = 1I+1D+1A bytes,
			       * RUN18 = 1I+1D+1A. */

#define MAX_MODES         9  /* Maximum number of nodes used for compression--does not limit decompression. */

#define ENC_SECTS         4  /* Number of separate output sections. */

#define HDR_TAIL(s)  (stream->enc_tails[0])
#define DATA_TAIL(s) (stream->enc_tails[1])
#define INST_TAIL(s) (stream->enc_tails[2])
#define ADDR_TAIL(s) (stream->enc_tails[3])

#define HDR_HEAD(s)  (stream->enc_heads[0])
#define DATA_HEAD(s) (stream->enc_heads[1])
#define INST_HEAD(s) (stream->enc_heads[2])
#define ADDR_HEAD(s) (stream->enc_heads[3])

#define SIZEOF_ARRAY(x) (sizeof(x) / sizeof(x[0]))

#define TOTAL_MODES(x) (2+(x)->acache.s_same+(x)->acache.s_near)

/* Template instances. */
#if XD3_BUILD_SLOW
#define IF_BUILD_SLOW(x) x
#else
#define IF_BUILD_SLOW(x)
#endif
#if XD3_BUILD_FAST
#define IF_BUILD_FAST(x) x
#else
#define IF_BUILD_FAST(x)
#endif
#if XD3_BUILD_SOFT
#define IF_BUILD_SOFT(x) x
#else
#define IF_BUILD_SOFT(x)
#endif

IF_BUILD_SOFT(static const xd3_smatcher    __smatcher_soft;)
IF_BUILD_FAST(static const xd3_smatcher    __smatcher_fast;)
IF_BUILD_SLOW(static const xd3_smatcher    __smatcher_slow;)

#if XD3_DEBUG
#define SMALL_HASH_DEBUG1(s,inp)                                  \
  usize_t debug_hval = xd3_checksum_hash (& (s)->small_hash,       \
         xd3_scksum ((inp), (s)->small_look))
#define SMALL_HASH_DEBUG2(s,inp)                                  \
  XD3_ASSERT (debug_hval == xd3_checksum_hash (& (s)->small_hash, \
	 xd3_scksum ((inp), (s)->small_look)))
#define SMALL_HASH_STATS(x) x
#else
#define SMALL_HASH_DEBUG1(s,inp)
#define SMALL_HASH_DEBUG2(s,inp)
#define SMALL_HASH_STATS(x)
#endif /* XD3_DEBUG */

/* Config fields: three structures contain these variables, so this is non-typed. */
#define XD3_COPY_CONFIG_FIELDS(dst,src)       \
  do {                                        \
  (dst)->large_look    = (src)->large_look;   \
  (dst)->large_step    = (src)->large_step;   \
  (dst)->small_look    = (src)->small_look;   \
  (dst)->small_chain   = (src)->small_chain;  \
  (dst)->small_lchain  = (src)->small_lchain; \
  (dst)->ssmatch       = (src)->ssmatch;      \
  (dst)->try_lazy      = (src)->try_lazy;     \
  (dst)->max_lazy      = (src)->max_lazy;     \
  (dst)->long_enough   = (src)->long_enough;  \
  (dst)->promote       = (src)->promote;      \
  } while (0)

/* Update the run-length state */
#define NEXTRUN(c) do { if ((c) == run_c) { run_l += 1; } else { run_c = (c); run_l = 1; } } while (0)

/* Update the checksum state. */
#define LARGE_CKSUM_UPDATE(cksum,base,look)                              \
  do {                                                                   \
    uint32_t old_c = PERMUTE((base)[0]);                                    \
    uint32_t new_c = PERMUTE((base)[(look)]);                               \
    uint32_t low   = (((cksum) & 0xffff) - old_c + new_c) & 0xffff;         \
    uint32_t high  = (((cksum) >> 16) - (old_c * (look)) + low) & 0xffff;   \
    (cksum) = (high << 16) | low;                                        \
  } while (0)

/* Multiply and add hash function */
#if ARITH_SMALL_CKSUM
#define SMALL_CKSUM_UPDATE(cksum,base,look) (cksum) = ((*(unsigned long*)(base+1)) * 71143)
#else
#define SMALL_CKSUM_UPDATE LARGE_CKSUM_UPDATE
#endif

/* Consume N bytes of input, only used by the decoder. */
#define DECODE_INPUT(n)             \
  do {                              \
  stream->total_in += (xoff_t) (n); \
  stream->avail_in -= (n);          \
  stream->next_in  += (n);          \
  } while (0)

/* This CPP-conditional stuff can be cleaned up... */
#if XD3_DEBUG
#define IF_DEBUG(x) x
#define DEBUG_ARG(x) , x
#else
#define IF_DEBUG(x)
#define DEBUG_ARG(x)
#endif
#if XD3_DEBUG > 1
#define IF_DEBUG1(x) x
#else
#define IF_DEBUG1(x)
#endif
#if REGRESSION_TEST
#define IF_REGRESSION(x) x
#else
#define IF_REGRESSION(x)
#endif

/******************************************************************************************/

#if XD3_ENCODER
static void*       xd3_alloc0 (xd3_stream *stream,
			       usize_t      elts,
			       usize_t      size);


static xd3_output* xd3_alloc_output (xd3_stream *stream,
				     xd3_output *old_output);



static void        xd3_free_output (xd3_stream *stream,
				    xd3_output *output);

static int         xd3_emit_byte (xd3_stream  *stream,
				  xd3_output **outputp,
				  uint8_t      code);

static int         xd3_emit_bytes (xd3_stream     *stream,
				   xd3_output    **outputp,
				   const uint8_t  *base,
				   usize_t          size);

static int         xd3_emit_double (xd3_stream *stream, xd3_rinst *first, xd3_rinst *second, uint code);
static int         xd3_emit_single (xd3_stream *stream, xd3_rinst *single, uint code);

static usize_t      xd3_sizeof_output (xd3_output *output);

static int         xd3_source_match_setup (xd3_stream *stream, xoff_t srcpos);
static int         xd3_srcwin_setup (xd3_stream *stream);
static int         xd3_source_extend_match (xd3_stream *stream);
static int         xd3_advance_cksum_pos (xd3_stream *stream, usize_t bytes);
static usize_t      xd3_iopt_last_matched (xd3_stream *stream);
static int         xd3_srcwin_update_point (xd3_stream *stream);
static void        xd3_srcwin_expand_size (xd3_stream *stream);
static int         xd3_emit_uint32_t (xd3_stream *stream, xd3_output **output, uint32_t num);
#endif /* XD3_ENCODER */

static int         xd3_decode_allocate (xd3_stream  *stream, usize_t       size,
					uint8_t    **copied1, usize_t      *alloc1,
					uint8_t    **copied2, usize_t      *alloc2);

static void        xd3_compute_code_table_string (const xd3_dinst *code_table, uint8_t *str);
static void*       xd3_alloc (xd3_stream *stream, usize_t      elts, usize_t      size);
static void        xd3_free  (xd3_stream *stream, void       *ptr);

static int         xd3_read_uint32_t (xd3_stream *stream, const uint8_t **inpp,
				      const uint8_t *max, uint32_t *valp);

#if REGRESSION_TEST
static int         xd3_selftest      (void);
#endif

/******************************************************************************************/

#define UINT32_OFLOW_MASK 0xfe000000U
#define UINT64_OFLOW_MASK 0xfe00000000000000ULL

#define UINT32_MAX 4294967295U
#define UINT64_MAX 18446744073709551615ULL

#if SIZEOF_USIZE_T == 4
#define USIZE_T_MAX        UINT32_MAX
#define xd3_decode_size   xd3_decode_uint32_t
#define xd3_emit_size     xd3_emit_uint32_t
#define xd3_sizeof_size   xd3_sizeof_uint32_t
#define xd3_read_size     xd3_read_uint32_t
#elif SIZEOF_USIZE_T == 8
#define USIZE_T_MAX        UINT64_MAX
#define xd3_decode_size   xd3_decode_uint64_t
#define xd3_emit_size     xd3_emit_uint64_t
#define xd3_sizeof_size   xd3_sizeof_uint64_t
#define xd3_read_size     xd3_read_uint64_t
#endif

#if SIZEOF_XOFF_T == 4
#define XOFF_T_MAX        UINT32_MAX
#define xd3_decode_offset xd3_decode_uint32_t
//#define xd3_emit_offset   xd3_emit_uint32_t
//#define xd3_sizeof_offset xd3_sizeof_uint32_t
//#define xd3_read_offset   xd3_read_uint32_t
#elif SIZEOF_XOFF_T == 8
#define XOFF_T_MAX        UINT64_MAX
#define xd3_decode_offset xd3_decode_uint64_t
//#define xd3_emit_offset   xd3_emit_uint64_t
//#define xd3_sizeof_offset xd3_sizeof_uint64_t
//#define xd3_read_offset   xd3_read_uint64_t
#endif

#define USIZE_T_OVERFLOW(a,b) ((USIZE_T_MAX - (usize_t) (a)) < (usize_t) (b))
#define XOFF_T_OVERFLOW(a,b) ((XOFF_T_MAX - (xoff_t) (a)) < (xoff_t) (b))

const char* xd3_strerror (int ret)
{
  switch (ret)
    {
    case XD3_INPUT: return "XD3_INPUT";
    case XD3_OUTPUT: return "XD3_OUTPUT";
    case XD3_GETSRCBLK: return "XD3_GETSRCBLK";
    case XD3_GOTHEADER: return "XD3_GOTHEADER";
    case XD3_WINSTART: return "XD3_WINSTART";
    case XD3_WINFINISH: return "XD3_WINFINISH";
    }
  return strerror (ret);
}

/******************************************************************************************/

#if SECONDARY_ANY == 0
#define IF_SEC(x)
#define IF_NSEC(x) x
#else /* yuck */
#define IF_SEC(x) x
#define IF_NSEC(x)
#include "xdelta3-second.h"
#endif /* SECONDARY_ANY */

#if SECONDARY_FGK
#include "xdelta3-fgk.h"

static const xd3_sec_type fgk_sec_type =
{
  VCD_FGK_ID,
  "FGK Adaptive Huffman",
  SEC_NOFLAGS,
  (xd3_sec_stream* (*)())  fgk_alloc,
  (void (*)())             fgk_destroy,
  (void (*)())             fgk_init,
  (int (*)())              xd3_decode_fgk,
  IF_ENCODER((int (*)())   xd3_encode_fgk)
};

#define IF_FGK(x) x
#define FGK_CASE(s) \
  s->sec_type = & fgk_sec_type; \
  break;
#else
#define IF_FGK(x)
#define FGK_CASE(s) \
  s->msg = "unavailable secondary compressor: FGK Adaptive Huffman"; \
  return EINVAL;
#endif

#if SECONDARY_DJW
#include "xdelta3-djw.h"

static const xd3_sec_type djw_sec_type =
{
  VCD_DJW_ID,
  "Static Huffman",
  SEC_COUNT_FREQS,
  (xd3_sec_stream* (*)())  djw_alloc,
  (void (*)())             djw_destroy,
  (void (*)())             djw_init,
  (int (*)())              xd3_decode_huff,
  IF_ENCODER((int (*)())   xd3_encode_huff)
};

#define IF_DJW(x) x
#define DJW_CASE(s) \
  s->sec_type = & djw_sec_type; \
  break;
#else
#define IF_DJW(x)
#define DJW_CASE(s) \
  s->msg = "unavailable secondary compressor: DJW Static Huffman"; \
  return EINVAL;
#endif

/******************************************************************************************/

/* Abbreviate frequently referenced fields. */
#define max_in    stream->avail_in
#define pos_in    stream->input_position
#define min_match stream->min_match

/* Process the inline pass. */
#define __XDELTA3_C_INLINE_PASS__
#include "xdelta3.c"
#undef __XDELTA3_C_INLINE_PASS__

/* Process template passes - this includes xdelta3.c several times. */
#define __XDELTA3_C_TEMPLATE_PASS__
#include "xdelta3-cfgs.h"
#undef __XDELTA3_C_TEMPLATE_PASS__

#undef max_in
#undef pos_in
#undef min_match

#if XD3_MAIN || PYTHON_MODULE
#include "xdelta3-main.h"
#endif

#if REGRESSION_TEST
#include "xdelta3-test.h"
#endif

#if PYTHON_MODULE
#include "xdelta3-python.h"
#endif

#endif /* __XDELTA3_C_HEADER_PASS__ */
#ifdef __XDELTA3_C_INLINE_PASS__

/******************************************************************************************
 Instruction tables
 ******************************************************************************************/

/* The following code implements a parametrized description of the
 * code table given above for a few reasons.  It is not necessary for
 * implementing the standard, to support compression with variable
 * tables, so an implementation is only required to know the default
 * code table to begin decompression.  (If the encoder uses an
 * alternate table, the table is included in compressed form inside
 * the VCDIFF file.)
 *
 * Before adding variable-table support there were two functions which
 * were hard-coded to the default table above.
 * xd3_compute_default_table() would create the default table by
 * filling a 256-elt array of xd3_dinst values.  The corresponding
 * function, xd3_choose_instruction(), would choose an instruction
 * based on the hard-coded parameters of the default code table.
 *
 * Notes: The parametrized code table description here only generates
 * tables of a certain regularity similar to the default table by
 * allowing to vary the distribution of single- and
 * double-instructions and change the number of near and same copy
 * modes.  More exotic tables are only possible by extending this
 * code, but a detailed experiment would need to be carried out first,
 * probably using separate code.  I would like to experiment with a
 * double-copy instruction, for example.
 *
 * For performance reasons, both the parametrized and non-parametrized
 * versions of xd3_choose_instruction remain.  The parametrized
 * version is only needed for testing multi-table decoding support.
 * If ever multi-table encoding is required, this can be optimized by
 * compiling static functions for each table.
 */

/* The XD3_CHOOSE_INSTRUCTION calls xd3_choose_instruction with the
 * table description when GENERIC_ENCODE_TABLES are in use.  The
 * IF_GENCODETBL macro enables generic-code-table specific code. */
#if GENERIC_ENCODE_TABLES
#define XD3_CHOOSE_INSTRUCTION(stream,prev,inst) xd3_choose_instruction (stream->code_table_desc, prev, inst)
#define IF_GENCODETBL(x) x
#else
#define XD3_CHOOSE_INSTRUCTION(stream,prev,inst) xd3_choose_instruction (prev, inst)
#define IF_GENCODETBL(x)
#endif

/* This structure maintains information needed by
 * xd3_choose_instruction to compute the code for a double instruction
 * by first indexing an array of code_table_sizes by copy mode, then
 * using (offset + (muliplier * X)) */
struct _xd3_code_table_sizes {
  uint8_t cpy_max;
  uint8_t offset;
  uint8_t mult;
};

/* This contains a complete description of a code table. */
struct _xd3_code_table_desc
{
  /* Assumes a single RUN instruction */
  /* Assumes that MIN_MATCH is 4 */

  uint8_t add_sizes;            /* Number of immediate-size single adds (default 17) */
  uint8_t near_modes;           /* Number of near copy modes (default 4) */
  uint8_t same_modes;           /* Number of same copy modes (default 3) */
  uint8_t cpy_sizes;            /* Number of immediate-size single copies (default 15) */

  uint8_t addcopy_add_max;      /* Maximum add size for an add-copy double instruction, all modes (default 4) */
  uint8_t addcopy_near_cpy_max; /* Maximum cpy size for an add-copy double instruction, up through VCD_NEAR modes (default 6) */
  uint8_t addcopy_same_cpy_max; /* Maximum cpy size for an add-copy double instruction, VCD_SAME modes (default 4) */

  uint8_t copyadd_add_max;      /* Maximum add size for a copy-add double instruction, all modes (default 1) */
  uint8_t copyadd_near_cpy_max; /* Maximum cpy size for a copy-add double instruction, up through VCD_NEAR modes (default 4) */
  uint8_t copyadd_same_cpy_max; /* Maximum cpy size for a copy-add double instruction, VCD_SAME modes (default 4) */

  xd3_code_table_sizes addcopy_max_sizes[MAX_MODES];
  xd3_code_table_sizes copyadd_max_sizes[MAX_MODES];
};

/* The rfc3284 code table is represented: */
static const xd3_code_table_desc __rfc3284_code_table_desc = {
  17, /* add sizes */
  4,  /* near modes */
  3,  /* same modes */
  15, /* copy sizes */

  4,  /* add-copy max add */
  6,  /* add-copy max cpy, near */
  4,  /* add-copy max cpy, same */

  1,  /* copy-add max add */
  4,  /* copy-add max cpy, near */
  4,  /* copy-add max cpy, same */

  /* addcopy */
  { {6,163,3},{6,175,3},{6,187,3},{6,199,3},{6,211,3},{6,223,3},{4,235,1},{4,239,1},{4,243,1} },
  /* copyadd */
  { {4,247,1},{4,248,1},{4,249,1},{4,250,1},{4,251,1},{4,252,1},{4,253,1},{4,254,1},{4,255,1} },
};

#if GENERIC_ENCODE_TABLES
/* An alternate code table for testing (5 near, 0 same):
 *
 *         TYPE      SIZE     MODE    TYPE     SIZE     MODE     INDEX
 *        ---------------------------------------------------------------
 *     1.  Run         0        0     Noop       0        0        0
 *     2.  Add    0, [1,23]     0     Noop       0        0      [1,24]
 *     3.  Copy   0, [4,20]     0     Noop       0        0     [25,42]
 *     4.  Copy   0, [4,20]     1     Noop       0        0     [43,60]
 *     5.  Copy   0, [4,20]     2     Noop       0        0     [61,78]
 *     6.  Copy   0, [4,20]     3     Noop       0        0     [79,96]
 *     7.  Copy   0, [4,20]     4     Noop       0        0     [97,114]
 *     8.  Copy   0, [4,20]     5     Noop       0        0    [115,132]
 *     9.  Copy   0, [4,20]     6     Noop       0        0    [133,150]
 *    10.  Add       [1,4]      0     Copy     [4,6]      0    [151,162]
 *    11.  Add       [1,4]      0     Copy     [4,6]      1    [163,174]
 *    12.  Add       [1,4]      0     Copy     [4,6]      2    [175,186]
 *    13.  Add       [1,4]      0     Copy     [4,6]      3    [187,198]
 *    14.  Add       [1,4]      0     Copy     [4,6]      4    [199,210]
 *    15.  Add       [1,4]      0     Copy     [4,6]      5    [211,222]
 *    16.  Add       [1,4]      0     Copy     [4,6]      6    [223,234]
 *    17.  Copy        4      [0,6]   Add      [1,3]      0    [235,255]
 *        --------------------------------------------------------------- */
static const xd3_code_table_desc __alternate_code_table_desc = {
  23, /* add sizes */
  5,  /* near modes */
  0,  /* same modes */
  17, /* copy sizes */

  4,  /* add-copy max add */
  6,  /* add-copy max cpy, near */
  0,  /* add-copy max cpy, same */

  3,  /* copy-add max add */
  4,  /* copy-add max cpy, near */
  0,  /* copy-add max cpy, same */

  /* addcopy */
  { {6,151,3},{6,163,3},{6,175,3},{6,187,3},{6,199,3},{6,211,3},{6,223,3},{0,0,0},{0,0,0} },
  /* copyadd */
  { {4,235,1},{4,238,1},{4,241,1},{4,244,1},{4,247,1},{4,250,1},{4,253,1},{0,0,0},{0,0,0} },
};
#endif

/* Computes code table entries of TBL using the specified description. */
static void
xd3_build_code_table (const xd3_code_table_desc *desc, xd3_dinst *tbl)
{
  int size1, size2, mode;
  int cpy_modes = 2 + desc->near_modes + desc->same_modes;
  xd3_dinst *d = tbl;

  (d++)->type1 = XD3_RUN;
  (d++)->type1 = XD3_ADD;

  for (size1 = 1; size1 <= desc->add_sizes; size1 += 1, d += 1)
    {
      d->type1 = XD3_ADD;
      d->size1 = size1;
    }

  for (mode = 0; mode < cpy_modes; mode += 1)
    {
      (d++)->type1 = XD3_CPY + mode;

      for (size1 = MIN_MATCH; size1 < MIN_MATCH + desc->cpy_sizes; size1 += 1, d += 1)
	{
	  d->type1 = XD3_CPY + mode;
	  d->size1 = size1;
	}
    }

  for (mode = 0; mode < cpy_modes; mode += 1)
    {
      for (size1 = 1; size1 <= desc->addcopy_add_max; size1 += 1)
	{
	  int max = (mode < 2 + desc->near_modes) ? desc->addcopy_near_cpy_max : desc->addcopy_same_cpy_max;

	  for (size2 = MIN_MATCH; size2 <= max; size2 += 1, d += 1)
	    {
	      d->type1 = XD3_ADD;
	      d->size1 = size1;
	      d->type2 = XD3_CPY + mode;
	      d->size2 = size2;
	    }
	}
    }

  for (mode = 0; mode < cpy_modes; mode += 1)
    {
      int max = (mode < 2 + desc->near_modes) ? desc->copyadd_near_cpy_max : desc->copyadd_same_cpy_max;

      for (size1 = MIN_MATCH; size1 <= max; size1 += 1)
	{
	  for (size2 = 1; size2 <= desc->copyadd_add_max; size2 += 1, d += 1)
	    {
	      d->type1 = XD3_CPY + mode;
	      d->size1 = size1;
	      d->type2 = XD3_ADD;
	      d->size2 = size2;
	    }
	}
    }

  XD3_ASSERT (d - tbl == 256);
}

/* This function generates the static default code table. */
static const xd3_dinst*
xd3_rfc3284_code_table (void)
{
  static xd3_dinst __rfc3284_code_table[256];

  if (__rfc3284_code_table[0].type1 != XD3_RUN)
    {
      xd3_build_code_table (& __rfc3284_code_table_desc, __rfc3284_code_table);
    }

  return __rfc3284_code_table;
}

#if XD3_ENCODER
#if GENERIC_ENCODE_TABLES
/* This function generates the alternate code table. */
static const xd3_dinst*
xd3_alternate_code_table (void)
{
  static xd3_dinst __alternate_code_table[256];

  if (__alternate_code_table[0].type1 != XD3_RUN)
    {
      xd3_build_code_table (& __alternate_code_table_desc, __alternate_code_table);
    }

  return __alternate_code_table;
}

/* This function computes the ideal second instruction INST based on preceding instruction
 * PREV.  If it is possible to issue a double instruction based on this pair it sets
 * PREV->code2, otherwise it sets INST->code1. */
static void
xd3_choose_instruction (const xd3_code_table_desc *desc, xd3_rinst *prev, xd3_rinst *inst)
{
  switch (inst->type)
    {
    case XD3_RUN:
      /* The 0th instruction is RUN */
      inst->code1 = 0;
      break;

    case XD3_ADD:

      if (inst->size > desc->add_sizes)
	{
	  /* The first instruction is non-immediate ADD */
	  inst->code1 = 1;
	}
      else
	{
	  /* The following ADD_SIZES instructions are immediate ADDs */
	  inst->code1 = 1 + inst->size;

	  /* Now check for a possible COPY-ADD double instruction */
	  if (prev != NULL)
	    {
	      int prev_mode = prev->type - XD3_CPY;

	      /* If previous is a copy.  Note: as long as the previous is not a RUN
	       * instruction, it should be a copy because it cannot be an add.  This check
	       * is more clear. */
	      if (prev_mode >= 0 && inst->size <= desc->copyadd_add_max)
		{
		  const xd3_code_table_sizes *sizes = & desc->copyadd_max_sizes[prev_mode];

		  /* This check and the inst->size-<= above are == in the default table. */
		  if (prev->size <= sizes->cpy_max)
		    {
		      /* The second and third exprs are 0 in the default table. */
		      prev->code2 = sizes->offset + (sizes->mult * (prev->size - MIN_MATCH)) + (inst->size - MIN_ADD);
		    }
		}
	    }
	}
      break;

    default:
      {
	int mode = inst->type - XD3_CPY;

	/* The large copy instruction is offset by the run, large add, and immediate adds,
	 * then multipled by the number of immediate copies plus one (the large copy)
	 * (i.e., if there are 15 immediate copy instructions then there are 16 copy
	 * instructions per mode). */
	inst->code1 = 2 + desc->add_sizes + (1 + desc->cpy_sizes) * mode;

	/* Now if the copy is short enough for an immediate instruction. */
	if (inst->size < MIN_MATCH + desc->cpy_sizes)
	  {
	    inst->code1 += inst->size + 1 - MIN_MATCH;

	    /* Now check for a possible ADD-COPY double instruction. */
	    if ( (prev != NULL) &&
		 (prev->type == XD3_ADD) &&
		 (prev->size <= desc->addcopy_add_max) )
	      {
		const xd3_code_table_sizes *sizes = & desc->addcopy_max_sizes[mode];

		if (inst->size <= sizes->cpy_max)
		  {
		    prev->code2 = sizes->offset + (sizes->mult * (prev->size - MIN_ADD)) + (inst->size - MIN_MATCH);
		  }
	      }
	  }
      }
    }
}
#else /* GENERIC_ENCODE_TABLES */

/* This version of xd3_choose_instruction is hard-coded for the default table. */
static void
xd3_choose_instruction (/* const xd3_code_table_desc *desc,*/ xd3_rinst *prev, xd3_rinst *inst)
{
  switch (inst->type)
    {
    case XD3_RUN:
      inst->code1 = 0;
      break;

    case XD3_ADD:
      inst->code1 = 1;

      if (inst->size <= 17)
	{
	  inst->code1 += inst->size;

	  if ( (inst->size == 1) &&
	       (prev != NULL) &&
	       (prev->size == 4) &&
	       (prev->type >= XD3_CPY) )
	    {
	      prev->code2 = 247 + (prev->type - XD3_CPY);
	    }
	}

      break;

    default:
      {
	int mode = inst->type - XD3_CPY;

	XD3_ASSERT (inst->type >= XD3_CPY && inst->type < 12);

	inst->code1 = 19 + 16 * mode;

	if (inst->size <= 18)
	  {
	    inst->code1 += inst->size - 3;

	    if ( (prev != NULL) &&
		 (prev->type == XD3_ADD) &&
		 (prev->size <= 4) )
	      {
		if ( (inst->size <= 6) &&
		     (mode       <= 5) )
		  {
		    prev->code2 = 163 + (mode * 12) + (3 * (prev->size - 1)) + (inst->size - 4);

		    XD3_ASSERT (prev->code2 <= 234);
		  }
		else if ( (inst->size == 4) &&
			  (mode       >= 6) )
		  {
		    prev->code2 = 235 + ((mode - 6) * 4) + (prev->size - 1);

		    XD3_ASSERT (prev->code2 <= 246);
		  }
	      }
	  }

	XD3_ASSERT (inst->code1 <= 162);
      }
      break;
    }
}
#endif /* GENERIC_ENCODE_TABLES */

/******************************************************************************************
 Instruction table encoder/decoder
 ******************************************************************************************/

#if GENERIC_ENCODE_TABLES
#if GENERIC_ENCODE_TABLES_COMPUTE == 0

/* In this case, we hard-code the result of compute_code_table_encoding for each alternate
 * code table, presuming that saves time/space.  This has been 131 bytes, but secondary
 * compression was turned off. */
static const uint8_t __alternate_code_table_compressed[178] =
{0xd6,0xc3,0xc4,0x00,0x00,0x01,0x8a,0x6f,0x40,0x81,0x27,0x8c,0x00,0x00,0x4a,0x4a,0x0d,0x02,0x01,0x03,
0x01,0x03,0x00,0x01,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,
0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x00,0x01,0x01,0x01,0x02,0x02,0x02,0x03,0x03,0x03,0x04,
0x04,0x04,0x04,0x00,0x04,0x05,0x06,0x01,0x02,0x03,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x05,0x05,0x05,
0x06,0x06,0x06,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x00,0x02,0x00,0x18,0x13,0x63,0x00,0x1b,0x00,0x54,
0x00,0x15,0x23,0x6f,0x00,0x28,0x13,0x54,0x00,0x15,0x01,0x1a,0x31,0x23,0x6c,0x0d,0x23,0x48,0x00,0x15,
0x93,0x6f,0x00,0x28,0x04,0x23,0x51,0x04,0x32,0x00,0x2b,0x00,0x12,0x00,0x12,0x00,0x12,0x00,0x12,0x00,
0x12,0x00,0x12,0x53,0x57,0x9c,0x07,0x43,0x6f,0x00,0x34,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,0x0c,0x00,
0x0c,0x00,0x0c,0x00,0x15,0x00,0x82,0x6f,0x00,0x15,0x12,0x0c,0x00,0x03,0x03,0x00,0x06,0x00,};

static int
xd3_compute_alternate_table_encoding (xd3_stream *stream, const uint8_t **data, usize_t *size)
{
  (*data) = __alternate_code_table_compressed;
  (*size) = sizeof (__alternate_code_table_compressed);
  return 0;
}

#else

/* The alternate code table will be computed and stored here. */
static uint8_t __alternate_code_table_compressed[CODE_TABLE_VCDIFF_SIZE];
static usize_t  __alternate_code_table_compressed_size;

/* This function generates a delta describing the code table for encoding within a VCDIFF
 * file.  This function is NOT thread safe because it is only intended that this function
 * is used to generate statically-compiled strings. */
int xd3_compute_code_table_encoding (xd3_stream *in_stream, const xd3_dinst *code_table,
				     uint8_t *comp_string, usize_t *comp_string_size)
{
  uint8_t dflt_string[CODE_TABLE_STRING_SIZE];
  uint8_t code_string[CODE_TABLE_STRING_SIZE];
  xd3_stream stream;
  xd3_source source;
  xd3_config config;
  int ret;

  memset (& source, 0, sizeof (source));

  xd3_compute_code_table_string (xd3_rfc3284_code_table (), dflt_string);
  xd3_compute_code_table_string (code_table, code_string);

  /* Use DJW secondary compression if it is on by default.  This saves about 20 bytes. */
  xd3_init_config (& config, XD3_FLUSH | (SECONDARY_DJW ? XD3_SEC_DJW : 0));

  /* Be exhaustive. */
  config.sprevsz = 1<<11;
  config.memsize = CODE_TABLE_STRING_SIZE * 10;

  config.large_look    = 4;
  config.large_step    = 1;
  config.small_look    = 4;
  config.small_chain   = CODE_TABLE_STRING_SIZE;
  config.small_lchain  = CODE_TABLE_STRING_SIZE;
  config.ssmatch       = 1;
  config.try_lazy      = 1;
  config.max_lazy      = CODE_TABLE_STRING_SIZE;
  config.long_enough   = CODE_TABLE_STRING_SIZE;
  config.promote       = 1;
  config.srcwin_size   = CODE_TABLE_STRING_SIZE;
  config.srcwin_maxsz  = CODE_TABLE_STRING_SIZE;

  if ((ret = xd3_config_stream (& stream, & config))) { goto fail; }

  source.size     = CODE_TABLE_STRING_SIZE;
  source.blksize  = CODE_TABLE_STRING_SIZE;
  source.onblk    = CODE_TABLE_STRING_SIZE;
  source.name     = "";
  source.curblk   = dflt_string;
  source.curblkno = 0;

  if ((ret = xd3_set_source (& stream, & source))) { goto fail; }

  if ((ret = xd3_encode_completely (& stream, code_string, CODE_TABLE_STRING_SIZE,
				    comp_string, comp_string_size, CODE_TABLE_VCDIFF_SIZE))) { goto fail; }

 fail:

  in_stream->msg = stream.msg;
  xd3_free_stream (& stream);
  return ret;
}

/* Compute a delta between alternate and rfc3284 tables.  As soon as another alternate
 * table is added, this code should become generic.  For now there is only one alternate
 * table for testing. */
static int
xd3_compute_alternate_table_encoding (xd3_stream *stream, const uint8_t **data, usize_t *size)
{
  int ret;

  if (__alternate_code_table_compressed[0] == 0)
    {
      if ((ret = xd3_compute_code_table_encoding (stream, xd3_alternate_code_table (),
						  __alternate_code_table_compressed,
						  & __alternate_code_table_compressed_size)))
	{
	  return ret;
	}

      /* During development of a new code table, enable this variable to print the new
       * static contents and determine its size.  At run time the table will be filled in
       * appropriately, but at least it should have the proper size beforehand. */
#if GENERIC_ENCODE_TABLES_COMPUTE_PRINT
      {
	int i;

	P(RINT, "\nstatic const usize_t __alternate_code_table_compressed_size = %u;\n",
		 __alternate_code_table_compressed_size);

	P(RINT, "static const uint8_t __alternate_code_table_compressed[%u] =\n{",
		 __alternate_code_table_compressed_size);

	for (i = 0; i < __alternate_code_table_compressed_size; i += 1)
	  {
	    P(RINT, "0x%02x,", __alternate_code_table_compressed[i]);
	    if ((i % 20) == 19) { P(RINT, "\n"); }
	  }

	P(RINT, "};\n");
      }
#endif
    }

  (*data) = __alternate_code_table_compressed;
  (*size) = __alternate_code_table_compressed_size;

  return 0;
}
#endif /* GENERIC_ENCODE_TABLES_COMPUTE != 0 */
#endif /* GENERIC_ENCODE_TABLES */

#endif /* XD3_ENCODER */

/* This function generates the 1536-byte string specified in sections 5.4 and 7 of
 * rfc3284, which is used to represent a code table within a VCDIFF file. */
void xd3_compute_code_table_string (const xd3_dinst *code_table, uint8_t *str)
{
  int i, s;

  XD3_ASSERT (CODE_TABLE_STRING_SIZE == 6 * 256);

  for (s = 0; s < 6; s += 1)
    {
      for (i = 0; i < 256; i += 1)
	{
	  switch (s)
	    {
	    case 0: *str++ = (code_table[i].type1 >= XD3_CPY ? XD3_CPY : code_table[i].type1); break;
	    case 1: *str++ = (code_table[i].type2 >= XD3_CPY ? XD3_CPY : code_table[i].type2); break;
	    case 2: *str++ = (code_table[i].size1); break;
	    case 3: *str++ = (code_table[i].size2); break;
	    case 4: *str++ = (code_table[i].type1 >= XD3_CPY ? code_table[i].type1 - XD3_CPY : 0); break;
	    case 5: *str++ = (code_table[i].type2 >= XD3_CPY ? code_table[i].type2 - XD3_CPY : 0); break;
	    }
	}
    }
}

/* This function translates the code table string into the internal representation.  The
 * stream's near and same-modes should already be set. */
static int
xd3_apply_table_string (xd3_stream *stream, const uint8_t *code_string)
{
  int i, s;
  int modes = TOTAL_MODES (stream);
  xd3_dinst *code_table;

  if ((code_table = stream->code_table_alloc = xd3_alloc (stream, sizeof (xd3_dinst), 256)) == NULL)
    {
      return ENOMEM;
    }

  for (s = 0; s < 6; s += 1)
    {
      for (i = 0; i < 256; i += 1)
	{
	  switch (s)
	    {
	    case 0:
	      if (*code_string > XD3_CPY)
		{
		  stream->msg = "invalid code-table opcode";
		  return EINVAL;
		}
	      code_table[i].type1 = *code_string++;
	      break;
	    case 1:
	      if (*code_string > XD3_CPY)
		{
		  stream->msg = "invalid code-table opcode";
		  return EINVAL;
		}
	      code_table[i].type2 = *code_string++;
	      break;
	    case 2:
	      if (*code_string != 0 && code_table[i].type1 == XD3_NOOP)
		{
		  stream->msg = "invalid code-table size";
		  return EINVAL;
		}
	      code_table[i].size1 = *code_string++;
	      break;
	    case 3:
	      if (*code_string != 0 && code_table[i].type2 == XD3_NOOP)
		{
		  stream->msg = "invalid code-table size";
		  return EINVAL;
		}
	      code_table[i].size2 = *code_string++;
	      break;
	    case 4:
	      if (*code_string >= modes)
		{
		  stream->msg = "invalid code-table mode";
		  return EINVAL;
		}
	      if (*code_string != 0 && code_table[i].type1 != XD3_CPY)
		{
		  stream->msg = "invalid code-table mode";
		  return EINVAL;
		}
	      code_table[i].type1 += *code_string++;
	      break;
	    case 5:
	      if (*code_string >= modes)
		{
		  stream->msg = "invalid code-table mode";
		  return EINVAL;
		}
	      if (*code_string != 0 && code_table[i].type2 != XD3_CPY)
		{
		  stream->msg = "invalid code-table mode";
		  return EINVAL;
		}
	      code_table[i].type2 += *code_string++;
	      break;
	    }
	}
    }

  stream->code_table = code_table;
  return 0;
}

/* This function applies a code table delta and returns an actual code table. */
static int
xd3_apply_table_encoding (xd3_stream *in_stream, const uint8_t *data, usize_t size)
{
  uint8_t dflt_string[CODE_TABLE_STRING_SIZE];
  uint8_t code_string[CODE_TABLE_STRING_SIZE];
  usize_t code_size;
  xd3_stream stream;
  xd3_source source;
  int ret;

  /* The default code table string can be cached if alternate code tables ever become
   * popular. */
  xd3_compute_code_table_string (xd3_rfc3284_code_table (), dflt_string);

  source.size     = CODE_TABLE_STRING_SIZE;
  source.blksize  = CODE_TABLE_STRING_SIZE;
  source.onblk    = CODE_TABLE_STRING_SIZE;
  source.name     = "rfc3284 code table";
  source.curblk   = dflt_string;
  source.curblkno = 0;

  if ((ret = xd3_config_stream (& stream, NULL)) ||
      (ret = xd3_set_source (& stream, & source)) ||
      (ret = xd3_decode_completely (& stream, data, size, code_string, & code_size, sizeof (code_string))))
    {
      in_stream->msg = stream.msg;
      goto fail;
    }

  if (code_size != sizeof (code_string))
    {
      stream.msg = "corrupt code-table encoding";
      ret = EINVAL;
      goto fail;
    }

  if ((ret = xd3_apply_table_string (in_stream, code_string))) { goto fail; }

 fail:

  xd3_free_stream (& stream);
  return ret;
}

/******************************************************************************************
 Permute stuff
 ******************************************************************************************/

#if HASH_PERMUTE == 0
#define PERMUTE(x) (x)
#else
#define PERMUTE(x) (__single_hash[(uint)x])

static const uint16_t __single_hash[256] =
{
  /* Random numbers generated using SLIB's pseudo-random number generator.  This hashes
   * the input alphabet.  TODO: Reread Knuth on this subject, and use a better random
   * generator, and measure it and the other hash functions. */
  0xbcd1, 0xbb65, 0x42c2, 0xdffe, 0x9666, 0x431b, 0x8504, 0xeb46,
  0x6379, 0xd460, 0xcf14, 0x53cf, 0xdb51, 0xdb08, 0x12c8, 0xf602,
  0xe766, 0x2394, 0x250d, 0xdcbb, 0xa678, 0x02af, 0xa5c6, 0x7ea6,
  0xb645, 0xcb4d, 0xc44b, 0xe5dc, 0x9fe6, 0x5b5c, 0x35f5, 0x701a,
  0x220f, 0x6c38, 0x1a56, 0x4ca3, 0xffc6, 0xb152, 0x8d61, 0x7a58,
  0x9025, 0x8b3d, 0xbf0f, 0x95a3, 0xe5f4, 0xc127, 0x3bed, 0x320b,
  0xb7f3, 0x6054, 0x333c, 0xd383, 0x8154, 0x5242, 0x4e0d, 0x0a94,
  0x7028, 0x8689, 0x3a22, 0x0980, 0x1847, 0xb0f1, 0x9b5c, 0x4176,
  0xb858, 0xd542, 0x1f6c, 0x2497, 0x6a5a, 0x9fa9, 0x8c5a, 0x7743,
  0xa8a9, 0x9a02, 0x4918, 0x438c, 0xc388, 0x9e2b, 0x4cad, 0x01b6,
  0xab19, 0xf777, 0x365f, 0x1eb2, 0x091e, 0x7bf8, 0x7a8e, 0x5227,
  0xeab1, 0x2074, 0x4523, 0xe781, 0x01a3, 0x163d, 0x3b2e, 0x287d,
  0x5e7f, 0xa063, 0xb134, 0x8fae, 0x5e8e, 0xb7b7, 0x4548, 0x1f5a,
  0xfa56, 0x7a24, 0x900f, 0x42dc, 0xcc69, 0x02a0, 0x0b22, 0xdb31,
  0x71fe, 0x0c7d, 0x1732, 0x1159, 0xcb09, 0xe1d2, 0x1351, 0x52e9,
  0xf536, 0x5a4f, 0xc316, 0x6bf9, 0x8994, 0xb774, 0x5f3e, 0xf6d6,
  0x3a61, 0xf82c, 0xcc22, 0x9d06, 0x299c, 0x09e5, 0x1eec, 0x514f,
  0x8d53, 0xa650, 0x5c6e, 0xc577, 0x7958, 0x71ac, 0x8916, 0x9b4f,
  0x2c09, 0x5211, 0xf6d8, 0xcaaa, 0xf7ef, 0x287f, 0x7a94, 0xab49,
  0xfa2c, 0x7222, 0xe457, 0xd71a, 0x00c3, 0x1a76, 0xe98c, 0xc037,
  0x8208, 0x5c2d, 0xdfda, 0xe5f5, 0x0b45, 0x15ce, 0x8a7e, 0xfcad,
  0xaa2d, 0x4b5c, 0xd42e, 0xb251, 0x907e, 0x9a47, 0xc9a6, 0xd93f,
  0x085e, 0x35ce, 0xa153, 0x7e7b, 0x9f0b, 0x25aa, 0x5d9f, 0xc04d,
  0x8a0e, 0x2875, 0x4a1c, 0x295f, 0x1393, 0xf760, 0x9178, 0x0f5b,
  0xfa7d, 0x83b4, 0x2082, 0x721d, 0x6462, 0x0368, 0x67e2, 0x8624,
  0x194d, 0x22f6, 0x78fb, 0x6791, 0xb238, 0xb332, 0x7276, 0xf272,
  0x47ec, 0x4504, 0xa961, 0x9fc8, 0x3fdc, 0xb413, 0x007a, 0x0806,
  0x7458, 0x95c6, 0xccaa, 0x18d6, 0xe2ae, 0x1b06, 0xf3f6, 0x5050,
  0xc8e8, 0xf4ac, 0xc04c, 0xf41c, 0x992f, 0xae44, 0x5f1b, 0x1113,
  0x1738, 0xd9a8, 0x19ea, 0x2d33, 0x9698, 0x2fe9, 0x323f, 0xcde2,
  0x6d71, 0xe37d, 0xb697, 0x2c4f, 0x4373, 0x9102, 0x075d, 0x8e25,
  0x1672, 0xec28, 0x6acb, 0x86cc, 0x186e, 0x9414, 0xd674, 0xd1a5
};
#endif

/******************************************************************************************
 Ctable stuff
 ******************************************************************************************/

#if HASH_PRIME
static const usize_t __primes[] =
{
  11, 19, 37, 73, 109,
  163, 251, 367, 557, 823,
  1237, 1861, 2777, 4177, 6247,
  9371, 14057, 21089, 31627, 47431,
  71143, 106721, 160073, 240101, 360163,
  540217, 810343, 1215497, 1823231, 2734867,
  4102283, 6153409, 9230113, 13845163, 20767711,
  31151543, 46727321, 70090921, 105136301, 157704401,
  236556601, 354834919, 532252367, 798378509, 1197567719,
  1796351503
};

static const usize_t __nprimes = SIZEOF_ARRAY (__primes);
#endif

static INLINE uint32_t
xd3_checksum_hash (const xd3_hash_cfg *cfg, const uint32_t cksum)
{
#if HASH_PRIME
  /* If the table is prime compute the modulus. */
  return (cksum % cfg->size);
#else
  /* If the table is power-of-two compute the mask.*/
  return (cksum ^ (cksum >> cfg->shift)) & cfg->mask;
#endif
}

/******************************************************************************************
 Create the hash table.
 ******************************************************************************************/

static INLINE void
xd3_swap_uint8p (uint8_t** p1, uint8_t** p2)
{
  uint8_t *t = (*p1);
  (*p1) = (*p2);
  (*p2) = t;
}

static INLINE void
xd3_swap_usize_t (usize_t* p1, usize_t* p2)
{
  usize_t t = (*p1);
  (*p1) = (*p2);
  (*p2) = t;
}

/* It's not constant time, but it computes the log. */
static int
xd3_check_pow2 (usize_t value, usize_t *logof)
{
  int x = 1;
  usize_t nolog;
  if (logof == NULL) {
    logof = &nolog;
  }

  *logof = 0;

  for (; x != 0; x <<= 1, *logof += 1)
    {
      if (x == value)
	{
	  return 0;
	}
    }

  return EINVAL;
}

static usize_t
xd3_round_blksize (usize_t sz, usize_t blksz)
{
  usize_t mod = sz & (blksz-1);

  XD3_ASSERT (xd3_check_pow2 (blksz, NULL) == 0);

  return mod ? (sz + (blksz - mod)) : sz;
}

#if XD3_ENCODER
#if !HASH_PRIME
static usize_t
xd3_size_log2 (usize_t slots)
{
  int bits = 28; /* This should not be an unreasonable limit. */
  int i;

  for (i = 3; i <= bits; i += 1)
    {
      if (slots < (1 << i))
	{
	  bits = i-1;
	  break;
	}
    }

  return bits;
}
#endif

static void
xd3_size_hashtable (xd3_stream    *stream,
		    usize_t         space,
		    xd3_hash_cfg  *cfg)
{
  usize_t slots = space / sizeof (usize_t);

  /* initialize ctable: the number of hash buckets is computed from the table of primes or
   * the nearest power-of-two, in both cases rounding down in favor of using less
   * memory. */

#if HASH_PRIME
  usize_t i;

  cfg->size = __primes[__nprimes-1];

  for (i = 1; i < __nprimes; i += 1)
    {
      if (slots < __primes[i])
	{
	  cfg->size = __primes[i-1];
	  break;
	}
    }
#else
  int bits = xd3_size_log2 (slots);

  cfg->size  = (1 << bits);
  cfg->mask  = (cfg->size - 1);
  cfg->shift = min (32 - bits, 16);
#endif
}
#endif

/******************************************************************************************
 Cksum function
 ******************************************************************************************/

/* OPT: It turns out that the compiler can't unroll the loop as well as you can by hand. */
static INLINE uint32_t
xd3_lcksum (const uint8_t *seg, const int ln)
{
  int   i    = 0;
  uint32_t low  = 0;
  uint32_t high = 0;

  for (; i < ln; i += 1)
    {
      low  += PERMUTE(*seg++);
      high += low;
    }

  return ((high & 0xffff) << 16) | (low & 0xffff);
}

#if ARITH_SMALL_CKSUM
static INLINE usize_t
xd3_scksum (const uint8_t *seg, const int ln)
{
  usize_t c;
  /* The -1 is because UPDATE operates on seg[1..ln] */
  SMALL_CKSUM_UPDATE (c,(seg-1),ln);
  return c;
}
#else
#define xd3_scksum(seg,ln) xd3_lcksum(seg,ln)
#endif

/******************************************************************************************
 Adler32 stream function: code copied from Zlib, defined in RFC1950
 ******************************************************************************************/

#define A32_BASE 65521L /* Largest prime smaller than 2^16 */
#define A32_NMAX 5552   /* NMAX is the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */

#define A32_DO1(buf,i)  {s1 += buf[i]; s2 += s1;}
#define A32_DO2(buf,i)  A32_DO1(buf,i); A32_DO1(buf,i+1);
#define A32_DO4(buf,i)  A32_DO2(buf,i); A32_DO2(buf,i+2);
#define A32_DO8(buf,i)  A32_DO4(buf,i); A32_DO4(buf,i+4);
#define A32_DO16(buf)   A32_DO8(buf,0); A32_DO8(buf,8);

static unsigned long adler32 (unsigned long adler, const uint8_t *buf, usize_t len)
{
    unsigned long s1 = adler & 0xffff;
    unsigned long s2 = (adler >> 16) & 0xffff;
    int k;

    while (len > 0)
      {
        k    = (len < A32_NMAX) ? len : A32_NMAX;
        len -= k;

	while (k >= 16)
	  {
	    A32_DO16(buf);
	    buf += 16;
            k -= 16;
	  }

	if (k != 0)
	  {
	    do
	      {
		s1 += *buf++;
		s2 += s1;
	      }
	    while (--k);
	  }

        s1 %= A32_BASE;
        s2 %= A32_BASE;
    }

    return (s2 << 16) | s1;
}

/******************************************************************************************
 Run-length function
 ******************************************************************************************/

static INLINE int
xd3_comprun (const uint8_t *seg, int slook, uint8_t *run_cp)
{
  int i;
  int     run_l = 0;
  uint8_t run_c = 0;

  for (i = 0; i < slook; i += 1)
    {
      NEXTRUN(seg[i]);
    }

  (*run_cp) = run_c;

  return run_l;
}

/******************************************************************************************
 Basic encoder/decoder functions
 ******************************************************************************************/

static int
xd3_decode_byte (xd3_stream *stream, uint *val)
{
  if (stream->avail_in == 0)
    {
      stream->msg = "further input required";
      return XD3_INPUT;
    }

  (*val) = stream->next_in[0];

  DECODE_INPUT (1);
  return 0;
}

static int
xd3_decode_bytes (xd3_stream *stream, uint8_t *buf, usize_t *pos, usize_t size)
{
  usize_t want;
  usize_t take;

  /* Note: The case where (*pos == size) happens when a zero-length appheader or code
   * table is transmitted, but there is nothing in the standard against that. */

  while (*pos < size)
    {
      if (stream->avail_in == 0)
	{
	  stream->msg = "further input required";
	  return XD3_INPUT;
	}

      want = size - *pos;
      take = min (want, stream->avail_in);

      memcpy (buf + *pos, stream->next_in, take);

      DECODE_INPUT (take);
      (*pos) += take;
    }

  return 0;
}

#if XD3_ENCODER
static int
xd3_emit_byte (xd3_stream  *stream,
	       xd3_output **outputp,
	       uint8_t      code)
{
  xd3_output *output = (*outputp);

  if (output->next == output->avail)
    {
      xd3_output *aoutput;

      if ((aoutput = xd3_alloc_output (stream, output)) == NULL)
	{
	  return ENOMEM;
	}

      output = (*outputp) = aoutput;
    }

  output->base[output->next++] = code;

  return 0;
}

static int
xd3_emit_bytes (xd3_stream     *stream,
		xd3_output    **outputp,
		const uint8_t  *base,
		usize_t          size)
{
  xd3_output *output = (*outputp);

  do
    {
      usize_t take;

      if (output->next == output->avail)
	{
	  xd3_output *aoutput;

	  if ((aoutput = xd3_alloc_output (stream, output)) == NULL)
	    {
	      return ENOMEM;
	    }

	  output = (*outputp) = aoutput;
	}

      take = min (output->avail - output->next, size);

      memcpy (output->base + output->next, base, take);

      output->next += take;
      size -= take;
      base += take;
    }
  while (size > 0);

  return 0;
}
#endif /* XD3_ENCODER */

/******************************************************************************************
 Integer encoder/decoder functions
 ******************************************************************************************/

#define DECODE_INTEGER_TYPE(PART,OFLOW)                                \
  while (stream->avail_in != 0)                                        \
    {                                                                  \
      uint next = stream->next_in[0];                                  \
                                                                       \
      DECODE_INPUT(1);                                                 \
                                                                       \
      if (PART & OFLOW)                                                \
	{                                                              \
	  stream->msg = "overflow in decode_integer";                  \
	  return EINVAL;                                               \
	}                                                              \
                                                                       \
      PART = (PART << 7) | (next & 127);                               \
                                                                       \
      if ((next & 128) == 0)                                           \
	{                                                              \
	  (*val) = PART;                                               \
	  PART = 0;                                                    \
	  return 0;                                                    \
	}                                                              \
    }                                                                  \
                                                                       \
  stream->msg = "further input required";                              \
  return XD3_INPUT

#define READ_INTEGER_TYPE(TYPE, OFLOW)                                 \
  TYPE val = 0;                                                        \
  const uint8_t *inp = (*inpp);                                        \
  uint next;                                                           \
                                                                       \
  do                                                                   \
    {                                                                  \
      if (inp == max)                                                  \
	{                                                              \
	  stream->msg = "end-of-input in read_integer";                \
	  return EINVAL;                                               \
	}                                                              \
                                                                       \
      if (val & OFLOW)                                                 \
	{                                                              \
	  stream->msg = "overflow in read_intger";                     \
	  return EINVAL;                                               \
	}                                                              \
                                                                       \
      next = (*inp++);                                                 \
      val  = (val << 7) | (next & 127);                                \
    }                                                                  \
  while (next & 128);                                                  \
                                                                       \
  (*valp) = val;                                                       \
  (*inpp) = inp;                                                       \
                                                                       \
  return 0

#define EMIT_INTEGER_TYPE()                                            \
  /* max 64-bit value in base-7 encoding is 9.1 bytes */               \
  uint8_t buf[10];                                                     \
  usize_t  bufi = 10;                                                   \
                                                                       \
  XD3_ASSERT (num >= 0);                                               \
                                                                       \
  /* This loop performs division and turns on all MSBs. */             \
  do                                                                   \
    {                                                                  \
      buf[--bufi] = (num & 127) | 128;                                 \
      num >>= 7;                                                       \
    }                                                                  \
  while (num != 0);                                                    \
                                                                       \
  /* Turn off MSB of the last byte. */                                 \
  buf[9] &= 127;                                                       \
                                                                       \
  XD3_ASSERT (bufi >= 0);                                              \
                                                                       \
  return xd3_emit_bytes (stream, output, buf + bufi, 10 - bufi)

#define IF_SIZEOF32(x) if (num < (1U   << (7 * (x)))) return (x);
#define IF_SIZEOF64(x) if (num < (1ULL << (7 * (x)))) return (x);

#if USE_UINT32
static uint
xd3_sizeof_uint32_t (uint32_t num)
{
  IF_SIZEOF32(1);
  IF_SIZEOF32(2);
  IF_SIZEOF32(3);
  IF_SIZEOF32(4);

  return 5;
}

static int
xd3_decode_uint32_t (xd3_stream *stream, uint32_t *val)
{ DECODE_INTEGER_TYPE (stream->dec_32part, UINT32_OFLOW_MASK); }
static int
xd3_read_uint32_t (xd3_stream *stream, const uint8_t **inpp, const uint8_t *max, uint32_t *valp)
{ READ_INTEGER_TYPE (uint32_t, UINT32_OFLOW_MASK); }
#if XD3_ENCODER
static int
xd3_emit_uint32_t (xd3_stream *stream, xd3_output **output, uint32_t num)
{ EMIT_INTEGER_TYPE (); }
#endif
#endif

#if USE_UINT64
/* We only ever decode offsets, but the other three are part of the regression test
 * anyway. */
static int
xd3_decode_uint64_t (xd3_stream *stream, uint64_t *val)
{ DECODE_INTEGER_TYPE (stream->dec_64part, UINT64_OFLOW_MASK); }
#if REGRESSION_TEST
#if XD3_ENCODER
static int
xd3_emit_uint64_t (xd3_stream *stream, xd3_output **output, uint64_t num)
{ EMIT_INTEGER_TYPE (); }
#endif
static int
xd3_read_uint64_t (xd3_stream *stream, const uint8_t **inpp, const uint8_t *max, uint64_t *valp)
{ READ_INTEGER_TYPE (uint64_t, UINT64_OFLOW_MASK); }

static uint
xd3_sizeof_uint64_t (uint64_t num)
{
  IF_SIZEOF64(1);
  IF_SIZEOF64(2);
  IF_SIZEOF64(3);
  IF_SIZEOF64(4);
  IF_SIZEOF64(5);
  IF_SIZEOF64(6);
  IF_SIZEOF64(7);
  IF_SIZEOF64(8);
  IF_SIZEOF64(9);

  return 10;
}
#endif
#endif

/******************************************************************************************
 Debug instruction statistics
 ******************************************************************************************/

#if XD3_DEBUG
static void
xd3_count_inst (xd3_stream *stream, uint code)
{
  IF_DEBUG1 ({
    if (stream->i_freqs == NULL &&
	(stream->i_freqs = xd3_alloc0 (stream, sizeof (stream->i_freqs[0]), 256)) == NULL) { abort (); }

    stream->i_freqs[code] += 1;
  });
  stream->n_ibytes += 1;
}

static void
xd3_count_mode (xd3_stream *stream, uint mode)
{
  IF_DEBUG1 ({
  if (stream->i_modes == NULL &&
      (stream->i_modes = xd3_alloc0 (stream, sizeof (stream->i_modes[0]), TOTAL_MODES (stream))) == NULL) { abort (); }
  stream->i_modes[mode] += 1;
  });
}

static void
xd3_count_size (xd3_stream *stream, usize_t size)
{
  IF_DEBUG1({
    if (stream->i_sizes == NULL &&
	(stream->i_sizes = xd3_alloc0 (stream, sizeof (stream->i_sizes[0]), 64)) == NULL) { abort (); }

    if (size < 64) { stream->i_sizes[size] += 1; }
  });
  stream->n_sbytes += xd3_sizeof_size (size);
}
#endif

/******************************************************************************************
 Address cache stuff
 ******************************************************************************************/

static int
xd3_alloc_cache (xd3_stream *stream)
{
  if (((stream->acache.s_near > 0) &&
       (stream->acache.near_array = xd3_alloc (stream, stream->acache.s_near, sizeof (usize_t))) == NULL) ||
      ((stream->acache.s_same > 0) &&
       (stream->acache.same_array = xd3_alloc (stream, stream->acache.s_same * 256, sizeof (usize_t))) == NULL))
    {
      return ENOMEM;
    }

  return 0;
}

static void
xd3_init_cache (xd3_addr_cache* acache)
{
  if (acache->s_near > 0)
    {
      memset (acache->near_array, 0, acache->s_near * sizeof (usize_t));
      acache->next_slot = 0;
    }

  if (acache->s_same > 0)
    {
      memset (acache->same_array, 0, acache->s_same * 256 * sizeof (usize_t));
    }
}

static void
xd3_update_cache (xd3_addr_cache* acache, usize_t addr)
{
  if (acache->s_near > 0)
    {
      acache->near_array[acache->next_slot] = addr;
      acache->next_slot = (acache->next_slot + 1) % acache->s_near;
    }

  if (acache->s_same > 0)
    {
      acache->same_array[addr % (acache->s_same*256)] = addr;
    }
}

#if XD3_ENCODER
/* OPT: this gets called a lot, can it be optimized? */
static int
xd3_encode_address (xd3_stream *stream, usize_t addr, usize_t here, uint8_t* mode)
{
  usize_t d, bestd;
  int   i, bestm, ret;
  xd3_addr_cache* acache = & stream->acache;

#define SMALLEST_INT(x) do { if (((x) & ~127) == 0) { goto good; } } while (0)

  /* Attempt to find the address mode that yields the smallest integer value for "d", the
   * encoded address value, thereby minimizing the encoded size of the address. */
  bestd = addr;
  bestm = VCD_SELF;

  XD3_ASSERT (addr < here);

  SMALLEST_INT (bestd);

  if ((d = here-addr) < bestd)
    {
      bestd = d;
      bestm = VCD_HERE;

      SMALLEST_INT (bestd);
    }

  for (i = 0; i < acache->s_near; i += 1)
    {
      d = addr - acache->near_array[i];

      if (d >= 0 && d < bestd)
	{
	  bestd = d;
	  bestm = i+2; /* 2 counts the VCD_SELF, VCD_HERE modes */

	  SMALLEST_INT (bestd);
	}
    }

  if (acache->s_same > 0 && acache->same_array[d = addr%(acache->s_same*256)] == addr)
    {
      bestd = d%256;
      bestm = acache->s_near + 2 + d/256; /* 2 + s_near offsets past the VCD_NEAR modes */

      if ((ret = xd3_emit_byte (stream, & ADDR_TAIL (stream), bestd))) { return ret; }
    }
  else
    {
    good:

      if ((ret = xd3_emit_size (stream, & ADDR_TAIL (stream), bestd))) { return ret; }
    }

  xd3_update_cache (acache, addr);

  IF_DEBUG (xd3_count_mode (stream, bestm));

  (*mode) += bestm;

  return 0;
}
#endif

static int
xd3_decode_address (xd3_stream *stream, usize_t here, uint mode, const uint8_t **inpp, const uint8_t *max, uint32_t *valp)
{
  int ret;
  uint same_start = 2 + stream->acache.s_near;

  if (mode < same_start)
    {
      if ((ret = xd3_read_size (stream, inpp, max, valp))) { return ret; }

      switch (mode)
	{
	case VCD_SELF:
	  break;
	case VCD_HERE:
	  (*valp) = here - (*valp);
	  break;
	default:
	  (*valp) += stream->acache.near_array[mode - 2];
	  break;
	}
    }
  else
    {
      if (*inpp == max)
	{
	  stream->msg = "address underflow";
	  return EINVAL;
	}

      mode -= same_start;

      (*valp) = stream->acache.same_array[mode*256 + (**inpp)];

      (*inpp) += 1;
    }

  xd3_update_cache (& stream->acache, *valp);

  return 0;
}

/******************************************************************************************
 Alloc/free
 ******************************************************************************************/

static void*
__xd3_alloc_func (void* opaque, usize_t items, usize_t size)
{
  return malloc (items * size);
}

static void
__xd3_free_func (void* opaque, void* address)
{
  free (address);
}

static void*
xd3_alloc (xd3_stream *stream,
	   usize_t      elts,
	   usize_t      size)
{
  void *a = stream->alloc (stream->opaque, elts, size);

  if (a != NULL)
    {
      IF_DEBUG (stream->alloc_cnt += 1);
    }
  else
    {
      stream->msg = "out of memory";
    }

  return a;
}

static void
xd3_free (xd3_stream *stream,
	  void       *ptr)
{
  if (ptr != NULL)
    {
      IF_DEBUG (stream->free_cnt += 1);
      XD3_ASSERT (stream->free_cnt <= stream->alloc_cnt);
      stream->free (stream->opaque, ptr);
    }
}

#if XD3_ENCODER
static void*
xd3_alloc0 (xd3_stream *stream,
	    usize_t      elts,
	    usize_t      size)
{
  void *a = xd3_alloc (stream, elts, size);

  if (a != NULL)
    {
      memset (a, 0, elts * size);
    }

  return a;
}

static xd3_output*
xd3_alloc_output (xd3_stream *stream,
		  xd3_output *old_output)
{
  xd3_output *output;
  uint8_t    *base;

  if (stream->enc_free != NULL)
    {
      output = stream->enc_free;
      stream->enc_free = output->next_page;
    }
  else
    {
      if ((output = xd3_alloc (stream, 1, sizeof (xd3_output))) == NULL)
	{
	  return NULL;
	}

      if ((base = xd3_alloc (stream, XD3_ALLOCSIZE, sizeof (uint8_t))) == NULL)
	{
	  xd3_free (stream, output);
	  return NULL;
	}

      output->base  = base;
      output->avail = XD3_ALLOCSIZE;
    }

  output->next = 0;

  if (old_output)
    {
      old_output->next_page = output;
    }

  output->next_page = NULL;

  return output;
}

static usize_t
xd3_sizeof_output (xd3_output *output)
{
  usize_t s = 0;

  for (; output; output = output->next_page)
    {
      s += output->next;
    }

  return s;
}

static void
xd3_freelist_output (xd3_stream *stream,
		     xd3_output *output)
{
  xd3_output *tmp;

  while (output)
    {
      tmp    = output;
      output = output->next_page;

      tmp->next = 0;
      tmp->next_page = stream->enc_free;
      stream->enc_free = tmp;
    }
}

static void
xd3_free_output (xd3_stream *stream,
		 xd3_output *output)
{
  xd3_output *next;

 again:
  if (output == NULL)
    {
      return;
    }

  next = output->next_page;

  xd3_free (stream, output->base);
  xd3_free (stream, output);

  output = next;
  goto again;
}
#endif /* XD3_ENCODER */

void
xd3_free_stream (xd3_stream *stream)
{

  xd3_free (stream, stream->large_table);
  xd3_free (stream, stream->small_table);
  xd3_free (stream, stream->small_prev);
  xd3_free (stream, stream->iopt.buffer);

#if XD3_ENCODER
  {
    int i;
    for (i = 0; i < ENC_SECTS; i += 1)
      {
	xd3_free_output (stream, stream->enc_heads[i]);
      }
    xd3_free_output (stream, stream->enc_free);
  }
#endif

  xd3_free (stream, stream->acache.near_array);
  xd3_free (stream, stream->acache.same_array);

  xd3_free (stream, stream->inst_sect.copied1);
  xd3_free (stream, stream->addr_sect.copied1);
  xd3_free (stream, stream->data_sect.copied1);

  xd3_free (stream, stream->dec_buffer);
  xd3_free (stream, (uint8_t*) stream->dec_lastwin);

  xd3_free (stream, stream->buf_in);
  xd3_free (stream, stream->dec_appheader);
  xd3_free (stream, stream->dec_codetbl);
  xd3_free (stream, stream->code_table_alloc);

#if SECONDARY_ANY
  xd3_free (stream, stream->inst_sect.copied2);
  xd3_free (stream, stream->addr_sect.copied2);
  xd3_free (stream, stream->data_sect.copied2);

  if (stream->sec_type != NULL)
    {
      stream->sec_type->destroy (stream, stream->sec_stream_d);
      stream->sec_type->destroy (stream, stream->sec_stream_i);
      stream->sec_type->destroy (stream, stream->sec_stream_a);
    }
#endif

  IF_DEBUG (xd3_free (stream, stream->i_freqs));
  IF_DEBUG (xd3_free (stream, stream->i_modes));
  IF_DEBUG (xd3_free (stream, stream->i_sizes));

  XD3_ASSERT (stream->alloc_cnt == stream->free_cnt);

  memset (stream, 0, sizeof (xd3_stream));
}

#if (XD3_DEBUG || VCDIFF_TOOLS)
static const char*
xd3_rtype_to_string (xd3_rtype type, int print_mode)
{
  switch (type)
    {
    case XD3_NOOP:
      return "NOOP ";
    case XD3_RUN:
      return "RUN  ";
    case XD3_ADD:
      return "ADD  ";
    default: break;
    }
  if (! print_mode)
    {
      return "CPY  ";
    }
  switch (type)
    {
    case XD3_CPY + 0: return "CPY_0";
    case XD3_CPY + 1: return "CPY_1";
    case XD3_CPY + 2: return "CPY_2";
    case XD3_CPY + 3: return "CPY_3";
    case XD3_CPY + 4: return "CPY_4";
    case XD3_CPY + 5: return "CPY_5";
    case XD3_CPY + 6: return "CPY_6";
    case XD3_CPY + 7: return "CPY_7";
    case XD3_CPY + 8: return "CPY_8";
    case XD3_CPY + 9: return "CPY_9";
    default:          return "CPY>9";
    }
}
#endif

/******************************************************************************************
 Stream configuration
 ******************************************************************************************/

int
xd3_config_stream(xd3_stream *stream,
		   xd3_config *config)
{
  int ret;
  xd3_config defcfg;
  const xd3_smatcher* smatcher;

  if (config == NULL)
    {
      config = & defcfg;
      memset (config, 0, sizeof (*config));
    }

  /* Initial setup: no error checks yet */
  memset (stream, 0, sizeof (*stream));

  stream->memsize   = config->memsize   ? config->memsize : XD3_DEFAULT_MEMSIZE;
  stream->winsize   = config->winsize   ? config->winsize : XD3_DEFAULT_WINSIZE;
  stream->sprevsz   = config->sprevsz   ? config->sprevsz : XD3_DEFAULT_SPREVSZ;
  stream->srcwin_size  = config->srcwin_size ? config->srcwin_size : XD3_DEFAULT_START_CKSUM_ADVANCE;
  stream->srcwin_maxsz = config->srcwin_maxsz ? config->srcwin_maxsz : XD3_DEFAULT_MAX_CKSUM_ADVANCE;
  stream->iopt_size = config->iopt_size ? config->iopt_size : XD3_DEFAULT_IOPT_SIZE;
  stream->getblk    = config->getblk;
  stream->alloc     = config->alloc ? config->alloc : __xd3_alloc_func;
  stream->free      = config->freef ? config->freef : __xd3_free_func;
  stream->opaque    = config->opaque;
  stream->flags     = config->flags;

  XD3_ASSERT (stream->winsize > 0);

  /* Secondary setup. */
  stream->sec_data  = config->sec_data;
  stream->sec_inst  = config->sec_inst;
  stream->sec_addr  = config->sec_addr;

  stream->sec_data.data_type = DATA_SECTION;
  stream->sec_inst.data_type = INST_SECTION;
  stream->sec_addr.data_type = ADDR_SECTION;

  /* Check static sizes. */
  if (sizeof (usize_t) != SIZEOF_USIZE_T ||
      sizeof (xoff_t) != SIZEOF_XOFF_T ||
      (ret = xd3_check_pow2(XD3_ALLOCSIZE, NULL)))
    {
      stream->msg = "incorrect compilation: wrong integer sizes";
      return EINVAL;
    }

  /* Check/set secondary compressor. */
  switch (stream->flags & XD3_SEC_TYPE)
    {
    case 0:
      if (stream->flags & XD3_SEC_OTHER)
	{
	  stream->msg = "XD3_SEC flags require a secondary compressor type";
	  return EINVAL;
	}
      break;
    case XD3_SEC_FGK:
      FGK_CASE (stream);
    case XD3_SEC_DJW:
      DJW_CASE (stream);
    default:
      stream->msg = "too many secondary compressor types set";
      return EINVAL;
    }

  /* Check/set encoder code table. */
  switch (stream->flags & XD3_ALT_CODE_TABLE) {
  case 0:
    stream->code_table_desc = & __rfc3284_code_table_desc;
    stream->code_table_func = xd3_rfc3284_code_table;
    break;
#if GENERIC_ENCODE_TABLES
  case XD3_ALT_CODE_TABLE:
    stream->code_table_desc = & __alternate_code_table_desc;
    stream->code_table_func = xd3_alternate_code_table;
    stream->comp_table_func = xd3_compute_alternate_table_encoding;
    break;
#endif
  default:
    stream->msg = "alternate code table support was not compiled";
    return EINVAL;
  }

  /* Check sprevsz */
  if (config->small_chain == 1)
    {
      stream->sprevsz = 0;
    }
  else
    {
      if ((ret = xd3_check_pow2 (stream->sprevsz, NULL)))
	{
	  stream->msg = "sprevsz is required to be a power of two";
	  return EINVAL;
	}

      stream->sprevmask = stream->sprevsz - 1;
    }

  /* Default scanner settings. */
  switch (config->smatch_cfg)
    {
      IF_BUILD_SOFT(case XD3_SMATCH_SOFT:
      smatcher = & __smatcher_soft; break;

      if (config->large_look  < MIN_MATCH ||
	  config->large_step  < 1         ||
	  config->small_look  < MIN_MATCH ||
	  config->small_chain < 1         ||
	  config->large_look  < config->small_look ||
	  config->small_chain < config->small_lchain ||
	  (config->small_lchain == 0 && config->try_lazy) ||
	  config->srcwin_size < stream->large_look ||
	  config->srcwin_maxsz < stream->srcwin_size)
	{
	  stream->msg = "invalid soft string-match config";
	  return EINVAL;
	}
      break;)

      IF_BUILD_SLOW(case XD3_SMATCH_DEFAULT:)
      IF_BUILD_SLOW(case XD3_SMATCH_SLOW: smatcher = & __smatcher_slow; break;)
      IF_BUILD_FAST(case XD3_SMATCH_FAST: smatcher = & __smatcher_fast; break;)
    default:
      stream->msg = "invalid string match config type";
      return EINVAL;
    }

  stream->string_match  = smatcher->string_match;
  XD3_ASSERT(stream->string_match);

  XD3_COPY_CONFIG_FIELDS (stream, smatcher);

  /* If it is a soft config, the smatcher fields didn't set anything, copy from config
   * instead. */
  if (stream->large_look == 0)
    {
      XD3_COPY_CONFIG_FIELDS (stream, config);
    }

  /* Compute large step values */
  stream->large_step_mask = stream->large_step - 1;

  if ((ret = xd3_check_pow2 (stream->large_step, & stream->large_step_bits)))
    {
      stream->msg = "large_step is required to be a power of two";
      return EINVAL;
    }

  IF_DEBUG1 (P(RINT "[stream cfg] llook %u lstep %u slook %u\n",
	       stream->large_look, stream->large_step, stream->small_look));
  return 0;
}

/******************************************************************************************
 Getblk interface
 ******************************************************************************************/

/* This function interfaces with the client getblk function, checks its results, etc. */
static int
xd3_getblk (xd3_stream *stream/*, xd3_source *source*/, xoff_t blkno)
{
  int ret;
  xd3_source *source = stream->src;

  if (blkno >= source->blocks)
    {
      stream->msg = "source file too short";
      return EINVAL;
    }

  if (blkno != source->curblkno || source->curblk == NULL)
    {
      XD3_ASSERT (source->curblk != NULL || blkno != source->curblkno);

      source->getblkno = blkno;

      if (stream->getblk == NULL)
	{
	  stream->msg = "getblsk source input";
	  return XD3_GETSRCBLK;
	}
      else if ((ret = stream->getblk (stream, source, blkno)) != 0)
	{
	  stream->msg = "getblk failed";
	  return ret;
	}
      
      XD3_ASSERT (source->curblk != NULL);
    }
  
  if (source->onblk != xd3_bytes_on_srcblk (source, blkno))
    {
      stream->msg = "getblk returned short block";
      return EINVAL;
    }

  return 0;
}

/******************************************************************************************
 Stream open/close
 ******************************************************************************************/

int
xd3_set_source (xd3_stream *stream,
		xd3_source *src)
{
  xoff_t blk_num;
  xoff_t tail_size;

  IF_DEBUG1 (P(RINT "[set source] size %"Q"u\n", src->size));

  if (src == NULL || src->size < stream->large_look) { return 0; }

  stream->src  = src;
  blk_num      = src->size / src->blksize;
  tail_size    = src->size % src->blksize;
  src->blocks  = blk_num + (tail_size > 0);
  src->srclen  = 0;
  src->srcbase = 0;

  return 0;
}

void
xd3_abort_stream (xd3_stream *stream)
{
  stream->dec_state = DEC_ABORTED;
  stream->enc_state = ENC_ABORTED;
}

/* TODO: In general, xd3_close_stream() doesn't check all the things it could. */
int
xd3_close_stream (xd3_stream *stream)
{
#if 0
  SMALL_HASH_STATS ({
    P(RINT "small hash chain avg %.2f [ %d / %d ]\n",
      stream->sh_compares / (double) stream->sh_searches,
      stream->sh_compares,
      stream->sh_searches);
  });
#endif
  if (stream->enc_state != 0 && stream->enc_state != ENC_ABORTED)
    {
      /* If encoding, should be ready for more input but not actually have any. */
      if (stream->enc_state != ENC_INPUT || stream->avail_in != 0)
	{
	  stream->msg = "encoding is incompete";
	  return EINVAL;
	}
    }
  else
    {
      switch (stream->dec_state)
	{
	case DEC_VCHEAD:
	case DEC_WININD:
	  /* TODO: Address the zero-byte ambiguity.  Does the encoder emit a window or
	   * not?  If so, then catch an error here.  If not, need another routine to say
	   * decode_at_least_one_if_empty. */
	case DEC_ABORTED:
	  break;
	default:
	  /* If decoding, should be ready for the next window. */
	  stream->msg = "EOF in decode";
	  return EINVAL;
	}
    }

  return 0;
}

/******************************************************************************************
 Application header
 ******************************************************************************************/

int
xd3_get_appheader (xd3_stream  *stream,
		   uint8_t    **data,
		   usize_t      *size)
{
  if (stream->dec_state < DEC_WININD)
    {
      stream->msg = "application header not available";
      return EINVAL;
    }

  (*data) = stream->dec_appheader;
  (*size) = stream->dec_appheadsz;
  return 0;
}

#if XD3_ENCODER
void
xd3_set_appheader (xd3_stream    *stream,
		   const uint8_t *data,
		   usize_t         size)
{
  stream->enc_appheader = data;
  stream->enc_appheadsz = size;
}

/******************************************************************************************
 Encoder stuff
 ******************************************************************************************/

#if XD3_DEBUG
static int
xd3_iopt_check (xd3_stream *stream)
{
  int ul = xd3_rlist_length (& stream->iopt.used);
  int fl = xd3_rlist_length (& stream->iopt.free);

  return (ul + fl + (stream->iout ? 1 : 0)) == stream->iopt_size;
}
#endif

static xd3_rinst*
xd3_iopt_free (xd3_stream *stream, xd3_rinst *i)
{
  xd3_rinst *n = xd3_rlist_remove (i);
  xd3_rlist_push_back (& stream->iopt.free, i);
  return n;
}

static void
xd3_iopt_free_nonadd (xd3_stream *stream, xd3_rinst *i)
{
  if (i->type != XD3_ADD)
    {
      xd3_rlist_push_back (& stream->iopt.free, i);
    }
}

/* When an instruction is ready to flush from the iopt buffer, this function is called to
 * produce an encoding.  It writes the instruction plus size, address, and data to the
 * various encoding sections. */
static int
xd3_iopt_finish_encoding (xd3_stream *stream, xd3_rinst *inst)
{
  int ret;

  /* Check for input overflow. */
  XD3_ASSERT (inst->pos + inst->size <= stream->avail_in);

  switch (inst->type)
    {
    case XD3_CPY:
      {
	/* the address may have an offset if there is a source window. */
	usize_t addr;
	xd3_source *src = stream->src;

	if (src != NULL)
	  {
	    /* If there is a source copy, the source must have its source window decided
	     * before we can encode.  This can be bad -- we have to make this decision
	     * even if no source matches have been found. */
	    if (stream->srcwin_decided == 0)
	      {
		if ((ret = xd3_srcwin_setup (stream))) { return ret; }
	      }

	    /* xtra field indicates the copy is from the source */
	    if (inst->xtra)
	      {
		XD3_ASSERT (inst->addr >= src->srcbase);
		XD3_ASSERT (inst->addr + inst->size <= src->srcbase + src->srclen);
		addr = (inst->addr - src->srcbase);
	      }
	    else
	      {
		/* with source window: target copy address is offset by taroff. */
		addr = stream->taroff + (usize_t) inst->addr;
	      }
	  }
	else
	  {
	    addr = (usize_t) inst->addr;
	  }

	XD3_ASSERT (inst->size >= MIN_MATCH);

	/* the "here" position is always offset by taroff */
	if ((ret = xd3_encode_address (stream, addr, inst->pos + stream->taroff, & inst->type)))
	  {
	    return ret;
	  }

	IF_DEBUG (stream->n_cpy += 1);
	IF_DEBUG (stream->l_cpy += inst->size);

	IF_DEBUG1 ({
	  static int cnt;
	  P(RINT "[iopt copy:%d] pos %"Q"u-%"Q"u addr %"Q"u-%"Q"u size %u\n",
		   cnt++,
		   stream->total_in + inst->pos,
		   stream->total_in + inst->pos + inst->size,
		   inst->addr, inst->addr + inst->size, inst->size);
	});
	break;
      }
    case XD3_RUN:
      {
	XD3_ASSERT (inst->size >= MIN_MATCH);

	if ((ret = xd3_emit_byte (stream, & DATA_TAIL (stream), inst->xtra))) { return ret; }

	IF_DEBUG (stream->n_run += 1);
	IF_DEBUG (stream->l_run += inst->size);
	IF_DEBUG (stream->n_dbytes += 1);

	IF_DEBUG1 ({
	  static int cnt;
	  P(RINT "[iopt run:%d] pos %"Q"u size %u\n", cnt++, stream->total_in + inst->pos, inst->size);
	});
	break;
      }
    case XD3_ADD:
      {
	if ((ret = xd3_emit_bytes (stream, & DATA_TAIL (stream),
				   stream->next_in + inst->pos, inst->size))) { return ret; }

	IF_DEBUG (stream->n_add += 1);
	IF_DEBUG (stream->l_add += inst->size);
	IF_DEBUG (stream->n_dbytes += inst->size);

	IF_DEBUG1 ({
	  static int cnt;
	  P(RINT "[iopt add:%d] pos %"Q"u size %u\n", cnt++, stream->total_in + inst->pos, inst->size);
	});

	break;
      }
    }

  /* This is the only place stream->unencoded_offset is incremented. */
  XD3_ASSERT (stream->unencoded_offset == inst->pos);
  stream->unencoded_offset += inst->size;

  IF_DEBUG (stream->n_emit += inst->size);

  inst->code2 = 0;

  XD3_CHOOSE_INSTRUCTION (stream, stream->iout, inst);

  if (stream->iout != NULL)
    {
      if (stream->iout->code2 != 0)
	{
	  if ((ret = xd3_emit_double (stream, stream->iout, inst, stream->iout->code2))) { return ret; }

	  xd3_iopt_free_nonadd (stream, stream->iout);
	  xd3_iopt_free_nonadd (stream, inst);
	  stream->iout = NULL;
	  return 0;
	}
      else
	{
	  if ((ret = xd3_emit_single (stream, stream->iout, stream->iout->code1))) { return ret; }

	  xd3_iopt_free_nonadd (stream, stream->iout);
	}
    }

  stream->iout = inst;

  return 0;
}

/* This possibly encodes an add instruction, iadd, which must remain on the stack until
 * the following call to xd3_iopt_finish_encoding. */
static int
xd3_iopt_add (xd3_stream *stream, usize_t pos, xd3_rinst *iadd)
{
  int ret;
  usize_t off = stream->unencoded_offset;

  if (pos > off)
    {
      iadd->type = XD3_ADD;
      iadd->pos  = off;
      iadd->size = pos - off;

      if ((ret = xd3_iopt_finish_encoding (stream, iadd))) { return ret; }
    }

  return 0;
}

/* This function calls xd3_iopt_finish_encoding to finish encoding an instruction, and it
 * may also produce an add instruction for an unmatched region. */
static int
xd3_iopt_add_encoding (xd3_stream *stream, xd3_rinst *inst)
{
  int ret;
  xd3_rinst iadd;

  if ((ret = xd3_iopt_add (stream, inst->pos, & iadd))) { return ret; }

  if ((ret = xd3_iopt_finish_encoding (stream, inst))) { return ret; }

  return 0;
}

/* Generates a final add instruction to encode the remaining input. */
static int
xd3_iopt_add_finalize (xd3_stream *stream)
{
  int ret;
  xd3_rinst iadd;

  if ((ret = xd3_iopt_add (stream, stream->avail_in, & iadd))) { return ret; }

  if (stream->iout)
    {
      if ((ret = xd3_emit_single (stream, stream->iout, stream->iout->code1))) { return ret; }

      xd3_iopt_free_nonadd (stream, stream->iout);
      stream->iout = NULL;
    }

  return 0;
}

/* Compact the instruction buffer by choosing the best non-overlapping instructions when
 * lazy string-matching.  There are no ADDs in the iopt buffer because those are
 * synthesized in xd3_iopt_add_encoding and during xd3_iopt_add_finalize. */
static int
xd3_iopt_flush_instructions (xd3_stream *stream, int force)
{
  xd3_rinst *r1 = xd3_rlist_front (& stream->iopt.used);
  xd3_rinst *r2;
  xd3_rinst *r3;
  usize_t r1end;
  usize_t r2end;
  usize_t r2off;
  usize_t r2moff;
  usize_t gap;
  usize_t flushed;
  int ret;

  XD3_ASSERT (xd3_iopt_check (stream));

  /* Note: once tried to skip this step if it's possible to assert there are no
   * overlapping instructions.  Doesn't work because xd3_opt_erase leaves overlapping
   * instructions. */
  while (! xd3_rlist_end (& stream->iopt.used, r1) &&
	 ! xd3_rlist_end (& stream->iopt.used, r2 = xd3_rlist_next (r1)))
    {
      r1end = r1->pos + r1->size;

      /* If the instructions do not overlap, continue. */
      if (r1end <= r2->pos)
	{
	  r1 = r2;
	  continue;
	}

      r2end = r2->pos + r2->size;

      /* The min_match adjustments prevent this. */
      XD3_ASSERT (r2end > (r1end + LEAST_MATCH_INCR));

      /* If r3 is available... */
      if (! xd3_rlist_end (& stream->iopt.used, r3 = xd3_rlist_next (r2)))
	{
	  /* If r3 starts before r1 finishes or just about, r2 is irrelevant */
	  if (r3->pos <= r1end + 1)
	    {
	      xd3_iopt_free (stream, r2);
	      continue;
	    }
	}
      else if (! force)
	{
	  /* Unless force, end the loop when r3 is not available. */
	  break;
	}

      r2off  = r2->pos - r1->pos;
      r2moff = r2end - r1end;
      gap    = r2end - r1->pos;

      /* If the two matches overlap almost entirely, choose the better match and discard
       * the other.  This heuristic is BLACK MAGIC.  Havesomething better? */
      if (gap < 2*MIN_MATCH || r2moff <= 2 || r2off <= 2)
	{
	  /* Only one match should be used, choose the longer one. */
	  if (r1->size < r2->size)
	    {
	      xd3_iopt_free (stream, r1);
	      r1 = r2;
	    }
	  else
	    {
	      /* We are guaranteed that r1 does not overlap now, so advance past r2 */
	      r1 = xd3_iopt_free (stream, r2);
	    }
	  continue;
	}
      else
	{
	  /* Shorten one of the instructions -- could be optimized based on the address
	   * cache. */
	  usize_t average;
	  usize_t newsize;
	  usize_t adjust1;

	  XD3_ASSERT (r1end > r2->pos && r2end > r1->pos);

	  /* Try to balance the length of both instructions, but avoid making both longer
	   * than MAX_MATCH_SPLIT . */
	  average = (gap) / 2;
	  newsize = min (MAX_MATCH_SPLIT, gap - average);

	  /* Should be possible to simplify this code. */
	  if (newsize > r1->size)
	    {
	      /* shorten r2 */
	      adjust1 = r1end - r2->pos;
	    }
	  else if (newsize > r2->size)
	    {
	      /* shorten r1 */
	      adjust1 = r1end - r2->pos;

	      XD3_ASSERT (r1->size > adjust1);

	      r1->size -= adjust1;

	      /* don't shorten r2 */
	      adjust1 = 0;
	    }
	  else
	    {
	      /* shorten r1 */
	      adjust1 = r1->size - newsize;

	      if (r2->pos > r1end - adjust1)
		{
		  adjust1 -= r2->pos - (r1end - adjust1);
		}

	      XD3_ASSERT (r1->size > adjust1);

	      r1->size -= adjust1;

	      /* shorten r2 */
	      XD3_ASSERT (r1->pos + r1->size >= r2->pos);

	      adjust1 = r1->pos + r1->size - r2->pos;
	    }

	  /* Fallthrough above if-else, shorten r2 */
	  XD3_ASSERT (r2->size > adjust1);

	  r2->size -= adjust1;
	  r2->pos  += adjust1;
	  r2->addr += adjust1;

	  XD3_ASSERT (r1->size >= MIN_MATCH);
	  XD3_ASSERT (r2->size >= MIN_MATCH);

	  r1 = r2;
	}
    }

  XD3_ASSERT (xd3_iopt_check (stream));

  /* If forcing, pick instructions until the list is empty, otherwise this empties 50% of
   * the queue. */
  for (flushed = 0; ! xd3_rlist_empty (& stream->iopt.used); )
    {
      xd3_rinst *renc = xd3_rlist_pop_front (& stream->iopt.used);
      if ((ret = xd3_iopt_add_encoding (stream, renc)))
	{
	  return ret;
	}

      if (! force) 
	{
	  if (++flushed > stream->iopt_size / 2)
	    {
	      break;
	    }

	  /* If there are only two instructions remaining, break, because they were
	   * not optimized.  This means there were more than 50% eliminated by the
	   * loop above. */
	  r1 = xd3_rlist_front (& stream->iopt.used);
	  if (xd3_rlist_end(& stream->iopt.used, r1) ||
	      xd3_rlist_end(& stream->iopt.used, r2 = xd3_rlist_next (r1)) ||
	      xd3_rlist_end(& stream->iopt.used, r3 = xd3_rlist_next (r2)))
	    {
	      break;
	    }
	}
    }

  XD3_ASSERT (xd3_iopt_check (stream));

  XD3_ASSERT (!force || xd3_rlist_length (& stream->iopt.used) == 0);

  return 0;
}

static int
xd3_iopt_get_slot (xd3_stream *stream, xd3_rinst** iptr)
{
  xd3_rinst *i;
  int ret;

  if (xd3_rlist_empty (& stream->iopt.free))
    {
      if ((ret = xd3_iopt_flush_instructions (stream, 0))) { return ret; }

      XD3_ASSERT (! xd3_rlist_empty (& stream->iopt.free));
    }

  i = xd3_rlist_pop_back (& stream->iopt.free);

  xd3_rlist_push_back (& stream->iopt.used, i);

  (*iptr) = i;

  return 0;
}

/* A copy is about to be emitted that extends backwards to POS, therefore it may
 * completely cover some existing instructions in the buffer.  If an instruction is
 * completely covered by this new match, erase it.  If the new instruction is covered by
 * the previous one, return 1 to skip it. */
static void
xd3_iopt_erase (xd3_stream *stream, usize_t pos, usize_t size)
{
  while (! xd3_rlist_empty (& stream->iopt.used))
    {
      xd3_rinst *r = xd3_rlist_back (& stream->iopt.used);

      /* Verify that greedy is working.  The previous instruction should end before the
       * new one begins. */
      XD3_ASSERT ((stream->flags & XD3_BEGREEDY) == 0 || (r->pos + r->size <= pos));
      /* Verify that min_match is working.  The previous instruction should end before the
       * new one ends. */
      XD3_ASSERT ((stream->flags & XD3_BEGREEDY) != 0 || (r->pos + r->size < pos + size));

      /* See if the last instruction starts before the new instruction.  If so, there is
       * nothing to erase. */
      if (r->pos < pos)
	{
	  return;
	}

      /* Otherwise, the new instruction covers the old one, delete it and repeat. */
      xd3_rlist_remove (r);
      xd3_rlist_push_back (& stream->iopt.free, r);
    }
}

/* This function tells the last matched input position. */
static usize_t
xd3_iopt_last_matched (xd3_stream *stream)
{
  xd3_rinst *r;

  if (xd3_rlist_empty (& stream->iopt.used))
    {
      return 0;
    }

  r = xd3_rlist_back (& stream->iopt.used);

  return r->pos + r->size;
}

/******************************************************************************************
 Emit routines
 ******************************************************************************************/

static int
xd3_emit_single (xd3_stream *stream, xd3_rinst *single, uint code)
{
  int has_size = stream->code_table[code].size1 == 0;
  int ret;

  IF_DEBUG1 (P(RINT "[emit1] %u %s (%u) code %u\n",
	       single->pos,
	       xd3_rtype_to_string (single->type, 0),
	       single->size,
	       code));

  if ((ret = xd3_emit_byte (stream, & INST_TAIL (stream), code))) { return ret; }

  if (has_size)
    {
      if ((ret = xd3_emit_size (stream, & INST_TAIL (stream), single->size))) { return ret; }

      IF_DEBUG (xd3_count_size (stream, single->size));
    }

  IF_DEBUG (xd3_count_inst (stream, code));

  return 0;
}

static int
xd3_emit_double (xd3_stream *stream, xd3_rinst *first, xd3_rinst *second, uint code)
{
  int ret;

  /* All double instructions use fixed sizes, so all we need to do is output the
   * instruction code, no sizes. */
  XD3_ASSERT (stream->code_table[code].size1 != 0 &&
	      stream->code_table[code].size2 != 0);

  if ((ret = xd3_emit_byte (stream, & INST_TAIL (stream), code))) { return ret; }

  IF_DEBUG1 (P(RINT "[emit2]: %u %s (%u) %s (%u) code %u\n",
	       first->pos,
	       xd3_rtype_to_string (first->type, 0),
	       first->size,
	       xd3_rtype_to_string (second->type, 0),
	       second->size,
	       code));

  IF_DEBUG (xd3_count_inst (stream, code));

  return 0;
}

/* This enters a potential run instruction into the iopt buffer.  The position argument is
 * relative to the target window. */
static INLINE int
xd3_emit_run (xd3_stream *stream, usize_t pos, usize_t size, uint8_t run_c)
{
  xd3_rinst* ri;
  int ret;

  XD3_ASSERT (pos + size <= stream->avail_in);

  if ((ret = xd3_iopt_get_slot (stream, & ri))) { return ret; }

  ri->type = XD3_RUN;
  ri->xtra = run_c;
  ri->pos  = pos;
  ri->size = size;

  return 0;
}

/* This enters a potential copy instruction into the iopt buffer.  The position argument
 * is relative to the target window.. */
static INLINE int
xd3_found_match (xd3_stream *stream, usize_t pos, usize_t size, xoff_t addr, int is_source)
{
  xd3_rinst* ri;
  int ret;

  XD3_ASSERT (pos + size <= stream->avail_in);

  if ((ret = xd3_iopt_get_slot (stream, & ri))) { return ret; }

  ri->type = XD3_CPY;
  ri->xtra = is_source;
  ri->pos  = pos;
  ri->size = size;
  ri->addr = addr;

  return 0;
}

static int
xd3_emit_hdr (xd3_stream *stream)
{
  int  ret;
  int  use_secondary = stream->sec_type != NULL;
  int  use_adler32   = stream->flags & XD3_ADLER32;
  int  vcd_source    = xd3_encoder_used_source (stream);
  uint win_ind = 0;
  uint del_ind = 0;
  usize_t enc_len;
  usize_t tgt_len;
  usize_t data_len;
  usize_t inst_len;
  usize_t addr_len;

  XD3_ASSERT (stream->n_emit == stream->avail_in);

  if (stream->current_window == 0)
    {
      uint hdr_ind = 0;
      int use_appheader  = stream->enc_appheader != NULL;
      int use_gencodetbl = GENERIC_ENCODE_TABLES && (stream->code_table_desc != & __rfc3284_code_table_desc);

      if (use_secondary)  { hdr_ind |= VCD_SECONDARY; }
      if (use_gencodetbl) { hdr_ind |= VCD_CODETABLE; }
      if (use_appheader)  { hdr_ind |= VCD_APPHEADER; }

      if ((ret = xd3_emit_byte (stream, & HDR_TAIL (stream), VCDIFF_MAGIC1)) != 0 ||
	  (ret = xd3_emit_byte (stream, & HDR_TAIL (stream), VCDIFF_MAGIC2)) != 0 ||
	  (ret = xd3_emit_byte (stream, & HDR_TAIL (stream), VCDIFF_MAGIC3)) != 0 ||
	  (ret = xd3_emit_byte (stream, & HDR_TAIL (stream), VCDIFF_VERSION)) != 0 ||
	  (ret = xd3_emit_byte (stream, & HDR_TAIL (stream), hdr_ind)) != 0)
	{
	  return ret;
	}

      /* Secondary compressor ID */
#if SECONDARY_ANY
      if (use_secondary && (ret = xd3_emit_byte (stream, & HDR_TAIL (stream), stream->sec_type->id))) { return ret; }
#endif

      /* Compressed code table */
      if (use_gencodetbl)
	{
	  usize_t code_table_size;
	  const uint8_t *code_table_data;

	  if ((ret = stream->comp_table_func (stream, & code_table_data, & code_table_size))) { return ret; }

	  if ((ret = xd3_emit_size (stream, & HDR_TAIL (stream), code_table_size + 2)) ||
	      (ret = xd3_emit_byte (stream, & HDR_TAIL (stream), stream->code_table_desc->near_modes)) ||
	      (ret = xd3_emit_byte (stream, & HDR_TAIL (stream), stream->code_table_desc->same_modes)) ||
	      (ret = xd3_emit_bytes (stream, & HDR_TAIL (stream), code_table_data, code_table_size))) { return ret; }
	}

      /* Application header */
      if (use_appheader)
	{
	  if ((ret = xd3_emit_size (stream, & HDR_TAIL (stream), stream->enc_appheadsz)) ||
	      (ret = xd3_emit_bytes (stream, & HDR_TAIL (stream), stream->enc_appheader, stream->enc_appheadsz)))
	    {
	      return ret;
	    }
	}
    }

  /* try to compress this window */
#if SECONDARY_ANY
  if (use_secondary)
    {
      int data_sec = 0;
      int inst_sec = 0;
      int addr_sec = 0;

#     define ENCODE_SECONDARY_SECTION(UPPER,LOWER) \
             ((stream->flags & XD3_SEC_NO ## UPPER) == 0 && \
              (ret = xd3_encode_secondary (stream, & UPPER ## _HEAD (stream), & UPPER ## _TAIL (stream), \
					& xd3_sec_ ## LOWER (stream), \
				        & stream->sec_ ## LOWER, & LOWER ## _sec)))

      if (ENCODE_SECONDARY_SECTION (DATA, data) ||
	  ENCODE_SECONDARY_SECTION (INST, inst) ||
	  ENCODE_SECONDARY_SECTION (ADDR, addr))
	{
	  return ret;
	}

      del_ind |= (data_sec ? VCD_DATACOMP : 0);
      del_ind |= (inst_sec ? VCD_INSTCOMP : 0);
      del_ind |= (addr_sec ? VCD_ADDRCOMP : 0);
    }
#endif

  /* if (vcd_target) { win_ind |= VCD_TARGET; } */
  if (vcd_source)  { win_ind |= VCD_SOURCE; }
  if (use_adler32) { win_ind |= VCD_ADLER32; }

  /* window indicator */
  if ((ret = xd3_emit_byte (stream, & HDR_TAIL (stream), win_ind))) { return ret; }

  /* source window */
  if (vcd_source)
    {
      /* or (vcd_target) { ... } */
      if ((ret = xd3_emit_size (stream, & HDR_TAIL (stream), stream->src->srclen)) ||
	  (ret = xd3_emit_size (stream, & HDR_TAIL (stream), stream->src->srcbase))) { return ret; }
    }

  tgt_len  = stream->avail_in;
  data_len = xd3_sizeof_output (DATA_HEAD (stream));
  inst_len = xd3_sizeof_output (INST_HEAD (stream));
  addr_len = xd3_sizeof_output (ADDR_HEAD (stream));

  /* The enc_len field is redundent... doh! */
  enc_len = (1 + (xd3_sizeof_size (tgt_len) +
		  xd3_sizeof_size (data_len) +
		  xd3_sizeof_size (inst_len) +
		  xd3_sizeof_size (addr_len)) +
	     data_len +
	     inst_len +
	     addr_len +
	     (use_adler32 ? 4 : 0));

  if ((ret = xd3_emit_size (stream, & HDR_TAIL (stream), enc_len)) ||
      (ret = xd3_emit_size (stream, & HDR_TAIL (stream), tgt_len)) ||
      (ret = xd3_emit_byte (stream, & HDR_TAIL (stream), del_ind)) ||
      (ret = xd3_emit_size (stream, & HDR_TAIL (stream), data_len)) ||
      (ret = xd3_emit_size (stream, & HDR_TAIL (stream), inst_len)) ||
      (ret = xd3_emit_size (stream, & HDR_TAIL (stream), addr_len)))
    {
      return ret;
    }

  if (use_adler32)
    {
      uint8_t  send[4];
      uint32_t a32 = adler32 (1L, stream->next_in, stream->avail_in);

      send[0] = (a32 >> 24);
      send[1] = (a32 >> 16);
      send[2] = (a32 >> 8);
      send[3] = (a32 & 0xff);

      if ((ret = xd3_emit_bytes (stream, & HDR_TAIL (stream), send, 4))) { return ret; }
    }

  return 0;
}

/******************************************************************************************
 Encode routines
 ******************************************************************************************/

static int
xd3_encode_buffer_leftover (xd3_stream *stream)
{
  usize_t take;
  usize_t room;

  /* Allocate the buffer. */
  if (stream->buf_in == NULL && (stream->buf_in = xd3_alloc (stream, stream->winsize, 1)) == NULL)
    {
      return ENOMEM;
    }

  /* Take leftover input first. */
  if (stream->buf_leftover != NULL)
    {
      XD3_ASSERT (stream->buf_avail == 0);
      XD3_ASSERT (stream->buf_leftavail < stream->winsize);

      IF_DEBUG (P(RINT "[leftover] previous %u avail %u\n", stream->buf_leftavail, stream->avail_in));

      memcpy (stream->buf_in, stream->buf_leftover, stream->buf_leftavail);

      stream->buf_leftover = NULL;
      stream->buf_avail    = stream->buf_leftavail;
    }

  /* Copy into the buffer. */
  room = stream->winsize - stream->buf_avail;
  take = min (room, stream->avail_in);

  memcpy (stream->buf_in + stream->buf_avail, stream->next_in, take);

  stream->buf_avail += take;

  if (take < stream->avail_in)
    {
      /* Buffer is full */
      stream->buf_leftover  = stream->next_in  + take;
      stream->buf_leftavail = stream->avail_in - take;

      IF_DEBUG (P(RINT "[leftover] take %u remaining %u\n", take, stream->buf_leftavail));
    }
  else if ((stream->buf_avail < stream->winsize) && !(stream->flags & XD3_FLUSH))
    {
      /* Buffer has space */
      IF_DEBUG (P(RINT "[leftover] %u emptied\n", take));
      return XD3_INPUT;
    }

  /* Use the buffer: */
  stream->next_in   = stream->buf_in;
  stream->avail_in  = stream->buf_avail;
  stream->buf_avail = 0;

  return 0;
}

/* This function allocates all memory initially used by the encoder. */
static int
xd3_encode_init (xd3_stream *stream)
{
  int i;
  int large_comp = (stream->src != NULL);
  int small_comp = ! (stream->flags & XD3_NOCOMPRESS);
  /*int small_prev = (stream->small_chain > 1);*/
  int space_fact = (large_comp + small_comp);
  int memsize    = stream->memsize;

  /* Memory allocations for checksum tables are delayed until xd3_string_match_init in the
   * first call to string_match--that way identical or short inputs require no table
   * allocation. */
  if (large_comp)
    {
      xd3_size_hashtable (stream, memsize / space_fact, & stream->large_hash);
    }

  if (small_comp)
    {
      xd3_size_hashtable (stream, memsize / space_fact, & stream->small_hash);
    }

  for (i = 0; i < ENC_SECTS; i += 1)
    {
      if ((stream->enc_heads[i] = stream->enc_tails[i] =
	   xd3_alloc_output (stream, NULL)) == NULL)
	{
	  goto fail;
	}
    }

  /* iopt buffer */
  xd3_rlist_init (& stream->iopt.used);
  xd3_rlist_init (& stream->iopt.free);

  if ((stream->iopt.buffer = xd3_alloc (stream, sizeof (xd3_rinst), stream->iopt_size)) == NULL)
    {
      goto fail;
    }

  for (i = 0; i < stream->iopt_size; i += 1)
    {
      xd3_rlist_push_back (& stream->iopt.free, & stream->iopt.buffer[i]);
    }

  XD3_ASSERT (xd3_rlist_length (& stream->iopt.free) == stream->iopt_size);
  XD3_ASSERT (xd3_rlist_length (& stream->iopt.used) == 0);

  /* address cache, code table */
  stream->acache.s_near = stream->code_table_desc->near_modes;
  stream->acache.s_same = stream->code_table_desc->same_modes;
  stream->code_table    = stream->code_table_func ();

  return xd3_alloc_cache (stream);

 fail:

  return ENOMEM;
}

#if XD3_DEBUG
static int
xd3_check_sprevlist (xd3_stream *stream)
{
  int i;
  for (i = 0; i < stream->sprevsz; i += 1)
    {
      xd3_slist *l = & stream->small_prev[i];

      XD3_ASSERT (l->prev->next == l);
      XD3_ASSERT (l->next->prev == l);
    }
  return 1;
}
#endif

/* Called after the ENC_POSTOUT state, this puts the output buffers back into separate
 * lists and re-initializes some variables.  (The output lists were spliced together
 * during the ENC_FLUSH state.) */
static void
xd3_encode_reset (xd3_stream *stream)
{
  int i;
  xd3_output *olist;

  XD3_ASSERT (stream->small_prev == NULL || xd3_check_sprevlist (stream));

  /* Setup expand_point for the next target window (unless the source has been completely
   * scanned).  The expand_point always maintains an offset relative to the base of the
   * input window.  We do not re-initialize the expand_point, we simply subtract the old
   * available input size. */
  if (stream->src != NULL && ! stream->end_of_source)
    {
      stream->expand_point -= stream->avail_in;
      IF_DEBUG1 (P(RINT "[postwin expand] %d\n", stream->expand_point));

      /* The expand point should still be forward of the input pos. */
      XD3_ASSERT(stream->expand_point >= 0);
    }

  IF_DEBUG (stream->n_emit = 0);
  stream->avail_in     = 0;
  stream->small_reset  = 1;

  if (stream->src != NULL)
    {
      stream->src->srcbase   = 0;
      stream->src->srclen    = 0;
      stream->srcwin_decided = 0;
      stream->match_minaddr  = 0;
      stream->match_maxaddr  = 0;
      stream->taroff         = 0;
    }

  /* Reset output chains. */
  olist = stream->enc_heads[0];

  for (i = 0; i < ENC_SECTS; i += 1)
    {
      XD3_ASSERT (olist != NULL);

      stream->enc_heads[i] = olist;
      stream->enc_tails[i] = olist;
      olist = olist->next_page;

      stream->enc_heads[i]->next = 0;
      stream->enc_heads[i]->next_page = NULL;

      stream->enc_tails[i]->next_page = NULL;
      stream->enc_tails[i] = stream->enc_heads[i];
    }

  xd3_freelist_output (stream, olist);
}

/* The main encoding routine. */
int
xd3_encode_input (xd3_stream *stream)
{
  int ret, i;
  
  if (stream->dec_state != 0)
    {
      stream->msg = "encoder/decoder transition";
      return EINVAL;
    }

  switch (stream->enc_state)
    {
    case ENC_INIT:
      /* Only reached on first time through: memory setup. */
      if ((ret = xd3_encode_init (stream))) { return ret; }

      stream->enc_state = ENC_INPUT;

    case ENC_INPUT:

      /* If there is no input yet, just return.  This checks for next_in == NULL, not
       * avail_in == 0 since zero bytes is a valid input.  There is an assertion in
       * xd3_avail_input() that next_in != NULL for this reason.  By returning right away
       * we avoid creating an input buffer before the caller has supplied its first data.
       * It is possible for xd3_avail_input to be called both before and after the first
       * call to xd3_encode_input(). */
      if (stream->next_in == NULL)
	{
	  return XD3_INPUT;
	}

    enc_flush:
      /* See if we should buffer the input: either if there is already a leftover buffer,
       * or if the input is short of winsize without flush.  The label at this point is
       * reached by a goto below, when there is leftover input after postout. */
      if ((stream->buf_leftover != NULL) ||
	  (stream->avail_in < stream->winsize && ! (stream->flags & XD3_FLUSH)))
	{
	  if ((ret = xd3_encode_buffer_leftover (stream))) { return ret; }
	}

      /* Initalize the address cache before each window. */
      xd3_init_cache (& stream->acache);

      pos_in    = 0;
      min_match = MIN_MATCH;
      stream->unencoded_offset = 0;

      stream->enc_state = ENC_SEARCH;

      IF_DEBUG1 (P(RINT "[input window:%"Q"u] input bytes %u offset %"Q"u\n",
		   stream->current_window, stream->avail_in, stream->total_in));

      return XD3_WINSTART;

    case ENC_SEARCH:

      /* Reentrant matching. */
      if (stream->src != NULL)
	{
	  switch (stream->match_state)
	    {
	    case MATCH_TARGET:
	      /* Try matching forward at the start of the target.  This is entered the
	       * first time through, to check for a perfect match, and whenever there is a
	       * source match that extends to the end of the previous window.  The
	       * match_srcpos field is initially zero and later set during
	       * xd3_source_extend_match.  This call can't fail because the source window
	       * is unrestricted. */
	      (void) xd3_source_match_setup (stream, stream->match_srcpos);
	      stream->match_state = MATCH_FORWARD;

	    case MATCH_FORWARD:
	    case MATCH_BACKWARD:
	      /* Try continuing any previous match_input calls. */
	      if (stream->avail_in != 0 &&
		  (ret = xd3_source_extend_match (stream)) != 0) { return ret; }

	      /* Update stream position.  match_fwd is zero if no match. */
	      stream->input_position += stream->match_fwd;

	    case MATCH_SEARCHING:
	      /* Continue string matching. */
	      break;
	    }
	}

      /* String matching... */
      if (stream->avail_in != 0 &&
	  (ret = stream->string_match (stream))) { return ret; }

      /* Possibly expand source. */
      if (stream->src != NULL)
	{
	  /* The expand statistics are incomplete at XD3_WINFINISH if pos_in >=
	   * expand_pos, but this isn't necessary: */
	  // TODO
	  /* if ((ret = xd3_srcwin_update_point (stream))) { return ret; } */
	}

      /* Flush the instrution buffer, then possibly add one more instruction, then emit
       * the header. */
      stream->enc_state = ENC_FLUSH;
      if ((ret = xd3_iopt_flush_instructions (stream, 1)) ||
          (ret = xd3_iopt_add_finalize (stream)) ||
	  (ret = xd3_emit_hdr (stream)))
	{
	  return ret;
	}

      /* Begin output. */
      stream->enc_current = HDR_HEAD (stream);

      /* Chain all the outputs together.  After doing this, it looks as if there is only
       * one section.  The other enc_heads are set to NULL to avoid freeing them more than
       * once. */
       for (i = 1; i < ENC_SECTS; i += 1)
	{
	  stream->enc_tails[i-1]->next_page = stream->enc_heads[i];
	  stream->enc_heads[i] = NULL;
	}

    enc_output:

      stream->enc_state  = ENC_POSTOUT;
      stream->next_out   = stream->enc_current->base;
      stream->avail_out  = stream->enc_current->next;
      stream->total_out += (xoff_t) stream->avail_out;

      /* If there is any output in this buffer, return it, otherwise fall through to
       * handle the next buffer or finish the window after all buffers have been
       * output. */
      if (stream->avail_out > 0)
	{
	  /* This is the only place xd3_encode returns XD3_OUTPUT */
	  return XD3_OUTPUT;
	}

    case ENC_POSTOUT:

      if (stream->avail_out != 0)
	{
	  stream->msg = "missed call to consume output";
	  return EINVAL;
	}

      /* Continue outputting one buffer at a time, until the next is NULL. */
      if ((stream->enc_current = stream->enc_current->next_page) != NULL)
	{
	  goto enc_output;
	}

      stream->total_in += (xoff_t) stream->avail_in;
      stream->enc_state = ENC_POSTWIN;

      return XD3_WINFINISH;

    case ENC_POSTWIN:

      xd3_encode_reset (stream);

      stream->current_window += 1;
      stream->enc_state = ENC_INPUT;

      /* If there is leftover input to flush, repeat. */
      if ((stream->buf_leftover != NULL) && (stream->flags & XD3_FLUSH))
	{
	  goto enc_flush;
	}

      /* Ready for more input. */
      return XD3_INPUT;

    default:
      stream->msg = "invalid state";
      return EINVAL;
    }
}
#endif /* XD3_ENCODER */

/******************************************************************************************
 Client convenience functions
 ******************************************************************************************/

/* This function invokes either encode or decode to and from in-memory arrays.  The output array
 * must be large enough to hold the output or else ENOSPC is returned. */
static int
xd3_process_completely (xd3_stream    *stream,
			int          (*func) (xd3_stream *),
			int            close_stream,
			const uint8_t *input,
			usize_t         input_size,
			uint8_t       *output,
			usize_t        *output_size,
			usize_t         avail_size)
{
  (*output_size) = 0;

  xd3_avail_input (stream, input, input_size);

  for (;;)
    {
      int ret = func(stream);
      
      switch(ret)
	{
	case XD3_OUTPUT: { /* memcpy below */ break; }
	case XD3_INPUT: { /* this means EOF */ goto done; }
	case XD3_GOTHEADER: { /* ignore */ continue; }
	case XD3_WINSTART: { /* ignore */ continue; }
	case XD3_WINFINISH: { /* ignore */ continue; }
	case XD3_GETSRCBLK:
	  {
	    stream->msg = "stream requires source input\n";
	    return EINVAL;
	  }
	case 0: /* there is no plain "success" return for xd3_encode/decode */
	  XD3_ASSERT (ret != 0);
	default:
	  return ret;
	}

      if (*output_size + stream->avail_out > avail_size)
	{
	  stream->msg = "insufficient output space";
	  return ENOSPC;
	}

      memcpy (output + *output_size, stream->next_out, stream->avail_out);

      *output_size += stream->avail_out;
      xd3_consume_output (stream);
    }
 done:
  return (close_stream == 0) ? 0 : xd3_close_stream (stream);
}

int
xd3_decode_completely (xd3_stream    *stream,
		       const uint8_t *input,
		       usize_t         input_size,
		       uint8_t       *output,
		       usize_t        *output_size,
		       usize_t         avail_size)
{
  return xd3_process_completely (stream, & xd3_decode_input, 1,
				 input, input_size,
				 output, output_size, avail_size);
}

#if XD3_ENCODER
int
xd3_encode_completely (xd3_stream    *stream,
		       const uint8_t *input,
		       usize_t         input_size,
		       uint8_t       *output,
		       usize_t        *output_size,
		       usize_t         avail_size)
{
  return xd3_process_completely (stream, & xd3_encode_input, 1,
				 input, input_size,
				 output, output_size, avail_size);
}
#endif

/******************************************************************************************
 DECODE stuff
 ******************************************************************************************/

/* Return true if the caller must provide a source.  Theoretically, this has to be checked
 * after every window.  It could be that the first window requires no source, but the
 * second window does.  In practice? */
int xd3_decoder_needs_source (xd3_stream *stream)
{
  return stream->dec_win_ind & VCD_SOURCE;
}

/* Initialize the decoder for a new window.  The dec_tgtlen value is preserved across
 * successive window decodings, and the update to dec_winstart is delayed until a new
 * window actually starts.  This is to avoid throwing an error due to overflow until the
 * last possible moment.  This makes it possible to encode exactly 4GB through a 32-bit
 * encoder. */
static int
xd3_decode_init_window (xd3_stream *stream)
{
  stream->dec_cpylen = 0;
  stream->dec_cpyoff = 0;
  stream->dec_cksumbytes = 0;

  xd3_init_cache (& stream->acache);

  return 0;
}

/* Allocates buffer space for the target window and possibly the VCD_TARGET copy-window.
 * Also sets the base of the two copy segments. */
static int
xd3_decode_setup_buffers (xd3_stream *stream)
{
  /* If VCD_TARGET is set then the previous buffer may be reused. */
  if (stream->dec_win_ind & VCD_TARGET)
    {
      /* But this implementation only supports copying from the last target window.  If the
       * offset is outside that range, it can't be done. */
      if (stream->dec_cpyoff < stream->dec_laststart)
	{
	  stream->msg = "unsupported VCD_TARGET offset";
	  return EINVAL;
	}

      /* See if the two windows are the same.  This indicates the first time VCD_TARGET is
       * used.  This causes a second buffer to be allocated, after that the two are
       * swapped in the DEC_FINISH case. */
      if (stream->dec_lastwin == stream->next_out)
	{
	  stream->next_out  = NULL;
	  stream->space_out = 0;
	}

      stream->dec_cpyaddrbase = stream->dec_lastwin + (usize_t) (stream->dec_cpyoff - stream->dec_laststart);
    }

  /* See if the current output window is large enough. */
  if (stream->space_out < stream->dec_tgtlen)
    {
      xd3_free (stream, stream->dec_buffer);

      stream->space_out = xd3_round_blksize (stream->dec_tgtlen, XD3_ALLOCSIZE);

      if ((stream->dec_buffer = xd3_alloc (stream, stream->space_out, 1)) == NULL)
	{
	  return ENOMEM;
	}

      stream->next_out = stream->dec_buffer;
    }

  /* dec_tgtaddrbase refers to an invalid base address, but it is always used with a
   * sufficiently large instruction offset (i.e., beyond the copy window).  This condition
   * is enforced by xd3_decode_output_halfinst. */
  stream->dec_tgtaddrbase = stream->next_out - stream->dec_cpylen;

  return 0;
}

static int
xd3_decode_allocate (xd3_stream  *stream,
		     usize_t       size,
		     uint8_t    **copied1,
		     usize_t      *alloc1,
		     uint8_t    **copied2,
		     usize_t      *alloc2)
{
  if (*copied1 != NULL && *alloc1 < size)
    {
      xd3_free (stream, *copied1);
      *copied1 = NULL;
    }

  if (*copied1 == NULL)
    {
#if SECONDARY_ANY
      /* Borrow from the secondary compressor's allocation. */
      if (copied2 != NULL && *copied2 != NULL && *alloc2 < size)
	{
	  *copied1 = *copied2;
	  *alloc1  = *alloc2;
	  *copied2 = NULL;
	  *alloc2  = 0;
	}
      else
#endif
	{
	  *alloc1 = xd3_round_blksize (size, XD3_ALLOCSIZE);

	  if ((*copied1 = xd3_alloc (stream, *alloc1, 1)) == NULL)
	    {
	      return ENOMEM;
	    }
	}
    }

  return 0;
}

static int
xd3_decode_section (xd3_stream *stream,
		    xd3_desect *section,
		    xd3_decode_state nstate,
		    int copy)
{
  XD3_ASSERT (section->pos <= section->size);
  XD3_ASSERT (stream->dec_state != nstate);

  if (section->pos < section->size)
    {
      usize_t sect_take;

      if (stream->avail_in == 0)
	{
	  return XD3_INPUT;
	}

      if ((copy == 0) && (section->pos == 0))
	{
	  /* No allocation/copy needed */
	  section->buf = stream->next_in;
	  sect_take    = section->size;
	}
      else
	{
	  usize_t sect_need = section->size - section->pos;

	  /* Allocate and copy */
	  sect_take = min (sect_need, stream->avail_in);

	  if (section->pos == 0)
	    {
	      int ret;

	      if ((ret = xd3_decode_allocate (stream,
					      section->size,
					      & section->copied1,
					      & section->alloc1,
					      & section->copied2,
					      & section->alloc2))) { return ret; }

	      section->buf = section->copied1;
	    }

	  memcpy (section->copied1 + section->pos,
		  stream->next_in,
		  sect_take);
	}

      section->pos += sect_take;

      stream->dec_winbytes += sect_take;

      DECODE_INPUT (sect_take);
    }

  if (section->pos < section->size)
    {
      stream->msg = "further input required";
      return XD3_INPUT;
    }

  XD3_ASSERT (section->pos == section->size);

  stream->dec_state = nstate;
  section->buf_max  = section->buf + section->size;
  section->pos      = 0;
  return 0;
}

/* Decode the size and address for half of an instruction (i.e., a single opcode).  This
 * updates the stream->dec_position, which are bytes already output prior to processing
 * this instruction.  Perform bounds checking for sizes and copy addresses, which uses the
 * dec_position (which is why these checks are done here). */
static int
xd3_decode_parse_halfinst (xd3_stream *stream, xd3_hinst *inst)
{
  int ret;

  /* If the size from the instruction table is zero then read a size value. */
  if ((inst->size == 0) &&
      (ret = xd3_read_size (stream,
 			    & stream->inst_sect.buf,
			      stream->inst_sect.buf_max,

			    & inst->size)))
    {
      return EINVAL;
    }

  /* For copy instructions, read address. */
  if (inst->type >= XD3_CPY)
    {
      IF_DEBUG1 ({
	static int cnt = 0;
	P(RINT "DECODE:%u: COPY at %"Q"u (winoffset %u) size %u winaddr %u\n",
		 cnt++,
		 stream->total_out + (stream->dec_position - stream->dec_cpylen),
		 (stream->dec_position - stream->dec_cpylen),
		 inst->size,
		 inst->addr);
      });

      if ((ret = xd3_decode_address (stream,
				     stream->dec_position,
				     inst->type - XD3_CPY,
				     & stream->addr_sect.buf,
				     stream->addr_sect.buf_max,
				     & inst->addr)))
	{
	  return ret;
	}

      /* Cannot copy an address before it is filled-in. */
      if (inst->addr >= stream->dec_position)
	{
	  stream->msg = "address too large";
	  return EINVAL;
	}

      /* Check: a VCD_TARGET or VCD_SOURCE copy cannot exceed the remaining buffer space
       * in its own segment. */
      if (inst->addr < stream->dec_cpylen && inst->addr + inst->size > stream->dec_cpylen)
	{
	  stream->msg = "size too large";
	  return EINVAL;
	}
    }
  else
    {
      IF_DEBUG1 ({
	if (inst->type == XD3_ADD)
	  {
	    static int cnt;
	    P(RINT "DECODE:%d: ADD at %"Q"u (winoffset %u) size %u\n",
		     cnt++,
		     stream->total_out + stream->dec_position - stream->dec_cpylen,
		     stream->dec_position - stream->dec_cpylen,
		     inst->size);
	  }
	else
	  {
	    static int cnt;
	    XD3_ASSERT (inst->type == XD3_RUN);
	    P(RINT "DECODE:%d: RUN at %"Q"u (winoffset %u) size %u\n",
		     cnt++,
		     stream->total_out + stream->dec_position - stream->dec_cpylen,
		     stream->dec_position - stream->dec_cpylen,
		     inst->size);
	  }
      });
    }

  /* Check: The instruction will not overflow the output buffer. */
  if (stream->dec_position + inst->size > stream->dec_maxpos)
    {
      stream->msg = "size too large";
      return EINVAL;
    }

  stream->dec_position += inst->size;
  return 0;
}

/* Decode a single opcode and then decode the two half-instructions. */
static int
xd3_decode_instruction (xd3_stream *stream)
{
  int ret;
  const xd3_dinst *inst;

  if (stream->inst_sect.buf == stream->inst_sect.buf_max)
    {
      stream->msg = "instruction underflow";
      return EINVAL;
    }

  inst = &stream->code_table[*stream->inst_sect.buf++];

  stream->dec_current1.type = inst->type1;
  stream->dec_current2.type = inst->type2;
  stream->dec_current1.size = inst->size1;
  stream->dec_current2.size = inst->size2;

  /* For each instruction with a real operation, decode the corresponding size and
   * addresses if necessary.  Assume a code-table may have NOOP in either position,
   * although this is unlikely. */
  if (inst->type1 != XD3_NOOP && (ret = xd3_decode_parse_halfinst (stream, & stream->dec_current1)))
    {
      return ret;
    }
  if (inst->type2 != XD3_NOOP && (ret = xd3_decode_parse_halfinst (stream, & stream->dec_current2)))
    {
      return ret;
    }
  return 0;
}

/* Output the result of a single half-instruction. OPT: This the decoder hotspot. */
static int
xd3_decode_output_halfinst (xd3_stream *stream, xd3_hinst *inst)
{
  /* To make this reentrant, set take = min (inst->size, available space)... */
  usize_t take = inst->size;

  XD3_ASSERT (inst->type != XD3_NOOP);

  switch (inst->type)
    {
    case XD3_RUN:
      {
	/* Only require a single data byte. */
	if (stream->data_sect.buf == stream->data_sect.buf_max)
	  {
	    stream->msg = "data underflow";
	    return EINVAL;
	  }

	/* TUNE: Probably want to eliminate memset/memcpy here */
	memset (stream->next_out + stream->avail_out,
		stream->data_sect.buf[0],
		take);

	stream->data_sect.buf += 1;
	stream->avail_out += take;
	inst->type = XD3_NOOP;
	break;
      }
    case XD3_ADD:
      {
	/* Require at least TAKE data bytes. */
	if (stream->data_sect.buf + take > stream->data_sect.buf_max)
	  {
	    stream->msg = "data underflow";
	    return EINVAL;
	  }

	memcpy (stream->next_out + stream->avail_out,
		stream->data_sect.buf,
		take);

	stream->data_sect.buf += take;
	stream->avail_out += take;
	inst->type = XD3_NOOP;
	break;
      }
    default:
      {
	usize_t i;
	const uint8_t *src;
	uint8_t *dst;

	/* See if it copies from the VCD_TARGET/VCD_SOURCE window or the target window.
	 * Out-of-bounds checks for the addresses and sizes are performed in
	 * xd3_decode_parse_halfinst. */
	if (inst->addr < stream->dec_cpylen)
	  {
	    if (stream->dec_win_ind & VCD_TARGET)
	      {
		/* For VCD_TARGET we know the entire range is in-memory, as established by
		 * decode_setup_buffers. */
		src = stream->dec_cpyaddrbase + inst->addr;
		inst->type = XD3_NOOP;
		inst->size = 0;
	      }
	    else
	      {
		/* In this case we have to read a source block, which could return control
		 * to the caller.  We need to know the first block number needed for this
		 * copy. */
		xd3_source *source;
		xoff_t block;
		usize_t blkoff;
		usize_t blksize;
		int ret;

	      more:

		source  = stream->src;
		block   = source->cpyoff_blocks;
		blkoff  = source->cpyoff_blkoff + inst->addr;
		blksize = source->blksize;

 		while (blkoff >= blksize)
		  {
		    block  += 1;
		    blkoff -= blksize;
		  }

		if ((ret = xd3_getblk (stream, block)))
		  {
		    /* could be a XD3_GETSRCBLK failure. */
		    return ret;
		  }

		src = source->curblk + blkoff;

		/* This block either contains enough data or the source file is
		 * short. */
		if ((source->onblk != blksize) && (blkoff + take > source->onblk))
		  {
		    stream->msg = "source file too short";
		    return EINVAL;

		  }

		XD3_ASSERT (blkoff != blksize);

		if (blkoff + take <= blksize)
		  {
		    inst->type = XD3_NOOP;
		    inst->size = 0;
		  }
		else
		  {
		    /* This block doesn't contain all the data, modify the instruction, do
		     * not set to XD3_NOOP. */
		    take = blksize - blkoff;
		    inst->size -= take;
		    inst->addr += take;
		  }
	      }
	  }
	else
	  {
	    /* For a target-window copy, we know the entire range is in-memory.  The
	     * dec_tgtaddrbase is negatively offset by dec_cpylen because the addresses
	     * start beyond that point. */
	    src = stream->dec_tgtaddrbase + inst->addr;
	    inst->type = XD3_NOOP;
	    inst->size = 0;
	  }

 	dst = stream->next_out + stream->avail_out;

	stream->avail_out += take;

	/* Can't just memcpy here due to possible overlap. */
	for (i = take; i != 0; i -= 1)
	  {
	    *dst++ = *src++;
	  }

	take = inst->size;

	/* If there is more to copy, call getblk again. */
	if (inst->type != XD3_NOOP)
	  {
	    XD3_ASSERT (take > 0);
	    goto more;
	  }
	else
	  {
	    XD3_ASSERT (take == 0);
	  }
      }
    }

  return 0;
}

static int
xd3_decode_finish_window (xd3_stream *stream)
{
  stream->dec_winbytes  = 0;
  stream->dec_state     = DEC_FINISH;

  stream->data_sect.pos = 0;
  stream->inst_sect.pos = 0;
  stream->addr_sect.pos = 0;

  return XD3_OUTPUT;
}

static int
xd3_decode_sections (xd3_stream *stream)
{
  usize_t need, more, take;
  int copy, ret;

  if ((stream->flags & XD3_JUST_HDR) != 0)
    {
      /* Nothing left to do. */
      return xd3_decode_finish_window (stream);
    }

  /* To avoid copying, need this much data available */
  need = (stream->inst_sect.size +
	  stream->addr_sect.size +
	  stream->data_sect.size);

  /* The window may be entirely processed. */
  XD3_ASSERT (stream->dec_winbytes <= need);

  /* Compute how much more input is needed. */
  more = (need - stream->dec_winbytes);

  /* How much to consume. */
  take = min (more, stream->avail_in);

  /* See if the input is completely available, to avoid copy. */
  copy = (take != more);

  /* If the window is skipped... */
  if ((stream->flags & XD3_SKIP_WINDOW) != 0)
    {
      /* Skip the available input. */
      DECODE_INPUT (take);

      stream->dec_winbytes += take;

      if (copy)
	{
	  stream->msg = "further input required";
	  return XD3_INPUT;
	}

      return xd3_decode_finish_window (stream);
    }

  /* Process all but the DATA section. */
  switch (stream->dec_state)
    {
    default:
      stream->msg = "internal error";
      return EINVAL;

    case DEC_DATA:
      if ((ret = xd3_decode_section (stream, & stream->data_sect, DEC_INST, copy))) { return ret; }
    case DEC_INST:
      if ((ret = xd3_decode_section (stream, & stream->inst_sect, DEC_ADDR, copy))) { return ret; }
    case DEC_ADDR:
      if ((ret = xd3_decode_section (stream, & stream->addr_sect, DEC_EMIT, copy))) { return ret; }
    }

  XD3_ASSERT (stream->dec_winbytes == need);

#if SECONDARY_ANY
#define DECODE_SECONDARY_SECTION(UPPER,LOWER) \
  ((stream->dec_del_ind & VCD_ ## UPPER ## COMP) && \
   (ret = xd3_decode_secondary (stream, & stream-> LOWER ## _sect, \
					& xd3_sec_ ## LOWER (stream))))

  if (DECODE_SECONDARY_SECTION (DATA, data) ||
      DECODE_SECONDARY_SECTION (INST, inst) ||
      DECODE_SECONDARY_SECTION (ADDR, addr))
    {
      return ret;
    }
#endif

  if (stream->flags & XD3_SKIP_EMIT)
    {
      return xd3_decode_finish_window (stream);
    }

  /* OPT: A possible optimization is to avoid allocating memory in decode_setup_buffers
   * and to avoid a large memcpy when the window consists of a single VCD_SOURCE copy
   * instruction.  The only potential problem is if the following window is a VCD_TARGET,
   * then you need to remember... */
  if ((ret = xd3_decode_setup_buffers (stream))) { return ret; }

  return 0;
}

static int
xd3_decode_emit (xd3_stream *stream)
{
  int ret;

  /* Produce output: originally structured to allow reentrant code that fills as much of
   * the output buffer as possible, but VCDIFF semantics allows to copy from anywhere from
   * the target window, so instead allocate a sufficiently sized buffer after the target
   * window length is decoded.
   *
   * This code still needs to be reentrant to allow XD3_GETSRCBLK to return control.  This
   * is handled by setting the stream->dec_currentN instruction types to XD3_NOOP after
   * they have been processed. */
  XD3_ASSERT (! (stream->flags & XD3_SKIP_EMIT));
  XD3_ASSERT (stream->avail_out == 0);
  XD3_ASSERT (stream->dec_tgtlen <= stream->space_out);

  while (stream->inst_sect.buf != stream->inst_sect.buf_max)
    {
      /* Decode next instruction pair. */
      if ((stream->dec_current1.type == XD3_NOOP) &&
	  (stream->dec_current2.type == XD3_NOOP) &&
	  (ret = xd3_decode_instruction (stream))) { return ret; }

      /* Output for each instruction. */
      if ((stream->dec_current1.type != XD3_NOOP) &&
	  (ret = xd3_decode_output_halfinst (stream, & stream->dec_current1))) { return ret; }

      if ((stream->dec_current2.type != XD3_NOOP) &&
	  (ret = xd3_decode_output_halfinst (stream, & stream->dec_current2))) { return ret; }
    }

  if (stream->avail_out != stream->dec_tgtlen)
    {
      IF_DEBUG1 (P(RINT "AVAIL_OUT(%d) != DEC_TGTLEN(%d)\n", stream->avail_out, stream->dec_tgtlen));
      stream->msg = "wrong window length";
      return EINVAL;
    }

  if (stream->data_sect.buf != stream->data_sect.buf_max)
    {
      stream->msg = "extra data section";
      return EINVAL;
    }

  if (stream->addr_sect.buf != stream->addr_sect.buf_max)
    {
      stream->msg = "extra address section";
      return EINVAL;
    }

  /* OPT: Should cksum computation be combined with the above loop? */
  if ((stream->dec_win_ind & VCD_ADLER32) != 0 &&
      (stream->flags & XD3_ADLER32_NOVER) == 0)
    {
      uint32_t a32 = adler32 (1L, stream->next_out, stream->avail_out);

      if (a32 != stream->dec_adler32)
	{
	  stream->msg = "target window checksum mismatch";
	  return EINVAL;
	}
    }

  /* Finished with a window. */
  return xd3_decode_finish_window (stream);
}

int
xd3_decode_input (xd3_stream *stream)
{
  int ret;

  if (stream->enc_state != 0)
    {
      stream->msg = "encoder/decoder transition";
      return EINVAL;
    }

#define BYTE_CASE(expr,x,nstate)                                               \
      do {                                                                     \
      if ( (expr) &&                                                           \
           ((ret = xd3_decode_byte (stream, (uint *)(&(x)))) != 0) ) { return ret; }    \
      stream->dec_state = (nstate);                                            \
      } while (0)

#define OFFSET_CASE(expr,x,nstate)                                             \
      do {                                                                     \
      if ( (expr) &&                                                           \
           ((ret = xd3_decode_offset (stream, & (x))) != 0) ) { return ret; }  \
      stream->dec_state = (nstate);                                            \
      } while (0)

#define SIZE_CASE(expr,x,nstate)                                               \
      do {                                                                     \
      if ( (expr) &&                                                           \
           ((ret = xd3_decode_size (stream, & (x))) != 0) ) { return ret; }    \
      stream->dec_state = (nstate);                                            \
      } while (0)

#define SRCORTGT(x) (((x) & VCD_SRCORTGT) == VCD_SOURCE ||                     \
		     ((x) & VCD_SRCORTGT) == VCD_TARGET)

  switch (stream->dec_state)
    {
    case DEC_VCHEAD:
      {
	if ((ret = xd3_decode_bytes (stream, stream->dec_magic, & stream->dec_magicbytes, 4))) { return ret; }

	if (stream->dec_magic[0] != VCDIFF_MAGIC1 ||
	    stream->dec_magic[1] != VCDIFF_MAGIC2 ||
	    stream->dec_magic[2] != VCDIFF_MAGIC3)
	  {
	    stream->msg = "not a VCDIFF input";
	    return EINVAL;
	  }

	if (stream->dec_magic[3] != 0)
	  {
	    stream->msg = "VCDIFF input version > 0 is not supported";
	    return EINVAL;
	  }

	stream->dec_state = DEC_HDRIND;
      }
    case DEC_HDRIND:
      {
	if ((ret = xd3_decode_byte (stream, (uint *)&stream->dec_hdr_ind))) { return ret; }

	if ((stream->dec_hdr_ind & VCD_INVHDR) != 0)
	  {
	    stream->msg = "unrecognized header indicator bits set";
	    return EINVAL;
	  }

	stream->dec_state = DEC_SECONDID;
      }

    case DEC_SECONDID:
      /* Secondary compressor ID: only if VCD_SECONDARY is set */
      if ((stream->dec_hdr_ind & VCD_SECONDARY) != 0)
	{
	  BYTE_CASE (1, stream->dec_secondid, DEC_TABLEN);

	  switch (stream->dec_secondid)
	    {
	    case VCD_FGK_ID:
	      FGK_CASE (stream);
	    case VCD_DJW_ID:
	      DJW_CASE (stream);
	    default:
	      stream->msg = "unknown secondary compressor ID";
	      return EINVAL;
	    }
	}

    case DEC_TABLEN:
      /* Length of code table data: only if VCD_CODETABLE is set */
      SIZE_CASE ((stream->dec_hdr_ind & VCD_CODETABLE) != 0, stream->dec_codetblsz, DEC_NEAR);

      /* The codetblsz counts the two NEAR/SAME bytes */
      if ((stream->dec_hdr_ind & VCD_CODETABLE) != 0) {
	if (stream->dec_codetblsz <= 2) {
	  stream->msg = "invalid code table size";
	  return ENOMEM;
	}
	stream->dec_codetblsz -= 2;
      }
    case DEC_NEAR:
      /* Near modes: only if VCD_CODETABLE is set */
      BYTE_CASE((stream->dec_hdr_ind & VCD_CODETABLE) != 0, stream->acache.s_near, DEC_SAME);
    case DEC_SAME:
      /* Same modes: only if VCD_CODETABLE is set */
      BYTE_CASE((stream->dec_hdr_ind & VCD_CODETABLE) != 0, stream->acache.s_same, DEC_TABDAT);
    case DEC_TABDAT:
      /* Compressed code table data */

      if ((stream->dec_hdr_ind & VCD_CODETABLE) != 0)
	{
	  /* Get the code table data. */
	  if ((stream->dec_codetbl == NULL) &&
	      (stream->dec_codetbl = xd3_alloc (stream, stream->dec_codetblsz, 1)) == NULL) { return ENOMEM; }

	  if ((ret = xd3_decode_bytes (stream, stream->dec_codetbl, & stream->dec_codetblbytes, stream->dec_codetblsz)))
	    {
	      return ret;
	    }

	  if ((ret = xd3_apply_table_encoding (stream, stream->dec_codetbl, stream->dec_codetblbytes)))
	    {
	      return ret;
	    }
	}
      else
	{
	  /* Use the default table. */
	  stream->acache.s_near = __rfc3284_code_table_desc.near_modes;
	  stream->acache.s_same = __rfc3284_code_table_desc.same_modes;
	  stream->code_table    = xd3_rfc3284_code_table ();
	}

      if ((ret = xd3_alloc_cache (stream))) { return ret; }

      stream->dec_state = DEC_APPLEN;

    case DEC_APPLEN:
      /* Length of application data */
      SIZE_CASE((stream->dec_hdr_ind & VCD_APPHEADER) != 0, stream->dec_appheadsz, DEC_APPDAT);

    case DEC_APPDAT:
      /* Application data */
      if (stream->dec_hdr_ind & VCD_APPHEADER)
	{
	  /* Note: we add an additional byte for padding, to allow 0-termination. */
	  if ((stream->dec_appheader == NULL) &&
	      (stream->dec_appheader = xd3_alloc (stream, stream->dec_appheadsz+1, 1)) == NULL) { return ENOMEM; }

	  stream->dec_appheader[stream->dec_appheadsz] = 0;

	  if ((ret = xd3_decode_bytes (stream, stream->dec_appheader, & stream->dec_appheadbytes, stream->dec_appheadsz)))
	    {
	      return ret;
	    }
	}

      stream->dec_hdrsize = stream->total_in;
      stream->dec_state = DEC_WININD;

    case DEC_WININD:
      {
	/* Start of a window: the window indicator */

	if ((ret = xd3_decode_byte (stream, (uint *)&stream->dec_win_ind))) { return ret; }

	stream->current_window = stream->dec_window_count;

	if (XOFF_T_OVERFLOW (stream->dec_winstart, stream->dec_tgtlen))
	  {
	    stream->msg = "decoder file offset overflow";
	    return EINVAL;
	  }

	stream->dec_winstart += stream->dec_tgtlen;

	if ((stream->dec_win_ind & VCD_INVWIN) != 0)
	  {
	    stream->msg = "unrecognized window indicator bits set";
	    return EINVAL;
	  }

	if ((ret = xd3_decode_init_window (stream))) { return ret; }

	stream->dec_state = DEC_CPYLEN;

	IF_DEBUG1 (P(RINT "--------- TARGET WINDOW %"Q"u ------------------\n", stream->current_window));
      }

    case DEC_CPYLEN:
      /* Copy window length: only if VCD_SOURCE or VCD_TARGET is set */
      SIZE_CASE(SRCORTGT (stream->dec_win_ind), stream->dec_cpylen, DEC_CPYOFF);

      /* Set the initial, logical decoder position (HERE address) in dec_position.  This
       * is set to just after the source/copy window, as we are just about to output the
       * first byte of target window. */
      stream->dec_position = stream->dec_cpylen;

    case DEC_CPYOFF:
      /* Copy window offset: only if VCD_SOURCE or VCD_TARGET is set */
      OFFSET_CASE(SRCORTGT (stream->dec_win_ind), stream->dec_cpyoff, DEC_ENCLEN);

      /* Copy offset and copy length may not overflow. */
      if (XOFF_T_OVERFLOW (stream->dec_cpyoff, stream->dec_cpylen))
	{
	  stream->msg = "decoder copy window overflows a file offset";
	  return EINVAL;
	}

      /* Check copy window bounds: VCD_TARGET window may not exceed current position. */
      if ((stream->dec_win_ind & VCD_TARGET) &&
	  (stream->dec_cpyoff + (xoff_t) stream->dec_cpylen > stream->dec_winstart))
	{
	  stream->msg = "VCD_TARGET window out of bounds";
	  return EINVAL;
	}

    case DEC_ENCLEN:
      /* Length of the delta encoding */
      SIZE_CASE(1, stream->dec_enclen, DEC_TGTLEN);
    case DEC_TGTLEN:
      /* Length of target window */
      SIZE_CASE(1, stream->dec_tgtlen, DEC_DELIND);

      /* Set the maximum decoder position, beyond which we should not decode any data.
       * This is the maximum value for dec_position.  This may not exceed the size of a
       * usize_t. */
      if (USIZE_T_OVERFLOW (stream->dec_cpylen, stream->dec_tgtlen))
	{
	  stream->msg = "decoder target window overflows a usize_t";
	  return EINVAL;
	}

      /* Check for malicious files. */
      if (stream->dec_tgtlen > XD3_HARDMAXWINSIZE)
	{
	  stream->msg = "hard window size exceeded";
	  return EINVAL;
	}

      stream->dec_maxpos = stream->dec_cpylen + stream->dec_tgtlen;

    case DEC_DELIND:
      /* Delta indicator */
      BYTE_CASE(1, stream->dec_del_ind, DEC_DATALEN);

      if ((stream->dec_del_ind & VCD_INVDEL) != 0)
	{
	  stream->msg = "unrecognized delta indicator bits set";
	  return EINVAL;
	}

      /* Delta indicator is only used with secondary compression. */
      if ((stream->dec_del_ind != 0) && (stream->sec_type == NULL))
	{
	  stream->msg = "invalid delta indicator bits set";
	  return EINVAL;
	}

      /* Section lengths */
    case DEC_DATALEN:
      SIZE_CASE(1, stream->data_sect.size, DEC_INSTLEN);
    case DEC_INSTLEN:
      SIZE_CASE(1, stream->inst_sect.size, DEC_ADDRLEN);
    case DEC_ADDRLEN:
      SIZE_CASE(1, stream->addr_sect.size, DEC_CKSUM);

    case DEC_CKSUM:
      /* Window checksum. */
      if ((stream->dec_win_ind & VCD_ADLER32) != 0)
	{
	  int i;

	  if ((ret = xd3_decode_bytes (stream, stream->dec_cksum, & stream->dec_cksumbytes, 4))) { return ret; }

	  for (i = 0; i < 4; i += 1)
	    {
	      stream->dec_adler32 = (stream->dec_adler32 << 8) | stream->dec_cksum[i];
	    }
	}

      stream->dec_state = DEC_DATA;

      /* Check dec_enclen for redundency, otherwise it is not really used. */
      {
	usize_t enclen_check = (1 + (xd3_sizeof_size (stream->dec_tgtlen) +
				    xd3_sizeof_size (stream->data_sect.size) +
				    xd3_sizeof_size (stream->inst_sect.size) +
				    xd3_sizeof_size (stream->addr_sect.size)) +
			       stream->data_sect.size +
			       stream->inst_sect.size +
			       stream->addr_sect.size +
			       ((stream->dec_win_ind & VCD_ADLER32) ? 4 : 0));

	if (stream->dec_enclen != enclen_check)
	  {
	    stream->msg = "incorrect encoding length (redundent)";
	    return EINVAL;
	  }
      }

      /* Returning here gives the application a chance to inspect the header, skip the
       * window, etc. */
      if (stream->current_window == 0) { return XD3_GOTHEADER; }
      else                             { return XD3_WINSTART; }

    case DEC_DATA:
    case DEC_INST:
    case DEC_ADDR:
      /* Next read the three sections. */
     if ((ret = xd3_decode_sections (stream))) { return ret; }

    case DEC_EMIT:

      /* To speed VCD_SOURCE block-address calculations, the source cpyoff_blocks and
       * cpyoff_blkoff are pre-computed. */
      if (stream->dec_win_ind & VCD_SOURCE)
	{
	  xd3_source *src = stream->src;

	  if (src == NULL)
	    {
	      stream->msg = "source input required";
	      return EINVAL;
	    }

	  src->cpyoff_blocks = stream->dec_cpyoff / src->blksize;
	  src->cpyoff_blkoff = stream->dec_cpyoff % src->blksize;
	}

      /* xd3_decode_emit returns XD3_OUTPUT on every success. */
      if ((ret = xd3_decode_emit (stream)) == XD3_OUTPUT)
	{
	  stream->total_out += (xoff_t) stream->avail_out;
	}

      return ret;

    case DEC_FINISH:
      {
	if (stream->dec_win_ind & VCD_TARGET)
	  {
	    if (stream->dec_lastwin == NULL)
	      {
		stream->dec_lastwin   = stream->next_out;
		stream->dec_lastspace = stream->space_out;
	      }
	    else
	      {
		xd3_swap_uint8p (& stream->dec_lastwin,   & stream->next_out);
		xd3_swap_usize_t (& stream->dec_lastspace, & stream->space_out);
	      }
	  }

	stream->dec_lastlen   = stream->dec_tgtlen;
	stream->dec_laststart = stream->dec_winstart;
	stream->dec_window_count += 1;

	/* Note: the updates to dec_winstart & current_window are deferred until after the
	 * next DEC_WININD byte is read. */
	stream->dec_state = DEC_WININD;
	return XD3_WINFINISH;
      }

    default:
      stream->msg = "invalid state";
      return EINVAL;
    }
}

/******************************************************************************************
 String matching helpers
 ******************************************************************************************/

#if XD3_ENCODER
/* Do the initial xd3_string_match() checksum table setup.  Allocations are delayed until
 * first use to avoid allocation sometimes (e.g., perfect matches, zero-length inputs). */
static int
xd3_string_match_init (xd3_stream *stream)
{
  const int DO_SMALL = ! (stream->flags & XD3_NOCOMPRESS);
  const int DO_LARGE = (stream->src != NULL);

  if (DO_SMALL)
    {
      /* Subsequent calls can return immediately after checking reset. */
      if (stream->small_table != NULL)
	{
	  /* The target hash table is reinitialized once per window. */
	  if (stream->small_reset)
	    {
	      stream->small_reset = 0;
	      memset (stream->small_table, 0, sizeof (usize_t) * stream->small_hash.size);
	    }

	  return 0;
	}

      if ((stream->small_table = xd3_alloc0 (stream, stream->small_hash.size, sizeof (usize_t))) == NULL)
	{
	  return ENOMEM;
	}

      /* If there is a previous table needed. */
      if (stream->small_chain > 1)
	{
	  xd3_slist *p, *m;

	  if ((stream->small_prev = xd3_alloc (stream, stream->sprevsz, sizeof (xd3_slist))) == NULL)
	    {
	      return ENOMEM;
	    }

	  /* Initialize circular lists. */
	  for (p = stream->small_prev, m = stream->small_prev + stream->sprevsz; p != m; p += 1)
	    {
	      p->next = p;
	      p->prev = p;
	    }
	}
    }

  if (DO_LARGE && stream->large_table == NULL)
    {
      if ((stream->large_table = xd3_alloc0 (stream, stream->large_hash.size, sizeof (usize_t))) == NULL)
	{
	  return ENOMEM;
	}
    }

  return 0;
}

#if XD3_DEBUG > 1
/* This debug output helps understand the xd3_srcwin_update and
 * xd3_advance_cksum_pos functions. */
static void
xd3_srcwin_debug (xd3_stream *stream, const char *label)
{
  char buf[64];
  int  len;

  strcpy (buf, label);  /* Pad label with spaces */
  len = strlen (buf);
  while (len < 20) { buf[len++] = ' '; }
  buf[len] = 0;

  P(RINT "%20s input %"Q"d<->%"Q"d (%d<->%d) far-tgt-match:%"Q"d exp %d far-src-match:%"Q"d lag=%d cksum pos %"Q"d\n",
	   buf,

	   stream->total_in + stream->input_position,
	   stream->total_in + stream->input_position + stream->match_fwd,

	   stream->input_position,
	   stream->input_position + stream->match_fwd,

	   stream->target_farthest_match,

	   stream->expand_point,

	   stream->srcwin_farthest_match,
	   stream->srcwin_lagging_by,
           stream->srcwin_cksum_pos);

}
#endif /* XD3_DEBUG */

/* To expand, double the srcwin_size up to srcwin_maxsz. */
static void
xd3_srcwin_expand_size (xd3_stream *stream)
{
  /* Except for the initial advance, double the size each time. */
  if (stream->expand_cnt++ != 0)
    {
      stream->srcwin_size *= 2;
    }
  if (stream->srcwin_size > stream->srcwin_maxsz)
    {
      stream->srcwin_size = stream->srcwin_maxsz;
    }
}

/* This is called any time the input position becomes >= stream->expand_point either
 * during or after string match completes.*/
static int
xd3_srcwin_update_point (xd3_stream *stream)
{
  int ret;

  if (stream->end_of_source)
    {
      stream->expand_point = stream->input_position + (1 << 28);
      return 0;
    }

  /* Advance_cksum_pos uses match_fwd. */
  stream->match_srcpos = stream->srcwin_farthest_match;
  stream->match_fwd = 0;

  IF_DEBUG1 (xd3_srcwin_debug (stream, "[srcwin expand]"));

  XD3_ASSERT (stream->input_position >= stream->expand_point);

  /* We have reached the expansion point, double the window
   * size. */
  xd3_srcwin_expand_size (stream);

  /* Now advance the cksum_pos by two times window size. */
  if ((ret = xd3_advance_cksum_pos (stream, stream->srcwin_size * 2)))
    {
      return ret;
    }

  /* Advance expand_point  */
  stream->expand_point = stream->input_position + stream->srcwin_size;
  IF_DEBUG1 (P(RINT "[expand point] pos %"Q"u expand next %u\n", stream->srcwin_cksum_pos, stream->expand_point));
  return 0;
}

/* This function updates the stream srcwin variables after a source match.  The relevant
 * inputs are the stream's input_position, match_fwd, match_srcpos, expand_point,
 * srcwin_cksum_point, and srcwin_size. */
static int
xd3_srcwin_update (xd3_stream *stream)
{
  int ret;
  xoff_t tgt_match_end;
  xoff_t src_match_end;
  usize_t advance_by; /* how many bytes to advance the srcwin_cksum_pos */
  usize_t expand_ahead = stream->expand_point - stream->input_position;
  const char *l;

  IF_DEBUG1 ({
    char buf[32];
    static int x = 0;
    sprintf (buf, "[srcwin preup:%d]", x++);
    xd3_srcwin_debug (stream, buf);
  });

  /* Check that we never find a match after the checksum point.  The distance from the
   * match to the last checksum must be >= 0, this is the number of bytes of remaining
   * source that can still be expected to match.  It actually should be strictly greater-
   * than since cksum_pos represents the next checksum to be inserted, but the initial
   * condition has them equal. */
  XD3_ASSERT (stream->match_srcpos <= stream->srcwin_cksum_pos);
  XD3_ASSERT (stream->srcwin_cksum_pos >= stream->srcwin_farthest_match);
  XD3_ASSERT (stream->match_back == 0);

  /* Skip this if we've reached end-of-source. */
  if (stream->end_of_source)
    {
      return 0;
    }

  /* Compute the ends of the match in both source and target. */
  tgt_match_end = stream->total_in + stream->input_position + stream->match_fwd;
  src_match_end = stream->match_srcpos + stream->match_fwd;

  XD3_ASSERT (tgt_match_end > stream->target_farthest_match);

  /* There are three cases depending on whether the source match
   * begins/ends before/after srcwin_farthest_match/srcwin_cksum_pos. */
  if (src_match_end > stream->srcwin_cksum_pos)
    {
      /* If the match completely crosses the srcwin_cksum_pos, assume complete
       * realignment.  Set the expand point to the window size and twice that for the
       * checksum position, which restores the default advance/expand ratio at the
       * current srcwin_size.  This skips checksum computation for some or all of the
       * matched region. */
      l = "REALIGN";
      advance_by = stream->srcwin_size * 2;

      stream->srcwin_lagging_by = 0;
      stream->srcwin_cksum_pos = src_match_end;
      //stream->expand_point += stream->match_fwd;
    }
  else if (src_match_end > stream->srcwin_farthest_match)
    {
      /* Limit is (srcwin_cksum_pos - src_match_end == 0) -- complete realignment
       * occurs.  This is considered partial realignment.  There are actually two
       * cases here, based on (stream->match_srcpos >=
       * stream->srcwin_farthest_match). */
      int howmuch;
      l = "PARTIAL";

      advance_by = (src_match_end - stream->srcwin_farthest_match);
      howmuch = min (advance_by, stream->srcwin_lagging_by);

      /* If it's ahead by more than the lagging count, it should have reached the
       * realign case above. */
      //stream->expand_point += stream->match_fwd;
      stream->srcwin_lagging_by -= howmuch;
      advance_by -= howmuch;
    }
  else
    {
      /* The match is lagging behind the srcwin_farthest_match, but the srcwin_cksum_pos
       * advances conservatively, in case the stream is being modified and the next match
       * is still ahead in the source.  If, on the other hand, this is an insertion the
       * source, it would be correct to wait for the input to begin matching again. If
       * this is a deleteion from the source, it is especially important to advance
       * because the current input matches further ahead in the source. */
      l = "LAGGING";

      advance_by = tgt_match_end - stream->target_farthest_match;

      stream->srcwin_lagging_by += advance_by;
      //stream->expand_point += stream->match_fwd;
    }

  /* The first call to xd3_advance_cksum_pos must come from xd3_string_match, tested by
   * expand_cnt == 0.  xd3_srcwin_update may be called after an initial copy is
   * discovered, in that case we haven't begun checksum computation yet, there is no table
   * to fill. */
  if (stream->expand_cnt != 0 && advance_by != 0)
    {
      IF_DEBUG1 ({
	/* This info tells whether the source and target addresses are the same in
	 * absolute stream offset. */
	char buf[64];
	int  cal = (stream->total_in + stream->input_position) - stream->match_srcpos;
	static int cnt;
	sprintf (buf, "%d:%s %s:",
		 cnt++, l,
		 (cal == 0) ? "[srcwin cal]" :
		 (cal > 0 ? "[srcwin tgt]" : "[srcwin src]"));

	xd3_srcwin_debug (stream, buf);
      });

      if ((ret = xd3_advance_cksum_pos (stream, advance_by)))
	{
	  return ret;
	}
    }

  stream->expand_point = stream->input_position + stream->match_fwd + expand_ahead;

  /* Always a new target_farthest_match. */
  stream->target_farthest_match = tgt_match_end;

  /* Update srcwin_farthest_match: */
  if (src_match_end > stream->srcwin_farthest_match)
    {
      stream->srcwin_farthest_match = src_match_end;
    }

  /* Check that the expansion is still ahead of us. */
  XD3_ASSERT (stream->input_position + stream->match_fwd < stream->expand_point ||
	      stream->end_of_source ||
	      stream->expand_cnt == 0);

  IF_DEBUG1 ({
    char buf[32];
    static int x = 0;
    sprintf (buf, "[srcwin update:%d]", x++);
    xd3_srcwin_debug (stream, buf);
  });

  return 0;
}

/* This function computes more source checksums to advance the window.  The expand
 * variable indicates whether we are advancing the window because the input reached the
 * last advancement point (???), in which case set expand = 1 in xd3_string_match().  This
 * doubles the window size.  (Note the initial state assumes the first call will have --
 * see xd3_config_stream()).  The other call is from xd3_srcwin_update(), in which case
 * the window size remains the same.*/
static int
xd3_advance_cksum_pos (xd3_stream *stream, usize_t bytes)
{
  /* TODO config: compile these?  this division stuff is cheaper that way. */
  usize_t LLOOK = stream->large_look;
  usize_t LSTEP = stream->large_step;

  int ret;
  xd3_source *source = stream->src;
  xoff_t      blkno;
  xoff_t      offset;
  usize_t      blkoff;
  usize_t      blkoff_start;
  usize_t      blkoff_end;
  usize_t      entry;
  usize_t      rem;
  usize_t      onblk;

  /* Skip to expand if the srcwin_cksum_pos is not advancing.  Skip if the table is not
   * allocated.  That way this is never the initial call to xd3_advance_cksum_pos, which
   * assumes the first expand call has been made (initial condition). */
  if (bytes == 0
      || stream->end_of_source
      || stream->large_table == NULL) { return 0; }

  rem    = stream->srcwin_cksum_pos & stream->large_step_mask;
  offset = stream->srcwin_cksum_pos & ~stream->large_step_mask;

  if (rem + bytes < LLOOK) { goto advance; }

  IF_DEBUG1 ({
    static int x = 0;
    P(RINT "[adv cksum:%u] bytes %u rem %u pos %"Q"u\n", x++, bytes, rem, stream->srcwin_cksum_pos);
  });

  bytes += rem;

  /* Notice this narrowing of the 64-bit offset into "entry". */
  entry  = (offset >> stream->large_step_bits) + HASH_CKOFFSET;

 again:

  /* When the end of the source is reached, make sure this is not called again. */
  if (offset + LLOOK >= source->size)
    {
      stream->end_of_source = 1;
      goto advance;
    }

  XD3_ASSERT (bytes >= LLOOK);

  blkno  = offset / source->blksize;
  blkoff = offset % source->blksize;

  onblk  = xd3_bytes_on_srcblk (source, blkno);

  /* Check whether there is not enough data left on this block. */
  if (onblk < LLOOK || blkoff > (onblk - LLOOK))
    {
      /* Skip block-crossing checksums. */
      offset += LLOOK;
      bytes  -= LLOOK;
      goto again;
    }

  if ((ret = xd3_getblk (stream, blkno))) { return ret; }

  blkoff_start = blkoff;
  blkoff_end   = min (source->onblk, blkoff + bytes) - LLOOK;

  for (; blkoff <= blkoff_end; blkoff += LSTEP, entry += 1)
    {
      uint32_t  cksum = xd3_lcksum (source->curblk + blkoff, LLOOK);
      usize_t  hval = xd3_checksum_hash (& stream->large_hash, cksum);

      stream->large_table[hval] = entry;

      IF_DEBUG (stream->large_ckcnt += 1);
    }

  /* Advance to the next block. */
  bytes  -= blkoff - blkoff_start;
  offset += blkoff - blkoff_start;

  /* Maybe go to the next block. */
  if (bytes >= LLOOK) { goto again; }

 advance:
  stream->srcwin_cksum_pos = offset + bytes;
  return 0;
}

/* This function sets up the stream->src fields srcbase, srclen.  The call is delayed
 * until these values are needed to encode a copy address.  At this point the decision has
 * to be made. */
static int
xd3_srcwin_setup (xd3_stream *stream)
{
  xd3_source *src = stream->src;
  xoff_t length;

  IF_DEBUG1 (P(RINT "[srcwin setup:%"Q"u] iopt buffer %s\n",
		stream->current_window,
		stream->enc_state < ENC_FLUSH ? "overflow" : "fit"));

  /* Check the undecided state. */
  XD3_ASSERT (src->srclen == 0 && src->srcbase == 0);

  /* Avoid repeating this call. */
  stream->srcwin_decided = 1;

  /* If the stream is flushing, then the iopt buffer was able to contain the complete
   * encoding.  If no copies were issued no source window is actually needed.  This
   * prevents the VCDIFF header from including source base/len.  xd3_emit_hdr checks
   * for srclen == 0. */
  if (stream->enc_state == ENC_FLUSH && stream->match_maxaddr == 0)
    {
      goto done;
    }

  /* Check for overflow, srclen is usize_t - this can't happen unless XD3_DEFAULT_SRCBACK
   * and related parameters are extreme - should use smaller windows. */
  length = stream->match_maxaddr - stream->match_minaddr;

  if (length > (xoff_t) USIZE_T_MAX)
    {
      stream->msg = "source window length overflow (not 64bit)";
      return EINVAL;
    }

  /* If ENC_FLUSH, then we know the exact source window to use because no more copies can
   * be issued. */
  if (stream->enc_state == ENC_FLUSH)
    {
      src->srcbase = stream->match_minaddr;
      src->srclen  = (usize_t) length;
      XD3_ASSERT (src->srclen);
      goto done;
    }

  /* Otherwise, we have to make a guess.  More copies may still be issued, but we have to
   * decide the source window base and length now.  */

  /* TODO window selection: should consider more variables.
   * 1. Use srcwin_farthest_match, or srcwin_cksum_pos
   * 2. Consider XD3_DEFAULT_SRCBACK
   */
  src->srcbase = stream->match_minaddr;
  src->srclen  = max ((usize_t) length, stream->avail_in + (stream->avail_in >> 2));
  if (src->size < src->srcbase + (xoff_t) src->srclen)
    {
      /* Could reduce srcbase, as well. */
      src->srclen = src->size - src->srcbase;
    }

  XD3_ASSERT (src->srclen);
 done:
  IF_DEBUG1 (P(RINT "[srcwin setup:%"Q"u] base %"Q"u size %u\n",
		      stream->current_window,
		      src->srcbase,
		      src->srclen));
  /* Set the taroff.  This convenience variable is used even when stream->src == NULL. */
  stream->taroff = src->srclen;
  return 0;
}

/* Sets the bounding region for a newly discovered source match, prior to calling
 * xd3_source_extend_match().  This sets the match_maxfwd, match_maxback variables.  Note: srcpos is
 * an absolute position (xoff_t) but the match_maxfwd, match_maxback variables are usize_t.  Returns
 * 0 if the setup succeeds, or 1 if the source position lies outside an already-decided
 * srcbase/srclen window. */
static int
xd3_source_match_setup (xd3_stream *stream, xoff_t srcpos)
{
  xd3_source *src = stream->src;
  usize_t greedy_or_not;

  if (max_in == 0)
    {
      goto bad;
    }

  /* Prevent the match from seeking too far back.  The extra blocksize tends to
   * keep xdelta3-main's block cache from seeking during xd3_encode, but TODO: doesn't work!
   */
  if ((stream->srcwin_cksum_pos - srcpos) > (stream->srcwin_maxsz - src->blksize))
    {
      goto bad;
    }

  if (sizeof (xoff_t) != sizeof (usize_t))
    {
      /* Unfinished case: large_table contains usize_t, not xoff_t, so there is a point at
       * which the large_table entry will overflow.  This will become an issue only for
       * files near ~100GB or larger based on match size. */
      XD3_ASSERT ((srcpos / (xoff_t) stream->large_step) < USIZE_T_MAX);
    }

  /* First configure the match_maxback, match_maxfwd variables based on the available target
   * input. */

  /* Going backwards, the 1.5-pass algorithm allows some already-matched input may be
   * covered by a longer source match.  The greedy algorithm does not allow this. */
  if (stream->flags & XD3_BEGREEDY)
    {
      /* The greedy algorithm allows backward matching to the last matched position. */
      greedy_or_not = xd3_iopt_last_matched (stream);
    }
  else
    {
      /* The 1.5-pass algorithm allows backward matching to go back as far as the
       * unencoded offset, which is updated as instructions pass out of the iopt buffer.
       * If this (default) is chosen, it means xd3_iopt_erase may be called to eliminate
       * instructions when a covering source match is found. */
      greedy_or_not = stream->unencoded_offset;
    }

  /* Backward target match limit. */
  XD3_ASSERT (pos_in >= greedy_or_not);
  stream->match_maxback = pos_in - greedy_or_not;

  /* Forward target match limit. */
  XD3_ASSERT (max_in > pos_in);
  stream->match_maxfwd = max_in - pos_in;

  /* Now we take the source position into account.  It depends whether the srclen/srcbase
   * have been decided yet. */
  if (stream->srcwin_decided == 0)
    {
      /* Unrestricted case: the match can cover the entire source, 0--src->size.  We
       * compare the usize_t match_maxfwd/match_maxback against the xoff_t src->size/srcpos values
       * and take the min. */
      xoff_t srcavail;

      if (srcpos < (xoff_t) stream->match_maxback)
	{
	  stream->match_maxback = srcpos;
	}

      srcavail = src->size - srcpos;
      if (srcavail < (xoff_t) stream->match_maxfwd)
	{
	  stream->match_maxfwd = srcavail;
	}

      goto good;
    }

  /* Decided some source window. */
  XD3_ASSERT (src->srclen > 0);

  /* Restricted case: fail if the srcpos lies outside the source window */
  if ((srcpos < src->srcbase) || (srcpos > (src->srcbase + (xoff_t) src->srclen)))
    {
      goto bad;
    }
  else
    {
      usize_t srcavail;

      srcavail = (usize_t) (srcpos - src->srcbase);
      if (srcavail < stream->match_maxback)
	{
	  stream->match_maxback = srcavail;
	}

      srcavail = (usize_t) (src->srcbase + (xoff_t) src->srclen - srcpos);
      if (srcavail < stream->match_maxfwd)	{
	  stream->match_maxfwd = srcavail;
	}

      goto good;
    }

 good:
  stream->match_state  = MATCH_BACKWARD;
  stream->match_srcpos = srcpos;
  stream->match_back   = 0;
  stream->match_fwd    = 0;
  return 0;

 bad:
  stream->match_state  = MATCH_SEARCHING;
  return 1;
}

/* This function expands the source match backward and forward.  It is reentrant, since
 * xd3_getblk may return XD3_GETSRCBLK, so most variables are kept in xd3_stream.  There
 * are two callers of this function, the string_matching routine when a checksum match is
 * discovered, and xd3_encode_input whenever a continuing (or initial) match is suspected.
 * The two callers do different things with the input_position, thus this function leaves
 * that variable untouched.  If a match is taken the resulting stream->match_fwd is left
 * non-zero. */
static int
xd3_source_extend_match (xd3_stream *stream)
{
  int ret;
  xd3_source *src = stream->src;
  xoff_t matchoff;  /* matchoff is the current right/left-boundary of the source match being tested. */
  usize_t streamoff; /* streamoff is the current right/left-boundary of the input match being tested. */
  xoff_t tryblk;    /* tryblk, tryoff are the block, offset position of matchoff */
  usize_t tryoff;
  usize_t tryrem;    /* tryrem is the number of matchable bytes on the source block */

  XD3_ASSERT (src != NULL);

  /* Does it make sense to compute backward match AFTER forward match? */
  if (stream->match_state == MATCH_BACKWARD)
    {
      /* Note: this code is practically duplicated below, substituting
       * match_fwd/match_back and direction.  Consolidate? */
      matchoff  = stream->match_srcpos - stream->match_back;
      streamoff = pos_in - stream->match_back;
      tryblk    = matchoff / src->blksize;
      tryoff    = matchoff % src->blksize;

      /* this loops backward over source blocks */
      while (stream->match_back < stream->match_maxback)
	{
	  /* see if we're backing across a source block boundary */
	  if (tryoff == 0)
	    {
	      tryoff  = src->blksize;
	      tryblk -= 1;
	    }

	  if ((ret = xd3_getblk (stream, tryblk)))
	    {
	      /* could be a XD3_GETSRCBLK failure. */
	      return ret;
	    }

	  /* OPT: This code can be optimized. */
	  for (tryrem = min (tryoff, stream->match_maxback - stream->match_back);
	       tryrem != 0;
	       tryrem -= 1, stream->match_back += 1)
	    {
	      if (src->curblk[tryoff-1] != stream->next_in[streamoff-1])
		{
		  goto doneback;
		}

	      tryoff    -= 1;
	      streamoff -= 1;
	    }
	}

    doneback:
      stream->match_state = MATCH_FORWARD;
    }

  XD3_ASSERT (stream->match_state == MATCH_FORWARD);

  matchoff  = stream->match_srcpos + stream->match_fwd;
  streamoff = pos_in + stream->match_fwd;
  tryblk    = matchoff / src->blksize;
  tryoff    = matchoff % src->blksize;

  /* Note: practically the same code as backwards case above: same comments */
  while (stream->match_fwd < stream->match_maxfwd)
    {
      if ((ret = xd3_getblk (stream, tryblk)))
	{
	  return ret;
	}

      /* There's a good speedup for doing word comparions: see zlib. */
      for (tryrem = min(stream->match_maxfwd - stream->match_fwd,
			src->blksize - tryoff);
	   tryrem != 0;
	   tryrem -= 1, stream->match_fwd += 1)
	{
	  if (src->curblk[tryoff] != stream->next_in[streamoff])
	    {
	      goto donefwd;
	    }

	  tryoff    += 1;
	  streamoff += 1;
	}

      if (tryoff == src->blksize)
	{
	  tryoff  = 0;
	  tryblk += 1;
	}
    }

 donefwd:
  stream->match_state = MATCH_SEARCHING;

  /* Now decide whether to take the match.  There are several ways to answer this
   * question and this is likely the best answer.  There is currently an assertion
   * in xd3_iopt_erase that checks whether min_match works.  This variable maintains
   * that every match exceeds the end of the previous match.  However, it is
   * possible that match_back allows us to find a match that goes a long way back
   * but not enough forward.  We could try an alternate approach, which might help
   * or it might just be extra complexity: eliminate the next match_fwd >= min_match
   * test and call xd3_iopt_erase right away.  Erase instructions as far as it goes
   * back, then either remember what was deleted and re-insert it, or count on the
   * string-matching algorithm to find that match again.  I think it is more
   * worthwhile to implement large_hash duplicates. */
  if (stream->match_fwd < min_match)
    {
      stream->match_fwd = 0;
    }
  else
    {
      usize_t total  = stream->match_fwd + stream->match_back;
      xoff_t match_end;

      /* Correct the variables to remove match_back from the equation. */
      stream->input_position -= stream->match_back;
      stream->match_srcpos   -= stream->match_back;
      stream->match_fwd      += stream->match_back;
      match_end               = stream->match_srcpos + stream->match_fwd;

      /* At this point we may have to erase any iopt-buffer instructions that are
       * fully covered by a backward-extending copy. */
      if (stream->match_back > 0)
	{
	  xd3_iopt_erase (stream, pos_in, total);
	}

      stream->match_back = 0;

      /* Update ranges.  The first source match occurs with both values set to 0. */
      if (stream->match_maxaddr == 0 ||
	  stream->match_srcpos < stream->match_minaddr)
	{
	  stream->match_minaddr = stream->match_srcpos;
	}

      if (match_end > stream->match_maxaddr)
	{
	  stream->match_maxaddr = match_end;
	}

      IF_DEBUG1 ({
	static int x = 0;
	P(RINT "[source match:%d] <inp %"Q"u %"Q"u>  <src %"Q"u %"Q"u> (%s) [ %u bytes ]\n",
	   x++,
	   stream->total_in + pos_in,
	   stream->total_in + pos_in + stream->match_fwd,
	   stream->match_srcpos,
	   stream->match_srcpos + stream->match_fwd,
	   (stream->total_in + stream->input_position == stream->match_srcpos) ? "same" : "diff",
	   stream->match_fwd);
      });

      if ((ret = xd3_found_match (stream,
				/* decoder position */ pos_in,
				/* length */ stream->match_fwd,
				/* address */ stream->match_srcpos,
				/* is_source */ 1)))
	{
	  return ret;
	}

      if ((ret = xd3_srcwin_update (stream)))
	{
	  return ret;
	}

      /* If the match ends with the available input: */
      if (pos_in + stream->match_fwd == max_in)
	{
	  /* Setup continuing match for the next window. */
	  stream->match_state   = MATCH_TARGET;
	  stream->match_srcpos += stream->match_fwd;
	}
    }

  return 0;
}

/* Update the small hash.  Values in the small_table are offset by HASH_CKOFFSET (1) to
 * distinguish empty buckets the zero offset.  This maintains the previous linked lists.
 * If owrite is true then this entry is replacing the existing record, otherwise it is
 * merely being called to promote the existing record in the hash bucket (for the same
 * address cache). */
static void
xd3_scksum_insert (xd3_stream *stream, usize_t inx, usize_t scksum, usize_t pos)
{
  /* If we are maintaining previous links. */
  if (stream->small_prev)
    {
      usize_t     last_pos = stream->small_table[inx];
      xd3_slist *pos_list = & stream->small_prev[pos & stream->sprevmask];
      xd3_slist *prev     = pos_list->prev;
      xd3_slist *next     = pos_list->next;

      /* Assert link structure, update pos, cksum */
      XD3_ASSERT (prev->next == pos_list);
      XD3_ASSERT (next->prev == pos_list);
      pos_list->pos = pos;
      pos_list->scksum = scksum;

      /* Subtract HASH_CKOFFSET and test for a previous offset. */
      if (last_pos-- != 0)
	{
	  xd3_slist *last_list = & stream->small_prev[last_pos & stream->sprevmask];
	  xd3_slist *last_next;

	  /* Verify existing entry. */
	  SMALL_HASH_DEBUG1 (stream, stream->next_in + last_pos);
	  SMALL_HASH_DEBUG2 (stream, stream->next_in + pos);

	  /* The two positions (mod sprevsz) may have the same checksum, making the old
	   * and new entries the same.  That is why the removal step is not before the
	   * above if-stmt. */
	  if (last_list != pos_list)
	    {
	      /* Remove current position from any list it may belong to. */
	      next->prev = prev;
	      prev->next = next;

	      /* The ordinary case, add current position to last_list. */
	      last_next = last_list->next;

	      pos_list->next = last_next;
	      pos_list->prev = last_list;

	      last_next->prev = pos_list;
	      last_list->next = pos_list;
	    }
	}
      else
	{
	  /* Remove current position from any list it may belong to. */
	  next->prev = prev;
	  prev->next = next;

	  /* Re-initialize current position. */
	  pos_list->next = pos_list;
	  pos_list->prev = pos_list;
	}
    }

  /* Enter the new position into the hash bucket. */
  stream->small_table[inx] = pos + HASH_CKOFFSET;
}

#if XD3_DEBUG
static int
xd3_check_smatch (const char *ref0, const char *inp0, const char *inp_max, usize_t cmp_len)
{
  int i;

  for (i = 0; i < cmp_len; i += 1)
    {
      XD3_ASSERT (ref0[i] == inp0[i]);
    }

  if (inp0 + cmp_len < inp_max)
    {
      XD3_ASSERT (inp0[i] != ref0[i]);
    }

  return 1;
}
#endif /* XD3_DEBUG */

/* When the hash table indicates a possible small string match, it calls this routine to
 * find the best match.  The first matching position is taken from the small_table,
 * HASH_CKOFFSET is subtracted to get the actual position.  After checking that match, if
 * previous linked lists are in use (because stream->small_chain > 1), previous matches
 * are tested searching for the longest match.  If (min_match > MIN_MATCH) then a lazy
 * match is in effect.
 *
 * OPT: This is by far the most expensive function.  The slowdown is in part due to the data
 * structure it maintains, which is relatively more expensive than it needs to be (in
 * comparison to zlib) in order to support the PROMOTE decision, which is to prefer the
 * most recently used matching address of a certain string to aid the VCDIFF same cache.
 *
 * TODO: weak reasoning? it's time to modularize this routine...?  Let's say the PROMOTE
 * feature supported by this slow data structure contributes around 2% improvement in
 * compressed size, is there a better code table that doesn't use the SAME address cache,
 * for which the speedup-discount could produce a better encoding?
 */
static /*inline*/ usize_t
xd3_smatch (xd3_stream *stream, usize_t base, usize_t scksum, usize_t *match_offset)
{
  usize_t         cmp_len;
  usize_t         match_length = 0;
  usize_t         chain        = (min_match == MIN_MATCH ?
				  stream->small_chain :
				  stream->small_lchain);
  xd3_slist     *current      = NULL;
  xd3_slist     *first        = NULL;
  const uint8_t *inp_max      = stream->next_in + max_in;
  const uint8_t *inp;
  const uint8_t *ref;

  SMALL_HASH_STATS  (usize_t search_cnt = 0);
  SMALL_HASH_DEBUG1 (stream, stream->next_in + pos_in);
  SMALL_HASH_STATS  (stream->sh_searches += 1);

  XD3_ASSERT (min_match + pos_in <= max_in);

  base -= HASH_CKOFFSET;

  /* Initialize the chain. */
  if (stream->small_prev != NULL)
    {
      first = current = & stream->small_prev[base & stream->sprevmask];

      /* Check if current->pos is correct. */
      if (current->pos != base) { goto done; }
    }

 again:

  SMALL_HASH_STATS (search_cnt += 1);

  /* For small matches, we can always go to the end-of-input because the matching position
   * must be less than the input position. */
  XD3_ASSERT (base < pos_in);

  ref = stream->next_in + base;
  inp = stream->next_in + pos_in;

  SMALL_HASH_DEBUG2 (stream, ref);

  /* Expand potential match forward. */
  while (inp < inp_max && *inp == *ref) { ++inp; ++ref; }

  cmp_len = inp - (stream->next_in + pos_in);

  /* Verify correctness */
  XD3_ASSERT (xd3_check_smatch (stream->next_in + base, stream->next_in + pos_in, inp_max, cmp_len));

  /* Update longest match */
  if (cmp_len > match_length)
    {
      ( match_length) = cmp_len;
      (*match_offset) = base;

      /* Stop if we match the entire input or discover a long_enough match. */
      if (inp == inp_max || cmp_len >= stream->long_enough)
	{
	  goto done;
	}
    }

  /* If we have not reached the chain limit, see if there is another previous position. */
  if (current)
    {
      while (--chain != 0)
	{
	  /* Calculate the next base offset. */
	  current = current->prev;
	  base    = current->pos;

	  /* Stop if the next position was the first.  Stop if the position is wrong (because
	   * the lists are not re-initialized across input windows). Skip if the scksum is wrong. */
	  if (current != first && base < pos_in)
	    {
	      if (current->scksum != scksum)
		{
		  continue;
		}
	      goto again;
	    }
	}
    }

 done:
  SMALL_HASH_STATS (stream->sh_compares += search_cnt);
  return match_length;
}

#if XD3_DEBUG
static void
xd3_verify_small_state (xd3_stream    *stream,
			const uint8_t *inp,
			uint32_t          x_cksum)
{
  uint32_t cksum = xd3_scksum (inp, stream->small_look);

  XD3_ASSERT (cksum == x_cksum);
}

static void
xd3_verify_large_state (xd3_stream    *stream,
			const uint8_t *inp,
			uint32_t          x_cksum)
{
  uint32_t cksum = xd3_lcksum (inp, stream->large_look);

  XD3_ASSERT (cksum == x_cksum);
}

static void
xd3_verify_run_state (xd3_stream    *stream,
		      const uint8_t *inp,
		      int            x_run_l,
		      uint8_t        x_run_c)
{
  int     slook = stream->small_look;
  uint8_t run_c;
  int     run_l = xd3_comprun (inp, slook, &run_c);

  XD3_ASSERT (run_l == 0 || run_c == x_run_c);
  XD3_ASSERT (x_run_l > slook || run_l == x_run_l);
}
#endif /* XD3_DEBUG */
#endif /* XD3_ENCODER */

/******************************************************************************************
 TEMPLATE pass
 ******************************************************************************************/

#endif /* __XDELTA3_C_INLINE_PASS__ */
#ifdef __XDELTA3_C_TEMPLATE_PASS__

#if XD3_ENCODER

/******************************************************************************************
 Templates
 ******************************************************************************************/

/* Template macros: less than 30 lines work.  the template parameters appear as, e.g.,
 * SLOOK, MIN_MATCH, TRYLAZY, etc. */
#define XD3_TEMPLATE(x)      XD3_TEMPLATE2(x,TEMPLATE)
#define XD3_TEMPLATE2(x,n)   XD3_TEMPLATE3(x,n)
#define XD3_TEMPLATE3(x,n)   x ## n
#define XD3_STRINGIFY(x)     XD3_STRINGIFY2(x)
#define XD3_STRINGIFY2(x)    #x

static int XD3_TEMPLATE(xd3_string_match_) (xd3_stream *stream);

static const xd3_smatcher XD3_TEMPLATE(__smatcher_) =
{
  XD3_STRINGIFY(TEMPLATE),
  XD3_TEMPLATE(xd3_string_match_),
#if SOFTCFG == 1
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0
#else
  LLOOK, LSTEP, SLOOK, SCHAIN, SLCHAIN, SSMATCH, TRYLAZY, MAXLAZY,
  LONGENOUGH, PROMOTE
#endif
};

static int
XD3_TEMPLATE(xd3_string_match_) (xd3_stream *stream)
{
  /* TODO config: These next three variables should be statically compliled in various
   * scan_cfg configurations? */
  const int      DO_SMALL = ! (stream->flags & XD3_NOCOMPRESS);
  const int      DO_LARGE = (stream->src != NULL);
  const int      DO_RUN   = (1);

  const uint8_t *inp;
  uint32_t       scksum = 0;
  uint32_t       lcksum = 0;
  usize_t         sinx;
  usize_t         linx;
  uint8_t        run_c;
  int            run_l;
  int            ret;
  usize_t         match_length;
  usize_t         match_offset;

  /* If there will be no compression due to settings or short input, skip it entirely. */
  if (! (DO_SMALL || DO_LARGE || DO_RUN) || pos_in + SLOOK > max_in) { goto loopnomore; }

  if ((ret = xd3_string_match_init (stream))) { return ret; }

  /* The restartloop label is reached when the incremental loop state needs to be
   * reset. */
 restartloop:

  /* If there is not enough input remaining for any kind of match, skip it. */
  if (pos_in + SLOOK > max_in) { goto loopnomore; }

  IF_DEBUG1 ({
    static int x = 0;
    P(RINT "[string match:%d] pos_in %d; cksum_pos %"Q"d; expand %d; "
       "size %d; ck-far %"Q"d\n",
       x++, pos_in, stream->srcwin_cksum_pos, stream->expand_point, stream->srcwin_size,
       stream->srcwin_cksum_pos - stream->srcwin_farthest_match);
  });

  /* Now reset the incremental loop state: */

  /* The min_match variable is updated to avoid matching the same lazy match over and over
   * again.  For example, if you find a (small) match of length 9 at one position, you
   * will likely find a match of length 8 at the next position. */
  min_match = MIN_MATCH;

  /* The current input byte. */
  inp = stream->next_in + pos_in;

  /* Small match state. */
  if (DO_SMALL)
    {
      scksum = xd3_scksum (inp, SLOOK);
    }

  /* Run state. */
  if (DO_RUN)
    {
      run_l = xd3_comprun (inp, SLOOK, & run_c);
    }

  /* Large match state.  We continue the loop even after not enough bytes for LLOOK
   * remain, so always check pos_in in DO_LARGE code. */
  if (DO_LARGE)
    {
      /* Source window: In case a small copy resets passing stream->expand_point, skipping
       * the (pos_in ==) check below this call gets back in sync.  expand_point is a
       * logical offset to pos_in. */
      if (pos_in > stream->expand_point)
	{
	  if ((ret = xd3_srcwin_update_point (stream))) { return ret; }
	}

      /* The current input cksum (if enough input remains). */
      if (pos_in + LLOOK <= max_in)
	{
	  lcksum = xd3_lcksum (inp, LLOOK);
	}
    }

  /* TRYLAZYLEN: True if a certain length match should be followed by lazy search.  This
   * checks that LEN is shorter than MAXLAZY and that there is enough leftover data to
   * consider lazy matching.  "Enough" is set to 2 since the next match will start at the
   * next offset, it must match two extra characters. */
#define TRYLAZYLEN(LEN,POS,MAX) ((TRYLAZY && (LEN) < MAXLAZY) && ((POS) + (LEN) <= (MAX) - 2))

  /* HANDLELAZY: This statement is called each time an instruciton is emitted (three
   * cases).  If the instruction is large enough, the loop is restarted, otherwise lazy
   * matching may ensue. */
#define HANDLELAZY(mlen)                                         \
  if (TRYLAZYLEN ((mlen), pos_in, max_in))                       \
    { min_match = (mlen) + LEAST_MATCH_INCR; goto updateone; }   \
  else                                                           \
    { pos_in += (mlen); goto restartloop; }

  /* Now loop over one input byte at a time until a match is found... */
  for (;; inp += 1, pos_in += 1)
    {
      /* Now we try three kinds of string match in order of expense:
       * run, large match, small match. */

      /* Expand the start of a RUN.  The test for (run_l == SLOOK) avoids repeating this
       * check when we pass through a run area performing lazy matching.  The run is only
       * expanded once when the min_match is first reached.  If lazy matching is
       * performed, the run_l variable will remain inconsistent until the first
       * non-running input character is reached, at which time the run_l may then again
       * grow to SLOOK. */
      if (DO_RUN && unlikely (run_l == SLOOK))
	{
	  usize_t max_len = max_in - pos_in;

	  IF_DEBUG (xd3_verify_run_state (stream, inp, run_l, run_c));

	  while (run_l < max_len && inp[run_l] == run_c) { run_l += 1; }

	  /* Output a RUN instruction. */
	  if (run_l >= min_match && run_l >= MIN_RUN)
	    {
	      if ((ret = xd3_emit_run (stream, pos_in, run_l, run_c))) { return ret; }

	      /* Note: We never enter a checksum for the start of a run, but we do end
	       * up entering checksums during lazy matching for the subsequent offsets
	       * within the run.  This makes it possible we'll miss a byte of copy. */
	      stream->expand_point += run_l;

	      HANDLELAZY (run_l);
	    }
	}

      /* If there is enough input remaining. */
      if (DO_LARGE && (pos_in + LLOOK <= max_in))
	{
	  /* See if the srcwin should advance and expand the window.  The expand_point
	   * variable is maintained by xd3_srcwin_update(), but this routine also modifies
	   * pos_in.  If pos_in is incremented by one at a time, the following test is
	   * sufficient, but the DO_SMALL code may restart the loop, therefore the call to
	   * xd3_srcwin_update() above. */
	  // TODO: what???
	  XD3_ASSERT (pos_in <= stream->expand_point);

	  if (unlikely (pos_in == stream->expand_point))
	    {
	      if ((ret = xd3_srcwin_update_point (stream))) { return ret; }
	    }

	  linx = xd3_checksum_hash (& stream->large_hash, lcksum);

	  IF_DEBUG (xd3_verify_large_state (stream, inp, lcksum));

	  /* Note: To handle large checksum duplicates, this code should be rearranged to
	   * resemble the small_match case more.  But how much of the code can be truly
	   * shared?  The main difference is the need for xd3_source_extend_match to work
	   * outside of xd3_string_match, in the case where inputs are identical. */
	  if (unlikely (stream->large_table[linx] != 0))
	    {
	      /* the match_setup will fail if the source window has been decided and the
	       * match lies outside it.  You could consider forcing a window at this point
	       * to permit a new source window. */
	      if ((xd3_source_match_setup (stream,
		      (xoff_t) (stream->large_table[linx] - HASH_CKOFFSET) * (xoff_t) LSTEP)) == 0)
		{
		  if ((ret = xd3_source_extend_match (stream))) { return ret; }

		  /* Update stream position.  match_fwd is zero if no match. */
		  if (stream->match_fwd > 0)
		    {
		      HANDLELAZY (stream->match_fwd);
		    }
		}
	    }
	}

      /* Small matches. */
      if (DO_SMALL)
	{
	  sinx = xd3_checksum_hash (& stream->small_hash, scksum);

	  /* Verify incremental state in debugging mode. */
	  IF_DEBUG (xd3_verify_small_state (stream, inp, scksum));

	  /* Search for the longest match */
	  if (unlikely (stream->small_table[sinx] != 0))
	    {
	      match_length = xd3_smatch (stream,
					 stream->small_table[sinx],
					 scksum,
					 & match_offset);
	    }
	  else
	    {
	      match_length = 0;
	    }

	  /* Insert a hash for this string. */
	  xd3_scksum_insert (stream, sinx, scksum, pos_in);

	  /* Promote the previous match address to head of the hash bucket.  This is
	   * intended to improve the same cache hit rate. */
	  if (match_length != 0 && PROMOTE)
	    {
	      xd3_scksum_insert (stream, sinx, scksum, match_offset);
	    }

	  /* Maybe output a COPY instruction */
	  if (unlikely (match_length >= min_match))
	    {
	      IF_DEBUG1 ({
		static int x = 0;
		P(RINT "[target match:%d] <inp %u %u>  <cpy %u %u> (-%d) [ %u bytes ]\n",
		   x++,
		   pos_in,
		   pos_in + match_length,
		   match_offset,
		   match_offset + match_length,
		   pos_in - match_offset,
		   match_length);
	      });

	      stream->expand_point += match_length;

	      if ((ret = xd3_found_match (stream,
					/* decoder position */ pos_in,
					/* length */ match_length,
					/* address */ match_offset,
					/* is_source */ 0))) { return ret; }

	      /* SSMATCH option: search small matches: continue the incremental checksum
	       * through the matched material.  Only if not lazy matching.  */
	      if (SSMATCH && !TRYLAZYLEN (match_length, pos_in, max_in))
		{
		  usize_t avail = max_in - SLOOK - pos_in;
		  usize_t ml_m1 = match_length - 1;
		  usize_t right;
		  int    aincr;

		  IF_DEBUG (usize_t nposi = pos_in + match_length);

		  /* Avail is the last offset we can compute an incremental cksum.  If the
		   * match length exceeds that offset then we are finished performing
		   * incremental updates after this step.  */
		  if (ml_m1 < avail)
		    {
		      right = ml_m1;
		      aincr = 1;
		    }
		  else
		    {
		      right = avail;
		      aincr = 0;
		    }

		  /* Compute incremental checksums within the match. */
		  while (right > 0)
		    {
		      SMALL_CKSUM_UPDATE (scksum, inp, SLOOK);
		      if (DO_LARGE && (pos_in + LLOOK < max_in)) {
			LARGE_CKSUM_UPDATE (lcksum, inp, LLOOK);
		      }

		      inp    += 1;
		      pos_in += 1;
		      right  -= 1;
		      sinx = xd3_checksum_hash (& stream->small_hash, scksum);

		      IF_DEBUG (xd3_verify_small_state (stream, inp, scksum));

		      xd3_scksum_insert (stream, sinx, scksum, pos_in);
		    }

		  if (aincr)
		    {
		      /* Keep searching... */
		      if (DO_RUN) { run_l = xd3_comprun (inp+1, SLOOK-1, & run_c); }
		      XD3_ASSERT (nposi == pos_in + 1);
		      XD3_ASSERT (pos_in + SLOOK < max_in);
		      min_match = MIN_MATCH;
		      goto updatesure;
		    }
		  else
		    {
		      /* Not enough input for another match. */
		      XD3_ASSERT (pos_in + SLOOK >= max_in);
		      goto loopnomore;
		    }
		}

	      /* Else case: copy instruction, but no SSMATCH. */
	      HANDLELAZY (match_length);
	    }
	}

      /* The logic above prevents excess work during lazy matching by increasing min_match
       * to avoid smaller matches.  Each time we advance pos_in by one, the minimum match
       * shortens as well.  */
      if (min_match > MIN_MATCH)
	{
	  min_match -= 1;
	}

    updateone:

      /* See if there are no more incremental cksums to compute. */
      if (pos_in + SLOOK == max_in)
	{
	  goto loopnomore;
	}

    updatesure:

      /* Compute next RUN, CKSUM */
      if (DO_RUN)   { NEXTRUN (inp[SLOOK]); }
      if (DO_SMALL) { SMALL_CKSUM_UPDATE (scksum, inp, SLOOK); }
      if (DO_LARGE && (pos_in + LLOOK < max_in)) { LARGE_CKSUM_UPDATE (lcksum, inp, LLOOK); }
    }

 loopnomore:
  return 0;
}
#endif /* XD3_ENCODER */
#endif /* __XDELTA3_C_TEMPLATE_PASS__ */
