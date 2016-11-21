/*
 * NUMA support, based on the x86 implementation.
 *
 * Copyright (C) 2015 Cavium Inc.
 * Author: Ganapatrao Kulkarni <gkulkarni@cavium.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "numa: " fmt

#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/of.h>

#include <asm/sections.h>

struct pglist_data *node_data[MAX_NUMNODES] __read_mostly;
EXPORT_SYMBOL(node_data);
nodemask_t numa_nodes_parsed __initdata;
static int cpu_to_node_map[NR_CPUS] = { [0 ... NR_CPUS-1] = NUMA_NO_NODE };

static u8 numa_distance[MAX_NUMNODES][MAX_NUMNODES];
static bool numa_off;

static __init int numa_parse_early_param(char *opt)
{
	if (!opt)
		return -EINVAL;
	if (!strncmp(opt, "off", 3))
		numa_off = true;

	return 0;
}
early_param("numa", numa_parse_early_param);

cpumask_var_t node_to_cpumask_map[MAX_NUMNODES];
EXPORT_SYMBOL(node_to_cpumask_map);

#ifdef CONFIG_DEBUG_PER_CPU_MAPS

/*
 * Returns a pointer to the bitmask of CPUs on Node 'node'.
 */
const struct cpumask *cpumask_of_node(int node)
{
	if (WARN_ON(node >= nr_node_ids))
		return cpu_none_mask;

	if (WARN_ON(node_to_cpumask_map[node] == NULL))
		return cpu_online_mask;

	return node_to_cpumask_map[node];
}
EXPORT_SYMBOL(cpumask_of_node);

#endif

static void map_cpu_to_node(unsigned int cpu, int nid)
{
	set_cpu_numa_node(cpu, nid);
	if (nid >= 0)
		cpumask_set_cpu(cpu, node_to_cpumask_map[nid]);
}

void numa_clear_node(unsigned int cpu)
{
	int nid = cpu_to_node(cpu);

	if (nid >= 0)
		cpumask_clear_cpu(cpu, node_to_cpumask_map[nid]);
	set_cpu_numa_node(cpu, NUMA_NO_NODE);
}

/*
 * Allocate node_to_cpumask_map based on number of available nodes
 * Requires node_possible_map to be valid.
 *
 * Note: cpumask_of_node() is not valid until after this is done.
 * (Use CONFIG_DEBUG_PER_CPU_MAPS to check this.)
 */
static void __init setup_node_to_cpumask_map(void)
{
	int node;

	/* setup nr_node_ids if not done yet */
	if (nr_node_ids == MAX_NUMNODES)
		setup_nr_node_ids();

	/* allocate and clear the mapping */
	for (node = 0; node < nr_node_ids; node++) {
		alloc_bootmem_cpumask_var(&node_to_cpumask_map[node]);
		cpumask_clear(node_to_cpumask_map[node]);
	}

	/* cpumask_of_node() will now work */
	pr_debug("Node to cpumask map for %d nodes\n", nr_node_ids);
}

/*
 *  Set the cpu to node and mem mapping
 */
void numa_store_cpu_info(unsigned int cpu)
{
	map_cpu_to_node(cpu, numa_off ? 0 : cpu_to_node_map[cpu]);
}

void __init early_map_cpu_to_node(unsigned int cpu, int nid)
{
	/* fallback to node 0 */
	if (nid < 0 || nid >= MAX_NUMNODES || numa_off)
		nid = 0;

	cpu_to_node_map[cpu] = nid;

	/*
	 * We should set the numa node of cpu0 as soon as possible, because it
	 * has already been set up online before. cpu_to_node(0) will soon be
	 * called.
	 */
	if (!cpu)
		set_cpu_numa_node(cpu, nid);
}

#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);

static int __init early_cpu_to_node(int cpu)
{
	return cpu_to_node_map[cpu];
}

static int __init pcpu_cpu_distance(unsigned int from, unsigned int to)
{
	if (early_cpu_to_node(from) == early_cpu_to_node(to))
		return LOCAL_DISTANCE;
	else
		return REMOTE_DISTANCE;
}

static void * __init pcpu_fc_alloc(unsigned int cpu, size_t size,
				       size_t align)
{
	int nid = early_cpu_to_node(cpu);

	return  memblock_virt_alloc_try_nid(size, align,
			__pa(MAX_DMA_ADDRESS), MEMBLOCK_ALLOC_ACCESSIBLE, nid);
}

static void __init pcpu_fc_free(void *ptr, size_t size)
{
	memblock_free_early(__pa(ptr), size);
}

void __init setup_per_cpu_areas(void)
{
	unsigned long delta;
	unsigned int cpu;
	int rc;

	/*
	 * Always reserve area for module percpu variables.  That's
	 * what the legacy allocator did.
	 */
	rc = pcpu_embed_first_chunk(PERCPU_MODULE_RESERVE,
				    PERCPU_DYNAMIC_RESERVE, PAGE_SIZE,
				    pcpu_cpu_distance,
				    pcpu_fc_alloc, pcpu_fc_free);
	if (rc < 0)
		panic("Failed to initialize percpu areas.");

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu)
		__per_cpu_offset[cpu] = delta + pcpu_unit_offsets[cpu];
}
#endif

/**
 * numa_add_memblk - Set node id to memblk
 * @nid: NUMA node ID of the new memblk
 * @start: Start address of the new memblk
 * @end:  End address of the new memblk
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int __init numa_add_memblk(int nid, u64 start, u64 end)
{
	int ret;

	if (nid >= MAX_NUMNODES) {
		pr_warn("Node id %u exceeds maximum value\n", nid);
		return -EINVAL;
	}

	ret = memblock_set_node(start, (end - start), &memblock.memory, nid);
	if (ret < 0) {
		pr_err("memblock [0x%llx - 0x%llx] failed to add on node %d\n",
			start, (end - 1), nid);
		return ret;
	}

	node_set(nid, numa_nodes_parsed);
	pr_info("Adding memblock [0x%llx - 0x%llx] on node %d\n",
			start, (end - 1), nid);
	return ret;
}

static u64 __init alloc_node_data_from_nearest_node(int nid, const size_t size)
{
	int i, best_nid, distance;
	u64 pa;
	DECLARE_BITMAP(nodes_map, MAX_NUMNODES);

	bitmap_zero(nodes_map, MAX_NUMNODES);
	bitmap_set(nodes_map, nid, 1);

find_nearest_node:
	best_nid = NUMA_NO_NODE;
	distance = INT_MAX;

	for_each_clear_bit(i, nodes_map, MAX_NUMNODES)
		if (numa_distance[nid][i] < distance) {
			best_nid = i;
			distance = numa_distance[nid][i];
		}

	pa = memblock_alloc_nid(size, SMP_CACHE_BYTES, best_nid);
	if (!pa) {
		BUG_ON(best_nid == NUMA_NO_NODE);
		bitmap_set(nodes_map, best_nid, 1);
		goto find_nearest_node;
	}

	return pa;
}

/**
 * Initialize NODE_DATA for a node on the local memory
 */
static void __init setup_node_data(int nid, u64 start_pfn, u64 end_pfn)
{
	const size_t nd_size = roundup(sizeof(pg_data_t), SMP_CACHE_BYTES);
	u64 nd_pa;
	void *nd;
	int tnid;

	pr_info("Initmem setup node %d [mem %#010Lx-%#010Lx]\n",
		nid, start_pfn << PAGE_SHIFT, (end_pfn << PAGE_SHIFT) - 1);

	nd_pa = memblock_alloc_nid(nd_size, SMP_CACHE_BYTES, nid);
	if (!nd_pa)
		nd_pa = alloc_node_data_from_nearest_node(nid, nd_size);
	nd = __va(nd_pa);

	/* report and initialize */
	pr_info("  NODE_DATA [mem %#010Lx-%#010Lx]\n",
		nd_pa, nd_pa + nd_size - 1);
	tnid = early_pfn_to_nid(nd_pa >> PAGE_SHIFT);
	if (tnid != nid)
		pr_info("    NODE_DATA(%d) on node %d\n", nid, tnid);

	NODE_DATA(nid) = nd;
	memset(NODE_DATA(nid), 0, sizeof(pg_data_t));
	NODE_DATA(nid)->node_id = nid;
	NODE_DATA(nid)->node_start_pfn = start_pfn;
	NODE_DATA(nid)->node_spanned_pages = end_pfn - start_pfn;
}

/**
 * numa_set_distance - Set inter node NUMA distance from node to node.
 * @from: the 'from' node to set distance
 * @to: the 'to'  node to set distance
 * @distance: NUMA distance
 *
 * Set the distance from node @from to @to to @distance.
 * If distance table doesn't exist, a warning is printed.
 *
 * If @from or @to is higher than the highest known node or lower than zero
 * or @distance doesn't make sense, the call is ignored.
 *
 */
void __init numa_set_distance(int from, int to, int distance)
{
	if (from >= MAX_NUMNODES || to >= MAX_NUMNODES ||
			from < 0 || to < 0) {
		pr_warn_once("Warning: node ids are out of bound, from=%d to=%d distance=%d\n",
			    from, to, distance);
		return;
	}

	if ((u8)distance != distance ||
	    (from == to && distance != LOCAL_DISTANCE)) {
		pr_warn_once("Warning: invalid distance parameter, from=%d to=%d distance=%d\n",
			     from, to, distance);
		return;
	}

	numa_distance[from][to] = distance;
}

/**
 * Return NUMA distance @from to @to
 */
int __node_distance(int from, int to)
{
	if (from >= MAX_NUMNODES || to >= MAX_NUMNODES)
		return from == to ? LOCAL_DISTANCE : REMOTE_DISTANCE;
	return numa_distance[from][to];
}
EXPORT_SYMBOL(__node_distance);

#ifdef CONFIG_MEMORY_HOTPLUG
int memory_add_physaddr_to_nid(u64 start)
{
	return 0;
}
EXPORT_SYMBOL_GPL(memory_add_physaddr_to_nid);
#endif

static int __init numa_register_nodes(void)
{
	int nid;
	struct memblock_region *mblk;

	/* Check that valid nid is set to memblks */
	for_each_memblock(memory, mblk)
		if (mblk->nid == NUMA_NO_NODE || mblk->nid >= MAX_NUMNODES) {
			pr_warn("Warning: invalid memblk node %d [mem %#010Lx-%#010Lx]\n",
				mblk->nid, mblk->base,
				mblk->base + mblk->size - 1);
			return -EINVAL;
		}

	/* Finally register nodes. */
	for_each_node_mask(nid, numa_nodes_parsed) {
		unsigned long start_pfn, end_pfn;

		get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
		setup_node_data(nid, start_pfn, end_pfn);
		node_set_online(nid);
	}

	/* Setup online nodes to actual nodes*/
	node_possible_map = numa_nodes_parsed;

	return 0;
}

static int __init numa_init(int (*init_func)(void))
{
	int ret;

	nodes_clear(numa_nodes_parsed);
	nodes_clear(node_possible_map);
	nodes_clear(node_online_map);

	ret = init_func();
	if (ret < 0)
		return ret;

	if (nodes_empty(numa_nodes_parsed)) {
		pr_info("No NUMA configuration found\n");
		return -EINVAL;
	}

	ret = numa_register_nodes();
	if (ret < 0)
		return ret;

	setup_node_to_cpumask_map();

	return 0;
}

/**
 * dummy_numa_init - Fallback dummy NUMA init
 *
 * Used if there's no underlying NUMA architecture, NUMA initialization
 * fails, or NUMA is disabled on the command line.
 *
 * Must online at least one node (node 0) and add memory blocks that cover all
 * allowed memory. It is unlikely that this function fails.
 */
static int __init dummy_numa_init(void)
{
	int ret;
	struct memblock_region *mblk;

	if (numa_off)
		pr_warn("NUMA turned off by user\n"); /* Forced off on command line. */

	pr_info("Faking a node at [mem %#018Lx-%#018Lx]\n",
	        0LLU, PFN_PHYS(max_pfn) - 1);

	for_each_memblock(memory, mblk) {
		ret = numa_add_memblk(0, mblk->base, mblk->base + mblk->size);
		if (!ret)
			continue;

		pr_err("NUMA init failed\n");
		return ret;
	}

	numa_off = true;
	return 0;
}

/**
 * arm64_numa_init - Initialize NUMA
 *
 * Try each configured NUMA initialization method until one succeeds.  The
 * last fallback is dummy single node config encomapssing whole memory.
 */
void __init arm64_numa_init(void)
{
	if (!numa_off) {
		if (!numa_init(of_numa_init))
			return;
	}

	numa_init(dummy_numa_init);
}
