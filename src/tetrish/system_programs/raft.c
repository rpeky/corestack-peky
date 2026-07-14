#include "tetrish/raft.h"

// Helper functions
raft_index_t max(raft_index_t a, raft_index_t b) {
	return a > b ? a : b;
}

// Heartbeat
AppendEntriesRequest heartbeat_tick(raft_node *r) {
	pthread_mutex_lock(&(r->mu));
	if (r->state == FAILED || r->state != LEADER) {
		pthread_mutex_unlock(&(r->mu));
		return;
	}

	// create an empty append entry
	AppendEntriesRequest req = {
		.term = r->pstate.currentTerm,
		.leaderId = r->id,
		.prevLogIndex = 0,
		.prevLogTerm = 0,
		.leaderCommit = r->vstate.commitIndex,
		.log_len = 0,
	};

	// send the empty request

	pthread_mutex_unlock(&r->mu);
	return req;
}

size_t majority(size_t n) {
	return (n / 2) + 1;
}

bool checkLog(raft_index_t reqLogIdx, raft_term_t reqLogTerm, raft_index_t lastIdx,
	      raft_term_t lastTerm){
	if (reqLogTerm != lastTerm) {
		return reqLogTerm > lastTerm
	}
	// check last idx
	return reqLogIdx >= lastIdx
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

// Timing mechanics for timeout
static raft_msec_t raft_now_msec(void) {

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

static int raft_election_timeout_expired(const raft_node *r) {
	raft_msec_t now = raft_now_msec();

	// check if clock failed
	if (now < 0)
		return 0;

	return now - r->latest_heartbeat_ms >= r->election_timeout_duration_ms;
}

static raft_msec_t raft_random_election_timeout(void) {
	return 150 + rand() % 150; /* 150..299 ms */
}

void raft_reset_election_timer(raft_node *r) {
	r->election_timeout_duration_ms = raft_random_election_timeout();
	r->latest_heartbeat_ms = raft_now_msec();
}

// State Machine reset - candidate
void raft_init_candidate_state(raft_node *r, raft_term_t term) {
	// fill the struct w 0 to initialise
	memset(&r->cstate, 0, sizeof(r->cstate));
	r->cstate.electionterm = term;
}

// State Machine reset - leader
void raft_init_leader_state(raft_node *r) {
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
size_t raft_timeout(raft_node *r, raft_message out[], size_t out_cap) {
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

	r->

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

void raft_clear_candidate_state(raft_node *r) {
	memset(&r->cstate, 0, sizeof(r->cstate));
}

void raft_clear_leader_state(raft_node *r) {
	memset(&r->lstate, 0, sizeof(r->lstate));
}

// to use if mutex is already in place
void update_term_locked(raft_node *r, raft_term_t newTerm) {

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

void update_term(raft_node *r, raft_term_t newTerm){
	if (r == NULL) {
		return;
	}

	pthread_mutex_lock(&r->mu);
	update_term_locked(r, newTerm);
	pthread_mutex_unlock(&r->mu);
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

void HandleAppendEntriesRequest(raft_node *r, AppendEntriesRequest *req) {

	AppendEntriesResponse resp = {
		.term = r->pstate.currentTerm,
		.success = true,
	};
	// send response
}

void HandleAppendEntriesResponse(raft_node *r, AppendEntriesResponse *resp,
				 raft_node_id_t peer) {
	if (r->state != LEADER)
		return;

	// check for stale message, drop if stale
	if (r->pstate.currentTerm > resp->term)
		return;

	// if not up to date, update logs

	// if successful, check if leader is behind, otherwise update lstate

	// fail means log inconsistency, decrement nextIndex and retry overwrite
	// at worse it will hit zero and do a full overwrite for the logs
	if (r->lstate.next_index[peer_idx] > 1)
		r->lstate.next_index[peer_idx]--;
	AppendEntries(r, peer);
}

RequestVoteResponse HandleRequestVoteRequest(raft_node *r, RequestVoteRequest *req){
	bool voteGranted = false;

	// create empty response body
	RequestVoteResponse resp = {
		.term = r->pstate.currentTerm,
		.voteGranted = voteGranted,
	};

	if(r==NULL || req==NULL) return;

	pthread_mutex_lock(&r->mu);

	// check if req RPC term is strictly greater, if so run the update
	if(req->term > r->pstate.currentTerm)
		update_term_locked(r, req->term);


	// negate if req is stale
	// expect the other sm to run update with this more recent term
	if(req->term < r->pstate.currentTerm){
		pthread_mutex_unlock(&r->mu);
		return resp;
	}

	// at this point, if leader is behind would have demoted, 
	// remaining are followers, candidates and failed states
	// probably excessive
	if(r->state == LEADER){
		pthread_mutex_unlock(&r->mu);
		return resp;
	}

	// vote for candidate if able to 
	if((r->pstate.votedFor == RAFT_NONE 
		|| r->pstate.votedFor == req-> candidateId) 
		&& checkLog(req.lastLogIndex, req.lastLogTerm, 
	        raft_last_log_index(r), raft_last_log_term(r)){
		r->pstate.votedFor = candidateId;
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
