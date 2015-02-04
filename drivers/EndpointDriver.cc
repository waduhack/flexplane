/*
 * EndpointDriver.cc
 *
 *  Created on: Dec 21, 2014
 *      Author: yonch
 */

#include "EndpointDriver.h"

#include <assert.h>
#include <stdint.h>
#include "../config.h"
#include "../endpoint_group.h"
#include "../emulation.h"
#include "../graph-algo/fp_ring.h"
#include "../graph-algo/platform.h"
#include "../api.h"
#include "../api_impl.h"

#define MAX_PUSH_BURST			(EMU_ENDPOINTS_PER_RACK)
#define MAX_PULL_BURST			(EMU_ENDPOINTS_PER_RACK)
#define MAX_NEW_PACKET_BURST	(EMU_ENDPOINTS_PER_RACK)

EndpointDriver::EndpointDriver(struct fp_ring* q_new_packets,
		struct fp_ring* q_to_router, struct fp_ring* q_from_router,
		struct fp_ring *q_resets, EndpointGroup* epg)
	: m_q_new_packets(q_new_packets),
	  m_q_to_router(q_to_router),
	  m_q_from_router(q_from_router),
	  m_q_resets(q_resets),
	  m_epg(epg),
	  m_cur_time(0)
{}

void EndpointDriver::assign_to_core(EmulationOutput *out,
		struct emu_admission_core_statistics *stat, uint16_t core_index) {
	m_stat = stat;
	m_epg->assign_to_core(out, stat);
	m_core_index = core_index;
}

void EndpointDriver::cleanup() {
	free_packet_ring(g_state, m_q_from_router);

	delete m_epg;
}

void EndpointDriver::step() {
	uint64_t endpoint_id;

	/* handle any resets */
	while (fp_ring_dequeue(m_q_resets, (void **) &endpoint_id) != -ENOENT) {
		/* cast pointer to int identifying the endpoint */
		m_epg->reset((uint16_t) endpoint_id);
	}

	push();
	pull();
	process_new();

	m_cur_time++;
}

/**
 * Emulate push at a single endpoint group with index @index
 */

inline void EndpointDriver::push() {
	uint32_t n_pkts;
	struct emu_packet *pkts[MAX_PUSH_BURST];

	/* dequeue packets from network, pass to endpoint group */
	n_pkts = fp_ring_dequeue_burst(m_q_from_router,
			(void **) &pkts[0], MAX_PUSH_BURST);
	m_epg->push_batch(&pkts[0], n_pkts);

	adm_log_emu_endpoint_driver_pushed(m_stat, n_pkts);

#ifdef CONFIG_IP_FASTPASS_DEBUG
	printf("EndpointDriver on core %d pushed %d packets\n", m_core_index,
			n_pkts);
#endif
}

/**
 * Emulate pull at a single endpoint group with index @index
 */
inline void EndpointDriver::pull() {
	uint32_t n_pkts, i;
	struct emu_packet *pkts[MAX_PULL_BURST];

	/* pull a batch of packets from the epg, enqueue to router */
	n_pkts = m_epg->pull_batch(&pkts[0], MAX_PULL_BURST);
	assert(n_pkts <= MAX_PULL_BURST);
#ifdef DROP_ON_FAILED_ENQUEUE
	if (n_pkts > 0 && fp_ring_enqueue_bulk(m_q_to_router,
			(void **) &pkts[0], n_pkts) == -ENOBUFS) {
		/* no space in ring. log but don't retry. */
		adm_log_emu_send_packets_failed(m_stat, n_pkts);
		for (i = 0; i < n_pkts; i++)
			free_packet(g_state, pkts[i]);
	} else {
		adm_log_emu_endpoint_sent_packets(m_stat, n_pkts);
		adm_log_emu_endpoint_driver_pulled(m_stat, n_pkts);
	}
#else
	while (n_pkts > 0 && fp_ring_enqueue_bulk(m_q_to_router,
			(void **) &pkts[0], n_pkts) == -ENOBUFS) {
		/* no space in ring. log and retry. */
		adm_log_emu_send_packets_failed(m_stat, n_pkts);
	}
	adm_log_emu_endpoint_sent_packets(m_stat, n_pkts);
	adm_log_emu_endpoint_driver_pulled(m_stat, n_pkts);
#endif

#ifdef CONFIG_IP_FASTPASS_DEBUG
	printf("EndpointDriver on core %d pulled %d packets\n", m_core_index,
			n_pkts);
#endif
}

/**
 * Emulate new packets at a single endpoint group with index @index
 */
inline void EndpointDriver::process_new()
{
	uint32_t n_pkts;
	struct emu_packet *pkts[MAX_NEW_PACKET_BURST];

	/* dequeue new packets, pass to endpoint group */
	n_pkts = fp_ring_dequeue_burst(m_q_new_packets,
			(void **) &pkts, MAX_NEW_PACKET_BURST);
	m_epg->new_packets(&pkts[0], n_pkts, m_cur_time);
	adm_log_emu_endpoint_driver_processed_new(m_stat, n_pkts);

#ifdef CONFIG_IP_FASTPASS_DEBUG
	printf("EndpointDriver on core %d processed %d new packets\n",
			m_core_index, n_pkts);
#endif
}
