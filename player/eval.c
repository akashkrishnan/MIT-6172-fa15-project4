// Copyright (c) 2015 MIT License by 6.172 Staff

#include "./eval.h"
#include "./hdist_table.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include "./tbassert.h"

// -----------------------------------------------------------------------------
// Evaluation
// -----------------------------------------------------------------------------

typedef int32_t ev_score_t;  // Static evaluator uses "hi res" values

int RANDOMIZE;

int PCENTRAL;
int HATTACK;
int PBETWEEN;
int PCENTRAL;
int KFACE;
int KAGGRESSIVE;
int MOBILITY;
int PAWNPIN;

char laser_map_black[ARR_SIZE];
char laser_map_white[ARR_SIZE];

void laser_map_init() {
  for (int i = 0; i < ARR_SIZE; ++i) {
    laser_map_black[i] = 4;   // Invalid square
    laser_map_white[i] = 4;   // Invalid square
  }
}

float h_attackable = 0;

// Heuristics for static evaluation - described in the google doc
// mentioned in the handout.


// PCENTRAL heuristic: Bonus for Pawn near center of board

double pcentral_lookup_table[10][10];

double pcentral_old(fil_t f, rnk_t r) {
  double df = BOARD_WIDTH/2 - f - 1;
  if (df < 0)  df = f - BOARD_WIDTH/2;
  double dr = BOARD_WIDTH/2 - r - 1;
  if (dr < 0) dr = r - BOARD_WIDTH/2;
  double bonus = 1 - sqrt(df * df + dr * dr) / (BOARD_WIDTH / sqrt(2));
  return bonus;
}

void init_eval_tables() {
  for (fil_t f = 0; f < BOARD_WIDTH; f++) {
    for (rnk_t r = 0; r < BOARD_WIDTH; r++) {
      pcentral_lookup_table[f][r] = pcentral_old(f,r);
    }
  }
}
 

ev_score_t pcentral(fil_t f, rnk_t r) {
  return PCENTRAL * pcentral_lookup_table[f][r];
}


// returns true if c lies on or between a and b, which are not ordered
bool between(int c, int a, int b) {
  bool x = ((c >= a) && (c <= b)) || ((c <= a) && (c >= b));
  return x;
}

// PBETWEEN heuristic: Bonus for Pawn at (f, r) in rectangle defined by Kings at the corners
ev_score_t pbetween(position_t *p, fil_t f, rnk_t r) {
  bool is_between =
      between(f, fil_of(p->kloc[WHITE]), fil_of(p->kloc[BLACK])) &&
      between(r, rnk_of(p->kloc[WHITE]), rnk_of(p->kloc[BLACK]));
  return is_between ? PBETWEEN : 0;
}


// KFACE heuristic: bonus (or penalty) for King facing toward the other King
ev_score_t kface(position_t *p, fil_t f, rnk_t r) {
  square_t sq = square_of(f, r);
  piece_t x = p->board[sq];
  color_t c = color_of(x);
  square_t opp_sq = p->kloc[opp_color(c)];
  int delta_fil = fil_of(opp_sq) - f;
  int delta_rnk = rnk_of(opp_sq) - r;
  int bonus;

  switch (ori_of(x)) {
    case NN:
      bonus = delta_rnk;
      break;

    case EE:
      bonus = delta_fil;
      break;

    case SS:
      bonus = -delta_rnk;
      break;

    case WW:
      bonus = -delta_fil;
      break;

    default:
      bonus = 0;
      tbassert(false, "Illegal King orientation.\n");
  }

  return (bonus * KFACE) / (abs(delta_rnk) + abs(delta_fil));
}

// KAGGRESSIVE heuristic: bonus for King with more space to back
ev_score_t kaggressive_old(position_t *p, fil_t f, rnk_t r) {
  square_t sq = square_of(f, r);
  piece_t x = p->board[sq];
  color_t c = color_of(x);
  tbassert(ptype_of(x) == KING, "ptype_of(x) = %d\n", ptype_of(x));

  square_t opp_sq = p->kloc[opp_color(c)];
  fil_t of = fil_of(opp_sq);
  rnk_t _or = (rnk_t) rnk_of(opp_sq);

  int delta_fil = of - f;
  int delta_rnk = _or - r;

  int bonus = 0;

  if (delta_fil >= 0 && delta_rnk >= 0) {
    bonus = (f + 1) * (r + 1);
  } else if (delta_fil <= 0 && delta_rnk >= 0) {
    bonus = (BOARD_WIDTH - f) * (r + 1);
  } else if (delta_fil <= 0 && delta_rnk <= 0) {
    bonus = (BOARD_WIDTH - f) * (BOARD_WIDTH - r);
  } else if (delta_fil >= 0 && delta_rnk <= 0) {
    bonus = (f + 1) * (BOARD_WIDTH - r);
  }

  return (KAGGRESSIVE * bonus) / (BOARD_WIDTH * BOARD_WIDTH);
}

inline ev_score_t kaggressive(fil_t f, rnk_t r, fil_t otherf, rnk_t otherr) {

  assert(f != otherf);
  assert(r != otherr);
  int bonus = 0;

  if (otherf >= f) {   // delta_fil >= 0 is equivalent to otherf >= f
    bonus = f + 1;
  } else {
    bonus = BOARD_WIDTH - f;
  }

  if (otherr >= r) {   // delta_rnk >= 0 is equivalent to otherr >= r
    bonus *= r + 1;
  } else {
    bonus *= BOARD_WIDTH - r;
  }

  return (KAGGRESSIVE * bonus) / (BOARD_WIDTH * BOARD_WIDTH);
}

// Marks the path of the laser until it hits a piece or goes off the board. 
// Returns the number of unpinned pawns.
//
// p : current board state
// laser_map : end result will be stored here. Every square on the
//             path of the laser is marked with mark_mask
// c : color of king shooting laser
// mark_mask: what each square is marked with
int mark_laser_path(position_t *p, char *laser_map, color_t c,
                     char mark_mask) {
  int pinned_pawns = 0;
  uint8_t total_pawns;
  color_t color = opp_color(c);
  square_t o_king_sq = p->kloc[color];

  if (c == WHITE) { // opposing king pins our pawns 
    total_pawns = p->black_pawn_count;
  }
  else {
    total_pawns = p->white_pawn_count;
  }

  // Fire laser, recording in laser_map
  square_t sq = p->kloc[c];
  int bdir = ori_of(p->board[sq]);
  int beam = beam_of(bdir);

  tbassert(ptype_of(p->board[sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[sq]));
  laser_map[sq] |= mark_mask;

  // we update h_attackable here
  h_attackable = 0;

  while (true) {
    sq += beam;
    laser_map[sq] |= mark_mask;
    h_attackable += hdist_table[sq][o_king_sq];
    tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);

    switch (ptype_of(p->board[sq])) {
      case EMPTY:  // empty square
        break;
      case PAWN:  // Pawn
        if (color_of(p->board[sq]) == color) {
          pinned_pawns += 1;
        }
        bdir = reflect_of(bdir, ori_of(p->board[sq]));
        if (bdir < 0) {  // Hit back of Pawn
          return total_pawns - pinned_pawns;
        }
        beam = beam_of(bdir);
        break;
      case KING:  // King
          return total_pawns - pinned_pawns;
        break;
      case INVALID:  // Ran off edge of board
        return total_pawns - pinned_pawns;
        break;
      default:  // Shouldna happen, man!
        tbassert(false, "Not cool, man.  Not cool.\n");
        break;
    }
  }
}


// PAWNPIN Heuristic: count number of pawns that are pinned by the
//   opposing king's laser --- and are thus immobile.

// int pawnpin(position_t *p, color_t color) {
//   color_t c = opp_color(color);
  
//   int pinned_pawns = 0;
//   uint8_t total_pawns;
//   position_t np = *p;
// 
//   if (color == WHITE) {
//     total_pawns = p->white_pawn_count;
//   }
//   else {
//     total_pawns = p->black_pawn_count;
//   }
//   // Fire laser, recording in laser_map
//   square_t sq = np.kloc[c];
//   int bdir = ori_of(np.board[sq]);
// 
//   tbassert(ptype_of(np.board[sq]) == KING,
//            "ptype: %d\n", ptype_of(np.board[sq]));
//   
//   while (true) {
//     sq += beam_of(bdir);
//     if (color_of(p->board[sq]) == color &&
//         ptype_of(p->board[sq]) == PAWN) {
//         pinned_pawns += 1;
//     }
//     tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);
// 
//     switch (ptype_of(p->board[sq])) {
//       case EMPTY:  // empty square
//         break;
//       case PAWN:  // Pawn
//         bdir = reflect_of(bdir, ori_of(p->board[sq]));
//         if (bdir < 0) {  // Hit back of Pawn
//           return total_pawns - pinned_pawns;
//         }
//         break;
//       case KING:  // King
//         return total_pawns - pinned_pawns;
//         break;
//       case INVALID:  // Ran off edge of board
//         return total_pawns - pinned_pawns;
//         break;
//       default:  // Shouldna happen, man!
//         tbassert(false, "Not cool, man.  Not cool.\n");
//         break;
//     }
//   }
// }

// MOBILITY heuristic: safe squares around king of color color.
int mobility(position_t *p, color_t color) {
  char* laser_map;
  if (color == WHITE) {
    laser_map = laser_map_black;
  } else {
    laser_map = laser_map_white;
  }
  int mobility = 0;
  square_t king_sq = p->kloc[color];
  tbassert(ptype_of(p->board[king_sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[king_sq]));
  tbassert(color_of(p->board[king_sq]) == color,
           "color: %d\n", color_of(p->board[king_sq]));

  if (laser_map[king_sq] == 0) {
    mobility++;
  }
  for (int d = 0; d < 8; ++d) {
    square_t sq = king_sq + dir_of(d);
    if (laser_map[sq] == 0) {
      mobility++;
    }
  }
  return mobility;
}


// Harmonic-ish distance: 1/(|dx|+1) + 1/(|dy|+1)
float h_dist_old(square_t a, square_t b) {
  //  printf("a = %d, FIL(a) = %d, RNK(a) = %d\n", a, FIL(a), RNK(a));
  //  printf("b = %d, FIL(b) = %d, RNK(b) = %d\n", b, FIL(b), RNK(b));
  int delta_fil = abs(fil_of(a) - fil_of(b));
  int delta_rnk = abs(rnk_of(a) - rnk_of(b));
  float x = (1.0 / (delta_fil + 1)) + (1.0 / (delta_rnk + 1));
  //  printf("max_dist = %d\n\n", x);
  return x;
}

float h_dist(square_t a, square_t b) {
  //int df = fil_of(a) - fil_of(b) + 9;
  //int dr = rnk_of(a) - rnk_of(b) + 9;
  //return hdist_table[df][dr];
  return hdist_table[a][b];
}

void hdist_table_generate() {
  int size = 10;
  int size2 = 205;

  float hdist_table[size2][size2];
  memset(&hdist_table, 0, sizeof(hdist_table));

  for (int af = 0; af < size; af++) {
    for (int ar = 0; ar < size; ar++) {
      for (int bf = 0; bf < size; bf++) {
        for (int br = 0; br < size; br++) {
          square_t a = square_of(af, ar);
          square_t b = square_of(bf, br);
          int df = abs(af - bf);
          int dr = abs(ar - br);
          hdist_table[a][b] = (1.0 / (df + 1)) + (1.0 / (dr + 1));
          printf("%d %d %d %d %.6f\n", a, b, df, dr, hdist_table[a][b]);
        }
      }
    }
  }

  FILE *f = fopen("hdist_table.h", "w");
  fprintf(f, "static float hdist_table[%d][%d] = {\n", size2, size2);
  for (int a = 0; a < size2; a++) {
    fprintf(f, "{");
    for (int b = 0; b < size2; b++) {
      fprintf(f, "%.6f,", hdist_table[a][b]);
    }
    fprintf(f, "},\n");
  }
  fprintf(f, "};\n");
  fclose(f);
}

// H_SQUARES_ATTACKABLE heuristic: for shooting the enemy king
int h_squares_attackable(position_t *p, color_t c) {
  char* laser_map;
  if (c == WHITE) {
    laser_map = laser_map_white;
  } else {
    laser_map = laser_map_black;
  }
  square_t o_king_sq = p->kloc[opp_color(c)];
  tbassert(ptype_of(p->board[o_king_sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[o_king_sq]));
  tbassert(color_of(p->board[o_king_sq]) != c,
           "color: %d\n", color_of(p->board[o_king_sq]));

  float h_attackable_temp = 0;
  for (fil_t f = 0; f < BOARD_WIDTH; f++) {
    for (rnk_t r = 0; r < BOARD_WIDTH; r++) {
      square_t sq = square_of(f, r);
      if (laser_map[sq] != 0) {
        h_attackable_temp += h_dist(sq, o_king_sq);
      }
    }
  }
  return h_attackable_temp;
}

// Static evaluation.  Returns score
score_t eval(position_t *p, bool verbose) {
  // seed rand_r with a value of 1, as per
  // http://linux.die.net/man/3/rand_r
  static __thread unsigned int seed = 1;
  // verbose = true: print out components of score
  ev_score_t score[2] = { 0, 0 };
  //  int corner[2][2] = { {INF, INF}, {INF, INF} };
  ev_score_t bonus;

  char buf[MAX_CHARS_IN_MOVE];

  for (fil_t f = 0; f < BOARD_WIDTH; f++) {
    for (rnk_t r = 0; r < BOARD_WIDTH; r++) {
      square_t sq = square_of(f, r);
      piece_t x = p->board[sq];
      color_t c;

      if (verbose) {
        square_to_str(sq, buf, MAX_CHARS_IN_MOVE);
      }

      switch (ptype_of(x)) {
        case EMPTY:
          break;
        case PAWN:
          c = color_of(x);

          // MATERIAL heuristic: Bonus for each Pawn
          bonus = PAWN_EV_VALUE;
          // if (verbose) {
          //  printf("MATERIAL bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
          // }
          score[c] += bonus;

          // PBETWEEN heuristic
          bonus = pbetween(p, f, r);
          // if (verbose) {
          //   printf("PBETWEEN bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
          // }
          score[c] += bonus;

          // PCENTRAL heuristic
          bonus = pcentral(f, r);
          // if (verbose) {
          //   printf("PCENTRAL bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
         //  }
          score[c] += bonus;
          break;

        case KING:
          c = color_of(x);

          // KFACE heuristic
          bonus = kface(p, f, r);
          // if (verbose) {
          //   printf("KFACE bonus %d for %s King on %s\n", bonus,
          //          color_to_str(c), buf);
          // }
          score[c] += bonus;

          // KAGGRESSIVE heuristic
          color_t othercolor = opp_color(c);
          square_t otherking = p->kloc[othercolor];
          fil_t otherf = fil_of(otherking);
          rnk_t otherr = rnk_of(otherking);
          bonus = kaggressive(f, r, otherf, otherr);
          assert(bonus == kaggressive_old(p, f, r));

          // if (verbose) {
          //   printf("KAGGRESSIVE bonus %d for %s King on %s\n", bonus, color_to_str(c), buf);
         //  }
          score[c] += bonus;
          break;
        case INVALID:
          break;
        default:
          tbassert(false, "Jose says: no way!\n");   // No way, Jose!
      }
      laser_map_black[sq] = 0;
      laser_map_white[sq] = 0;
    }
  }
   
  int black_pawns_unpinned = mark_laser_path(p, laser_map_white, WHITE, 1);  // 1 = path of laser with no moves
  
  ev_score_t w_hattackable = HATTACK * h_squares_attackable(p, WHITE);//h_attackable;
  score[WHITE] += w_hattackable;
  // if (verbose) {
  //   printf("HATTACK bonus %d for White\n", w_hattackable);
  // }

  // PAWNPIN Heuristic --- is a pawn immobilized by the enemy laser.
  int b_pawnpin = PAWNPIN * black_pawns_unpinned;
  score[BLACK] += b_pawnpin;

  int b_mobility = MOBILITY * mobility(p, BLACK);
  score[BLACK] += b_mobility;
  // if (verbose) {
  //   printf("MOBILITY bonus %d for Black\n", b_mobility);
  // }

  int white_pawns_unpinned = mark_laser_path(p, laser_map_black, BLACK, 1);  // 1 = path of laser with no moves
  
  ev_score_t b_hattackable = HATTACK * h_squares_attackable(p, BLACK);// h_attackable;
  score[BLACK] += b_hattackable;
  // if (verbose) {
  //   printf("HATTACK bonus %d for Black\n", b_hattackable);
  // }

  int w_mobility = MOBILITY * mobility(p, WHITE);
  score[WHITE] += w_mobility;
  // if (verbose) {
  //   printf("MOBILITY bonus %d for White\n", w_mobility);
  // }
  int w_pawnpin = PAWNPIN * white_pawns_unpinned;
  score[WHITE] += w_pawnpin;


  // score from WHITE point of view
  ev_score_t tot = score[WHITE] - score[BLACK];

  if (RANDOMIZE) {
    ev_score_t  z = rand_r(&seed) % (RANDOMIZE*2+1);
    tot = tot + z - RANDOMIZE;
  }

  if (color_to_move_of(p) == BLACK) {
    tot = -tot;
  }

  return tot / EV_SCORE_RATIO;
}
