#include "fd_tower.h"
#include "../../flamenco/runtime/program/fd_vote_program.h"

#define THRESHOLD_DEPTH         ( 8 )
#define THRESHOLD_PCT           ( 2.0 / 3.0 )
#define SHALLOW_THRESHOLD_DEPTH ( 4 )
#define SHALLOW_THRESHOLD_PCT   ( 0.38 )
#define SWITCH_PCT              ( 0.38 )

/* Private implementation functions */

static inline ulong
lockout_expiration_slot( fd_tower_vote_t const * vote ) {
  ulong lockout = 1UL << vote->conf;
  return vote->slot + lockout;
}

void
print( fd_tower_vote_t * tower_votes, ulong root ) {
  ulong max_slot = 0;

  /* Determine spacing. */

  for( fd_tower_votes_iter_t iter = fd_tower_votes_iter_init_rev( tower_votes );
       !fd_tower_votes_iter_done_rev( tower_votes, iter );
       iter = fd_tower_votes_iter_prev( tower_votes, iter ) ) {

    max_slot = fd_ulong_max( max_slot, fd_tower_votes_iter_ele_const( tower_votes, iter )->slot );
  }

  /* Calculate the number of digits in the maximum slot value. */

  int           radix = 0;
  unsigned long rem   = max_slot;
  do {
    rem /= 10;
    ++radix;
  } while( rem > 0 );

  /* Print the table header */

  printf( "%*s | %s\n", radix, "slot", "confirmation count" );

  /* Print the divider line */

  for( int i = 0; i < radix; i++ ) {
    printf( "-" );
  }
  printf( " | " );
  for( ulong i = 0; i < strlen( "confirmation_count" ); i++ ) {
    printf( "-" );
  }
  printf( "\n" );

  /* Print each record in the table */

  for( fd_tower_votes_iter_t iter = fd_tower_votes_iter_init_rev( tower_votes );
       !fd_tower_votes_iter_done_rev( tower_votes, iter );
       iter = fd_tower_votes_iter_prev( tower_votes, iter ) ) {

    fd_tower_vote_t const * vote = fd_tower_votes_iter_ele_const( tower_votes, iter );
    printf( "%*lu | %lu\n", radix, vote->slot, vote->conf );
    max_slot = fd_ulong_max( max_slot, fd_tower_votes_iter_ele_const( tower_votes, iter )->slot );
  }
  printf( "%*lu | root\n", radix, root );
  printf( "\n" );
}

static inline ulong
simulate_vote( fd_tower_vote_t const * votes, ulong slot ) {
  ulong cnt = fd_tower_votes_cnt( votes );
  while( cnt ) {

    /* Return early if we can't pop the top tower vote, even if votes
       below it are expired. */

    if( FD_LIKELY( lockout_expiration_slot( fd_tower_votes_peek_index_const( votes, cnt - 1 ) ) >=
                   slot ) ) {
      break;
    }
    cnt--;
  }
  return cnt + 1; /* Add 1 to represent the simulated vote. */
}

static void
tower_votes_from_landed_votes( fd_tower_vote_t *        tower_votes,
                               fd_landed_vote_t const * landed_votes ) {
  for( deq_fd_landed_vote_t_iter_t iter = deq_fd_landed_vote_t_iter_init( landed_votes );
       !deq_fd_landed_vote_t_iter_done( landed_votes, iter );
       iter = deq_fd_landed_vote_t_iter_next( landed_votes, iter ) ) {
    fd_landed_vote_t const * landed_vote = deq_fd_landed_vote_t_iter_ele_const( landed_votes,
                                                                                iter );
    fd_tower_votes_push_tail( tower_votes,
                              ( fd_tower_vote_t ){
                                  .slot = landed_vote->lockout.slot,
                                  .conf = landed_vote->lockout.confirmation_count } );
  }
}

/* Public API */

/* clang-format off */
void *
fd_tower_new( void * shmem ) {
  if( FD_UNLIKELY( !shmem ) ) {
    FD_LOG_WARNING( ( "NULL mem" ) );
    return NULL;
  }

  if( FD_UNLIKELY( !fd_ulong_is_aligned( (ulong)shmem, fd_tower_align() ) ) ) {
    FD_LOG_WARNING( ( "misaligned mem" ) );
    return NULL;
  }

  ulong footprint = fd_tower_footprint();
  if( FD_UNLIKELY( !footprint ) ) {
    FD_LOG_WARNING( ( "bad mem" ) );
    return NULL;
  }

  fd_memset( shmem, 0, footprint );
  ulong        laddr = (ulong)shmem;
  fd_tower_t * tower = (void *)laddr;
  laddr             += sizeof( fd_tower_t );

  laddr        = fd_ulong_align_up( laddr, fd_tower_votes_align() );
  tower->votes = fd_tower_votes_new( (void *)laddr );
  laddr       += fd_tower_votes_footprint();

  laddr            = fd_ulong_align_up( laddr, fd_tower_vote_accs_align() );
  tower->vote_accs = fd_tower_vote_accs_new( (void *)laddr );
  laddr           += fd_tower_vote_accs_footprint();

  return shmem;
}
/* clang-format on */

/* clang-format off */
fd_tower_t *
fd_tower_join( void * shtower ) {

  if( FD_UNLIKELY( !shtower ) ) {
    FD_LOG_WARNING( ( "NULL tower" ) );
    return NULL;
  }

  if( FD_UNLIKELY( !fd_ulong_is_aligned( (ulong)shtower, fd_tower_align() ) ) ) {
    FD_LOG_WARNING( ( "misaligned tower" ) );
    return NULL;
  }

  ulong        laddr = (ulong)shtower; /* offset from a memory region */
  fd_tower_t * tower = (void *)shtower;
  laddr             += sizeof(fd_tower_t);

  laddr        = fd_ulong_align_up( laddr, fd_tower_votes_align() );
  tower->votes = fd_tower_votes_new( (void *)laddr );
  laddr       += fd_tower_votes_footprint();

  laddr            = fd_ulong_align_up( laddr, fd_tower_vote_accs_align() );
  tower->vote_accs = fd_tower_vote_accs_new( (void *)laddr );
  laddr           += fd_tower_vote_accs_footprint();

  return (fd_tower_t *)shtower;
}
/* clang-format on */

void *
fd_tower_leave( fd_tower_t const * tower ) {

  if( FD_UNLIKELY( !tower ) ) {
    FD_LOG_WARNING( ( "NULL tower" ) );
    return NULL;
  }

  return (void *)tower;
}

void *
fd_tower_delete( void * tower ) {

  if( FD_UNLIKELY( !tower ) ) {
    FD_LOG_WARNING( ( "NULL tower" ) );
    return NULL;
  }

  if( FD_UNLIKELY( !fd_ulong_is_aligned( (ulong)tower, fd_tower_align() ) ) ) {
    FD_LOG_WARNING( ( "misaligned tower" ) );
    return NULL;
  }

  return tower;
}

void
fd_tower_init( fd_tower_t *                tower,
               fd_pubkey_t const *         vote_acc_addr,
               fd_acc_mgr_t *              acc_mgr,
               fd_exec_epoch_ctx_t const * epoch_ctx,
               fd_fork_t const *           fork ) {

  if( FD_UNLIKELY( !tower ) ) {
    FD_LOG_WARNING( ( "NULL tower" ) );
    return;
  }

  /* Restore our tower using the vote account state. */

  FD_SCRATCH_SCOPE_BEGIN {
    fd_vote_state_versioned_t vote_state_versioned = { 0 };

    fd_cluster_tower_t * cluster_tower = fd_tower_cluster_query( tower,
                                                                 vote_acc_addr,
                                                                 acc_mgr,
                                                                 fork,
                                                                 fd_scratch_virtual(),
                                                                 &vote_state_versioned );
    if( FD_LIKELY( cluster_tower ) ) {
      fd_tower_cluster_sync( tower, cluster_tower );
    } else {
      FD_LOG_WARNING( ( "[%s] didn't find existing vote state for %32J",
                        __func__,
                        vote_acc_addr ) );
    }
  }
  FD_SCRATCH_SCOPE_END;

  /* Init vote_accs and total_stake. */

  fd_tower_epoch_update( tower, epoch_ctx );

  return;
}

int
fd_tower_lockout_check( fd_tower_t const * tower,
                        fd_fork_t const *  fork,
                        fd_ghost_t const * ghost ) {

  ulong cnt = simulate_vote( tower->votes, fork->slot );

  /* Subtract the simulated vote. */

  cnt--;

  /* Check all remaining votes on the tower to make sure they are on the
     same fork. */

  while( cnt ) {
    fd_tower_vote_t const * vote = fd_tower_votes_peek_index_const( tower->votes, cnt-- );

    /* We're locked out if the fork->slot is not a descendant of this
       previous vote->slot.

       If the vote slot is older than the ghost root, then we no longer
       have a valid ancestry. So we assume this fork slot is a
       descendant. */

    if( FD_UNLIKELY( vote->slot > ghost->root->slot &&
                     !fd_ghost_is_descendant( ghost, fork->slot, vote->slot ) ) ) {
      FD_LOG_NOTICE( ( "[fd_tower_lockout_check] lockout for %lu by prev vote (slot: "
                       "%lu, conf: %lu)",
                       fork->slot,
                       vote->slot,
                       vote->conf ) );
      return 0;
    }
  }

  FD_LOG_NOTICE( ( "[fd_tower_lockout_check] no lockout for %lu", fork->slot ) );

  /* All remaining votes in the tower are on the same fork, so we are
     not locked out and OK to vote. */

  return 1;
}

int
fd_tower_switch_check( fd_tower_t const * tower,
                       fd_fork_t const *  fork,
                       fd_ghost_t const * ghost ) {
  ulong switch_stake = 0;

  fd_ghost_node_t const * ancestor = fd_ghost_node_query_const( ghost, fork->slot );

#if FD_GHOST_USE_HANDHOLDING
  if( FD_UNLIKELY( !ancestor ) ) {

    /* It is invariant that the fork head must be in ghost, as it was just inserted during
       fd_tower_fork_update. */

    FD_LOG_ERR( ( "unable to find fork head %lu in ghost", fork->slot ) );
  }
#endif

  while( FD_LIKELY( ancestor->parent ) ) {
    fd_ghost_node_t * child = ancestor->child;

    /* Both conditionals are marked FD_LIKELY because we only try to
       switch if the best fork differs from our latest vote fork. */

    while( FD_LIKELY( child ) ) {

      if( FD_LIKELY( child != ancestor && child->slot ) ) switch_stake += child->weight;
      child = child->sibling;
    }
    ancestor = ancestor->parent;
  }

  double switch_pct = (double)switch_stake / (double)tower->total_stake;
  FD_LOG_NOTICE(
      ( "[fd_tower_switch_check] latest vote slot: %lu. switch slot: %lu. stake: %.0lf%%",
        fd_tower_votes_peek_tail_const( tower->votes )->slot,
        fork->slot,
        switch_pct * 100.0 ) );
  return switch_pct > SWITCH_PCT;
}

int
fd_tower_threshold_check( fd_tower_t const * tower,
                          fd_fork_t const *  fork,
                          fd_acc_mgr_t *     acc_mgr ) {
  ulong cnt = simulate_vote( tower->votes, fork->slot );

  /* Return early if our tower is not at least THRESHOLD_DEPTH deep after
     simulating. */

  if( FD_UNLIKELY( cnt < THRESHOLD_DEPTH ) ) return 1;

  /* Get the vote slot from THRESHOLD_DEPTH back. */

  fd_tower_vote_t * our_threshold_vote = fd_tower_votes_peek_index( tower->votes,
                                                                    cnt - THRESHOLD_DEPTH );

  /* Track the amount of stake that has vote slot >= threshold_slot. */

  ulong threshold_stake = 0;

  /* Iterate all the vote accounts. */

  for( fd_tower_vote_accs_iter_t iter = fd_tower_vote_accs_iter_init( tower->vote_accs );
       !fd_tower_vote_accs_iter_done( tower->vote_accs, iter );
       iter = fd_tower_vote_accs_iter_next( tower->vote_accs, iter ) ) {

    FD_SCRATCH_SCOPE_BEGIN {

      fd_tower_vote_acc_t * vote_acc = fd_tower_vote_accs_iter_ele( tower->vote_accs, iter );

      /* FIXME */
      fd_vote_state_versioned_t vote_state_versioned = { 0 };

      fd_cluster_tower_t * cluster_tower = fd_tower_cluster_query( tower,
                                                                   vote_acc->addr,
                                                                   acc_mgr,
                                                                   fork,
                                                                   fd_scratch_virtual(),
                                                                   &vote_state_versioned );
      if( FD_UNLIKELY( !cluster_tower ) ) {
        FD_LOG_WARNING( ( "[%s] failed to load vote acc addr %32J. skipping.",
                          __func__,
                          vote_acc->addr ) );
        continue;
      }

      fd_landed_vote_t * landed_votes = cluster_tower->votes;

      /* If the vote account has an empty tower, continue. */

      if( FD_UNLIKELY( deq_fd_landed_vote_t_empty( landed_votes ) ) ) continue;

      /* Convert the landed_votes into tower's vote_slots interface. */

      void * mem = fd_scratch_alloc( fd_tower_votes_align(), fd_tower_votes_footprint() );
      fd_tower_vote_t * their_tower_votes = fd_tower_votes_join( fd_tower_votes_new( mem ) );
      tower_votes_from_landed_votes( their_tower_votes, landed_votes );

      ulong cnt = simulate_vote( their_tower_votes, fork->slot );

      /* Continue if their tower is not yet THRESHOLD_DEPTH deep after
         simulating. */

      if( FD_UNLIKELY( cnt < THRESHOLD_DEPTH ) ) continue;

      /* Get the vote slot from THRESHOLD_DEPTH back.*/

      fd_tower_vote_t * their_threshold_vote = fd_tower_votes_peek_index( their_tower_votes,
                                                                          cnt - THRESHOLD_DEPTH );

      /* Add their stake if their threshold vote's slot >= our threshold
         vote's slot.

         Because we are iterating vote accounts on the same fork that we
         are threshold checking, we know these slots must occur in a
         common ancestry.

         If their_threshold_vote->slot >= our_threshold_vote->slot, we
         know their threshold vote is either for the same slot or a
         descendant slot of our threshold vote. */

      if( FD_LIKELY( their_threshold_vote->slot >= our_threshold_vote->slot ) ) {
        threshold_stake += vote_acc->stake;
      }
    }
    FD_SCRATCH_SCOPE_END;
  }

  double threshold_pct = (double)threshold_stake / (double)tower->total_stake;
  FD_LOG_NOTICE(
      ( "[fd_tower_threshold_check] latest vote slot %lu. threshold slot: %lu. stake: %.0lf%%",
        fd_tower_votes_peek_tail_const( tower->votes )->slot,
        our_threshold_vote->slot,
        threshold_pct * 100.0 ) );
  return threshold_pct > THRESHOLD_PCT;
}

fd_fork_t const *
fd_tower_best_fork_select( FD_PARAM_UNUSED fd_tower_t const * tower,
                           fd_forks_t const *                 forks,
                           fd_ghost_t const *                 ghost ) {
  fd_ghost_node_t const * head = fd_ghost_head_query_const( ghost );

  /* Search for the fork head in the frontier. */

  fd_fork_t const * best = fd_forks_query_const( forks, head->slot );

#if FD_TOWER_USE_HANDHOLDING
  if( FD_UNLIKELY( !best ) ) {

    /* If the best fork is not in the frontier, then we must have pruned
       it and we're now in a bad state. */

    /* TODO eqvoc */

    FD_LOG_ERR( ( "missing ghost head %lu in frontier", head->slot ) );
  }
#endif

  return best;
}

/* is_stale checks whether the latest vote slot is earlier than the
   ghost root. This indicates we just started up, as we restore the
   tower using the vote account state in funk, but can't do the same for
   slot ancestry information for ghost.

   So for a brief period, our tower and ghost will be out-of-sync until
   their respective root slots line up.

   We assume that if our tower is stale, we can safely vote on or reset
   to the best fork without violating lockout. */

static int
is_stale( fd_tower_t const * tower, fd_ghost_t const * ghost ) {
  return fd_tower_votes_peek_tail_const( tower->votes )->slot < ghost->root->slot;
}

fd_fork_t const *
fd_tower_reset_fork_select( fd_tower_t const * tower,
                            fd_forks_t const * forks,
                            fd_ghost_t const * ghost ) {

  fd_tower_vote_t const * latest_vote = fd_tower_votes_peek_tail_const( tower->votes );

  if( FD_UNLIKELY( fd_tower_votes_empty( tower->votes ) || is_stale( tower, ghost ) ) ) {
    return fd_tower_best_fork_select( tower, forks, ghost );
  }

  /* TODO this is O(n) in # of forks (frontier ele cnt). is that a
     problem? */

  fd_fork_frontier_t const * frontier = forks->frontier;
  fd_fork_t const *          pool     = forks->pool;

  for( fd_fork_frontier_iter_t iter = fd_fork_frontier_iter_init( frontier, pool );
       !fd_fork_frontier_iter_done( iter, frontier, pool );
       iter = fd_fork_frontier_iter_next( iter, frontier, pool ) ) {
    fd_fork_t const * fork = fd_fork_frontier_iter_ele_const( iter, frontier, pool );
    if( FD_LIKELY( fd_ghost_is_descendant( ghost, fork->slot, latest_vote->slot ) ) ) return fork;
  }

  /* TODO this can happen if somehow prune our last vote fork or we
     discard it due to equivocation. Both these cases are currently
     unhandled. */

  FD_LOG_ERR( ( "None of the frontier forks matched our last vote fork. Halting." ) );
}

fd_fork_t const *
fd_tower_vote_fork_select( fd_tower_t *       tower,
                           fd_forks_t const * forks,
                           fd_acc_mgr_t *     acc_mgr,
                           fd_ghost_t const * ghost ) {

  fd_fork_t const * vote_fork = NULL;

  fd_fork_t const * best = fd_tower_best_fork_select( tower, forks, ghost );

  if( FD_UNLIKELY( fd_tower_votes_empty( tower->votes ) || is_stale( tower, ghost ) ) ) {
    return fd_tower_best_fork_select( tower, forks, ghost );
  }

  fd_tower_vote_t const * latest_vote = fd_tower_votes_peek_tail_const( tower->votes );

  /* Optimize for when there is just one fork (most of the time). */

  if( FD_LIKELY( !latest_vote ||
                 fd_ghost_is_descendant( ghost, best->slot, latest_vote->slot ) ) ) {

    /* The best fork is on the same fork and we can vote for
       best_fork->slot if we pass the threshold check. */

    if( FD_LIKELY( fd_tower_threshold_check( tower, best, acc_mgr ) ) ) vote_fork = best;

  } else {

    /* The best fork is on a different fork, so try to switch if we pass
       lockout and switch threshold. */

    if( FD_UNLIKELY( fd_tower_lockout_check( tower, best, ghost ) &&
                     fd_tower_switch_check( tower, best, ghost ) ) ) {
      fd_tower_vote_t const * vote = fd_tower_votes_peek_tail_const( tower->votes );
      FD_LOG_NOTICE( ( "[fd_tower_vote_fork_select] switching to best fork %lu from last vote "
                       "(slot: %lu conf: %lu)",
                       best->slot,
                       vote->slot,
                       vote->conf ) );
      vote_fork = best;
    }
  }

  return vote_fork; /* NULL if we cannot vote. */

  /* Only process vote slots higher than our SMR. */

  // if( FD_LIKELY( latest_vote->root > tower->smr ) ) {

  //   /* Find the previous root vote by node pubkey. */

  //   fd_root_vote_t * prev_root_vote =
  //       fd_root_vote_map_query( root_votes, latest_vote->node_pubkey, NULL );

  //   if( FD_UNLIKELY( !prev_root_vote ) ) {

  //     /* This node pubkey has not yet voted. */

  //     prev_root_vote = fd_root_vote_map_insert( root_votes, latest_vote->node_pubkey );
  //   } else {

  //     fd_root_stake_t * root_stake =
  //         fd_root_stake_map_query( tower->root_stakes, prev_root_vote->root, NULL );
  //     root_stake->stake -= stake;
  //   }

  //   /* Update our bookkeeping of this node pubkey's root. */

  //   prev_root_vote->root = latest_vote->root;

  //   /* Add this node pubkey's stake to all slots in the ancestry back to the SMR. */

  //   fd_root_stake_t * root_stake =
  //       fd_root_stake_map_query( tower->root_stakes, latest_vote->root, NULL );
  //   if( FD_UNLIKELY( !root_stake ) ) {
  //     root_stake = fd_root_stake_map_insert( tower->root_stakes, latest_vote->root );
  //   }
  //   root_stake->stake += stake;
  // }
  // }

  // if( FD_LIKELY( smr > tower->smr ) ) tower->smr = smr;
}

void
fd_tower_epoch_update( fd_tower_t * tower, fd_exec_epoch_ctx_t const * epoch_ctx ) {
  fd_epoch_bank_t const * epoch_bank = fd_exec_epoch_ctx_epoch_bank_const( epoch_ctx );

  ulong total_stake = 0;

  for( fd_vote_accounts_pair_t_mapnode_t * curr = fd_vote_accounts_pair_t_map_minimum(
           epoch_bank->stakes.vote_accounts.vote_accounts_pool,
           epoch_bank->stakes.vote_accounts.vote_accounts_root );
       curr;
       curr = fd_vote_accounts_pair_t_map_successor( epoch_bank->stakes.vote_accounts
                                                         .vote_accounts_pool,
                                                     curr ) ) {

#if FD_TOWER_USE_HANDHOLDING
    if( FD_UNLIKELY( fd_tower_vote_accs_cnt( tower->vote_accs ) ==
                     fd_tower_vote_accs_max( tower->vote_accs ) ) )
      FD_LOG_ERR( ( "fd_tower_vote_accs overflow." ) );
#endif

    if( FD_UNLIKELY( curr->elem.stake > 0UL ) ) {
      fd_tower_vote_accs_push_tail( tower->vote_accs,
                                    ( fd_tower_vote_acc_t ){ .addr  = &curr->elem.key,
                                                             .stake = curr->elem.stake } );
    }
    total_stake += curr->elem.stake;
  }
  tower->total_stake = total_stake;
}

void
fd_tower_fork_update( fd_tower_t const * tower,
                      fd_fork_t const *  fork,
                      fd_acc_mgr_t *     acc_mgr,
                      fd_blockstore_t *  blockstore,
                      fd_ghost_t *       ghost ) {
  ulong root = tower->root; /* FIXME fseq */

  /* Get the parent key. Every slot except the root must have a parent. */

  fd_blockstore_start_read( blockstore );
  ulong parent_slot = fd_blockstore_parent_slot_query( blockstore, fork->slot );
#if FD_TOWER_USE_HANDHOLDING
  /* we must have a parent slot and bank hash, given we just executed
     its child. if not, likely a bug in blockstore pruning. */
  if( FD_UNLIKELY( parent_slot == FD_SLOT_NULL ) ) {
    FD_LOG_ERR( ( "missing parent slot for curr slot %lu", fork->slot ) );
  };
#endif
  fd_blockstore_end_read( blockstore );

  /* Insert the new fork head into ghost. */

  fd_ghost_node_t * ghost_node = fd_ghost_node_insert( ghost, fork->slot, parent_slot );

#if FD_TOWER_USE_HANDHOLDING
  if( FD_UNLIKELY( !ghost_node ) ) {
    FD_LOG_ERR( ( "failed to insert ghost node %lu", fork->slot ) );
  }
#endif

  for( fd_tower_vote_accs_iter_t iter = fd_tower_vote_accs_iter_init_rev( tower->vote_accs );
       !fd_tower_vote_accs_iter_done_rev( tower->vote_accs, iter );
       iter = fd_tower_vote_accs_iter_prev( tower->vote_accs, iter ) ) {

    fd_tower_vote_acc_t * vote_acc = fd_tower_vote_accs_iter_ele( tower->vote_accs, iter );

    FD_SCRATCH_SCOPE_BEGIN {

      /* FIXME */
      fd_vote_state_versioned_t vote_state_versioned = { 0 };

      fd_cluster_tower_t * cluster_tower = fd_tower_cluster_query( tower,
                                                                   vote_acc->addr,
                                                                   acc_mgr,
                                                                   fork,
                                                                   fd_scratch_virtual(),
                                                                   &vote_state_versioned );
      if( FD_UNLIKELY( !cluster_tower ) ) {
        FD_LOG_WARNING( ( "[%s] failed to load vote acc addr %32J. skipping.",
                          __func__,
                          vote_acc->addr ) );
        continue;
      }

      fd_landed_vote_t * landed_votes = cluster_tower->votes;

      /* If the vote account has an empty tower, continue. */

      if( FD_UNLIKELY( deq_fd_landed_vote_t_empty( landed_votes ) ) ) continue;

      /* Get the vote account's latest vote. */

      ulong vote_slot = deq_fd_landed_vote_t_peek_tail_const( landed_votes )->lockout.slot;

      /* Ignore votes for slots < root. This guards the ghost invariant
         that the vote slot must be present in the ghost tree. */

      if( FD_UNLIKELY( vote_slot < root ) ) continue;

      /* Upsert the vote into ghost. */

      fd_ghost_replay_vote_upsert( ghost, vote_slot, &cluster_tower->node_pubkey, vote_acc->stake );
    }
    FD_SCRATCH_SCOPE_END;
  }
}

ulong
fd_tower_simulate_vote( fd_tower_t const * tower, ulong slot ) {
  return simulate_vote( tower->votes, slot );
}

void
fd_tower_vote( fd_tower_t const * tower, ulong slot ) {
  FD_LOG_NOTICE( ( "[fd_tower_vote] voting for slot %lu", slot ) );

  /* Check we're not voting for the exact same slot as our latest tower
     vote. This can happen when there are forks. */

  fd_tower_vote_t * latest_vote = fd_tower_votes_peek_tail( tower->votes );
  if( FD_UNLIKELY( latest_vote && latest_vote->slot == slot ) ) {
    FD_LOG_NOTICE( ( "[fd_tower_vote] already voted for slot %lu", slot ) );
    return;
  }

#if FD_TOWER_USE_HANDHOLDING

  /* Check we aren't voting for a slot earlier than the latest tower
     vote. This should not happen and indicates a bug, because on the
     same vote fork the slot should be monotonically non-decreasing. */

  for( fd_tower_votes_iter_t iter = fd_tower_votes_iter_init_rev( tower->votes );
       !fd_tower_votes_iter_done_rev( tower->votes, iter );
       iter = fd_tower_votes_iter_prev( tower->votes, iter ) ) {
    fd_tower_vote_t const * vote = fd_tower_votes_iter_ele_const( tower->votes, iter );
    if( FD_UNLIKELY( slot == vote->slot ) ) {
      FD_LOG_WARNING( ( "[fd_tower_vote] double-voting for old slot %lu (new vote: %lu)",
                        slot,
                        vote->slot ) );
      fd_tower_print( tower );
      __asm__( "int $3" );
    }
  }

#endif

  /* First, simulate a vote for slot. We do this purely for
     implementation convenience and code reuse.

     As the name of this function indicates, we are not just
     simulating and in fact voting for this fork by pushing this a new
     vote onto the tower. */

  ulong cnt = simulate_vote( tower->votes, slot );

  /* Subtract the simulated vote. */

  cnt--;

  /* Pop everything that got expired. */

  while( fd_tower_votes_cnt( tower->votes ) > cnt ) {
    fd_tower_votes_pop_tail( tower->votes );
  }

  /* Increase confirmations (double lockouts) in consecutive votes. */

  ulong prev_conf = 0;
  for( fd_tower_votes_iter_t iter = fd_tower_votes_iter_init_rev( tower->votes );
       !fd_tower_votes_iter_done_rev( tower->votes, iter );
       iter = fd_tower_votes_iter_prev( tower->votes, iter ) ) {
    fd_tower_vote_t * vote = fd_tower_votes_iter_ele( tower->votes, iter );
    if( FD_UNLIKELY( vote->conf != ++prev_conf ) ) {
      break;
    }
    vote->conf++;
  }

  /* Add the new vote to the tower. */

  fd_tower_votes_push_tail( tower->votes, ( fd_tower_vote_t ){ .slot = slot, .conf = 1 } );
}

int
fd_tower_cluster_cmp( fd_tower_t const * tower, fd_cluster_tower_t * cluster_tower ) {
#if FD_TOWER_USE_HANDHOLDING
  if( FD_UNLIKELY( !tower->root ) ) {
    FD_LOG_ERR( ( "[%s] tower is missing root.", __func__ ) );
  }

  if( FD_UNLIKELY( fd_tower_votes_empty( tower->votes ) ) ) {
    FD_LOG_ERR( ( "[%s] tower is empty.", __func__ ) );
  }

  if( FD_UNLIKELY( !cluster_tower->has_root_slot ) ) {
    FD_LOG_ERR( ( "[%s] cluster_tower is missing root.", __func__ ) );
  }

  if( FD_UNLIKELY( deq_fd_landed_vote_t_empty( cluster_tower->votes ) ) ) {
    FD_LOG_ERR( ( "[%s] cluster_tower is empty.", __func__ ) );
  }
#endif

  ulong local   = fd_tower_votes_peek_tail_const( tower->votes )->slot;
  ulong cluster = deq_fd_landed_vote_t_peek_tail_const( cluster_tower->votes )->lockout.slot;
  return fd_int_if( local == cluster, 0, fd_int_if( local > cluster, 1, -1 ) );
}

fd_cluster_tower_t *
fd_tower_cluster_query( FD_PARAM_UNUSED fd_tower_t const * tower,
                        fd_pubkey_t const *                vote_acc_addr,
                        fd_acc_mgr_t *                     acc_mgr,
                        fd_fork_t const *                  fork,
                        fd_valloc_t                        valloc,
                        fd_vote_state_versioned_t *        versioned ) {
  int rc;

  FD_BORROWED_ACCOUNT_DECL( vote_acc );
  rc = fd_acc_mgr_view( acc_mgr, fork->slot_ctx.funk_txn, vote_acc_addr, vote_acc );
  if( FD_UNLIKELY( rc == FD_ACC_MGR_ERR_UNKNOWN_ACCOUNT ) ) {
    FD_LOG_WARNING( ( "[%s] fd_acc_mgr_view could not find vote account %32J. error: %d",
                      __func__,
                      vote_acc_addr,
                      rc ) );
    return NULL;
  } else if( FD_UNLIKELY( rc != FD_ACC_MGR_SUCCESS ) ) {
    FD_LOG_ERR( ( "[%s] fd_acc_mgr_view failed on vote account %32J. error: %d",
                  __func__,
                  vote_acc_addr,
                  rc ) );
  }

  rc = fd_vote_get_state( vote_acc, valloc, versioned );
  if( FD_UNLIKELY( rc != FD_ACC_MGR_SUCCESS ) ) {
    FD_LOG_ERR( ( "[%s] fd_vote_get_state failed on vote account %32J. error: %d",
                  __func__,
                  vote_acc_addr,
                  rc ) );
  }

  fd_vote_convert_to_current( versioned, valloc );

  return &versioned->inner.current;
}

void
fd_tower_cluster_sync( fd_tower_t * tower, fd_cluster_tower_t * cluster_tower ) {
#if FD_TOWER_USE_HANDHOLDING
  if( FD_UNLIKELY( !cluster_tower->has_root_slot ) ) {
    FD_LOG_ERR( ( "[%s] cluster_tower is missing root.", __func__ ) );
  }

  if( FD_UNLIKELY( deq_fd_landed_vote_t_empty( cluster_tower->votes ) ) ) {
    FD_LOG_ERR( ( "[%s] cluster_tower is empty.", __func__ ) );
  }
#endif

  fd_landed_vote_t const * cluster_latest_vote =
      deq_fd_landed_vote_t_peek_tail_const( cluster_tower->votes );
  if( FD_UNLIKELY( cluster_latest_vote->lockout.slot ) ) {
    FD_LOG_WARNING( ( "syncing with cluster" ) );

    /* Sync local with cluster.  */

    fd_tower_votes_remove_all( tower->votes );
    tower_votes_from_landed_votes( tower->votes, cluster_tower->votes );
    tower->root = cluster_tower->root_slot;
  }
}

// ulong i = fd_tower_votes_cnt( tower->votes );
// while ( fd_tower_vote(const fd_tower_t *tower, ulong slot) )

// /* If the local view of our tower is a strict subset or divergent from
//    the cluster view, then we need to sync with the cluster ie. replace
//    our local state with the cluster state. */

// int   sync = 0;
// ulong i    = 0;
// for( deq_fd_landed_vote_t_iter_t iter = deq_fd_landed_vote_t_iter_init( cluster_tower->votes );
//      !deq_fd_landed_vote_t_iter_done( cluster_tower->votes, iter );
//      iter = deq_fd_landed_vote_t_iter_next( cluster_tower->votes, iter ) ) {
//   fd_landed_vote_t const * landed_vote =
//       deq_fd_landed_vote_t_iter_ele_const( cluster_tower->votes, iter );
//   fd_tower_vote_t const * tower_vote = fd_tower_votes_peek_index_const( tower->votes, i++ );

//   if( FD_UNLIKELY( !tower_vote ) ) {

//     /* Local tower is shorter, need sync*/

//     sync = 1;
//   }

//   if( FD_UNLIKELY( tower_vote->slot != landed_vote->lockout.slot ||
//                    tower_vote->conf != landed_vote->lockout.confirmation_count ) ) {

//     /* Local tower diverges, need sync */

//     sync = 1;
//   }
// }

// /* Note we don't sync if the local view of the tower is taller.
//    Syncing can lead to a problem where we can otherwise get "stuck" in
//    a cluster-sync loop when our votes don't land.  For example, if our
//    tower is currently [1 3 5], and we vote [1 3 5 7].  That vote
//    doesn't land, so we re-sync and as a result vote [1 3 5 9].
//    Instead we would prefer [1 3 5 7 9] for the second vote. */

// if( FD_UNLIKELY( sync ) ) {

//   FD_LOG_WARNING(("syncing with cluster"));

//   /* Sync local with cluster.  */

//   fd_tower_votes_remove_all( tower->votes );
//   tower_votes_from_landed_votes( tower->votes, cluster_tower->votes );
//   tower->root = cluster_tower->root_slot;
// }

void
fd_tower_print( fd_tower_t const * tower ) {
  print( tower->votes, tower->root );
}

void
fd_tower_to_tower_sync( fd_tower_t const *               tower,
                        fd_hash_t const *                bank_hash,
                        fd_compact_vote_state_update_t * tower_sync ) {
  tower_sync->root         = tower->root;
  long ts                  = fd_log_wallclock();
  tower_sync->timestamp    = &ts;
  tower_sync->lockouts_len = (ushort)fd_tower_votes_cnt( tower->votes );
  tower_sync->lockouts     = (fd_lockout_offset_t *)
      fd_scratch_alloc( alignof( fd_lockout_offset_t ),
                        tower_sync->lockouts_len * sizeof( fd_lockout_offset_t ) );

  ulong i         = 0UL;
  ulong curr_slot = tower_sync->root;

  for( fd_tower_votes_iter_t iter = fd_tower_votes_iter_init( tower->votes );
       !fd_tower_votes_iter_done( tower->votes, iter );
       iter = fd_tower_votes_iter_next( tower->votes, iter ) ) {
    fd_tower_vote_t const * vote = fd_tower_votes_iter_ele_const( tower->votes, iter );
    FD_TEST( vote->slot >= tower_sync->root );
    ulong offset                               = vote->slot - curr_slot;
    curr_slot                                  = vote->slot;
    uchar conf                                 = (uchar)vote->conf;
    tower_sync->lockouts[i].offset             = offset;
    tower_sync->lockouts[i].confirmation_count = conf;
    memcpy( tower_sync->hash.uc, bank_hash, sizeof( fd_hash_t ) );
    i++;
  }
}
