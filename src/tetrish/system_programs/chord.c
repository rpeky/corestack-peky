#include <stdint.h>
#include <stdio.h>

// let's make the number of nodes small first and unsigned
// in chord the positions are always positive and moduloed
#define M_BITS 8u
#define KEYSPACE (1u << M_BITS)

// modify type size later if needed
uint8_t finger_start(uint8_t node_id, uint8_t finger) {
	if (finger < 1 || finger > M_BITS) {
		fprintf(stderr, "Entry must be within 1 <-> %u\n", M_BITS);
		return UINT8_MAX;
	}

	// increase size of node_id in case the value is above keyspace to modulo
	// then typecase back
	return (uint8_t)(((uint16_t)node_id + (1u << (finger - 1))) % (KEYSPACE));
}

// allow wrap around modulo for end in case it overshoots
uint8_t finger_interval_end(uint8_t node_id, uint8_t finger) {
	if (finger < 1 || finger > M_BITS) {
		fprintf(stderr, "Entry must be within 1 <-> %u\n", M_BITS);
		return UINT8_MAX;
	}

	// increase size of node_id in case the value is above keyspace to modulo
	// then typecase back
	return (uint8_t)(((uint16_t)node_id + (1u << (finger))) % (KEYSPACE));
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	printf("Test Chord\n");
	uint8_t node_id = 250;

	for (uint8_t finger = 1; finger <= M_BITS; finger++) {
		printf("finger[%u].start = %u |finger[%u].interval = [%u,%u)\n",
		       (unsigned)finger,
		       (unsigned)finger_start(node_id, finger),
		       (unsigned)finger,
		       (unsigned)finger_start(node_id, finger),
		       (unsigned)finger_interval_end(node_id, finger));
	}
	return 0;
}
