/*
 * emulation.h
 *
 *  Created on: June 23, 2014
 *      Author: aousterh
 */

#ifndef EMULATION_H_
#define EMULATION_H_

#include "admissible_log.h"
#include "config.h"
#include "endpoint.h"
#include "packet.h"
#include "packet_impl.h"
#include "router.h"
#include "queue_bank_log.h"
#include "../graph-algo/fp_ring.h"
#include "../graph-algo/platform.h"
#include "../protocol/topology.h"
#include <assert.h>
#include <inttypes.h>

#define ADMITTED_MEMPOOL_SIZE	128
#define ADMITTED_Q_LOG_SIZE		4
#define PACKET_MEMPOOL_SIZE		(1024 * 1024)
#define PACKET_Q_LOG_SIZE		16
#define EMU_NUM_PACKET_QS		(3 * EMU_NUM_ENDPOINT_GROUPS + EMU_NUM_ROUTERS)
#define MIN(X, Y)				(X <= Y ? X : Y)
#define EMU_ADD_BACKLOG_BATCH_SIZE	64

class EmulationCore;
class EndpointGroup;
class Router;
class EmulationOutput;
class EndpointDriver;
class RouterDriver;

/**
 * Emu state allocated for each comm core
 * @q_epg_new_pkts: queues of packets from comm core to each endpoint group
 * @q_resets: a queue of pending resets, for each endpoint group
 */
struct emu_comm_state {
	struct fp_ring	*q_epg_new_pkts[EPGS_PER_COMM];
	struct fp_ring	*q_resets[EPGS_PER_COMM];
};

/**
 * Data structure to store the state of the emulation.
 * @packet_mempool: pool of packet structs
 * @admitted_traffic_mempool: pool of admitted traffic structs
 * @q_admitted_out: queue of admitted structs to comm core
 * @stat: global emulation stats (mostly used by comm core)
 * @comm_state: state allocated per comm core to manage new packets
 * @queue_bank_stats: pointers to stats for each queue bank
 * @port_drop_stats: pointers to stats for each router
 * @core_stats: per-core stats, for easier access from logging core
 * @cores: the emulation cores
 */
class Emulation {
public:
	Emulation(struct fp_mempool *admitted_traffic_mempool,
			struct fp_ring *q_admitted_out, struct fp_mempool *packet_mempool,
			uint32_t packet_ring_size, enum RouterType r_type, void *r_args,
			enum EndpointType e_type, void *e_args);

	/**
	 * Run the emulation for one step.
	 */
	void step();

	/**
	 * Add backlog from @src to @dst for @flow. Add @amount MTUs, with the
	 * 	first id of @start_id. @areq_data provides additional information about
	 * 	each MTU.
	 */
	inline void add_backlog(uint16_t src, uint16_t dst, uint16_t flow,
			uint32_t amount, uint16_t start_id, u8* areq_data);

	/**
	 * Reset the emulation state for a single sender @src.
	 */
	inline void reset_sender(uint16_t src);

	/**
	 * Cleanup before destroying this Emulation.
	 */
	void cleanup();

private:
	/**
	 * Creates a packet, returns a pointer to the packet.
	 */
	inline struct emu_packet *create_packet(uint16_t src, uint16_t dst,
			uint16_t flow, uint16_t id, uint8_t *areq_data);

	void construct_topology(struct fp_ring **packet_queues,
			EndpointDriver **endpoint_drivers, RouterDriver **router_drivers,
			RouterType r_type, void *r_args, EndpointType e_type,
			void *e_args);
	void construct_single_rack_topology(struct fp_ring **packet_queues,
			EndpointDriver **endpoint_drivers, RouterDriver **router_drivers,
			RouterType r_type, void *r_args, EndpointType e_type,
			void *e_args);
	void construct_two_rack_topology(struct fp_ring **packet_queues,
			EndpointDriver **endpoint_drivers, RouterDriver **router_drivers,
			RouterType r_type, void *r_args, EndpointType e_type,
			void *e_args);
	void construct_three_rack_topology(struct fp_ring **packet_queues,
			EndpointDriver **endpoint_drivers, RouterDriver **router_drivers,
			RouterType r_type, void *r_args, EndpointType e_type,
			void *e_args);
	void assign_components_to_cores(EndpointDriver **epg_drivers,
			RouterDriver **router_drivers);

	/* Public variables for easy access from the log and emulation cores. */
public:
	struct emu_admission_statistics			m_stat;
	struct emu_admission_core_statistics	*m_core_stats[ALGO_N_CORES];
	EmulationCore							*m_cores[ALGO_N_CORES];

private:
	struct fp_mempool						*m_packet_mempool;
	struct fp_mempool						*m_admitted_traffic_mempool;
	struct fp_ring							*m_q_admitted_out;
	struct emu_comm_state					m_comm_state;
	struct queue_bank_stats					*m_queue_bank_stats[EMU_NUM_ROUTERS];
	struct port_drop_stats					*m_port_drop_stats[EMU_NUM_ROUTERS];
};


inline void Emulation::add_backlog(uint16_t src, uint16_t dst, uint16_t flow,
		uint32_t amount, uint16_t start_id, u8* areq_data) {
	uint32_t i, n_pkts;
	struct fp_ring *q_epg_new_pkts;
	assert(src < EMU_NUM_ENDPOINTS);
	assert(dst < EMU_NUM_ENDPOINTS);
	assert(flow < FLOWS_PER_NODE);

	q_epg_new_pkts = m_comm_state.q_epg_new_pkts[src / EMU_ENDPOINTS_PER_EPG];

#ifdef CONFIG_IP_FASTPASS_DEBUG
	printf("adding backlog from %d to %d, amount %d\n", src, dst, amount);
#endif

	/* create and enqueue a packet for each MTU, do this in batches */
	struct emu_packet *pkt_ptrs[EMU_ADD_BACKLOG_BATCH_SIZE];
	while (amount > 0) {
		n_pkts = 0;
		for (i = 0; i < MIN(amount, EMU_ADD_BACKLOG_BATCH_SIZE); i++) {
			pkt_ptrs[n_pkts] = create_packet(src, dst, flow, start_id++,
					areq_data);
			n_pkts++;
			areq_data += emu_req_data_bytes();
		}

		/* enqueue the packets to the correct endpoint group packet queue */
#ifdef DROP_ON_FAILED_ENQUEUE
		if (fp_ring_enqueue_bulk(q_epg_new_pkts, (void **) &pkt_ptrs[0],
				n_pkts) == -ENOBUFS) {
			/* no space in ring. log but don't retry. */
			adm_log_emu_enqueue_backlog_failed(&m_stat, n_pkts);
			for (i = 0; i < n_pkts; i++)
				free_packet(pkt_ptrs[i], m_packet_mempool);
		}
#else
		while (fp_ring_enqueue_bulk(q_epg_new_pkts, (void **) &pkt_ptrs[0],
				n_pkts) == -ENOBUFS) {
			/* no space in ring. log and retry. */
			adm_log_emu_enqueue_backlog_failed(&m_stat, n_pkts);
		}
#endif

		amount -= n_pkts;
	}
}

inline void Emulation::reset_sender(uint16_t src) {
	struct fp_ring *q_resets;
	uint64_t endpoint_id = src;

	q_resets = m_comm_state.q_resets[src / EMU_ENDPOINTS_PER_EPG];

	/* enqueue a notification to the endpoint driver's reset queue */
	/* cast src id to a pointer */
	while (fp_ring_enqueue(q_resets, (void *) endpoint_id) == -ENOBUFS) {
		/* no space in ring. this should never happen. log and retry. */
		adm_log_emu_enqueue_reset_failed(&m_stat);
	}
}

inline struct emu_packet *Emulation::create_packet(uint16_t src, uint16_t dst,
		uint16_t flow, uint16_t id, uint8_t *areq_data)
{
	struct emu_packet *packet;

	/* allocate a packet */
	while (fp_mempool_get(m_packet_mempool, (void **) &packet) == -ENOENT) {
		adm_log_emu_packet_alloc_failed(&m_stat);
	}
	packet_init(packet, src, dst, flow, id, areq_data);

	return packet;
}

#endif /* EMULATION_H_ */
