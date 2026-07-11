#ifndef TETRISH_TETRISD_H
#define TETRISH_TETRISD_H

#include "raft.h"
#include "chord.h"

typedef struct tetrisd_server{
	chord_node chord;
	raft_node raft;
} tetrisd_server;

#endif
