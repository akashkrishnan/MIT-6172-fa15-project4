import numpy as np
import sys, subprocess

k1_ori = ['NN','EE','SS','WW']
k2_ori = ['nn','ee','ss','ww']

def generate_fen(pos1, pos2, SIZE=10):
    empty_row = ["%d" % (SIZE)]
    king1_ori = k1_ori[pos1[2]]
    king2_ori = k2_ori[pos2[2]]

    if pos1[1] != pos2[1]:
        
        front1 = str(pos1[0])
        end1 = str(SIZE - 1 - pos1[0])
        if front1 == "0": front1 = ""
        if end1 == "0": end1 = ""
        king1_row = front1 + king1_ori + end1
        
        front2 = str(pos2[0])
        end2 = str(SIZE - 1 - pos2[0])
        if front2 == "0": front2 = ""
        if end2 == "0": end2 = ""
        king2_row = front2 + king2_ori + end2

        first_king = king1_row
        second_king = king2_row
        if pos1[1] < pos2[1]:
            first_king = king2_row
            second_king = king1_row

        fen = empty_row * (SIZE - 1 - max(pos1[1], pos2[1]))
        fen.append(first_king)
        fen.extend(empty_row * (abs(pos1[1] - pos2[1]) - 1))
        fen.append(second_king)
        fen.extend(empty_row * (min(pos1[1], pos2[1])))

    else:

        first_king = king1_ori
        second_king = king2_ori
        if pos1[0] > pos2[0]:
            first_king = king2_ori
            second_king = king1_ori
        
        front = str(min(pos1[0],pos2[0]))
        middle = str(abs(pos1[0] - pos2[0]) - 1)
        end = str(SIZE - 1 - max(pos1[0], pos2[0]))
        if front == "0": front = ""
        if middle == "0": middle = ""
        if end == "0": end = ""
        king_row = front + first_king + middle + second_king + end

        fen = empty_row * (SIZE - 1 - max(pos1[1], pos2[1]))
        fen.append(king_row)
        fen.extend(empty_row * (min(pos1[1], pos2[1])))

    return "/".join(fen) + " W"

def get_best_move(fen, DEPTH=10):
    p = subprocess.Popen('./leiserchess', stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    p.stdin.write("position fen %s\n" % fen)
    p.stdin.write("go depth %d\n" % DEPTH)
    p.stdin.write("quit\n")
    out = p.communicate()[0].split("\n")
    score = int(out[-3].split(" ")[3])
    move = out[-2].split(" ")[-1]
    if score == 32000: return move
    else: return None

def get_all_best_moves(SIZE=10):
    f = open('endgame_book.csv', 'w+')
    for k1 in np.ndindex((SIZE, SIZE, 4)):
        for k2 in np.ndindex((SIZE, SIZE, 4)):
            if k1[0] == k2[0] and k1[1] == k2[1]:
      	        continue;
            else:
                fen = generate_fen(k1, k2, SIZE)
                move = get_best_move(fen)
                if move:
                    print "%d,%d,%d,%d,%d,%d,%s" % (k1[0], k1[1], k1[2], k2[0], k2[1], k2[2], move)
                    f.write("%d,%d,%d,%d,%d,%d,%s\n" % (k1[0], k1[1], k1[2], k2[0], k2[1], k2[2], move))

if __name__ == "__main__":
    get_all_best_moves(10)

