// Copyright (c) 2015 MIT License by 6.172 Staff

#ifndef SEARCH_H
#define SEARCH_H

#include <stdio.h>
#include <cilk/cilk.h>
#include "./move_gen.h"

// score_t values
#define INF 32700
#define WIN 32000
#define PAWN_VALUE 100

// Enable all optimization tables.
#define ENABLE_TABLES true

// the maximum possible value for score_t type
#define MAX_SCORE_VAL INT16_MAX

#define MAX(a,b) (((a)>(b))?(a):(b))


/*//// Killer moves table and lookup function
//
#define __KMT_dim__ [MAX_PLY_IN_SEARCH][4]  // NOLINT(whitespace/braces)
#define KMT(ply, id) (4 * ply + id)
static move_t killer_reference __KMT_dim__;  // up to 4 killers

// Best move history table and lookup function
// Format: best_move_history[color_t][piece_t][square_t][orientation]
#define __BMH_dim__ [2][6][ARR_SIZE][NUM_ORI]  // NOLINT(whitespace/braces)
#define BMH(color, piece, square, ori)                             \
    (color * 6 * ARR_SIZE * NUM_ORI + piece * ARR_SIZE * NUM_ORI + \
     square * NUM_ORI + ori)

static int best_move_history_reference __BMH_dim__;
*/

typedef int16_t score_t;  // Search uses "low res" values

// Main search routines and helper functions
typedef enum searchType {  // different types of search
  SEARCH_ROOT,
  SEARCH_PV,
  SEARCH_SCOUT
} searchType_t;

typedef struct subpvNode {
    struct subpvNode* next;
    move_t value;
} subpvNode;

typedef struct searchNode {
    struct searchNode* parent;
    searchType_t type;
    score_t orig_alpha;
    score_t alpha;
    score_t beta;
    int depth;
    int ply;
    int fake_color_to_move;
    int quiescence;
    int pov;
    int legal_move_count;
    bool abort;
    score_t best_score;
    int best_move_index;
    position_t* position;
    uint64_t key;
    victims_t victims;
    move_t subpv[MAX_PLY_IN_SEARCH];
    //subpvNode* subpv;
} searchNode;

void apply_move(searchNode *node, move_t mv);
void undo_move(searchNode *node, move_t mv);

void init_tics();
void init_abort_timer(double goal_time);
double elapsed_time();
bool should_abort();
void reset_abort();
void init_best_move_history();
move_t get_move(sortable_move_t sortable_mv);
score_t searchRoot(position_t *p, score_t alpha, score_t beta, int depth,
                   int ply, move_t *pv, uint64_t *node_count_serial,
                   FILE *OUT);


inline static void mirror_exchange(sortable_move_t *move_list, int start, int end) {
  while (start < end) {
    sortable_move_t t = move_list[start];
    move_list[start] = move_list[end];
    move_list[end] = t;
    start++;
    end--;
  }
}

inline static void exchange(sortable_move_t *move_list, int start, int mid, int end) {
  mirror_exchange(move_list, start, mid);
  mirror_exchange(move_list, mid+1, end);
  
  mirror_exchange(move_list, start, end);
}


inline static int binary_search (sortable_move_t val, sortable_move_t *move_list, int start, int end) {
  int low = start;
  int high = MAX(start,end+1);
  while (low < high) {
    int mid = (low + high)/2;
    if ( val <= move_list[mid]) high = mid;
    else low = mid+1;
  }
  return high;
}
inline static void merge(sortable_move_t *move_list, int start, int mid, int end) {
  int len1 = mid - start + 1;
  int len2 = end - mid;
  if (len1 >= len2) {
    if (len2 <= 0) return;
    int q1 = (start + mid)/2;
    int q2 = binary_search(move_list[q1], move_list, mid+1, end);
    int q3 = q1 + (q2 - mid - 1);
    exchange(move_list, q1, mid, q2-1);
    merge(move_list, start, q1-1, q3-1);
    merge(move_list, q3+1, q2-1, end);
    
  }
  else {
    if (len1 <= 0) return;
    int q1 = (mid + 1 + end)/2;
    int q2 = binary_search(move_list[q1], move_list, start, mid);
    int q3 = q2 + (q1 - mid - 1);
    exchange(move_list, q2, mid, q1);
    merge(move_list, start, q2-1, q3-1);
    merge(move_list, q3+1, q1, end);
    
  }
}
void sort_incremental_full(sortable_move_t *move_list, int num_of_moves); 
inline static void parallel_merge_sort(sortable_move_t *move_list, int start, int end) {

  if ( end <= start) return;
  if (end-start < 100) {
    sort_incremental_full(move_list+start, end-start+1);

  }
  int mid = ((start + end)/2);
  parallel_merge_sort(move_list, start, mid);
  cilk_spawn parallel_merge_sort(move_list, mid+1, end);
  cilk_sync;
  merge(move_list, start, mid, end);
}
#endif  // SEARCH_H
