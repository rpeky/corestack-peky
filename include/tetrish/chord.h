#ifndef TETRISH_CHORD_H
#define TETRISH_CHORD_H

#include <stdint.h>

// let's make the number of nodes small first and unsigned
// in chord the positions are always positive and moduloed

// modify this M_BITS to increase size!!
#define M_BITS 3u
#define KEYSPACE (1u << M_BITS)


typedef uint8_t chord_id_t;

typedef struct chord_peer{
	chord_id_t id;
	// maybe store ip and port seperately or as a string
} chord_peer;

typedef struct finger_entry{
	// chord interval values
	chord_id_t start;
	chord_id_t end;

	// successor i
	chord_peer successor;
} finger_entry;

typedef struct chord_node{
	chord_id_t id;

	// for anticlockwise jump see 4.4 Node Join
	chord_peer predecessor;

	// next live node location
	chord_peer successor;

	// finger table -> as many entries as bits
	finger_entry fingers[M_BITS];
} chord_node;

chord_id_t finger_start(chord_id_t chonode_id, chord_id_t finger);
chord_id_t finger_interval_end(chord_id_t node_id, chord_id_t finger);

chord_peer find_successor_linear(chord_id_t key, const chord_peer *peers, 
		unsigned peer_count);

void calculate_fingertable(chord_node *node);
void calculate_fingertable_known_nodes(chord_node *node,
		const chord_peer *peers,
		unsigned peer_count);
#endif
