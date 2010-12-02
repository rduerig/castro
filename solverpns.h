
#ifndef _SOLVERPNS_H_
#define _SOLVERPNS_H_

#include <stdint.h>

#include "solver.h"
#include "compacttree.h"
#include "lbdist.h"
#include "log.h"


class SolverPNS : public Solver {
	static const uint32_t LOSS  = (1<<30)-1;
	static const uint32_t DRAW  = (1<<30)-2;
	static const uint32_t INF32 = (1<<30)-3;
public:

	struct PNSNode {
		uint32_t phi, delta;
		Move move;
		CompactTree<PNSNode>::Children children;

		PNSNode() { }
		PNSNode(int x, int y,   int v = 1)     : phi(v), delta(v), move(Move(x,y)) { }
		PNSNode(const Move & m, int v = 1)     : phi(v), delta(v), move(m)         { }
		PNSNode(int x, int y,   int p, int d)  : phi(p), delta(d), move(Move(x,y)) { }
		PNSNode(const Move & m, int p, int d)  : phi(p), delta(d), move(m)         { }

		PNSNode(const PNSNode & n) { *this = n; }
		PNSNode & operator = (const PNSNode & n){
			if(this != & n){ //don't copy to self
				//don't copy to a node that already has children
				assert(children.empty());

				phi = n.phi;
				delta = n.delta;
				move = n.move;
				//don't copy the children
			}
			return *this;
		}

		~PNSNode(){
			assert(children.empty());
		}

		PNSNode & abval(int outcome, int toplay, int assign, int value = 1){
			if(assign && (outcome == 1 || outcome == -1))
				outcome = (toplay == assign ? 2 : -2);

			if(     outcome ==  0)   { phi = value; delta = value; }
			else if(outcome ==  2)   { phi = LOSS;  delta = 0;     }
			else if(outcome == -2)   { phi = 0;     delta = LOSS; }
			else /*(outcome 1||-1)*/ { phi = 0;     delta = DRAW; }
			return *this;
		}

		bool terminal(){ return (phi == 0 || delta == 0); }

		unsigned int size() const {
			unsigned int num = children.num();

			for(PNSNode * i = children.begin(); i != children.end(); i++)
				num += i->size();

			return num;
		}

		void swap_tree(PNSNode & n){
			children.swap(n.children);
		}

		unsigned int alloc(unsigned int num, CompactTree<PNSNode> & ct){
			return children.alloc(num, ct);
		}
		unsigned int dealloc(CompactTree<PNSNode> & ct){
			unsigned int num = 0;

			for(PNSNode * i = children.begin(); i != children.end(); i++)
				num += i->dealloc(ct);
			num += children.dealloc(ct);

			return num;
		}
	};


//memory management for PNS which uses a tree to store the nodes
	uint64_t nodes, memlimit;
	CompactTree<PNSNode> ctmem;

	int   ab; // how deep of an alpha-beta search to run at each leaf node
	bool  df; // go depth first?
	float epsilon; //if depth first, how wide should the threshold be?
	int   ties;    //which player to assign ties to: 0 handle ties, 1 assign p1, 2 assign p2
	bool  lbdist;

	PNSNode root;
	LBDists dists;

	SolverPNS() {
		ab = 1;
		df = true;
		epsilon = 0.25;
		ties = 0;
		lbdist = false;

		reset();

		set_memlimit(100*1024*1024);
	}

	~SolverPNS(){
		root.dealloc(ctmem);
		ctmem.compact();
	}

	void reset(){
		outcome = -3;
		maxdepth = 0;
		nodes_seen = 0;
		bestmove = Move(M_UNKNOWN);

		timeout = false;
	}

	void set_board(const Board & board, bool clear = true){
		rootboard = board;
		rootboard.setswap(false);
		reset();
		if(clear)
			clear_mem();
	}
	void move(const Move & m){
		rootboard.move(m, true, true);
		reset();


		uint64_t nodesbefore = nodes;

		PNSNode child;

		for(PNSNode * i = root.children.begin(); i != root.children.end(); i++){
			if(i->move == m){
				child = *i;          //copy the child experience to temp
				child.swap_tree(*i); //move the child tree to temp
				break;
			}
		}

		nodes -= root.dealloc(ctmem);
		root = child;
		root.swap_tree(child);

		if(nodesbefore > 0)
			logerr(string("PNS Nodes before: ") + to_str(nodesbefore) + ", after: " + to_str(nodes) + ", saved " + to_str(100.0*nodes/nodesbefore, 1) + "% of the tree\n");

		assert(nodes == root.size());
		
		if(nodes == 0)
			clear_mem();
	}

	void set_memlimit(uint64_t lim){
		memlimit = lim;
	}

	void clear_mem(){
		reset();
		root.dealloc(ctmem);
		ctmem.compact();
		root = PNSNode(0, 0, 1);
		nodes = 0;
	}

	void solve(double time);

//basic proof number search building a tree
	void run_pns();
	bool pns(const Board & board, PNSNode * node, int depth, uint32_t tp, uint32_t td);

//update the phi and delta for the node
	bool updatePDnum(PNSNode * node);

//remove all the leaf nodes to free up some memory
	bool garbage_collect(PNSNode * node);
};

#endif

