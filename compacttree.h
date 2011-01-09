


#ifndef _COMPACTTREE_H_
#define _COMPACTTREE_H_

#include <new>
#include <cstring> //for memmove
#include <stdint.h>
#include <cassert>
#include "thread.h"

/* CompactTree is a Tree of Nodes. It malloc's one chunk at a time, and has a very efficient allocation strategy.
 * It maintains a freelist of empty segments, but never assigns a segment to a smaller amount of memory,
 * completely avoiding fragmentation, but potentially being having empty space in sizes that are no longer popular
 * Since it maintains forward and backward pointers within the tree structure, it can move nodes around,
 * compacting the empty space and freeing it back to the OS. It can scan memory since it is a contiguous block
 * of memory with no fragmentation.
 */
template <class Node> class CompactTree {
	static const unsigned int CHUNK_SIZE = 16*1024*1024;
	static const unsigned int MAX_NUM = 300; //maximum amount of Node's to allocate at once, needed for size of freelist

	struct Data {
		uint32_t    header;   //sanity check value, 0 means it's unused
		uint16_t    capacity; //number of Node's worth of memory to follow
		uint16_t    used;     //number of children to follow that are actually used, num <= capacity
		//sizes are chosen such that they add to a multiple of word size on 32bit and 64bit machines.

		union {
			Data ** parent;  //pointer to the Data* in the parent Node that references this Data instance
			Data *  nextfree; //next free Data block of this size when in the free list
		};
		Node        children[0]; //array of Nodes, runs past the end of the data block

		Data(unsigned int n, Data ** p) : capacity(n), used(n), parent(p) {
			header = ((unsigned long)this >> 2) & 0xFFFFFF;
			if(header == 0) header = 0xABCDF3;

			for(Node * i = begin(), * e = end(); i != e; ++i)
				new(i) Node(); //call the constructors
		}

		~Data(){
			for(Node * i = begin(), * e = end(); i != e; ++i)
				i->~Node();
			header = 0;
		}

		Node * begin(){
			return children;
		}

		Node * end(){
			return children + used;
		}

		//not thread safe, so only use in child creation or garbage collection
		//useful if you allocated too much memory, then realized you didn't need it all, or
		//to reduce the size once you realize some nodes are no longer needed, like if they're proven a loss and can be ignored
		//shrink will simply remove the last few Nodes, so make sure to reorder the nodes before calling shrink
		//allows compact() to move the Data segment to a location where capacity matches used again to save space
		int shrink(int n){
			assert(n > 0 && n <= capacity && n <= used);
			for(Node * i = children + n, * e = children + used; i != e; ++i)
				i->~Node();
			int diff = used - n;
			used = n;
			return diff;
		}

		//make sure the parent points back to the same place
		bool parent_consistent(){
			return (header == (*parent)->header);
		}

		void move(Data * s){
			assert(header > 0); //don't move an empty Data segment
			assert(*parent == s); //my parent points to my old location

			//update my parent with my new location
			*parent = this;

			//make sure the parent points back to the same place
			assert(parent_consistent());

			//update my children
			for(Node * i = begin(), * e = end(); i != e; ++i){
				if(i->children.data){
					i->children.data->parent = &(i->children.data);
					assert(i->children.data->parent_consistent());
				}
			}
		}
	};

public:
	class Children {
		static const int LOCK = 1; //must be cast to (Data *) at usage point
		Data * data;
		friend class Data;

	public:
		typedef Node * iterator;
		Children() : data(NULL) { }
		~Children() { assert(data == NULL); }

		bool lock()   { return CAS(data, (Data *) NULL, (Data *) LOCK); }
		bool unlock() { return CAS(data, (Data *) LOCK, (Data *) NULL); }

		unsigned int alloc(unsigned int n, CompactTree & ct){
			assert(data == NULL);
			data = ct.alloc(n, &data);
			return n;
		}
		void neuter(){
			data = NULL;
		}
		unsigned int dealloc(CompactTree & ct){
			Data * t = data;
			int n = 0;
			if(t && CAS(data, t, (Data*)NULL)){
				n = t->used;
				ct.dealloc(t);
			}
			return n;
		}
		void swap(Children & other){
			//swap data pointer
			Data * temp;
			temp = data;
			data = other.data;
			other.data = temp;
			temp = NULL;

			//update parent pointer
			if(data > (Data*)LOCK)
				data->parent = &data;
			if(other.data > (Data*)LOCK)
				other.data->parent = &(other.data);
		}
		int shrink(int n){
			return data->shrink(n);
		}
		unsigned int num() const {
			return (data > (Data *) LOCK ? data->used : 0);
		}
		bool empty() const {
			return num() == 0;
		}
		Node & operator[](unsigned int offset){
			assert(data > (Data *) LOCK);
			assert(offset >= 0 && offset < data->used);
			return data->children[offset];
		}
		Node * begin() const {
			if(data > (Data *) LOCK)
				return data->begin();
			return NULL;
		}
		Node * end() const {
			if(data > (Data *) LOCK)
				return data->end();
			return NULL;
		}
	};

private:
	struct Chunk {
		Chunk *  next; //linked list of chunks
		uint32_t id;   //number of chunks before this one
		uint32_t capacity; //in bytes
		uint32_t used;     //in bytes
		char *   mem;  //actual memory

		Chunk()               : next(NULL), id(0), capacity(0), used(0), mem(NULL) { }
		Chunk(unsigned int c) : next(NULL), id(0), capacity(0), used(0), mem(NULL) { alloc(c); }
		~Chunk() { assert_empty(); }

		void alloc(unsigned int c){
			assert_empty();
			capacity = c;
			used = 0;
			mem = (char*) new uint64_t[capacity / sizeof(uint64_t)]; //use uint64_t instead of char to guarantee alignment
		}
		void dealloc(bool deallocnext = false){
			assert(capacity > 0 && mem != NULL);
			if(deallocnext && next != NULL){
				next->dealloc(deallocnext);
				delete next;
				next = NULL;
			}
			assert(next == NULL);
			capacity = 0;
			used = 0;
			delete[] (uint64_t *)mem;
			mem = NULL;
		}
		void assert_empty(){ assert(capacity == 0 && used == 0 && mem == NULL && next == NULL); }
	};

	Chunk * head, * current;
	unsigned int numchunks;
	Data * freelist[MAX_NUM];
	uint64_t memused;

public:

	CompactTree() {
		for(unsigned int i = 0; i < MAX_NUM; i++)
			freelist[i] = NULL;

		//allocate the first chunk
		head = current = new Chunk(CHUNK_SIZE);
		numchunks = 1;
		memused = 0;
	}
	~CompactTree(){
		head->dealloc(true);
		delete head;
		head = current = NULL;
		numchunks = 0;
	}

	//how much memory is malloced and available for use
	uint64_t memarena() const {
		Chunk * c = current;
		while(c->next)
			c = c->next;
		return ((uint64_t)(c->id+1))*((uint64_t)CHUNK_SIZE);
	}

	//how much memory is in use or in a freelist, a good approximation of real memory usage from the OS perspective
	uint64_t memalloced() const {
		return ((uint64_t)(current->id))*((uint64_t)CHUNK_SIZE) + current->used;
	}

	//how much memory is actually in use by nodes in the tree, plus the overhead of the Data struct
	//uses capacity, so may be inacurate for data segments that were shrunk but haven't been compacted yet
	//Data segments that are in the freelist are not included here
	uint64_t meminuse() const {
		return memused;
	}

	Data * alloc(unsigned int num, Data ** parent){
		assert(num > 0 && num < MAX_NUM);

		unsigned int size = sizeof(Data) + sizeof(Node)*num;
		PLUS(memused, size);

	//check freelist
		while(Data * t = freelist[num]){
			if(CAS(freelist[num], t, t->nextfree))
				return new(t) Data(num, parent);
		}

	//allocate new memory
		while(1){
			Chunk * c = current;
			uint32_t used = c->used;
			if(used + size <= c->capacity){ //if there is room, try to use it
				if(CAS(c->used, used, used+size))
					return new((Data *)(c->mem + used)) Data(num, parent);
				else
					continue;
			}else if(c->next != NULL){ //if there is a next chunk, advance to it and try again
				CAS(current, c, c->next); //CAS to avoid skipping a chunk
				continue;
			}else{ //need to allocate a new chunk
				Chunk * next = new Chunk(CHUNK_SIZE);

				while(1){
					while(c->next != NULL) //advance to the end
						c = c->next;

					next->id = c->id+1;
					if(CAS(c->next, (Chunk *)NULL, next)){ //put it in place
						INCR(numchunks);
						//note that this doesn't move current forward since this may not be the next chunk
						// if there is a race condition where two threads allocate chunks at the same time
						break;
					}
				}
				continue;
			}
		}
		assert(false && "How'd CompactTree::alloc get here?");
		return NULL;
	}
	void dealloc(Data * d){
		assert(d->header > 0 && d->capacity > 0 && d->capacity < MAX_NUM);

		unsigned int size = sizeof(Data) + sizeof(Node)*d->capacity;
		PLUS(memused, -size);

		//call the destructor
		d->~Data();
		d->used = d->capacity;

		//add to the freelist
		while(1){
			Data * t = freelist[d->capacity];
			d->nextfree = t;
			if(CAS(freelist[d->capacity], t, d))
				break;
		}
	}

	//assume this is the only thread running
	//arenasize is how much memory to keep around, as a fraction of current usage
	//  0 frees all extra memory, 1 keeps enough memory allocated to avoid having to call malloc to get back to this level
	//generationsize is how far to go through the list only adding to the freelist, not compacting. This avoids moving memory
	//  at the potential cost of not freeing any space at the end. Values around 1.0 are a waste of time, but 0.2 - 0.6 is good
	void compact(float arenasize = 0, float generationsize = 0){
		assert(arenasize >= 0 && arenasize <= 1);
		assert(generationsize >= 0 && generationsize <= 1);

		memused = 0;

		if(head->used == 0)
			return;

		Chunk      * schunk = head; //source chunk
		unsigned int soff   = 0;    //source offset

		//clear the freelist
		for(unsigned int i = 0; i < MAX_NUM; i++)
			freelist[i] = NULL;

	//add the empty blocks for the first part of the memory space to the freelist to avoid moving so many blocks
		unsigned int generationid = (unsigned int)(generationsize * current->id);
		while(schunk != NULL && schunk->id < generationid){
			//iterate over each Data block
			Data * s = (Data *)(schunk->mem + soff);

			assert(s->capacity > 0 && s->capacity < MAX_NUM);
			int size = sizeof(Data) + sizeof(Node)*s->capacity;

			//add empty blocks to the freelist
			if(s->header == 0){
				s->nextfree = freelist[s->capacity];
				freelist[s->capacity] = s;
			}else{
				memused += size;
			}

			//update source
			soff += size;
			while(schunk && schunk->used <= soff){ //move forward, skip empty chunks
				schunk = schunk->next;
				soff = 0;
			}
		}

		Chunk      * dchunk = schunk; //destination chunk
		unsigned int doff   = soff;   //destination offset

		//iterate over each chunk, moving data blocks to the left to fill empty space
		while(schunk != NULL){
			//iterate over each Data block
			Data * s = (Data *)(schunk->mem + soff);
			assert(s->capacity > 0 && s->capacity < MAX_NUM);

			int ssize = sizeof(Data) + sizeof(Node)*s->capacity; //how much to move the source pointer

			//move from -> to, update parent pointer
			if(s->header != 0){
				assert(s->used > 0 && s->used <= s->capacity);
				int dsize = sizeof(Data) + sizeof(Node)*s->used; //how much to move the dest pointer
				Data * d = NULL;

				//where to move
				while(1){
					if((d = freelist[s->used])){ //allocate off the freelist if possible
						freelist[s->used] = d->nextfree;
						break;
					}else if(doff + dsize <= dchunk->capacity){ //if space, allocate from this chunk
						assert(schunk->id > dchunk->id || (schunk == dchunk && soff >= doff)); //make sure I'm moving left
						d = (Data *)(dchunk->mem + doff);
						doff += dsize;
						break;
					}else{ //otherwise finish this chunk and prepare the next
						dchunk->used = doff;

						//zero out the remainder of the chunk
						for(char * i = dchunk->mem + dchunk->used, * iend = dchunk->mem + dchunk->capacity ; i != iend; i++)
							*i = 0;

						dchunk = dchunk->next;
						doff = 0;
					}
				}

				//move!
				s->capacity = s->used;
				if(s != d){
					memmove(d, s, dsize);
					d->move(s);
				}
				memused += dsize;
			}

			//update source
			soff += ssize;
			while(schunk && schunk->used <= soff){ //move forward, skip empty chunks
				schunk = schunk->next;
				soff = 0;
			}
		}

		//free unused chunks
		Chunk * del = dchunk;
		while(del->next && del->id < arenasize*current->id){
			del = del->next;
			del->used = 0;
		}

		if(del->next != NULL){
			del->next->dealloc(true);
			delete del->next;
			del->next = NULL;
			numchunks = del->id + 1;
		}

		//save used last position
		dchunk->used = doff;
		current = dchunk;

		//zero out the remainder of the chunk
		for(char * i = dchunk->mem + dchunk->used, * iend = dchunk->mem + dchunk->capacity ; i != iend; i++)
			*i = 0;
	}
};

#endif

