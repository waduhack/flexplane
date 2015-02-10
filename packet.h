/*
 * packet.h
 *
 *  Created on: June 23, 2014
 *      Author: aousterh
 */

#ifndef PACKET_H_
#define PACKET_H_

#include "../protocol/flags.h"
#include <inttypes.h>

/* alignment macros based on /net/pkt_sched.h */
#define EMU_ALIGNTO				64
#define EMU_ALIGN(len)			(((len) + EMU_ALIGNTO-1) & ~(EMU_ALIGNTO-1))

/**
 * A representation of an MTU-sized packet in the emulated network.
 * @src: the id of the source endpoint in the emulation
 * @dst: the id of the destination endpoint in the emulation
 * @flow: id to disambiguate between flows with the same src and dst ips
 * @id: sequential id within this flow, to enforce ordering of packets
 * @flags: flags indicating marks or other info to be conveyed to endpoints
 */
struct emu_packet {
	uint16_t	src;
	uint16_t	dst;
	uint16_t	flow;
	uint16_t	id;
	uint16_t	flags;
};

/**
 * Initialize a packet with @src, @dst, @flow, and @id. @areq_data provides
 * additional information in an array of bytes. The use of this data varies by
 * emulated scheme.
 */
static inline
void packet_init(struct emu_packet *packet, uint16_t src, uint16_t dst,
		uint16_t flow, uint16_t id, uint8_t *areq_data);

/**
 * Creates a packet, returns a pointer to the packet.
 */
static inline
struct emu_packet *create_packet(struct emu_state *state, uint16_t src,
		uint16_t dst, uint16_t flow, uint16_t id, uint8_t *areq_data);

/**
 * Frees a packet when an emulation algorithm is done running.
 */
static inline
void free_packet(struct emu_state *state, struct emu_packet *packet);

/**
 * Returns the private part of the endpoint struct.
 */
static inline
void *packet_priv(struct emu_packet *packet);

#endif /* PACKET_H_ */
