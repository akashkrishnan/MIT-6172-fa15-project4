// Copyright (c) 2015 MIT License by 6.172 Staff

#include "./move_gen.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "./fen.h"
#include "./search.h"

#define MAX(x, y)  ((x) > (y) ? (x) : (y))
#define MIN(x, y)  ((x) < (y) ? (x) : (y))

int USE_KO; // Respect the Ko rule

static char *color_strs[2] = {"White", "Black"};

char *color_to_str(color_t c) {
  return color_strs[c];
}

// -----------------------------------------------------------------------------
// Piece getters and setters. Color, then type, then orientation.
// -----------------------------------------------------------------------------

// which color is moving next

// King orientations
char *king_ori_to_rep[2][NUM_ORI] = {
  { "NN", "EE", "SS", "WW"},
  { "nn", "ee", "ss", "ww"}
};

// Pawn orientations
char *pawn_ori_to_rep[2][NUM_ORI] = {
  { "NW", "NE", "SE", "SW"},
  { "nw", "ne", "se", "sw"}
};

char *nesw_to_str[NUM_ORI] = {"north", "east", "south", "west"};

// -----------------------------------------------------------------------------
// Board, squares
// -----------------------------------------------------------------------------

static uint64_t zob[ARR_SIZE][1 << PIECE_SIZE];
static uint64_t zob_color;
uint64_t myrand();

// Zobrist hashing

uint64_t compute_zob_key(position_t *p) {
  uint64_t key = 0;
  for (fil_t f = 0; f < BOARD_WIDTH; f++) {
    for (rnk_t r = 0; r < BOARD_WIDTH; r++) {
      square_t sq = square_of(f, r);
      key ^= zob[sq][p->board[sq]];
    }
  }
  if (color_to_move_of(p) == BLACK)
    key ^= zob_color;

  return key;
}

void init_zob() {
  for (int i = 0; i < ARR_SIZE; i++) {
    for (int j = 0; j < (1 << PIECE_SIZE); j++) {
      zob[i][j] = myrand();
    }
  }
  zob_color = myrand();
}


// converts a square to string notation, returns number of characters printed

int square_to_str(square_t sq, char *buf, size_t bufsize) {
  fil_t f = fil_of(sq);
  rnk_t r = rnk_of(sq);
  if (f >= 0) {
    return snprintf(buf, bufsize, "%c%d", 'a' + f, r);
  } else {
    return snprintf(buf, bufsize, "%c%d", 'z' + f + 1, r);
  }
}




// converts a move to string notation for FEN

void move_to_str(move_t mv, char *buf, size_t bufsize) {
  square_t f = from_square(mv); // from-square
  square_t t = to_square(mv); // to-square
  rot_t r = rot_of(mv); // rotation
  const char *orig_buf = buf;

  buf += square_to_str(f, buf, bufsize);
  if (f != t) {
    buf += square_to_str(t, buf, bufsize - (buf - orig_buf));
  } else {
    switch (r) {
      case NONE:
        buf += square_to_str(t, buf, bufsize - (buf - orig_buf));
        break;
      case RIGHT:
        buf += snprintf(buf, bufsize - (buf - orig_buf), "R");
        break;
      case UTURN:
        buf += snprintf(buf, bufsize - (buf - orig_buf), "U");
        break;
      case LEFT:
        buf += snprintf(buf, bufsize - (buf - orig_buf), "L");
        break;
      default:
        tbassert(false, "Whoa, now.  Whoa, I say.\n"); // Bad, bad, bad
        break;
    }
  }
}

// Generate all moves from position p.  Returns number of moves.
// strict currently ignored

int generate_all(position_t *p, sortable_move_t *sortable_move_list,
                 bool strict) {
  color_t color_to_move = color_to_move_of(p);
  // Make sure that the enemy_laser map is marked
  char laser_map[ARR_SIZE];

  for (int i = 0; i < ARR_SIZE; ++i) {
    laser_map[i] = 4; // Invalid square
  }

  for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
    for (rnk_t r = 0; r < BOARD_WIDTH; ++r) {
      laser_map[square_of(f, r)] = 0;
    }
  }

  // 1 = path of laser with no moves
  mark_laser_path(p, laser_map, opp_color(color_to_move), 1);

  int move_count = 0;

  for (fil_t f = 0; f < BOARD_WIDTH; f++) {
    for (rnk_t r = 0; r < BOARD_WIDTH; r++) {
      square_t sq = square_of(f, r);
      piece_t x = p->board[sq];

      ptype_t typ = ptype_of(x);
      color_t color = color_of(x);

      switch (typ) {
        case EMPTY:
          break;
        case PAWN:
          if (laser_map[sq] == 1) continue; // Piece is pinned down by laser.
        case KING:
          if (color != color_to_move) { // Wrong color
            break;
          }
          // directions
          for (int d = 0; d < 8; d++) {
            int dest = sq + dir_of(d);
            // Skip moves into invalid squares, squares occupied by
            // kings, nonempty squares if x is a king, and squares with
            // pawns of matching color
            if (ptype_of(p->board[dest]) == INVALID ||
                ptype_of(p->board[dest]) == KING ||
                (typ == KING && ptype_of(p->board[dest]) != EMPTY) ||
                (typ == PAWN && ptype_of(p->board[dest]) == PAWN &&
                color == color_of(p->board[dest]))) {
              continue; // illegal square
            }

            WHEN_DEBUG_VERBOSE(char buf[MAX_CHARS_IN_MOVE]);
            WHEN_DEBUG_VERBOSE({
                               move_to_str(move_of(typ, (rot_t) 0, sq, dest), buf, MAX_CHARS_IN_MOVE);
                               DEBUG_LOG(1, "Before: %s ", buf);
            });
            tbassert(move_count < MAX_NUM_MOVES, "move_count: %d\n", move_count);
            sortable_move_list[move_count++] = move_of(typ, (rot_t) 0, sq, dest);

            WHEN_DEBUG_VERBOSE({
                               move_to_str(get_move(sortable_move_list[move_count - 1]), buf, MAX_CHARS_IN_MOVE);
                               DEBUG_LOG(1, "After: %s\n", buf);
            });
          }

          // rotations - three directions possible
          for (int rot = 1; rot < 4; ++rot) {
            tbassert(move_count < MAX_NUM_MOVES, "move_count: %d\n", move_count);
            sortable_move_list[move_count++] = move_of(typ, (rot_t) rot, sq, sq);
          }
          if (typ == KING) { // Also generate null move
            tbassert(move_count < MAX_NUM_MOVES, "move_count: %d\n", move_count);
            sortable_move_list[move_count++] = move_of(typ, (rot_t) 0, sq, sq);
          }
          break;
        case INVALID:
        default:
          tbassert(false, "Bogus, man.\n"); // Couldn't BE more bogus!
      }
    }
  }

  WHEN_DEBUG_VERBOSE({
                     DEBUG_LOG(1, "\nGenerated moves: ");
                     for (int i = 0; i < move_count; ++i) {
                     char buf[MAX_CHARS_IN_MOVE];
                     move_to_str(get_move(sortable_move_list[i]), buf, MAX_CHARS_IN_MOVE);
                     DEBUG_LOG(1, "%s ", buf);
    }
                     DEBUG_LOG(1, "\n");
  });

  return move_count;
}

square_t low_level_make_move(position_t *old, position_t *p, move_t mv) {
  tbassert(mv != 0, "mv was zero.\n");

  square_t stomped_dst_sq = 0;

  WHEN_DEBUG_VERBOSE(char buf[MAX_CHARS_IN_MOVE]);
  WHEN_DEBUG_VERBOSE({
                     move_to_str(mv, buf, MAX_CHARS_IN_MOVE);
                     DEBUG_LOG(1, "low_level_make_move: %s\n", buf);
  });

  tbassert(old->key == compute_zob_key(old),
           "old->key: %"PRIu64", zob-key: %"PRIu64"\n",
           old->key, compute_zob_key(old));

  WHEN_DEBUG_VERBOSE({
                     fprintf(stderr, "Before:\n");
                     display(old);
  });

  square_t from_sq = from_square(mv);
  square_t to_sq = to_square(mv);
  rot_t rot = rot_of(mv);

  WHEN_DEBUG_VERBOSE({
                     DEBUG_LOG(1, "low_level_make_move 2:\n");
                     square_to_str(from_sq, buf, MAX_CHARS_IN_MOVE);
                     DEBUG_LOG(1, "from_sq: %s\n", buf);
                     square_to_str(to_sq, buf, MAX_CHARS_IN_MOVE);
                     DEBUG_LOG(1, "to_sq: %s\n", buf);
                     switch (rot) {
                     case NONE:
                     DEBUG_LOG(1, "rot: none\n");
                     break;
                     case RIGHT:
                     DEBUG_LOG(1, "rot: R\n");
                     break;
                     case UTURN:
                     DEBUG_LOG(1, "rot: U\n");
                     break;
                     case LEFT:
                     DEBUG_LOG(1, "rot: L\n");
                     break;
                     default:
                     tbassert(false, "Not like a boss at all.\n"); // Bad, bad, bad
                     break;
    }
  });

  memcpy(p, old, sizeof (position_t));

  p->history = old;
  p->last_move = mv;

  tbassert(from_sq < ARR_SIZE && from_sq > 0, "from_sq: %d\n", from_sq);
  tbassert(p->board[from_sq] < (1 << PIECE_SIZE) && p->board[from_sq] >= 0,
           "p->board[from_sq]: %d\n", p->board[from_sq]);
  tbassert(to_sq < ARR_SIZE && to_sq > 0, "to_sq: %d\n", to_sq);
  tbassert(p->board[to_sq] < (1 << PIECE_SIZE) && p->board[to_sq] >= 0,
           "p->board[to_sq]: %d\n", p->board[to_sq]);

  p->key ^= zob_color; // swap color to move

  piece_t from_piece = p->board[from_sq];
  piece_t to_piece = p->board[to_sq];

  // Pieces block each other, unless a pawn is stomping an enemy pawn
  tbassert(EMPTY == ptype_of(to_piece) ||
           from_sq == to_sq ||
           (PAWN == ptype_of(from_piece) &&
           PAWN == ptype_of(to_piece) &&
           color_of(to_piece) == opp_color(color_of(from_piece))),
           "from-type: %d, to-type: %d, from-sq: %d, to-sq: %d, from-color: %d, to-color: %d\n",
           ptype_of(from_piece), ptype_of(to_piece),
           from_sq, to_sq,
           color_of(from_piece), color_of(to_piece));

  if (to_sq != from_sq) { // move, not rotation
    if (PAWN == ptype_of(from_piece) &&
        PAWN == ptype_of(to_piece) &&
        color_of(to_piece) == opp_color(color_of(from_piece))) {
      // We're stomping a piece.  Return the destination of the
      // stomped piece.  Let the caller remove the piece from the
      // board.
      stomped_dst_sq = from_sq;
    }

    // Hash key updates
    p->key ^= zob[from_sq][from_piece]; // remove from_piece from from_sq
    p->key ^= zob[to_sq][to_piece]; // remove to_piece from to_sq

    p->board[to_sq] = from_piece; // swap from_piece and to_piece on board
    p->board[from_sq] = to_piece;

    p->key ^= zob[to_sq][from_piece]; // place from_piece in to_sq
    p->key ^= zob[from_sq][to_piece]; // place to_piece in from_sq

    // Update King locations if necessary
    if (ptype_of(from_piece) == KING) {
      p->kloc[color_of(from_piece)] = to_sq;
    }
    if (ptype_of(to_piece) == KING) {
      p->kloc[color_of(to_piece)] = from_sq;
    }

  } else { // rotation
    // remove from_piece from from_sq in hash
    p->key ^= zob[from_sq][from_piece];
    set_ori(&from_piece, rot + ori_of(from_piece)); // rotate from_piece
    p->board[from_sq] = from_piece; // place rotated piece on board
    p->key ^= zob[from_sq][from_piece]; // ... and in hash
  }

  // Increment ply
  p->ply++;

  tbassert(p->key == compute_zob_key(p),
           "p->key: %"PRIu64", zob-key: %"PRIu64"\n",
           p->key, compute_zob_key(p));

  WHEN_DEBUG_VERBOSE({
                     fprintf(stderr, "After:\n");
                     display(p);
  });

  return stomped_dst_sq;
}

/*
 * Stomped piece must be removed from hash and board according to returned value stomped_dst_sq
 * p              - the current position to be updated
 * mv             - the move to make in the current position
 */
square_t low_level_apply_move(position_t *p, move_t mv) {
  tbassert(mv != 0, "mv was zero.\n");

  square_t stomped_dst_sq = 0;

  // Decompose move data
  square_t from_sq = from_square(mv);
  square_t to_sq = to_square(mv);
  rot_t rot = rot_of(mv);

  // Swap turns
  p->key ^= zob_color;

  // Get pieces
  piece_t from_piece = p->board[from_sq];
  piece_t to_piece = p->board[to_sq];

  // Determine if move was a translation or a rotation
  if (to_sq != from_sq) {
    // Move was a translation

    // Check if we're stomping a piece
    if (PAWN == ptype_of(from_piece) &&
        PAWN == ptype_of(to_piece) &&
        color_of(to_piece) == opp_color(color_of(from_piece))) {
      stomped_dst_sq = from_sq;
    }

    // Swap from_piece and to_piece in hash
    p->key ^= zob[from_sq][from_piece]; // Remove from_piece from from_sq
    p->key ^= zob[to_sq][to_piece]; // Remove to_piece from to_sq
    p->key ^= zob[to_sq][from_piece]; // Add from_piece in to_sq
    p->key ^= zob[from_sq][to_piece]; // Add to_piece in from_sq

    // Swap from_piece and to_piece on board
    p->board[to_sq] = from_piece;
    p->board[from_sq] = to_piece;

    // Update King locations if necessary
    if (ptype_of(from_piece) == KING) {
      p->kloc[color_of(from_piece)] = to_sq;
    }
    if (ptype_of(to_piece) == KING) {
      p->kloc[color_of(to_piece)] = from_sq;
    }

  } else {
    // Move was a rotation

    p->key ^= zob[from_sq][from_piece]; // Remove from_piece from from_sq in hash
    set_ori(&from_piece, ori_of(from_piece) + rot); // Rotate from_piece
    p->board[from_sq] = from_piece; // Place piece on board
    p->key ^= zob[from_sq][from_piece]; // Add from_piece to from_sq in hash

  }

  // Increment ply
  p->ply++;

  return stomped_dst_sq;
}

/*
 * Stomped piece must be added to hash and board according to stomped_dst_sq
 * p              - the current position
 * mv             - the move that resulted in the current position
 */
void low_level_undo_move(position_t *p, move_t mv) {
  tbassert(mv != 0, "mv was zero.\n");

  // Decompose move data
  square_t from_sq = to_square(mv);
  square_t to_sq = from_square(mv);
  rot_t rot = -rot_of(mv);

  // Swap turns
  p->key ^= zob_color;

  // Get pieces
  piece_t from_piece = p->board[from_sq];
  piece_t to_piece = p->board[to_sq];

  // Determine if move was a translation or a rotation
  if (to_sq != from_sq) {
    // Move was a translation

    // Swap from_piece and to_piece in hash
    p->key ^= zob[from_sq][from_piece]; // Remove from_piece from from_sq
    p->key ^= zob[to_sq][to_piece]; // Remove to_piece from to_sq
    p->key ^= zob[to_sq][from_piece]; // Add from_piece in to_sq
    p->key ^= zob[from_sq][to_piece]; // Add to_piece in from_sq

    // Swap from_piece and to_piece on board
    p->board[to_sq] = from_piece;
    p->board[from_sq] = to_piece;

    // Update King locations if necessary
    if (ptype_of(from_piece) == KING) {
      p->kloc[color_of(from_piece)] = to_sq;
    }
    if (ptype_of(to_piece) == KING) {
      p->kloc[color_of(to_piece)] = from_sq;
    }

  } else {
    // Move was a rotation

    p->key ^= zob[to_sq][to_piece]; // Remove to_piece from to_sq in hash
    set_ori(&to_piece, ori_of(to_piece) + rot); // Rotate to_piece
    p->board[to_sq] = to_piece; // Place piece on board
    p->key ^= zob[to_sq][to_piece]; // Add to_piece to to_sq in hash

  }

  // Decrement ply
  p->ply--;

  return;
}


// returns square of piece to be removed from board or 0

square_t fire(position_t *p) {
  color_t fake_color_to_move = (color_to_move_of(p) == WHITE) ? BLACK : WHITE;
  square_t sq = p->kloc[fake_color_to_move];
  int bdir = ori_of(p->board[sq]);

  tbassert(ptype_of(p->board[ p->kloc[fake_color_to_move] ]) == KING,
           "ptype_of(p->board[ p->kloc[fake_color_to_move] ]): %d\n",
           ptype_of(p->board[ p->kloc[fake_color_to_move] ]));

  while (true) {
    sq += beam_of(bdir);
    tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);

    switch (ptype_of(p->board[sq])) {
      case EMPTY: // empty square
        break;
      case PAWN: // Pawn
        bdir = reflect_of(bdir, ori_of(p->board[sq]));
        if (bdir < 0) { // Hit back of Pawn
          return sq;
        }
        break;
      case KING: // King
        return sq; // sorry, game over my friend!
        break;
      case INVALID: // Ran off edge of board
        return 0;
        break;
      default: // Shouldna happen, man!
        tbassert(false, "Like porkchops and whipped cream.\n");
        break;
    }
  }
}

// Uncomment the following lines to test if undo_move properly undo's make_move
// victims_t apply_move(position_t *p, move_t mv);
// void undo_move(position_t *p, victims_t victims, move_t mv);

// return victim pieces or KO

victims_t make_move(position_t *old, position_t *p, move_t mv) {
  tbassert(mv != 0, "mv was zero.\n");

  WHEN_DEBUG_VERBOSE(char buf[MAX_CHARS_IN_MOVE]);

  // Uncomment the following lines to test if undo_move properly undo's make_move
  // victims_t victims = apply_move(old, mv);
  // undo_move(old, victims, mv);

  // move phase 1 - moving a piece, which may result in a stomp
  square_t stomped_sq = low_level_make_move(old, p, mv);

  WHEN_DEBUG_VERBOSE({
                     if (stomped_sq != 0) {
                     square_to_str(stomped_sq, buf, MAX_CHARS_IN_MOVE);
                     DEBUG_LOG(1, "Stomping piece on %s\n", buf);
    }
  });

  if (stomped_sq == 0) {
    p->victims.stomped = 0;

    // Don't check for Ko yet.

  } else { // we definitely stomped something
    p->victims.stomped = p->board[stomped_sq];
    if (color_of(p->victims.stomped) == WHITE) {
      p->white_pawn_count -= 1;
    } else if (color_of(p->victims.stomped) == BLACK) {
      p->black_pawn_count -= 1;
    }
    p->key ^= zob[stomped_sq][p->victims.stomped]; // remove from board
    p->board[stomped_sq] = 0;
    p->key ^= zob[stomped_sq][p->board[stomped_sq]];

    tbassert(p->key == compute_zob_key(p),
             "p->key: %"PRIu64", zob-key: %"PRIu64"\n",
             p->key, compute_zob_key(p));

    WHEN_DEBUG_VERBOSE({
                       square_to_str(stomped_sq, buf, MAX_CHARS_IN_MOVE);
                       DEBUG_LOG(1, "Stomped piece on %s\n", buf);
    });
  }

  // move phase 2 - shooting the laser
  square_t victim_sq = fire(p);

  WHEN_DEBUG_VERBOSE({
                     if (victim_sq != 0) {
                     square_to_str(victim_sq, buf, MAX_CHARS_IN_MOVE);
                     DEBUG_LOG(1, "Zapping piece on %s\n", buf);
    }
  });

  if (victim_sq == 0) {
    p->victims.zapped = 0;

    if (USE_KO && // Ko rule
        zero_victims(p->victims) &&
        (p->key == (old->key ^ zob_color))) {
      return KO();
    }
  } else { // we definitely hit something with laser
    p->victims.zapped = p->board[victim_sq];
    if (color_of(p->victims.zapped) == WHITE) {
      p->white_pawn_count -= 1;
    } else if (color_of(p->victims.zapped) == BLACK) {
      p->black_pawn_count -= 1;
    }
    p->key ^= zob[victim_sq][p->victims.zapped]; // remove from board
    p->board[victim_sq] = 0;
    p->key ^= zob[victim_sq][0];

    tbassert(p->key == compute_zob_key(p),
             "p->key: %"PRIu64", zob-key: %"PRIu64"\n",
             p->key, compute_zob_key(p));

    WHEN_DEBUG_VERBOSE({
                       square_to_str(victim_sq, buf, MAX_CHARS_IN_MOVE);
                       DEBUG_LOG(1, "Zapped piece on %s\n", buf);
    });
  }

  return p->victims;
}

// return victim pieces or KO

victims_t apply_move(position_t *p, move_t mv) {
  tbassert(mv != 0, "mv was zero.\n");

  victims_t victims = {
                       .stomped = 0, .stomped_sq = 0,
                       .zapped = 0, .zapped_sq = 0,
  };

  uint64_t old_key = p->key;

  // STEP 1: translating or rotating with stomping
  victims.stomped_sq = low_level_apply_move(p, mv);

  // Check for stomping
  if (victims.stomped_sq) {
    // Stomped
    victims.stomped = p->board[victims.stomped_sq];

    // Decrement pawn count
    if (color_of(victims.stomped) == WHITE) {
      p->white_pawn_count--;
    } else if (color_of(victims.stomped) == BLACK) {
      p->black_pawn_count--;
    }

    // Remove stomped piece
    p->key ^= zob[victims.stomped_sq][victims.stomped]; // Clear sq in hash
    p->board[victims.stomped_sq] = 0; // Remove piece from board
    p->key ^= zob[victims.stomped_sq][0]; // Set sq in hash

  }

  // STEP 2: firing laser with zapping
  victims.zapped_sq = fire(p);

  // Check for zapping
  if (victims.zapped_sq) {
    // Zapped
    victims.zapped = p->board[victims.zapped_sq];

    // Decrement pawn count
    if (color_of(victims.zapped) == WHITE) {
      p->white_pawn_count--;
    } else if (color_of(victims.zapped) == BLACK) {
      p->black_pawn_count--;
    }

    // Remove zapped piece
    p->key ^= zob[victims.zapped_sq][victims.zapped];  // Clear sq in hash
    p->board[victims.zapped_sq] = 0;                   // Remove piece from board
    p->key ^= zob[victims.zapped_sq][0];               // Set sq in hash

  } else if (USE_KO &&
             !victims.stomped &&
             (p->key == (old_key ^ zob_color))) {
    // No victims & board didn't change; return KO
    return KO();
  }

  return victims;
}

void undo_move(position_t *p, victims_t victims, move_t mv) {
  tbassert(mv != 0, "mv was zero.\n");

  // STEP 1: put any zapped and stomped pieces back on board

  // Check for KO
  if (is_KO(victims)) {
    // KO --- nothing actually changed
    return;
  } else {

    // Check for zapping
    if (victims.zapped) {
      // Zapped

      // Get square
      square_t zapped_sq = victims.zapped_sq;

      // Increment pawn count
      if (color_of(victims.zapped) == WHITE) {
        p->white_pawn_count++;
      } else if (color_of(victims.zapped) == BLACK) {
        p->black_pawn_count++;
      }

      // Add zapped piece back
      p->key ^= zob[zapped_sq][p->board[zapped_sq]]; // Clear sq in hash
      p->board[zapped_sq] = victims.zapped; // Add piece to sq on board
      p->key ^= zob[zapped_sq][victims.zapped]; // Add piece to sq in hash
    }

    // Check for stomping
    if (victims.stomped) {
      // Stomped

      // Get square
      square_t stomped_sq = victims.stomped_sq;

      // Increment pawn count
      if (color_of(victims.stomped) == WHITE) {
        p->white_pawn_count++;
      } else if (color_of(victims.stomped) == BLACK) {
        p->black_pawn_count++;
      }

      // Add stomped piece back
      p->key ^= zob[stomped_sq][p->board[stomped_sq]]; // Clear sq in hash
      p->board[stomped_sq] = victims.stomped; // Add piece to sq on board
      p->key ^= zob[stomped_sq][victims.stomped]; // Add piece to sq in hash
    }

  }

  // STEP 2: undo piece translation or rotation
  low_level_undo_move(p, mv);

}


// helper function for do_perft
// ply starting with 0

static uint64_t perft_search(position_t *p, int depth, int ply) {
  uint64_t node_count = 0;
  position_t np;
  sortable_move_t lst[MAX_NUM_MOVES];
  int num_moves;
  int i;

  if (depth == 0) {
    return 1;
  }

  num_moves = generate_all(p, lst, true);

  if (depth == 1) {
    return num_moves;
  }

  for (i = 0; i < num_moves; i++) {
    move_t mv = get_move(lst[i]);

    square_t stomped_sq = low_level_make_move(p, &np, mv); // make the move baby!

    if (stomped_sq) {
      tbassert(ptype_of(np.board[stomped_sq]) == PAWN,
               "ptype_of(np.board[stomped_sq]): %d\n",
               ptype_of(np.board[stomped_sq]));

      np.victims.stomped = np.board[stomped_sq];
      np.key ^= zob[stomped_sq][np.victims.stomped]; // remove from board
      np.board[stomped_sq] = 0;
      np.key ^= zob[stomped_sq][0];
    }

    square_t victim_sq = fire(&np); // the guy to disappear

    if (victim_sq != 0) { // hit a piece
      ptype_t typ = ptype_of(np.board[victim_sq]);
      tbassert((typ != EMPTY) && (typ != INVALID), "typ: %d\n", typ);
      if (typ == KING) { // do not expand further: hit a King
        node_count++;
        continue;
      }
      np.victims.zapped = np.board[victim_sq];
      np.key ^= zob[victim_sq][np.victims.zapped]; // remove from board
      np.board[victim_sq] = 0;
      np.key ^= zob[victim_sq][0];
    }

    uint64_t partialcount = perft_search(&np, depth - 1, ply + 1);
    node_count += partialcount;
  }

  return node_count;
}

// help to verify the move generator

void do_perft(position_t *gme, int depth, int ply) {
  fen_to_pos(gme, "");

  for (int d = 1; d <= depth; d++) {
    printf("perft %2d ", d);
    uint64_t j = perft_search(gme, d, 0);
    printf("%" PRIu64 "\n", j);
  }
}

void display(position_t *p) {
  char buf[MAX_CHARS_IN_MOVE];

  printf("\ninfo Ply: %d\n", p->ply);
  printf("info Color to move: %s\n", color_to_str(color_to_move_of(p)));

  square_to_str(p->kloc[WHITE], buf, MAX_CHARS_IN_MOVE);
  printf("info White King: %s, ", buf);
  square_to_str(p->kloc[BLACK], buf, MAX_CHARS_IN_MOVE);
  printf("info Black King: %s\n", buf);

  if (p->last_move != 0) {
    move_to_str(p->last_move, buf, MAX_CHARS_IN_MOVE);
    printf("info Last move: %s\n", buf);
  } else {
    printf("info Last move: NULL\n");
  }

  for (rnk_t r = BOARD_WIDTH - 1; r >= 0; --r) {
    printf("\ninfo %1d  ", r);
    for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
      square_t sq = square_of(f, r);

      tbassert(ptype_of(p->board[sq]) != INVALID,
               "ptype_of(p->board[sq]): %d\n", ptype_of(p->board[sq]));
      /*if (p->blocked[sq]) {
        printf(" xx");
        continue;
      }*/
      if (ptype_of(p->board[sq]) == EMPTY) { // empty square
        printf(" --");
        continue;
      }

      int ori = ori_of(p->board[sq]); // orientation
      color_t c = color_of(p->board[sq]);

      if (ptype_of(p->board[sq]) == KING) {
        printf(" %2s", king_ori_to_rep[c][ori]);
        continue;
      }

      if (ptype_of(p->board[sq]) == PAWN) {
        printf(" %2s", pawn_ori_to_rep[c][ori]);
        continue;
      }
    }
  }

  printf("\n\ninfo    ");
  for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
    printf(" %c ", 'a' + f);
  }
  printf("\n\n");
}

victims_t KO() {

  return ((victims_t) {
    KO_STOMPED, KO_ZAPPED
  });
}

victims_t ILLEGAL() {

  return ((victims_t) {
    ILLEGAL_STOMPED, ILLEGAL_ZAPPED
  });
}

bool is_ILLEGAL(victims_t victims) {
  return (victims.stomped == ILLEGAL_STOMPED) ||
    (victims.zapped == ILLEGAL_ZAPPED);
}

bool victim_exists(victims_t victims) {
  return (victims.stomped > 0) ||
    (victims.zapped > 0);
}
