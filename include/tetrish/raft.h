#ifndef TETRISH_RAFT_H
#define TETRISH_RAFT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

// change this later for more peers
#define RAFT_MAX_PEERS 8
#define RAFT_LOG_CAP 1024

// to show impossible values later (no vote etc)
#define RAFT_NONE UINT64_MAX

typedef int64_t raft_msec_t;

typedef uint64_t raft_node_id_t;
typedef uint64_t raft_index_t;
typedef uint64_t raft_term_t;

// Please read the Ongario Raft Paper included in the references
typedef enum ServerState{
	FOLLOWER,
	CANDIDATE,
	LEADER,
	FAILED
} ServerState;

typedef enum core_cmd_type {
	CORE_CMD_NOOP,
	CORE_CMD_LOBBY_JOIN,
	CORE_CMD_GAME_INPUT,
} core_cmd_type;

typedef struct core_command {
	core_cmd_type type;
	uint64_t room_key;
	uint64_t player_id;
	uint32_t input;
} core_command;

typedef struct LogEntry{
	raft_index_t index;
	raft_term_t term;
	core_command command;
} LogEntry;

// since there won't be so many peers im just gonna wing it w an array
// laze implement another hashmap
typedef struct raft_peer {
	raft_node_id_t id;
	char host[64];
	uint16_t port;
} raft_peer;

// probably have to do log compaction/rotation later, or increase the buffer size
typedef struct PersistentState{
	raft_term_t currentTerm;
	raft_node_id_t votedFor;
	LogEntry log[RAFT_LOG_CAP];
	size_t log_len;
} PersistentState;

typedef struct VolatileState{
	raft_index_t commitIndex;
	raft_index_t lastApplied;
} VolatileState;

// volatile state specific to candidate, resets on election
typedef struct VolatileStateCandidate{
	raft_term_t electionterm;
	// votes responded and votes granted map
	bool votes_responded[RAFT_MAX_PEERS];
	bool votes_granted[RAFT_MAX_PEERS];
} VolatileStateCandidate;

// volatile state specific to leader, resets on election
typedef struct VolatileStateLeader{
	// tracker for nextindex and match index for all followers
	raft_index_t next_index[RAFT_MAX_PEERS];
	raft_index_t match_index[RAFT_MAX_PEERS];
} VolatileStateLeader;

// overall state machine values
typedef struct raft_node{
	// server state/role
	ServerState state;

	// server info
	raft_node_id_t id;
	raft_node_id_t leader_id;
	raft_peer peers[RAFT_MAX_PEERS];
	size_t peer_count;

	// States
	PersistentState pstate;
	VolatileState vstate;
	VolatileStateCandidate cstate;
	VolatileStateLeader lstate;

	// voting mechanism - timeout counters
	// only measuring forward delta, time is monotonic here in raft
	// Man clock_gettime Man timespec
	raft_msec_t election_timeout_duration_ms;
	raft_msec_t latest_heartbeat_ms;
	raft_msec_t heartbeat_interval_ms;

	// need to have a logfile
	
	// the hero, mr mutex
	pthread_mutex_t mu;
} raft_node;


#endif
