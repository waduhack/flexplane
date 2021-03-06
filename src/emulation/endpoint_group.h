/*
 * endpoint_group.h
 *
 *  Created on: December 11, 2014
 *      Author: aousterh
 */

#ifndef ENDPOINT_GROUP_H_
#define ENDPOINT_GROUP_H_

#include "config.h"
#include "emu_topology.h"
#include "endpoint.h"
#include <assert.h>
#include <time.h> /* for seeding the random number generator */
#include "output.h"

class Dropper;

/**
 * A representation of a group of endpoints (servers) with a shared router
 * @reset: reset state of an endpoint (e.g. when it loses sync with the arbiter)
 * @new_packets: enqueue packets from the network stack to these endpoints
 * @push_batch: enqueue packets to these endpoints from the network
 * @pull_batch: dequeue packets from these endpoints to send on the network
 */
class EndpointGroup {
public:
	EndpointGroup();
	virtual ~EndpointGroup();
	virtual void assign_to_core(EmulationOutput *out) = 0;
	virtual void reset(uint16_t id) = 0;
	virtual void new_packets(struct emu_packet **pkts, uint32_t n_pkts,
			uint64_t cur_time, Dropper *dropper) = 0;
	virtual void push_batch(struct emu_packet **pkts, uint32_t n_pkts) = 0;
	virtual uint32_t pull_batch(struct emu_packet **pkts, uint32_t n_pkts,
			uint64_t cur_time, Dropper *dropper) = 0;
};

/**
 * A class for constructing endpoint groups of different types.
 * @NewEndpointGroup: constructs an endpoint group
 */
class EndpointGroupFactory {
public:
	static EndpointGroup *NewEndpointGroup(enum EndpointType type,
			uint16_t start_id, void *args,
			struct emu_topo_config *topo_config);
};

#endif /* ENDPOINT_GROUP_H_ */
