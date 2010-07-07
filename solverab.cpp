
#include "solver.h"

void Solver::solve_ab(Board board, double time, int mdepth){
	reset();
	if(board.won() >= 0){
		outcome = board.won();
		return;
	}
	board.setswap(false);

	Timer timer(time, bind(&Solver::timedout, this));
	int starttime = time_msec();

	int turn = board.toplay();

	for(maxdepth = 1; !timeout && maxdepth < mdepth; maxdepth++){
		fprintf(stderr, "Starting depth %d\n", maxdepth);

		int ret = run_negamax(board, maxdepth, -2, 2);

		if(ret){
			if(     ret == -2){ outcome = (turn == 1 ? 2 : 1); bestmove = Move(M_NONE); }
			else if(ret ==  2){ outcome = turn; }
			else /*-1 || 1*/  { outcome = 0; }

			fprintf(stderr, "Finished in %d msec\n", time_msec() - starttime);
			return;
		}
	}
	fprintf(stderr, "Timed out after %d msec\n", time_msec() - starttime);
}

int Solver::run_negamax(const Board & board, const int depth, int alpha, int beta){
	for(Board::MoveIterator move = board.moveit(); !move.done(); ++move){
		nodes_seen++;

		Board next = board;
		next.move(*move);

		int value = -negamax(next, maxdepth - 1, -beta, -alpha);

		if(value > alpha){
			alpha = value;
			bestmove = *move;
		}

		if(alpha >= beta)
			return beta;
	}
	return alpha;
}

int Solver::negamax(Board & board, const int depth, int alpha, int beta){
	if(board.won() >= 0)
		return (board.won() ? -2 : -1);

	if(depth <= 0 || timeout)
		return 0;

	int value, losses = 0;
	static const int lookup[4] = {0, 1, 2, 2};
	for(Board::MoveIterator move = board.moveit(); !move.done(); ++move){
		nodes_seen++;

		if(depth <= 2){
			value = lookup[board.test_win(*move)+1];

			if(board.test_win(*move, 3 - board.toplay()) > 0)
				losses++;
		}else{
			Board next = board;
			next.move(*move);

			value = -negamax(next, depth - 1, -beta, -alpha);
		}

		if(value > alpha)
			alpha = value;

		if(alpha >= beta)
			return beta;
	}
	if(losses >= 2)
		return -2;

	return alpha;
}

