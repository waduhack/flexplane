/*
 * router.h
 *
 *  Created on: June 23, 2014
 *      Author: aousterh
 */

#ifndef ROUTER_H_
#define ROUTER_H_

#include <inttypes.h>

struct emu_packet;

/**
 * A representation of a router in the emulated network.
 * @push: enqueue a single packet to this router
 * @pull: dequeue a single packet from port output at this router
 * @push_batch: enqueue a batch of several packets to this router
 * @pull_batch: dequeue a batch of several packets from this router
 * @id: the unique id of this router
 */
class Router {
public:
	Router(uint16_t id) : id(id) {};
	virtual ~Router() {};
	virtual void push(struct emu_packet *packet) = 0;
	virtual void pull(uint16_t output, struct emu_packet **packet) = 0;
	virtual void push_batch(struct emu_packet **pkts, uint32_t n_pkts) = 0;
	virtual uint32_t pull_batch(struct emu_packet **pkts, uint32_t n_pkts) = 0;
private:
	uint16_t id;
};

#endif /* ROUTER_H_ */
