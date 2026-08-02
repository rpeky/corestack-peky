#include "tetrish/tetrisd.h"

/*--------------------------Internal prototypes-------------------------------*/

/*--------------------------Internal prototypes-------------------------------*/

// zero all values in tetrisboard struct
void initialise_tetrisboard(tetrisboard *b) {
	// zero the struct
	memset(b, 0, sizeof(*b));

	// give the well empty characters
	memset(b->well, ' ', sizeof(b->well));

	// place boarders for well
	// bottom border
	memset(b->well[HEIGHT - 1], '-', WIDTH * sizeof(char));
	// side border
	for (size_t row = 0; row < HEIGHT - 1; ++row) {
		b->well[row][0] = 124;
		b->well[row][WIDTH - 1] = 124;
	}
}

void initialise_tetrisd(tetrisd_server *s) {
	//
	(void)s;

	// initialise_raft_sm(s->raft, );
}

void printboard(tetrisboard *b) {
	for (size_t row = 0; row < HEIGHT; ++row) {
		// write row by row
		fwrite(b->well[row], sizeof(b->well[row][0]), WIDTH, stdout);
		putchar('\n');
	}
}

void boarddebug(tetrisboard *b) {
	printf("Board state: \n");
	printboard(b);

	printf("tetrominos hold value: %d\n", b->hold);
	printf("board score: %zu\n", b->score);
}

// Movement
void tetris_tick() {
}

bool tetris_move_left(tetrisboard *b) {
	(void)b;
	return false;
}

bool tetris_move_right(tetrisboard *b) {
	(void)b;
	return false;
}

bool tetris_soft_drop(tetrisboard *b) {
	(void)b;
	return false;
}

bool tetris_hard_drop(tetrisboard *b) {
	(void)b;
	return false;
}

bool tetris_rotate_clockwise(tetrisboard *b) {
	(void)b;
	return false;
}

bool tetris_rotate_anticlockwise(tetrisboard *b) {
	(void)b;
	return false;
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	printf("Test tetrisd \n");

	tetrisd_server server;

	initialise_tetrisd(&server);

	tetrisboard sampleboard;
	initialise_tetrisboard(&sampleboard);

	boarddebug(&sampleboard);

	// set temp values
	sampleboard.hold = Z;
	sampleboard.score = 123;
	boarddebug(&sampleboard);

	return 0;
}
