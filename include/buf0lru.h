/****************************************************************************
Copyright (c) 1995, 2009, Innobase Oy. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place, Suite 330, Boston, MA 02111-1307 USA

*****************************************************************************/

/*** @file include/buf0lru.h
The database buffer pool LRU replacement algorithm

Created 11/5/1995 Heikki Tuuri
*******************************************************/

#pragma once

#include "innodb0types.h"

#include "buf0types.h"
#include "ut0byte.h"

/** The return type of buf_LRU_free_block() */
enum buf_lru_free_block_status {
  /** freed */
  BUF_LRU_FREED = 0,
  /** not freed because the caller asked to remove the
  uncompressed frame but the control block cannot be
  relocated */
  BUF_LRU_CANNOT_RELOCATE,
  /** not freed because of some other reason */
  BUF_LRU_NOT_FREED
};

/*** Tries to remove LRU flushed blocks from the end of the LRU list and put
them to the free list. This is beneficial for the efficiency of the insert
buffer operation, as flushed pages from non-unique non-clustered indexes are
here taken out of the buffer pool, and their inserts redirected to the insert
buffer. Otherwise, the flushed blocks could get modified again before read
operations need new buffer blocks, and the i/o work done in flushing would be
wasted. */

void buf_LRU_try_free_flushed_blocks(void);
/*** Returns true if less than 25 % of the buffer pool is available. This can be
used in heuristics to prevent huge transactions eating up the whole buffer
pool for their locks.
@return	true if less than 25 % of buffer pool left */

bool buf_LRU_buf_pool_running_out(void);

/*#######################################################################
These are low-level functions
#########################################################################*/

/** Minimum LRU list length for which the LRU_old pointer is defined */

/* 8 megabytes of 16k pages */
constexpr ulint BUF_LRU_OLD_MIN_LEN = 512;

/** Maximum LRU list search length in buf_flush_LRU_recommendation() */
#define BUF_LRU_FREE_SEARCH_LEN (5 + 2 * BUF_READ_AHEAD_AREA)

/*** Invalidates all pages belonging to a given tablespace when we are deleting
the data file(s) of that tablespace. A PROBLEM: if readahead is being started,
what guarantees that it will not try to read in pages after this operation has
completed? */

void buf_LRU_invalidate_tablespace(ulint id); /*!< in: space id */

/*** Try to free a block.  If bpage is a descriptor of a compressed-only
page, the descriptor object will be freed as well.

NOTE: If this function returns BUF_LRU_FREED, it will not temporarily
release buf_pool_mutex.  Furthermore, the page frame will no longer be
accessible via bpage.

The caller must hold buf_pool_mutex and buf_page_get_mutex(bpage) and
release these two mutexes after the call.  No other
buf_page_get_mutex() may be held when calling this function.
@return BUF_LRU_FREED if freed, BUF_LRU_CANNOT_RELOCATE or
BUF_LRU_NOT_FREED otherwise. */

buf_lru_free_block_status buf_LRU_free_block(
  buf_page_t *bpage, /*!< in: block to be freed */
  bool *buf_pool_mutex_released
);
/*!< in: pointer to a variable that will
be assigned true if buf_pool_mutex
was temporarily released, or NULL */
/*** Try to free a replaceable block.
@return	true if found and freed */

bool buf_LRU_search_and_free_block(ulint n_iterations); /*!< in: how many times this has been called
                         repeatedly without result: a high value means
                         that we should search farther; if
                         n_iterations < 10, then we search
                         n_iterations / 10 * buf_pool->curr_size
                         pages from the end of the LRU list */

/*** Returns a free block from the buf_pool.  The block is taken off the
free list.  If it is empty, returns NULL.
@return	a free control block, or NULL if the buf_block->free list is empty */

buf_block_t *buf_LRU_get_free_only(void);

/*** Returns a free block from the buf_pool. The block is taken off the
free list. If it is empty, blocks are moved from the end of the
LRU list to the free list.
@return	the free control block, in state BUF_BLOCK_READY_FOR_USE */
buf_block_t *buf_LRU_get_free_block();

/*** Puts a block back to the free list. */

void buf_LRU_block_free_non_file_page(buf_block_t *block); /*!< in: block, must not contain a file page */
/*** Adds a block to the LRU list. */

void buf_LRU_add_block(
  buf_page_t *bpage, /*!< in: control block */
  bool old
); /*!< in: true if should be put to the old
                                   blocks in the LRU list, else put to the
                                   start; if the LRU list is very short, added
                                   to the start regardless of this parameter */
/*** Moves a block to the start of the LRU list. */

void buf_LRU_make_block_young(buf_page_t *bpage); /*!< in: control block */
/*** Moves a block to the end of the LRU list. */

void buf_LRU_make_block_old(buf_page_t *bpage); /*!< in: control block */
/*** Updates buf_LRU_old_ratio.
@return	updated old_pct */

ulint buf_LRU_old_ratio_update(
  ulint old_pct, /*!< in: Reserve this percentage of
                   the buffer pool for "old" blocks. */
  bool adjust
); /*!< in: true=adjust the LRU list;
                    false=just assign buf_LRU_old_ratio
                    during the initialization of InnoDB */
/*** Update the historical stats that we are collecting for LRU eviction
policy at the end of each interval. */

void buf_LRU_stat_update(void);
/*** Reset buffer LRU variables. */

void buf_LRU_var_init(void);
#if defined UNIV_DEBUG || defined UNIV_BUF_DEBUG
/*** Validates the LRU list.
@return	true */

bool buf_LRU_validate(void);
#endif /* UNIV_DEBUG || UNIV_BUF_DEBUG */
#if defined UNIV_DEBUG_PRINT || defined UNIV_DEBUG || defined UNIV_BUF_DEBUG
/*** Prints the LRU list. */

void buf_LRU_print(void);
#endif /* UNIV_DEBUG_PRINT || UNIV_DEBUG || UNIV_BUF_DEBUG */

/** @name Heuristics for detecting index scan @{ */
/** Reserve this much/BUF_LRU_OLD_RATIO_DIV of the buffer pool for
"old" blocks.  Protected by buf_pool_mutex. */
extern ulint buf_LRU_old_ratio;

/** The denominator of buf_LRU_old_ratio. */
constexpr ulint BUF_LRU_OLD_RATIO_DIV = 1024;

/** Maximum value of buf_LRU_old_ratio.
@see buf_LRU_old_adjust_len
@see buf_LRU_old_ratio_update */
constexpr ulint BUF_LRU_OLD_RATIO_MAX = BUF_LRU_OLD_RATIO_DIV;

/** Minimum value of buf_LRU_old_ratio.
@see buf_LRU_old_adjust_len
@see buf_LRU_old_ratio_update
The minimum must exceed
(BUF_LRU_OLD_TOLERANCE + 5) * BUF_LRU_OLD_RATIO_DIV / BUF_LRU_OLD_MIN_LEN. */
constexpr ulint BUF_LRU_OLD_RATIO_MIN = 51;

static_assert(BUF_LRU_OLD_RATIO_MIN < BUF_LRU_OLD_RATIO_MAX, "BUF_LRU_OLD_RATIO_MIN >= BUF_LRU_OLD_RATIO_MAX");

static_assert(BUF_LRU_OLD_RATIO_MAX <= BUF_LRU_OLD_RATIO_DIV, "error BUF_LRU_OLD_RATIO_MAX > BUF_LRU_OLD_RATIO_DIV");

/** Move blocks to "new" LRU list only if the first access was at
least this many milliseconds ago.  Not protected by any mutex or latch. */
extern ulint buf_LRU_old_threshold_ms;

/* @} */

/** @brief Statistics for selecting the LRU list for eviction.

These statistics are not 'of' LRU but 'for' LRU.  We keep count of I/O
operations.  Based on the statistics we decide if we want to evict
from buf_pool->LRU. */
struct buf_LRU_stat_struct {
  /** Counter of buffer pool I/O operations. */
  ulint io;
};

/** Statistics for selecting the LRU list for eviction. */
typedef struct buf_LRU_stat_struct buf_LRU_stat_t;

/** Current operation counters.  Not protected by any mutex.
Cleared by buf_LRU_stat_update(). */
extern buf_LRU_stat_t buf_LRU_stat_cur;

/** Running sum of past values of buf_LRU_stat_cur.
Updated by buf_LRU_stat_update().  Protected by buf_pool_mutex. */
extern buf_LRU_stat_t buf_LRU_stat_sum;

// FIXME: Move it to where it's used
/*** Increments the I/O counter in buf_LRU_stat_cur. */
#define buf_LRU_stat_inc_io() (++buf_LRU_stat_cur.io)
