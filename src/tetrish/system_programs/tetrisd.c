#include "tetrish/tetrisd.h"

#include <stdio.h>

/*--------------------------Internal prototypes-------------------------------*/

/*--------------------------Internal prototypes-------------------------------*/

// zero all values in tetrisboard struct
void initialise_tetrisboard(tetrisboard *b){
	memset(b,0,sizeof(tetrisboard));
}

void initialise_tetrisd(tetrisd_server *s) {
	// 

	initialise_raft_sm(s->raft, );
}

void printboard(tetrisboard *b){
	for(int i=0;i<18;++i){
		puts(well+i);
	}
}


int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	printf("Test tetrisd \n");

	tetrisd_server server;

	initialise_tetrisd(&server);

	return 0;
}
