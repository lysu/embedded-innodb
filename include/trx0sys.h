/****************************************************************************
Copyright (c) 1996, 2010, Innobase Oy. All Rights Reserved.
Copyright (c) 2024 Sunny Bains. All rights reserved.

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

/** @file include/trx0sys.h
Transaction system

Created 3/26/1996 Heikki Tuuri
*******************************************************/

#pragma once

#include "innodb0types.h"

#include "buf0buf.h"
#include "fil0fil.h"
#include "fsp0fsp.h"
#include "mem0mem.h"
#include "mtr0mtr.h"
#include "page0types.h"
#include "read0types.h"
#include "sync0sync.h"
#include "trx0types.h"
#include "ut0byte.h"
#include "ut0lst.h"
#include "data0type.h"
#include "mtr0log.h"
#include "srv0srv.h"
#include "trx0trx.h"

/* The typedef for rseg slot in the file copy */
using trx_sysf_rseg_t = byte;

/** The automatically created system rollback segment has this id */
constexpr ulint TRX_SYS_SYSTEM_RSEG_ID = 0;

/** The transaction system tablespace.
Space id and page no where the trx system file copy resides */
constexpr auto TRX_SYS_SPACE = SYS_TABLESPACE;

/** Page numnber of the transaction system meta data. */
constexpr auto TRX_SYS_PAGE_NO = FSP_TRX_SYS_PAGE_NO;

/** The offset of the transaction system header on the page */
constexpr auto TRX_SYS = FSEG_PAGE_DATA;

/** Transaction system header */
/*@{ */

/** The maximum trx id or trx number modulo TRX_SYS_TRX_ID_UPDATE_MARGIN
written to a file page by any transaction; the assignment of transaction
ids continues from this number rounded up by TRX_SYS_TRX_ID_UPDATE_MARGIN
plus TRX_SYS_TRX_ID_UPDATE_MARGIN when the database is started */
constexpr ulint TRX_SYS_TRX_ID_STORE = 0;

/** Segment header for the tablespace segment the trx system is created into */
constexpr ulint TRX_SYS_FSEG_HEADER = 8;

/** The start of the array of rollback segment specification slots */
constexpr ulint TRX_SYS_RSEGS = 8 + FSEG_HEADER_SIZE;

/*@} */

/** Maximum number of rollback segments: the number of segment
specification slots in the transaction system array; rollback segment
id must fit in one byte, therefore 256; each slot is currently 8 bytes
in size */
constexpr ulint TRX_SYS_N_RSEGS = 256;

static_assert(UNIV_PAGE_SIZE >= 4096, "error UNIV_PAGE_SIZE < 4096");

/* Rollback segment specification slot offsets */
/*-------------------------------------------------------------*/

/** Tablespace where the segment header is placed. */
constexpr auto TRX_SYS_RSEG_SPACE = SYS_TABLESPACE;

/** Page number where the segment header is placed; this is FIL_NULL if the slot is unused */
constexpr page_no_t TRX_SYS_RSEG_PAGE_NO = 4;

/*-------------------------------------------------------------*/

/* Size of a rollback segment specification slot */
constexpr ulint TRX_SYS_RSEG_SLOT_SIZE = 8;

/** When a trx id which is zero modulo this number (which must be a power of
two) is assigned, the field TRX_SYS_TRX_ID_STORE on the transaction system
page is updated */
constexpr ulint TRX_SYS_TRX_ID_WRITE_MARGIN = 256;

/** The transaction system central memory data structure; protected by the
kernel mutex */
struct Trx_sys {

  /**
   * Constructor
   * 
   * @param[in] fsp               File space management instance
   */
  explicit Trx_sys(FSP *fsp) noexcept;

  /**
   * Destructor
   */
  ~Trx_sys() noexcept;

  /**
   * Create an instance of the transaction system.
   * 
   * @param[in] fsp             File management instance to use
   * 
   * @return new Trx_sys instance.
   */
  static Trx_sys *create(FSP *fsp) noexcept;

  /** 
   * Destroy a transaction system instance.
   * 
   * @param[in] recovery        Recovery flag
   */
  static void destroy(Trx_sys *&srv_trx_sys) noexcept;


  /** Start the transaction system.
   * 
   * @return DB_SUCCESS or error code.
  */
  dberr_t start(ib_recovery_t recovery) noexcept;

  /**
   * Create a new system tablespace.
   * 
   * @return DB_SUCCESS or error code.
   */
  db_err create_system_tablespace() noexcept;

  /**
   * Open an existing database instance.
   * 
   * @return DB_SUCCESS or error code.
   */
  db_err open_system_tablespace() noexcept;

  /**
   * Looks for a free slot for a rollback segment in the trx system file copy.
   * 
   * @param[in] mtr               Mini-transaction handle
   * 
   * @return	slot index or ULINT_UNDEFINED if not found
   */
  ulint frseg_find_free(mtr_t *mtr) noexcept;

  /**
   * Checks that trx is in the trx list.
   * 
   * @return	true if is in
   */
  bool in_trx_list(trx_t *in_trx) noexcept;

  /**
   * Writes the value of max_trx_id to the file based trx system header.
   */
  void flush_max_trx_id() noexcept;

  /**
   * Looks for the trx handle with the given id in trx_list.
   * 
   * @param[in] trx_id	Trx id to search for
   * 
   * @return	the trx handle or NULL if not found
   */
  trx_t *get_on_id(trx_id_t trx_id) noexcept {
    ut_ad(mutex_own(&kernel_mutex));

    for (const auto trx : m_trx_list) {
      if (trx_id == trx->m_id) {

        return trx;
      }
    }

    return nullptr;
  }

  /**
   * Returns the minumum trx id in trx list. This is the smallest id for which
   * the trx can possibly be active. (But, you must look at the trx->conc_state to
   * find out if the minimum trx id transaction itself is active, or already
   * committed.)
   * 
   * @return	the minimum trx id, or Trx_sys::max_trx_id if the trx list is empty
   */
  trx_id_t get_min_trx_id() const noexcept {
    ut_ad(mutex_own(&kernel_mutex));

    auto trx = UT_LIST_GET_LAST(m_trx_list);

    return trx == nullptr ? m_max_trx_id : trx->m_id;
  }

  /**
   * Checks if a transaction with the given id is active.
   * 
   * @param[in] trx_id	Trx id of the transaction
   * 
   * @return	true if active
   */
  bool is_active(trx_id_t trx_id) const noexcept {
    ut_ad(mutex_own(&kernel_mutex));

    if (trx_id < get_min_trx_id()) {

      return false;
    }

    if (trx_id >= m_max_trx_id) {

      /* There must be corruption: we return true because this
      function is only called by lock_clust_rec_some_has_impl()
      and row_vers_impl_x_locked_off_kernel() and they have
      diagnostic prints in this case */

      return true;
    } else {
      auto trx = srv_trx_sys->get_on_id(trx_id);

      return trx != nullptr && (trx->m_conc_state == TRX_ACTIVE || trx->m_conc_state == TRX_PREPARED);
    }
  }

  /**
   * Allocates a new transaction id.
   * 
   * @return	new, allocated trx id
   */
  trx_id_t get_new_trx_id() noexcept {
    ut_ad(mutex_own(&kernel_mutex));

    /* VERY important: after the database is started, max_trx_id value is
    divisible by TRX_SYS_TRX_ID_WRITE_MARGIN, and the following if
    will evaluate to true when this function is first time called,
    and the value for trx id will be written to disk-based header!
    Thus trx id values will not overlap when the database is
    repeatedly started! */

    if (m_max_trx_id % TRX_SYS_TRX_ID_WRITE_MARGIN == 0) {

      flush_max_trx_id();
    }

    auto id = m_max_trx_id;

    ++m_max_trx_id;

    return id;
  }

  /**
   * Allocates a new transaction number.
   * 
   * @return	new, allocated trx number
   */
  trx_id_t get_new_trx_no() noexcept {
    ut_ad(mutex_own(&kernel_mutex));

    return get_new_trx_id();
  }

  /**
   * Gets a pointer to the transaction system header and x-latches its page.
   * 
   * @param[in]	mtr	mtr
   * 
   * @return	pointer to system header, page x-latched.
   */
  trx_sysf_t *read_header(mtr_t *mtr) noexcept {
    ut_ad(mtr != nullptr);

    Buf_pool::Request req {
      .m_rw_latch = RW_X_LATCH,
      .m_page_id = { TRX_SYS_SPACE, TRX_SYS_PAGE_NO },
      .m_mode = BUF_GET,
      .m_file = __FILE__,
      .m_line = __LINE__,
      .m_mtr = mtr
    };

    auto block = m_fsp->m_buf_pool->get(req, nullptr);
    buf_block_dbg_add_level(IF_SYNC_DEBUG(block, SYNC_TRX_SYS_HEADER));

    return TRX_SYS + block->get_frame();
  }

  /** Gets the pointer in the nth slot of the rseg array.
   * 
   * @param[in] sys	Trx system
   * @param[in] n	Index of slot
   * 
   * @return	pointer to rseg object, NULL if slot not in use
   */
  trx_rseg_t *get_nth_rseg(ulint n) noexcept {
    ut_ad(mutex_own(&kernel_mutex));
    ut_ad(n < m_rsegs.size());

    return m_rsegs[n];
  }

  /**
   * Sets the pointer in the nth slot of the rseg array.
   * 
   * @param[in] sys	Trx system
   * @param[in] n	Index of slot
   * @param[in] rseg	Pointer to rseg object, NULL if slot not in use
   */
  void set_nth_rseg(ulint n, trx_rseg_t *rseg) noexcept {
    ut_ad(n < m_rsegs.size());

    m_rsegs[n] = rseg;
  }

  /**
   * Checks if a page address is the trx sys header page.
   * 
   * @param[in] space	Tablespace ID
   * @param[in] page_no	Page number
   * 
   * @return	true if trx sys header page
   */
  static bool is_hdr_page(space_id_t space, page_no_t page_no) noexcept {
    return space == TRX_SYS_SPACE && page_no == TRX_SYS_PAGE_NO;
  }

  /** Gets the space of the nth rollback segment slot in the trx system file copy.
   * 
   * @param[in]	sys_header	Trx sys header
   * @param[in]	i	Slot index == rseg id
   * @param[in]	mtr	mtr
   * 
   * @return	space id
   */
  static ulint frseg_get_space(trx_sysf_t *sys_header, ulint i, mtr_t *mtr) noexcept {
    ut_ad(i < TRX_SYS_N_RSEGS);
    ut_ad(mutex_own(&kernel_mutex));

    return mtr->read_ulint(sys_header + TRX_SYS_RSEGS + i * TRX_SYS_RSEG_SLOT_SIZE + TRX_SYS_RSEG_SPACE, MLOG_4BYTES);
  }

  /**
   * Gets the page number of the nth rollback segment slot in the trx system header.
   * 
   * @param[in]	sys_header	Trx sys header
   * @param[in]	i	Slot index == rseg id
   * @param[in]	mtr	mtr
   * 
   * @return	page number, FIL_NULL if slot unused
   */
  static ulint frseg_get_page_no(trx_sysf_t *sys_header, ulint i, mtr_t *mtr) noexcept {
    ut_ad(i < TRX_SYS_N_RSEGS);
    ut_ad(mutex_own(&kernel_mutex));

    return mtr->read_ulint(sys_header + TRX_SYS_RSEGS + i * TRX_SYS_RSEG_SLOT_SIZE + TRX_SYS_RSEG_PAGE_NO, MLOG_4BYTES);
  }

  /**
   * Sets the space id of the nth rollback segment slot in the trx system
   * file copy.
   * 
   * @param[in]	sys_header	Trx sys file copy
   * @param[in]	i	Slot index == rseg id
   * @param[in]	space	Space id
   * @param[in]	mtr	mtr
   */
  static void frseg_set_space(trx_sysf_t *sys_header, ulint i, space_id_t space, mtr_t *mtr) noexcept {
    ut_ad(i < TRX_SYS_N_RSEGS);
    ut_ad(mutex_own(&kernel_mutex));

    mlog_write_ulint(sys_header + TRX_SYS_RSEGS + i * TRX_SYS_RSEG_SLOT_SIZE + TRX_SYS_RSEG_SPACE, space, MLOG_4BYTES, mtr);
  }

  /**
   * Sets the page number of the nth rollback segment slot in the trx system header.
   * 
   * @param[in]	sys_header	Trx sys header
   * @param[in]	i	Slot index == rseg id
   * @param[in]	page_no	Page number, FIL_NULL if the slot is reset to unused
   * @param[in]	mtr	mtr
   */

  static void frseg_set_page_no(trx_sysf_t *sys_header, ulint i, page_no_t page_no, mtr_t *mtr) noexcept {
    ut_ad(i < TRX_SYS_N_RSEGS);
    ut_ad(mutex_own(&kernel_mutex));

    mlog_write_ulint(sys_header + TRX_SYS_RSEGS + i * TRX_SYS_RSEG_SLOT_SIZE + TRX_SYS_RSEG_PAGE_NO, page_no, MLOG_4BYTES, mtr);
  }

  /**
   * Writes a trx id to an index page. In case that the id size changes in
   * some future version, this function should be used instead of
   * mach_write_...
   * 
   * @param[in]	ptr	Pointer to memory where written
   * @param[in]	id	transaction id to write
  */
  static void write_trx_id(byte *ptr, trx_id_t id) noexcept {
    static_assert(DATA_TRX_ID_LEN == 6, "error DATA_TRX_ID_LEN != 6");

    mach_write_to_6(ptr, id);
  }

  /**
   * Reads a trx id from an index page. In case that the id size changes in
   * some future version, this function should be used instead of
   * mach_read_...
   * 
   * @param[in]	ptr	Pointer to memory from where to read
   * 
   * @return	id
   * */
  static trx_id_t read_trx_id(const byte *ptr) noexcept {
    static_assert(DATA_TRX_ID_LEN == 6, "error DATA_TRX_ID_LEN != 6");

    return mach_read_from_6(ptr);
  }

private:
  /**
   * Creates the file page for the transaction system. This function is called
   * only at the database creation, before init().
   * 
   * @param[in,out] mtr              Mini-transaction covering the operation.
   */
  void create_new_instance(mtr_t *mtr) noexcept;

public:
  /** The smallest number not yet assigned as a transaction
  id or transaction number */
  trx_id_t m_max_trx_id{};

  /** List of read views sorted on trx no, biggest first */
  UT_LIST_BASE_NODE_T_EXTERN(read_view_t, view_list) m_view_list{};

  /** List of active and committed in memory transactions,
  sorted on trx id, biggest first */
  UT_LIST_BASE_NODE_T_EXTERN(trx_t, trx_list) m_trx_list{};

  /** List of transactions created for users */
  UT_LIST_BASE_NODE_T_EXTERN(trx_t, client_trx_list) m_client_trx_list{};

  /** List of rollback segment objects */
  UT_LIST_BASE_NODE_T_EXTERN(trx_rseg_t, rseg_list) m_rseg_list{};

  /** Latest rollback segment in the round-robin assignment
  of rollback segments to transactions */
  trx_rseg_t *m_latest_rseg{};

  /** Pointer array to rollback segments; NULL if slot not in use */
  std::array<trx_rseg_t *, TRX_SYS_N_RSEGS> m_rsegs{};

  /** Length of the TRX_RSEG_HISTORY list (update undo logs for
  committed transactions), protected by rseg->mutex */
  ulint m_rseg_history_len{};

  /** The following is true when we are using the database in the file per table
   * format, we have successfully upgraded, or have created a new database installation */
  bool m_multiple_tablespace_format{};

  /** File space management instance. */
  FSP *m_fsp{};

  /** Purge system. */
  Purge_sys *m_purge{};
};
