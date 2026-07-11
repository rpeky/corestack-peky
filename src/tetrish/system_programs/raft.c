#include "tetrish/raft.h"

static raft_msec_t raft_now_msec(void){

	/* Man timespec
	struct timespec {
	   time_t     tv_sec;
	   tv_nsec;  
	};
	*/
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		return -1;

	return (raft_msec_t)ts.tv_sec*1000 + ts.tv_nsec/1000000;
}

static int raft_election_timeout_expired(const raft_node *r)
{
	raft_msec_t now = raft_now_msec();

	return now - r->latest_heartbeat_ms >= r->election_timeout_duration_ms;
}

static raft_msec_t raft_random_election_timeout(void)
{
	return 150 + rand() % 150; /* 150..299 ms */
}

void raft_reset_election_timer(raft_node *r)
{
	r->election_timeout_duration_ms = raft_random_election_timeout();
	r->latest_heartbeat_ms = raft_now_msec();
}

void initialise_raft_sm(raft_node *r, raft_node_id_t id){

	if (r == NULL)
		return;

	// need to validate the IPs later as well

	memset(r, 0, sizeof(*r));
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

	pthread_mutex_init(&r->mu, NULL);
}

int main(void){
	printf("test raft\n");
	// initialise a raft server
	raft_node r;
	initialise_raft_sm(&r, 0);
	return 0;
}
