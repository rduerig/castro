

/*
Compute a rough lower bound on the number of additional moves needed to win given this position

Done using floodfills from each edge/corner for each side

Increase distance when crossing an opponent virtual connection?
Decrease distance when crossing your own virtual connection?
*/

#ifndef _LBDIST_H_
#define _LBDIST_H_

#include <queue>
using namespace std;

#include "board.h"
#include "move.h"

class LBDists {
	struct MoveDist {
		Move pos;
		int dist;

		MoveDist() { }
		MoveDist(Move p, char d) : pos(p), dist(d) { }
		MoveDist(int x, int y, int d) : pos(Move(x,y)) , dist(d) { }
		bool operator < (const MoveDist & other) const { return dist > other.dist; }
		void print() const {
			printf("%i,%i: %i\n", pos.x, pos.y, dist);
		}
	};

	int dists[12][2][361]; //[edge/corner][player][cell]
	priority_queue<MoveDist> Q;
	Board * board;

	int & dist(int edge, int player, int i)          { return dists[edge][player-1][i]; }
	int & dist(int edge, int player, const Move & m) { return dist(edge, player, board->xy(m)); }
	int & dist(int edge, int player, int x, int y)   { return dist(edge, player, board->xy(x, y)); }

	void init(int x, int y, int edge, int player){
		if(board->get(x, y) != 3 - player){
			Q.push(MoveDist(x, y, 0));
			dist(edge, player, x, y) = 0;
		}
	}

public:

	LBDists() : board(NULL) {}
	LBDists(Board * b) { run(b); }

	void run(Board * b) {
		board = b;

		for(int i = 0; i < 12; i++)
			for(int j = 0; j < 2; j++)
				for(int k = 0; k < 361; k++)
					dists[i][j][k] = 1000; //far far away!

		int m = board->get_size()-1, e = board->get_size_d()-1;

	//corner 0
		init(0, 0, 0, 1); flood(0, 1);
		init(0, 0, 0, 2); flood(0, 2);
	//corner 1
		init(m, 0, 1, 1); flood(1, 1);
		init(m, 0, 1, 2); flood(1, 2);
	//corner 2
		init(e, m, 2, 1); flood(2, 1);
		init(e, m, 2, 2); flood(2, 2);
	//corner 3
		init(e, e, 3, 1); flood(3, 1);
		init(e, e, 3, 2); flood(3, 2);
	//corner 4
		init(m, e, 4, 1); flood(4, 1);
		init(m, e, 4, 2); flood(4, 2);
	//corner 5
		init(0, m, 5, 1); flood(5, 1);
		init(0, m, 5, 2); flood(5, 2);

	//edge 0
		for(int x = 1; x < m; x++)   { init(x,   0, 6,  1); } flood(6, 1);
		for(int x = 1; x < m; x++)   { init(x,   0, 6,  2); } flood(6, 2);
	//edge 1
		for(int y = 1; y < m; y++)   { init(m+y, y, 7,  1); } flood(7, 1);
		for(int y = 1; y < m; y++)   { init(m+y, y, 7,  2); } flood(7, 2);
	//edge 2
		for(int y = m+1; y < e; y++) { init(e,   y, 8,  1); } flood(8, 1);
		for(int y = m+1; y < e; y++) { init(e,   y, 8,  2); } flood(8, 2);
	//edge 3
		for(int x = m+1; x < e; x++) { init(x,   e, 9,  1); } flood(9, 1);
		for(int x = m+1; x < e; x++) { init(x,   e, 9,  2); } flood(9, 2);
	//edge 4
		for(int x = 1; x < m; x++)   { init(x, m+x, 10, 1); } flood(10, 1);
		for(int x = 1; x < m; x++)   { init(x, m+x, 10, 2); } flood(10, 2);
	//edge 5
		for(int y = 1; y < m; y++)   { init(0,   y, 11, 1); } flood(11, 1);
		for(int y = 1; y < m; y++)   { init(0,   y, 11, 2); } flood(11, 2);
	}

	void flood(int edge, int player){
		int otherplayer = 3 - player;

		while(!Q.empty()){
			MoveDist cur = Q.top(); Q.pop();
			int colour = board->get(cur.pos);

			for(int i = 0; i < 6; i++){
				MoveDist next(cur.pos + neighbours[i], cur.dist+1);

				if(board->onboard(next.pos) && board->get(next.pos) != otherplayer){
					if(colour == player && board->get(next.pos) == player)
						next.dist--;
					
					if( dist(edge, player, next.pos) > next.dist){
						dist(edge, player, next.pos) = next.dist;
						Q.push(next);
					}
				}
			}
		}
	}

	int get(Move pos){ return min(get(board->xy(pos), 1),  get(board->xy(pos), 2)); }
	int get(int pos, int player){

		int list[6];
		for(int i = 0; i < 6; i++)
			list[i] = dist(i, player, pos);
		partialsort(list, 2);
		int corners = list[0] + list[1];

		for(int i = 6; i < 12; i++)
			list[i-6] = dist(i, player, pos);
		partialsort(list, 3);
		int edges = list[0] + list[1] + list[2];

		return min(corners, edges);
	}

	//partially sort the list with selection sort
	int partialsort(int * list, int max){
		for(int i = 0; i < max; i++){
			int mini = i;
			
			for(int j = i+1; j < 6; j++)
				if(list[mini] > list[j])
					mini = j;
			
			if(mini != i){
				int t = list[i];
				list[i] = list[mini];
				list[mini] = t;
			}
		}
	}
};

#endif
