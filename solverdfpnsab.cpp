
#include "solver.h"

void Solver::solve_dfpnsab(const Board & board, double time, uint64_t memlimit){
	reset();

	if(board.won() >= 0){
		outcome = board.won();
		return;
	}

	Timer timer = Timer(time, bind(&Solver::timedout, this));
	int starttime = time_msec();

	int turn = board.toplay();
	int otherturn = (turn == 1 ? 2 : 1);

	int ret1 = run_dfpnsab(board, otherturn, memlimit);

	if(ret1 == 1){ //win
		outcome = turn;
	}else{
		int ret2 = run_dfpnsab(board, turn, memlimit);

		if(ret2 == -1){
			outcome = otherturn; //loss
		}else{
			if(ret1 == -1 && ret2 == 1) outcome = 0;          //tie
			if(ret1 == -1 && ret2 == 0) outcome = -otherturn; //loss or tie
			if(ret1 ==  0 && ret2 == 1) outcome = -turn;      //win or tie
			if(ret1 ==  0 && ret2 == 0) outcome = -3;         //unknown
		}
	}

	fprintf(stderr, "Finished in %d msec\n", time_msec() - starttime);
}

int Solver::run_dfpnsab(const Board & board, int ties, uint64_t memlimit){ //1 = win, 0 = unknown, -1 = loss
	assignties = ties;

	if(root) delete root;
	root = new PNSNode(0, 0, 1);
	nodesremain = memlimit*1024*1024/sizeof(PNSNode);

	bool mem = true;
	while(mem && !timeout && root->phi != 0 && root->delta != 0)
		mem = dfpnsab(board, root, 0, INF32/2, INF32/2);

	if(!mem)
		fprintf(stderr, "Ran out of memory\n");

	if(root->phi == 0){
		for(int i = 0; i < root->numchildren; i++)
			if(root->children[i].delta == 0)
				bestmove = root->children[i].move;
		return 1;
	}
	if(root->delta == 0)
		return -1;
	return 0;
}

bool Solver::dfpnsab(const Board & board, PNSNode * node, int depth, uint32_t tp, uint32_t td){
	if(depth > maxdepth)
		maxdepth = depth;

	if(node->numchildren == 0){
		if(nodesremain <= 0)
			return false;
	
		nodesremain -= node->alloc(board.movesremain());
		nodes += board.movesremain();

		int i = 0;
		for(Board::MoveIterator move = board.moveit(); !move.done(); ++move){
			Board next = board;
			next.move(*move);
/*
			node->children[i] = PNSNode(*move).abval((next.won() > 0) + (next.won() >= 0), (board.toplay() == assignties));
/*/					
			uint64_t prevnodes = nodes;

			int abval = -negamax(next, 1, -2, 2); //higher depth goes farther but takes longer, depth 1 seems to be best

			node->children[i] = PNSNode(*move).abval(abval, (board.toplay() == assignties), 1 + int(nodes - prevnodes));
//*/
			i++;
		}

		updatePDnum(node);

		return true;
	}

	bool mem;
	do{
		PNSNode * c1 = node->children;
		PNSNode * c2 = node->children;
		for(PNSNode * i = node->children; i != node->children + node->numchildren; i++){
			if(i->delta <= c1->delta){
				c2 = c1;
				c1 = i;
			}
		}

		Board next = board;
		next.move(c1->move);
		
		uint32_t tpc = min(INF32/2, (td + c1->phi - node->delta));
		uint32_t tdc = min(tp, c2->delta + 1);

//		printf("depth: %i, tp: %i, td: %i, tpc: %i, tdc: %i\n", depth, tp, td, tpc, tdc);

		mem = dfpnsab(next, c1, depth + 1, tpc, tdc);

		if(c1->phi == 0 || c1->delta == 0)
			nodesremain += c1->dealloc();

		updatePDnum(node);
	}while(!timeout && mem && node->phi < tp && node->delta < td);

	return mem;
}

