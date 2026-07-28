#ifndef TETRISH_TETRISD_H
#define TETRISH_TETRISD_H

#include "chord.h"
#include "raft.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define HEIGHT 19
#define WIDTH 12

/*
	I
	[][][][]

	J
	[]
	[][][]

	L
	    []
	[][][]

	O
	[][]
	[][]

	S
	  [][]
	[][]

	T
	  []
	[][][]

	Z
	[][]
	  [][]
*/

enum tetrominos {
	NONE,
	I,
	J,
	L,
	O,
	S,
	T,
	Z
};

// board should be inverted
typedef struct tetrisboard {
	char well[HEIGHT][WIDTH];
	enum tetrominos hold;
	size_t score;
} tetrisboard;

typedef struct tetrisd_room {
	size_t player_count;
	tetrisboard boards[32];
} tetrisd_room;

typedef struct tetrisd_server {
	chord_node chord;
	raft_node raft;
} tetrisd_server;

#endif
