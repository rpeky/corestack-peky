#include <stdio.h>

void print_options() {
	printf("1 - Single Player\n");
	printf("2 - Lobbies\n");
	printf("3 - Server Details\n");
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	print_options();
	return 0;
}
