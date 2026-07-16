#include "tetrish/raft.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>

/*--------------------------Internal prototypes-------------------------------*/
static raft_index_t max(raft_index_t a, raft_index_t b);
static size_t find_peer_idx(raft_node *r, raft_node_id_t peer);
static bool heartbeat_tick(raft_node *r, AppendEntriesRequest *req);
static bool checkLog(raft_index_t reqLogIdx, raft_term_t reqLogTerm, raft_index_t lastIdx, raft_term_t lastTerm);
static void raft_reset_election_timer(raft_node *r);
static void raft_init_candidate_state(raft_node *r, raft_term_t term);
static void raft_init_leader_state(raft_node *r);
static void raft_clear_candidate_state(raft_node *r);
static void raft_clear_leader_state(raft_node *r);
static void update_term_locked(raft_node *r, raft_term_t newTerm);
static void update_term(raft_node *r, raft_term_t newTerm);
static void AdvanceCommitIndex_locked(raft_node *r);
static void ApplyCommitedEnteries_locked(raft_node *r);

/*--------------------------Internal prototypes-------------------------------*/

// Helper functions
static raft_index_t max(raft_index_t a, raft_index_t b) {
	return a > b ? a : b;
}

// Create a logfile for commits
void create_log_file(void) {
	char log_path[PATH_MAX];
	strncpy(log_path, project_root, sizeof(log_path) - 1);
	strncat(log_path, "/tmp/raft.log",
	 sizeof(log_path) - strlen(log_path) - 1);

	int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd == -1) {
		perror("log open");
		return;
	}

	time_t now = time(NULL);
	close(fd);
}

static void debug_printlog(raft_node *r) {
	pthread_mutex_lock(&r->mu);
	for (size_t i = 0; i < r->pstate.log_len; ++i) {
		LogEntry entry = r->pstate.log[i];
		printf("Log entry index %lu | Commit term %lu\n",
	 entry.index, entry.term);
		printf("Command (type): %d\n", entry.command.type);
		printf("Command (roomkey): %lu\n", entry.command.room_key);
		printf("Command (player_id): %lu\n", entry.command.player_id);
		printf("Command (input): %u\n", entry.command.input);
		printf("-----------------------------------------\n");
	}
	pthread_mutex_unlock(&r->mu);
}

static void writeLog(raft_node *r, LogEntry entry) {
}

static size_t find_peer_idx(raft_node *r, raft_node_id_t peer) {
	size_t peeridx = SIZE_MAX;
	for (size_t i = 0; i < r->peer_count; ++i) {
		if (r->peers[i].id == peer)
			peeridx = i;
	}
	return peeridx;
}

// Heartbeat
static bool heartbeat_tick(raft_node *r, AppendEntriesRequest *req) {
	pthread_mutex_lock(&(r->mu));
	if (r->state == FAILED || r->state != LEADER) {
		pthread_mutex_unlock(&(r->mu));
		return false;
	}

	// create an empty append entry
	*req = (AppendEntriesRequest){
		.term = r->pstate.currentTerm,
		.leaderId = r->id,
		.prevLogIndex = 0,
		.prevLogTerm = 0,
		.leaderCommit = r->vstate.commitIndex,
		.log_len = 0,
	};

	// send the empty request

	pthread_mutex_unlock(&r->mu);
	return true;
}

size_t majority(size_t n) {
	return (n / 2) + 1;
}

static bool checkLog(raft_index_t reqLogIdx, raft_term_t reqLogTerm, raft_index_t lastIdx, raft_term_t lastTerm) {
	if (reqLogTerm != lastTerm) {
		return reqLogTerm > lastTerm;
	}
	// check last idx
	return reqLogIdx >= lastIdx;
}

// local calculation
raft_index_t raft_last_log_index(raft_node *r) {
	return (raft_index_t)r->pstate.log_len;
}

// local calculation
raft_term_t raft_last_log_term(raft_node *r) {
	if (r->pstate.log_len == 0)
		return 0;

	// search through the log for the last used term
	return r->pstate.log[r->pstate.log_len - 1].term;
}

// local calculation
bool raft_find_peer_index(const raft_node *r, raft_node_id_t peer_id, size_t *peer_idx) {
	if (r == NULL || peer_idx == NULL)
		return false;

	for (size_t i = 0; i < r->peer_count; i++) {
		if (r->peers[i].id == peer_id) {
			*peer_idx = i;
			return true;
		}
	}

	return false;
}

// Timing mechanics for timeout
raft_msec_t raft_now_msec(void) {
	/* Man timespec
	struct timespec {
	   time_t     tv_sec;
	   tv_nsec;
	};
	*/
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		return -1;

	return (raft_msec_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int raft_election_timeout_expired(const raft_node *r) {
	raft_msec_t now = raft_now_msec();

	// check if clock failed
	if (now < 0)
		return 0;

	return now - r->latest_heartbeat_ms >= r->election_timeout_duration_ms;
}

raft_msec_t raft_random_election_timeout(void) {
	return 150 + rand() % 150; /* 150..299 ms */
}

static void raft_reset_election_timer(raft_node *r) {
	r->election_timeout_duration_ms = raft_random_election_timeout();
	r->latest_heartbeat_ms = raft_now_msec();
}

// State Machine reset - candidate
static void raft_init_candidate_state(raft_node *r, raft_term_t term) {
	// fill the struct w 0 to initialise
	memset(&r->cstate, 0, sizeof(r->cstate));
	r->cstate.electionterm = term;
}

// State Machine reset - leader
static void raft_init_leader_state(raft_node *r) {
	// set as leader
	r->state = LEADER;
	r->leader_id = r->id;

	// zero the trackers
	memset(&r->lstate, 0, sizeof(r->lstate));

	// fill in trackers based on peer count
	for (size_t i = 0; i < r->peer_count; i++) {
		r->lstate.next_index[i] = raft_last_log_index(r) + 1;
		r->lstate.match_index[i] = 0;
	}
}

// send request vote on timeout
// tetrisd will pass the out container and send the message to the peers
static size_t raft_timeout(raft_node *r, raft_message out[], size_t out_cap) {
	size_t out_len = 0;

	// chekc input fail
	if (r == NULL || out == NULL)
		return 0;

	pthread_mutex_lock(&(r->mu));

	// leader does not need to check timeout, it sends heartbeats
	if (r->state == FAILED || r->state == LEADER) {
		// remember to unlock lol
		pthread_mutex_unlock(&r->mu);
		return 0;
	}

	// check delta between now and heartbeat, if not expired leave
	if (!raft_election_timeout_expired(r)) {
		pthread_mutex_unlock(&r->mu);
		return 0;
	}

	// debug
	printf("[node=%lu] TIMEOUT currentTerm=%lu state=%d -> start election\n",
	(unsigned long)r->id,
	(unsigned long)r->pstate.currentTerm,
	r->state);

	// timeout expired
	// leader is now invalid
	// transition to candidate, increment term, reset vote tracking
	// vote for yourself, reset volatilecandidate states
	// update for self voting
	r->leader_id = RAFT_NONE;
	r->state = CANDIDATE;
	r->pstate.currentTerm++;
	r->pstate.votedFor = r->id;

	raft_init_candidate_state(r, r->pstate.currentTerm);

	// debug
	printf("[node=%lu] BECAME CANDIDATE term=%lu votedFor=%lu lastIdx=%lu lastTerm=%lu\n",
	(unsigned long)r->id,
	(unsigned long)r->pstate.currentTerm,
	(unsigned long)r->pstate.votedFor,
	(unsigned long)raft_last_log_index(r),
	(unsigned long)raft_last_log_term(r));

	// reset election timeout
	raft_reset_election_timer(r);

	// edge case, single node in raft
	if (majority(r->peer_count + 1) == 1) {
		raft_init_leader_state(r);
		pthread_mutex_unlock(&r->mu);
		return 0;
	}

	// put the values in the message body for request vote while we still
	// have the lock. send afterwards in tetrisd
	raft_term_t term = r->pstate.currentTerm;
	raft_index_t last_idx = raft_last_log_index(r);
	raft_term_t last_term = raft_last_log_term(r);

	for (size_t i = 0; i < r->peer_count && out_len < out_cap; i++) {
		out[out_len].type = RAFT_MSG_REQUEST_VOTE;
		out[out_len].peer_idx = i;
		out[out_len].body.request_vote.term = term;
		out[out_len].body.request_vote.lastLogIndex = last_idx;
		out[out_len].body.request_vote.lastLogTerm = last_term;
		out[out_len].body.request_vote.candidateId = r->id;
		out_len++;
	}

	pthread_mutex_unlock(&r->mu);
	return out_len;
}

void initialise_raft_sm(raft_node *r, raft_node_id_t id) {
	if (r == NULL)
		return;

	// need to validate the IPs later as well

	// fill the struct with 0 to initialise
	memset(r, 0, sizeof(*r));

	// create the god mutex
	pthread_mutex_init(&r->mu, NULL);

	// always start as a follower and vote afterwards
	r->state = FOLLOWER;
	r->id = id;
	r->leader_id = RAFT_NONE;

	r->peer_count = 0;

	r->pstate.currentTerm = 0;
	r->pstate.votedFor = RAFT_NONE;
	r->pstate.log_len = 0;

	r->vstate.commitIndex = 0;
	r->vstate.lastApplied = 0;

	r->heartbeat_interval_ms = 50;
	raft_reset_election_timer(r);

	// need a fd to logfile for this server
}

static void raft_clear_candidate_state(raft_node *r) {
	memset(&r->cstate, 0, sizeof(r->cstate));
}

static void raft_clear_leader_state(raft_node *r) {
	memset(&r->lstate, 0, sizeof(r->lstate));
}

// to use if mutex is already in place
static void update_term_locked(raft_node *r, raft_term_t newTerm) {
	if (newTerm <= r->pstate.currentTerm) {
		return;
	}

	// reset state to follower, current node is out of sync and behind
	if (r->state != FAILED) {
		r->state = FOLLOWER;
	}

	// update current term
	r->pstate.currentTerm = newTerm;
	r->pstate.votedFor = RAFT_NONE;
	r->leader_id = RAFT_NONE;

	// zero cstate and lstate
	raft_clear_candidate_state(r);
	raft_clear_leader_state(r);

	raft_reset_election_timer(r);
}

static void update_term(raft_node *r, raft_term_t newTerm) {
	if (r == NULL) {
		return;
	}

	pthread_mutex_lock(&r->mu);
	update_term_locked(r, newTerm);
	pthread_mutex_unlock(&r->mu);
}

// only run by leader -> used when mutex is locked
static void AdvanceCommitIndex_locked(raft_node *r) {
	// check if leader
	if (r->state != LEADER) {
		fprintf(stderr, "AdvanceCommitIndex_locked failed\n");
		return;
	}

	raft_index_t lastIdx = raft_last_log_index(r);
	size_t clustersize = r->peer_count + 1;

	for (raft_index_t n = lastIdx; n > r->vstate.commitIndex; --n) {
		if (r->pstate.log[n - 1].term != r->pstate.currentTerm) {
			continue;
		}

		// exclude self
		size_t track = 1;
		for (size_t peer = 0; peer < r->peer_count; ++peer) {
			// advance if greater to push pointer forward
			if (r->lstate.match_index[peer] >= n) {
				track++;
			}
		}

		// check for majority, update to n if majority has this
		if (track >= majority(clustersize)) {
			r->vstate.commitIndex = n;
			return;
		} else {
			// otherwise, this is the highest majority value
			break;
		}
	}

	return;
}

// this functio runs while muxtex is locked
static void ApplyCommitedEnteries_locked(raft_node *r) {
	// update last applied after logging
	while (r->vstate.lastApplied < r->vstate.commitIndex) {
		// extract log entry in index
		LogEntry entry = r->pstate.log[r->vstate.lastApplied - 1];

		// write to log file, on success increment last applied

		r->vstate.lastApplied++;
	}
}

// used by leader to replicate log entries using leder data
// is also a kind of heartbeat
void AppendEntries(raft_node *r, size_t peer_idx) {
	// safety check, only leader should call this function
	if (r->state != LEADER)
		return;

	// critical section work
	pthread_mutex_lock(&r->mu);

	// get lstate info to add to the payload
	raft_index_t nextIdx = r->lstate.next_index[peer_idx];
	raft_index_t prevLogIdx = nextIdx - 1;
	raft_index_t prevLogTerm = 0;
	if (prevLogIdx > 0) {
		// retrieve the latest known commited term of leader
		prevLogTerm = r->pstate.log[prevLogIdx - 1].term;
	}

	// count the number of entries to send, up to batch size max
	size_t entryCount = 0;

	if (nextIdx <= r->pstate.log_len) {
		raft_index_t available =
			r->pstate.log_len - nextIdx + 1;

		entryCount =
			available < RAFT_APPEND_BATCH
			? (size_t)available
			: RAFT_APPEND_BATCH;
	}

	// build struct to send
	AppendEntriesRequest req = {
		.term = r->pstate.currentTerm,
		.leaderId = r->id,
		.prevLogIndex = prevLogIdx,
		.prevLogTerm = prevLogTerm,
		.leaderCommit = r->vstate.commitIndex,
		.log_len = entryCount,
	};

	for (size_t j = 0; j < entryCount; ++j) {
		size_t log_pos = (size_t)(nextIdx - 1) + j;
		req.log[j] = r->pstate.log[log_pos];
	}

	pthread_mutex_unlock(&r->mu);

	// send the appendentryreq *req payload to peer after unlock
}

AppendEntriesResponse HandleAppendEntriesRequest(raft_node *r, AppendEntriesRequest *req) {
	// default to fail message
	AppendEntriesResponse resp = {
		.term = r->pstate.currentTerm,
		.success = false,
	};

	// send response

	pthread_mutex_lock(&r->mu);

	// mismatched term - 5.1, return fail if lower
	// release lock and return
	// if a leader receives this, it means that it is lagging behind
	if (req->term < r->pstate.currentTerm) {
		pthread_mutex_unlock(&r->mu);
		return resp;
	}

	// run update term if larger, if leader demote
	if (req->term > r->pstate.currentTerm) {
		update_term_locked(r, req->term);
	}

	// term is valid, send back a response timestamped to current term
	resp.term = r->pstate.currentTerm;

	// transition candidate to follower on leader contact (this message),
	// reset heartbeat timer
	// in my head, only candidates or followers should reach this point of the code
	if (r->state != FAILED) {
		r->state = FOLLOWER;
	}

	// update state machine fields
	r->leader_id = req->leaderId;
	r->latest_heartbeat_ms = raft_now_msec();

	pthread_mutex_unlock(&r->mu);
	return resp;
}

void HandleAppendEntriesResponse(raft_node *r, AppendEntriesResponse *resp, raft_node_id_t peer) {
	// Only leader will handle AE response
	if (r->state != LEADER)
		return;

	// check for stale message, drop if stale
	if (r->pstate.currentTerm > resp->term)
		return;

	pthread_mutex_lock(&r->mu);

	// if not up to date, update logs
	// leader should demote since term is lagging behind
	if (r->pstate.currentTerm < resp->term) {
		update_term_locked(r, resp->term);
		pthread_mutex_unlock(&r->mu);
		return;
	}

	// calculate peer index
	size_t peer_idx = find_peer_idx(r, peer);

	// return if peer can't be found
	if (peer_idx == SIZE_MAX) {
		fprintf(stderr, "HandleAppendEntriesResponse | Unable to find peer_idx\n");
		pthread_mutex_unlock(&r->mu);
		return;
	}

	// if successful, check if leader is behind, otherwise update lstate
	if (resp->success) {
		if (r->lstate.next_index[peer_idx] <= r->pstate.log_len) {
			r->lstate.match_index[peer_idx] = r->lstate.next_index[peer];
			r->lstate.next_index[peer_idx]++;
		}

		// commit index, apply commited index, return
		AdvanceCommitIndex_locked(r);
		ApplyCommitedEnteries_locked(r);

		pthread_mutex_unlock(&r->mu);
		return;
	}

	// otherwise
	// fail means log inconsistency, decrement nextIndex and retry overwrite
	// at worse it will hit zero and do a full overwrite for the logs
	if (r->lstate.next_index[peer_idx] > 1)
		r->lstate.next_index[peer_idx]--;
	AppendEntries(r, peer);
	pthread_mutex_unlock(&r->mu);
	return;
}

RequestVoteResponse HandleRequestVoteRequest(raft_node *r, RequestVoteRequest *req) {
	bool voteGranted = false;

	// create empty response body
	RequestVoteResponse resp = {
		.term = r->pstate.currentTerm,
		.voteGranted = voteGranted,
	};

	if (r == NULL || req == NULL)
		return resp;

	pthread_mutex_lock(&r->mu);

	// check if req RPC term is strictly greater, if so run the update
	if (req->term > r->pstate.currentTerm)
		update_term_locked(r, req->term);

	// negate if req is stale
	// expect the other sm to run update with this more recent term
	if (req->term < r->pstate.currentTerm) {
		pthread_mutex_unlock(&r->mu);
		return resp;
	}

	// at this point, if leader is behind would have demoted,
	// remaining are followers, candidates and failed states
	// probably excessive
	if (r->state == LEADER) {
		pthread_mutex_unlock(&r->mu);
		return resp;
	}

	// vote for candidate if able to
	if ((r->pstate.votedFor == RAFT_NONE ||
		r->pstate.votedFor == req->candidateId) &&
		checkLog(req->lastLogIndex, req->lastLogTerm,
	   raft_last_log_index(r), raft_last_log_term(r))) {

		r->pstate.votedFor = req->candidateId;
		r->latest_heartbeat_ms = raft_now_msec();
		resp.voteGranted = true;
	}
	pthread_mutex_unlock(&r->mu);
	return resp;
}

int main(void) {
	printf("test raft\n");
	// initialise a raft server
	raft_node r;
	initialise_raft_sm(&r, 0);
	return 0;
}
