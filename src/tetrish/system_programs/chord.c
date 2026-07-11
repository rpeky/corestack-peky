#include "tetrish/chord.h"
#include <stdio.h>

// Read the Chord paper included in references

// modify type size later if needed
chord_id_t finger_start(uint8_t node_id, uint8_t finger) {
	if (finger < 1 || finger > M_BITS) {
		fprintf(stderr, "Entry must be within 1 <-> %u\n", M_BITS);
		return UINT8_MAX;
	}

	// increase size of node_id in case the value is above keyspace to modulo
	// then typecase back
	return (chord_id_t)(((uint16_t)node_id + (1u << (finger - 1))) % (KEYSPACE));
}

// allow wrap around modulo for end in case it overshoots
chord_id_t finger_interval_end(uint8_t node_id, uint8_t finger) {
	if (finger < 1 || finger > M_BITS) {
		fprintf(stderr, "Entry must be within 1 <-> %u\n", M_BITS);
		return UINT8_MAX;
	}

	// increase size of node_id in case the value is above keyspace to modulo
	// then typecase back
	return (chord_id_t)(((uint16_t)node_id + (1u << (finger))) % (KEYSPACE));
}

void calculate_fingertable(chord_node *node){
	if(node==NULL) return;
	for(unsigned i = 0;i<M_BITS;++i){
		// paper index starts at 1
		unsigned fingeridx = i+1;

		// calculate and set the intervals for the table entry
		node->fingers[i].start = finger_start(node->id, fingeridx);
		node->fingers[i].end= finger_interval_end(node->id, fingeridx);

		// set successor for ith entry
		node->fingers[i].successor = node->successor;
	}
}


chord_peer find_successor_linear(chord_id_t key, const chord_peer *peers, 
				 unsigned peer_count) {
	for (unsigned i = 0; i < peer_count; i++) {
		if (peers[i].id >= key)
			return peers[i];
	}

	/* Wrap around. */
	return peers[0];
}

void calculate_fingertable_known_nodes(chord_node *node,
				  const chord_peer *peers,
				  unsigned peer_count) {
	if (node == NULL || peers == NULL || peer_count == 0)
		return;

	for (unsigned i = 0; i < M_BITS; ++i) {
		unsigned fingeridx = i + 1;

		node->fingers[i].start = finger_start(node->id, fingeridx);
		node->fingers[i].end = finger_interval_end(node->id, fingeridx);
		node->fingers[i].successor =
			find_successor_linear(node->fingers[i].start, peers, peer_count);
	}
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	printf("Test Chord\n");
	uint8_t node_id = 6;

	for (uint8_t finger = 1; finger <= M_BITS; finger++) {
		printf("finger[%u].start = %u |finger[%u].interval = [%u,%u)\n",
	 (unsigned)finger,
	 (unsigned)finger_start(node_id, finger),
	 (unsigned)finger,
	 (unsigned)finger_start(node_id, finger),
	 (unsigned)finger_interval_end(node_id, finger));
	}

	printf("Testing table calculation (single node)\n");
	chord_node node;
	node.id=6;
	node.predecessor.id=6;
	node.successor.id=6;

	calculate_fingertable(&node);

	for (unsigned i = 0; i < M_BITS; i++) {
		printf("i = %u |finger[%u].start = %u | interval = [%u,%u) | successor = %u\n",
	 i + 1,
	 i + 1,
	 (unsigned)node.fingers[i].start,
	 (unsigned)node.fingers[i].start,
	 (unsigned)node.fingers[i].end,
	 (unsigned)node.fingers[i].successor.id);
	}

	printf("Testing table calculation (multi node)\n");
	chord_peer peers[] = {
		{ .id = 0 },
		{ .id = 1 },
		{ .id = 3 },
		{ .id = 6 },
	};

	chord_node node2 = {
		.id = 6,
		.predecessor = { .id = 3 },
		.successor = { .id = 0 },
	};

	calculate_fingertable_known_nodes(&node2, peers, 4);

	for (unsigned i = 0; i < M_BITS; i++) {
		printf("i = %u |finger[%u].start = %u | interval = [%u,%u) | successor = %u\n",
	 i + 1,
	 i + 1,
	 (unsigned)node2.fingers[i].start,
	 (unsigned)node2.fingers[i].start,
	 (unsigned)node2.fingers[i].end,
	 (unsigned)node2.fingers[i].successor.id);
	}
	return 0;
}
