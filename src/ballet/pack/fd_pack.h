#ifndef HEADER_fd_src_ballet_pack_fd_pack_h
#define HEADER_fd_src_ballet_pack_fd_pack_h

/* fd_pack defines methods that prioritizes Solana transactions,
   selecting a subset (potentially all) and ordering them to attempt to
   maximize the overall profitability of the validator. */

#include "../fd_ballet_base.h"
#include "../txn/fd_txn.h"
#include "fd_est_tbl.h"
#include "fd_microblock.h"

#define FD_PACK_ALIGN     (128UL)

#define FD_PACK_MAX_BANK_TILES 62UL

/* NOTE: THE FOLLOWING CONSTANTS ARE CONSENSUS CRITICAL AND CANNOT BE
   CHANGED WITHOUT COORDINATING WITH ANZA. */
#define FD_PACK_MAX_COST_PER_BLOCK      (48000000UL)
#define FD_PACK_MAX_VOTE_COST_PER_BLOCK (36000000UL)
#define FD_PACK_MAX_WRITE_COST_PER_ACCT (12000000UL)
#define FD_PACK_FEE_PER_SIGNATURE           (5000UL) /* In lamports */

/* Each block is limited to 32k parity shreds.  We don't want pack to
   produce a block with so many transactions we can't shred it, but the
   correspondence between transactions and parity shreds is somewhat
   complicated, so we need to use conservative limits.

   Except for the final batch in the block, the current version of the
   shred tile shreds microblock batches of size (25431, 63671] bytes,
   including the microblock headers, but excluding the microblock count.
   The worst case size by bytes/parity shred is a 25871 byte microblock
   batch, which produces 31 parity shreds.  The final microblock batch,
   however, may be as bad as 48 bytes triggering the creation of 17
   parity shreds.  This gives us a limit of floor((32k - 17)/31)*25871 +
   48 = 27,319,824 bytes.

   To get this right, the pack tile needs to add in the 48-byte
   microblock headers for each microblock, and we also need to subtract
   out the tick bytes, which aren't known until PoH initialization is
   complete.

   Note that the number of parity shreds in each FEC set is always at
   least as many as the number of data shreds, so we don't need to
   consider the data shreds limit. */
#define FD_PACK_MAX_DATA_PER_BLOCK (((32UL*1024UL-17UL)/31UL)*25871UL + 48UL)

/* Optionally allow up to 128k shreds per block for benchmarking. */
#define LARGER_MAX_DATA_PER_BLOCK  (((4UL*32UL*1024UL-17UL)/31UL)*25871UL + 48UL)

/* ---- End consensus-critical constants */

#define FD_TXN_P_FLAGS_IS_SIMPLE_VOTE   (1U)
#define FD_TXN_P_FLAGS_SANITIZE_SUCCESS (2U)
#define FD_TXN_P_FLAGS_EXECUTE_SUCCESS  (4U)


/* The Solana network and Firedancer implementation details impose
   several limits on what pack can produce.  These limits are grouped in
   this one struct fd_pack_limits_t, which is just a convenient way to
   pass them around.  The limits listed below are arithmetic limits.
   The limits imposed by practical constraints are almost certainly
   much, much tighter. */
struct fd_pack_limits {
  /* max_{cost, vote_cost}_per_block, max_write_cost_per_acct are
     consensus-critical limits and must be agreed on cluster-wide.  A
     block that consumes more than max_cost_per_block cost units
     (closely related to, but not identical to CUs) in total is invalid.
     Similarly, a block where the sum of the cost of all vote
     transactions exceeds max_vote_cost_per_block cost units is invalid.
     Similarly, a block in where the sum of the cost of all transactions
     that write to a given account exceeds max_write_cost_per_acct is
     invalid. */
  ulong max_cost_per_block;          /* in [0, ULONG_MAX) */
  ulong max_vote_cost_per_block;     /* in [0, max_cost_per_block] */
  ulong max_write_cost_per_acct;     /* in [0, max_cost_per_block] */

  /* max_data_bytes_per_block is derived from consensus-critical limits
     on the number of shreds in a block, but is not directly enforced.
     Separation of concerns means that it's not a good idea for pack to
     know exactly how the block will be shredded, but at the same time,
     we don't want to end up in a situation where we produced a block
     that had too many shreds, because the shred tile's only recourse
     would be to kill the block.  To address this, pack limits the size
     of the data it puts into the block to a limit that we can prove
     will never cause the shred tile to produce too many shreds.

     This limit includes transaction and microblock headers for
     non-empty microblocks that pack produces. */
  ulong max_data_bytes_per_block;    /* in [0, ULONG_MAX - 183] */

  /* max_txn_per_microblock and max_microblocks_per_block are
     Firedancer-imposed implementation limits to bound the amount of
     memory consumption that pack uses.  Pack will produce microblocks
     with no more than max_txn_per_microblock transactions.
     Additionally, once pack produces max_microblocks_per_block
     non-empty microblocks in a block, all subsequent attempts to
     schedule a microblock will return an empty microblock until
     fd_pack_end_block is called. */
  ulong max_txn_per_microblock;      /* in [0, 16777216] */
  ulong max_microblocks_per_block;   /* in [0, 1e12) */

};
typedef struct fd_pack_limits fd_pack_limits_t;


/* Forward declare opaque handle */
struct fd_pack_private;
typedef struct fd_pack_private fd_pack_t;

/* fd_pack_{align,footprint} return the required alignment and
   footprint in bytes for a region of memory to be used as a pack
   object.

   pack_depth sets the maximum number of pending transactions that pack
   stores and may eventually schedule.  pack_depth must be at least 4.

   bank_tile_cnt sets the number of bank tiles to which this pack object
   can schedule transactions.  bank_tile_cnt must be in [1,
   FD_PACK_MAX_BANK_TILES].

   limits sets various limits for the blocks and microblocks that pack
   can produce. */

FD_FN_CONST static inline ulong fd_pack_align       ( void ) { return FD_PACK_ALIGN; }

FD_FN_PURE ulong
fd_pack_footprint( ulong                    pack_depth,
                   ulong                    bank_tile_cnt,
                   fd_pack_limits_t const * limits );


/* fd_pack_new formats a region of memory to be suitable for use as a
   pack object.  mem is a non-NULL pointer to a region of memory in the
   local address space with the required alignment and footprint.
   pack_depth, bank_tile_cnt, and limits are as above.  rng is a local
   join to a random number generator used to perturb estimates.

   Returns `mem` (which will be properly formatted as a pack object) on
   success and NULL on failure.  Logs details on failure.  The caller
   will not be joined to the pack object when this function returns. */
void * fd_pack_new( void * mem,
    ulong pack_depth, ulong bank_tile_cnt, fd_pack_limits_t const * limits,
    fd_rng_t * rng );

/* fd_pack_join joins the caller to the pack object.  Every successful
   join should have a matching leave.  Returns mem. */
fd_pack_t * fd_pack_join( void * mem );


/* fd_pack_avail_txn_cnt returns the number of transactions that this
   pack object has available to schedule but that have not been
   scheduled yet. pack must be a valid local join.  The return value
   will be in [0, pack_depth). */

FD_FN_PURE ulong fd_pack_avail_txn_cnt( fd_pack_t const * pack );

/* fd_pack_bank_tile_cnt: returns the value of bank_tile_cnt provided in
   pack when the pack object was initialized with fd_pack_new.  pack
   must be a valid local join.  The result will be in [1,
   FD_PACK_MAX_BANK_TILES]. */
FD_FN_PURE ulong fd_pack_bank_tile_cnt( fd_pack_t const * pack );

/* fd_pack_set_block_limits: Updates the limits provided fd_pack_new to
   the new values.  Any future microblocks produced by this pack object
   will not cause a block to have more than max_microblocks_per_block
   non-empty microblocks or more than max_data_bytes_per_block data
   bytes (counting microblock headers as before).  Limits are inclusive,
   as per usual (i.e. a block may have exactly
   max_microblocks_per_block microblocks, but not more).  pack must be
   a valid local join.

   The typical place to call this is immediately after
   fd_pack_end_block; if this is called after some microblocks have been
   produced for the current block, and the current block already exceeds
   the limits, all the remaining microblocks in the block will be empty,
   but the call is valid. */
void fd_pack_set_block_limits( fd_pack_t * pack, ulong max_microblocks_per_block, ulong max_data_bytes_per_block );

/* Return values for fd_pack_insert_txn_fini:  Non-negative values
   indicate the transaction was accepted and may be returned in a future
   microblock.  Negative values indicate that the transaction was
   rejected and will never be returned in a future microblock.
   Transactions can be rejected through no fault of their own, so it
   doesn't necessarily imply bad behavior.

   The non-negative (success) codes are essentially a bitflag of two
   bits:
    * whether the transaction met the criteria for a simple vote or not,
    * whether this transaction replaced a previously accepted, low
      priority transaction, rather than being accepted in addition to all
      the previously accepted transactions.  Since pack maintains a heap
      with a fixed max size of pack_depth, replacing transaction is
      necessary whenever the heap is full.

   The negative (failure) codes are a normal enumeration (not a
   bitflag).
    * PRIORITY: pack's heap was full and the transaction's priority was
      lower than the worst currently accepted transaction.
    * DUPLICATE: the transaction is a duplicate of a currently accepted
      transaction.
    * UNAFFORDABLE: the fee payer could not afford the transaction fee
      (not yet implemented).
    * ADDR_LUT: the transaction tried to load an account from an address
      lookup table, which is not yet supported.
    * EXPIRED: the transaction was already expired upon insertion based
      on the provided value of expires_at compared to the last call to
      fd_pack_expire_before.
    * TOO_LARGE: the transaction requested too many CUs and would never
      be scheduled if it had been accepted.
    * ACCOUNT_CNT: the transaction tried to load more than 64 account
      addresses.
    * DUPLICATE_ACCT: the transaction included an account address twice
      in its list of account addresses to load.
    * ESTIMATION_FAIL: estimation of the transaction's compute cost and
      fee failed, typically because the transaction contained a
      malformed ComputeBudgetProgram instruction.
    * WRITES_SYSVAR: the transaction attempts to write-lock a sysvar.
      Write-locking a sysvar can cause heavy contention.  Solana Labs
      solves this by downgrading these to read locks, but we instead
      solve it by refusing to pack such transactions.

    NOTE: The corresponding enum in metrics.xml must be kept in sync
    with any changes to these return values. */
#define FD_PACK_INSERT_ACCEPT_VOTE_REPLACE    ( 3)
#define FD_PACK_INSERT_ACCEPT_NONVOTE_REPLACE ( 2)
#define FD_PACK_INSERT_ACCEPT_VOTE_ADD        ( 1)
#define FD_PACK_INSERT_ACCEPT_NONVOTE_ADD     ( 0)
#define FD_PACK_INSERT_REJECT_PRIORITY        (-1)
#define FD_PACK_INSERT_REJECT_DUPLICATE       (-2)
#define FD_PACK_INSERT_REJECT_UNAFFORDABLE    (-3)
#define FD_PACK_INSERT_REJECT_ADDR_LUT        (-4)
#define FD_PACK_INSERT_REJECT_EXPIRED         (-5)
#define FD_PACK_INSERT_REJECT_TOO_LARGE       (-6)
#define FD_PACK_INSERT_REJECT_ACCOUNT_CNT     (-7)
#define FD_PACK_INSERT_REJECT_DUPLICATE_ACCT  (-8)
#define FD_PACK_INSERT_REJECT_ESTIMATION_FAIL (-9)
#define FD_PACK_INSERT_REJECT_WRITES_SYSVAR   (-10)

/* The FD_PACK_INSERT_{ACCEPT, REJECT}_* values defined above are in the
   range [-FD_PACK_INSERT_RETVAL_OFF,
   -FD_PACK_INSERT_RETVAL_OFF+FD_PACK_INSERT_RETVAL_CNT ) */
#define FD_PACK_INSERT_RETVAL_OFF 10
#define FD_PACK_INSERT_RETVAL_CNT 14

FD_STATIC_ASSERT( FD_PACK_INSERT_REJECT_WRITES_SYSVAR>=-FD_PACK_INSERT_RETVAL_OFF, pack_retval );
FD_STATIC_ASSERT( FD_PACK_INSERT_ACCEPT_VOTE_REPLACE<FD_PACK_INSERT_RETVAL_CNT-FD_PACK_INSERT_RETVAL_OFF, pack_retval );

/* fd_pack_insert_txn_{init,fini,cancel} execute the process of
   inserting a new transaction into the pool of available transactions
   that may be scheduled by the pack object.

   fd_pack_insert_txn_init returns a piece of memory from the txnmem
   region where the transaction should be stored.  The lifetime of this
   memory is managed by fd_pack as explained below.

   Every call to fd_pack_insert_init must be paired with a call to
   exactly one of _fini or _cancel.  Calling fd_pack_insert_txn_fini
   finalizes the transaction insert process and makes the newly-inserted
   transaction available for scheduling.  Calling
   fd_pack_insert_txn_cancel aborts the transaction insertion process.
   The txn pointer passed to _fini or _cancel must come from the most
   recent call to _init.

   The caller of these methods should not retain any read or write
   interest in the transaction after _fini or _cancel have been called.

   expires_at (for _fini only) bounds the lifetime of the inserted
   transaction.  No particular unit is prescribed, and it need not be
   higher than the previous call to txn_fini.  If fd_pack_expire_before
   has been previously called with a value larger (strictly) than the
   provided expires_at, the transaction will be rejected with EXPIRED.
   See fd_pack_expire_before for more details.

   pack must be a local join of a pack object.  From the caller's
   perspective, these functions cannot fail, though pack may reject a
   transaction for a variety of reasons.  fd_pack_insert_txn_fini
   returns one of the FD_PACK_INSERT_ACCEPT_* or FD_PACK_INSERT_REJECT_*
   codes explained above.
 */
fd_txn_p_t * fd_pack_insert_txn_init  ( fd_pack_t * pack                                     );
int          fd_pack_insert_txn_fini  ( fd_pack_t * pack, fd_txn_p_t * txn, ulong expires_at );
void         fd_pack_insert_txn_cancel( fd_pack_t * pack, fd_txn_p_t * txn                   );


/* fd_pack_schedule_next_microblock schedules transactions to form a
   microblock, which is a set of non-conflicting transactions.

   pack must be a local join of a pack object.  Transactions part of the
   scheduled microblock are copied to out in no particular order.  The
   cumulative cost of these transactions will not exceed total_cus, and
   the number of transactions will not exceed the value of
   max_txn_per_microblock given in fd_pack_new.

   The block will not contain more than
   vote_fraction*max_txn_per_microblock votes, and votes in total will
   not consume more than vote_fraction*total_cus of the microblock.

   Returns the number of transactions in the scheduled microblock.  The
   return value may be 0 if there are no eligible transactions at the
   moment. */

ulong fd_pack_schedule_next_microblock( fd_pack_t * pack, ulong total_cus, float vote_fraction, ulong bank_tile, fd_txn_p_t * out );


/* fd_pack_microblock_complete signals that the bank_tile with index
   bank_tile has completed its previously scheduled microblock.  This
   permits the scheduling of transactions that conflict with the
   previously scheduled microblock. */
void fd_pack_microblock_complete( fd_pack_t * pack, ulong bank_tile );

/* fd_pack_expire_before deletes all available transactions with
   expires_at values strictly less than expire_before.  pack must be a
   local join of a pack object.  Returns the number of transactions
   deleted.  Subsequent calls to fd_pack_expire_before with the same or
   a smaller value are no-ops. */
ulong fd_pack_expire_before( fd_pack_t * pack, ulong expire_before );

/* fd_pack_delete_txn removes a transaction (identified by its first
   signature) from the pool of available transactions.  Returns 1 if the
   transaction was found (and then removed) and 0 if not. */
int fd_pack_delete_transaction( fd_pack_t * pack, fd_ed25519_sig_t const * sig0 );

/* fd_pack_end_block resets some state to prepare for the next block.
   Specifically, the per-block limits are cleared and transactions in
   the microblocks scheduled after the call to this function are allowed
   to conflict with transactions in microblocks scheduled before the
   call to this function, even within gap microblocks. */
void fd_pack_end_block( fd_pack_t * pack );


/* fd_pack_clear_all resets the state associated with this pack object.
   All pending transactions are removed from the pool of available
   transactions and all limits are reset. */
void fd_pack_clear_all( fd_pack_t * pack );


/* fd_pack_leave leaves a local join of a pack object.  Returns pack. */
void * fd_pack_leave(  fd_pack_t * pack );
/* fd_pack_delete unformats a memory region used to store a pack object
   and returns ownership of the memory to the caller.  Returns mem. */
void * fd_pack_delete( void      * mem  );

/* fd_pack_verify (for debugging use primarily) checks to ensure several
   invariants are satisfied.  scratch must point to the first byte of a
   piece of memory meeting the same alignment and footprint constraints
   as pack.  Returns 0 on success and a negative value on failure
   (logging a warning with details). */
int fd_pack_verify( fd_pack_t * pack, void * scratch );

FD_PROTOTYPES_END
#endif /* HEADER_fd_src_ballet_pack_fd_pack_h */
