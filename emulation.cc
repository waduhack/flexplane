/*
 * emulation.cc
 *
 *  Created on: June 24, 2014
 *      Author: aousterh
 */

#include "emulation.h"
#include "api.h"
#include "api_impl.h"
#include "admitted.h"
#include "drop_tail.h"
#include "endpoint_group.h"
#include "router.h"
#include "../protocol/topology.h"

#include <assert.h>

#define ENDPOINT_MAX_BURST	(EMU_NUM_ENDPOINTS * 2)
#define ROUTER_MAX_BURST	(EMU_NUM_ENDPOINTS * 2)

emu_state *g_state; /* global emulation state */

static inline void free_packet_ring(struct fp_ring *packet_ring);

void emu_init_state(struct emu_state *state,
		struct fp_mempool *admitted_traffic_mempool,
		struct fp_ring *q_admitted_out, struct fp_mempool *packet_mempool,
	    struct fp_ring **packet_queues, void *args) {
	uint32_t i, pq;
	uint32_t size;

	g_state = state;

	pq = 0;
	state->admitted_traffic_mempool = admitted_traffic_mempool;
	state->q_admitted_out = q_admitted_out;
	state->packet_mempool = packet_mempool;

	/* construct topology: 1 router with 1 rack of endpoints */

	/* initialize all the routers */
	for (i = 0; i < EMU_NUM_ROUTERS; i++) {
		// TODO: use fp_malloc?
		state->routers[i] = new DropTailRouter(i,
				(struct drop_tail_args *) args);
		assert(state->routers[i] != NULL);
		state->q_router_ingress[i] = packet_queues[pq++];
	}

	/* initialize all the endpoints in one endpoint group */
	state->q_epg_new_pkts[0] = packet_queues[pq++];
	state->q_epg_ingress[0] = packet_queues[pq++];
	state->endpoint_groups[0] = new DropTailEndpointGroup(EMU_NUM_ENDPOINTS);
	state->endpoint_groups[0]->init(0, (struct drop_tail_args *) args);
	assert(state->endpoint_groups[0] != NULL);

	/* get 1 admitted traffic for the core, init it */
	while (fp_mempool_get(state->admitted_traffic_mempool,
			(void **) &state->admitted) == -ENOENT)
		adm_log_emu_admitted_alloc_failed(&state->stat);
	admitted_init(state->admitted);
}

void emu_cleanup(struct emu_state *state) {
	uint32_t i;
	struct emu_admitted_traffic *admitted;

	/* free all endpoints */
	for (i = 0; i < EMU_NUM_ENDPOINT_GROUPS; i++) {
		delete state->endpoint_groups[i];

		/* free packet queues, return packets to mempool */
		free_packet_ring(state->q_epg_new_pkts[i]);
		free_packet_ring(state->q_epg_ingress[i]);
	}

	/* free all routers */
	for (i = 0; i < EMU_NUM_ROUTERS; i++) {
		// TODO: call fp_free?
		delete state->routers[i];

		/* free ingress queue for this router, return packets to mempool */
		free_packet_ring(state->q_router_ingress[i]);
	}

	/* return admitted struct to mempool */
	if (state->admitted != NULL)
		fp_mempool_put(state->admitted_traffic_mempool, state->admitted);

	/* empty queue of admitted traffic, return structs to the mempool */
	while (fp_ring_dequeue(state->q_admitted_out, (void **) &admitted) == 0)
		fp_mempool_put(state->admitted_traffic_mempool, admitted);
	fp_free(state->q_admitted_out);

	fp_free(state->admitted_traffic_mempool);
	fp_free(state->packet_mempool);
}

/*
 * Emulate a timeslot at a single router with index @index
 */
static inline void emu_emulate_router(struct emu_state *state,
		uint32_t index) {
	Router *router;
	uint32_t i, j, n_pkts;

	/* get the corresponding router */
	router = state->routers[index];

	struct emu_packet *pkt_ptrs[EMU_ROUTER_NUM_PORTS];
	/* fetch packets to send from router */
#ifdef EMU_NO_BATCH_CALLS
	n_pkts = 0;
	for (uint32_t i = 0; i < EMU_ROUTER_NUM_PORTS; i++) {
		router->pull(i, &pkt_ptrs[n_pkts]);

		if (pkt_ptrs[n_pkts] != NULL)
			n_pkts++;
	}
#else
	n_pkts = router->pull_batch(pkt_ptrs, EMU_ROUTER_NUM_PORTS);
#endif
	/* send packets */
	// TODO: use bulk enqueue
	for (j = 0; j < n_pkts; j++) {
		if (fp_ring_enqueue(state->q_epg_ingress[0], pkt_ptrs[j])
				== -ENOBUFS) {
			adm_log_emu_send_packet_failed(&state->stat);
			drop_packet(pkt_ptrs[j]);
		} else {
			adm_log_emu_router_sent_packet(&state->stat);
		}
	}

	/* push a batch of packets from the network into the router */
	struct emu_packet *packets[ROUTER_MAX_BURST];

	/* pass all incoming packets to the router */
	n_pkts = fp_ring_dequeue_burst(state->q_router_ingress[index],
			(void **) &packets, ROUTER_MAX_BURST);
#ifdef EMU_NO_BATCH_CALLS
	for (i = 0; i < n_pkts; i++) {
		router->push(packets[i]);
	}
#else
	router->push_batch(&packets[0], n_pkts);
#endif
}

void emu_emulate(struct emu_state *state) {
	uint32_t i, j, n_pkts;
	EndpointGroup *epg;
	struct emu_packet *pkts[ENDPOINT_MAX_BURST];

	/* handle push at each endpoint group */
	for (i = 0; i < EMU_NUM_ENDPOINT_GROUPS; i++) {
		epg = state->endpoint_groups[i];

		/* dequeue packets from network, pass to endpoint group */
		n_pkts = fp_ring_dequeue_burst(state->q_epg_ingress[i],
				(void **) &pkts[0], ENDPOINT_MAX_BURST);
		epg->push_batch(&pkts[0], n_pkts);
	}
	/* emulate one timeslot at each router */
	for (i = 0; i < EMU_NUM_ROUTERS; i++) {
		emu_emulate_router(state, i);
	}

	/* handle pull/new packets at each endpoint group */
	for (i = 0; i < EMU_NUM_ENDPOINT_GROUPS; i++) {
		epg = state->endpoint_groups[i];

		/* pull a batch of packets from the epg, enqueue to router */
		n_pkts = epg->pull_batch(&pkts[0]);
		for (j = 0; j < n_pkts; j++) {
			// TODO: use bulk enqueue?
			if (fp_ring_enqueue(state->q_router_ingress[0], pkts[j])
					== -ENOBUFS) {
				adm_log_emu_send_packet_failed(&state->stat);
				drop_packet(pkts[j]);
			} else {
				adm_log_emu_endpoint_sent_packet(&state->stat);
			}
		}

		/* dequeue new packets, pass to endpoint group */
		n_pkts = fp_ring_dequeue_burst(state->q_epg_new_pkts[i],
				(void **) &pkts, ENDPOINT_MAX_BURST);
		epg->new_packets(&pkts[0], n_pkts);
	}

	/* send out the admitted traffic */
	while (fp_ring_enqueue(state->q_admitted_out, state->admitted) != 0)
		adm_log_emu_wait_for_admitted_enqueue(&state->stat);

	/* get 1 new admitted traffic for the core, init it */
	while (fp_mempool_get(state->admitted_traffic_mempool,
				(void **) &state->admitted) == -ENOENT)
		adm_log_emu_admitted_alloc_failed(&state->stat);
	admitted_init(state->admitted);
}

void emu_reset_sender(struct emu_state *state, uint16_t src) {

	/* TODO: clear the packets in the routers too? */
	state->endpoint_groups[0]->reset(src);
}

/* frees all the packets in an fp_ring, and frees the ring itself */
static inline void free_packet_ring(struct fp_ring *packet_ring) {
	struct emu_packet *packet;

	while (fp_ring_dequeue(packet_ring, (void **) &packet) == 0) {
		free_packet(packet);
	}
	fp_free(packet_ring);
}
