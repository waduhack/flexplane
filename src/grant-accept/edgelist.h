/*
 * partitioned-msgs.h
 *
 *  Created on: Apr 27, 2014
 *      Author: yonch
 */

#ifndef PARTITIONED_EDGELIST_H_
#define PARTITIONED_EDGELIST_H_

#include "grant-accept.h"

#define GA_MAX_PARTITION_EDGES		GA_PARTITION_N_NODES

/**
 * A container for edges from some paritition x to some partition y.
 *
 * Assumes each partition has at most GA_MAX_PARTITION_EDGES edges to
 *  another partition.
 *
 * @param n: number of edges
 * @param edge: the actual edges
 *
 * To eliminate false sharing on x86, each slot is aligned to 64 bytes
 */
struct ga_edgelist {
	uint32_t n;
	struct ga_edge edge[GA_MAX_PARTITION_EDGES];
} __attribute__((align(64)));

/**
 * A container for all edgelists in the graph. See ga_edgelist for assumptions.
 */
struct ga_partd_edgelist {
	struct ga_edgelist elist[GA_N_PARTITIONS][GA_N_PARTITIONS];
};

/**
 * Adds edge to edgelist 'edgelist'
 */
void inline ga_edgelist_add(struct ga_edgelist *edgelist, uint16_t src,
		uint16_t dst)
{
	uint32_t n = edgelist->n++;
	edgelist->edge[n].src = src;
	edgelist->edge[n].dst = dst;
}

/**
 * Adds edge to the paritioned edgelist 'pedgelist'
 */
void inline ga_partd_edgelist_add(struct ga_partd_edgelist *pel,
		uint16_t src, uint16_t dst)
{
	uint16_t src_partition = src / GA_PARTITION_N_NODES;
	uint16_t dst_partition = dst / GA_PARTITION_N_NODES;
	ga_edgelist_add(&pel->elist[src_partition][dst_partition], src, dst);
}

/**
 * Deletes all edges in source partition 'src_partition'
 */
void inline ga_partd_edgelist_reset(struct ga_partd_edgelist *pel,
		uint16_t src_partition)
{
	uint32_t i;
	for(i = 0; i < GA_N_PARTITIONS; i++)
		pel->elist[src_partition][i].n = 0;
}

/**
 * Adds all edges destined to a partition to an adjacency structure,
 *   keyed by destination.
 * @param pedgelist: the partitioned edgelist structure
 * @param dst_partition: which destination partition to extract
 * @param dest_adj: the adjacency structure where edges to the destination will
 *    be added
 */
void inline ga_msgs_to_adj(struct ga_partd_edgelist *pel,
		uint16_t dst_partition, struct ga_adj *dest_adj)
{
	uint32_t i;
	for(i = 0; i < GA_N_PARTITIONS; i++) {
		struct ga_edgelist *edgelist = &pel->elist[i][dst_partition];
		ga_edges_to_adj(&edgelist->edge, edgelist->n, dest_adj);
	}
}

#endif /* PARTITIONED_EDGELIST_H_ */