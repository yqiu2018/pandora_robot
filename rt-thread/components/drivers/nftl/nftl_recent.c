/*
 * File      : nftl_recent.c
 *             recent implementation in NFTL
 * COPYRIGHT (C) 2012-2014, Shanghai Real-Thread Electronic Technology Co.,Ltd
 *
 * Change Logs:
 * Date           Author       Notes
 * 2014-01-21     Bernard      Add COPYRIGHT file header.
 */

#include <nftl.h>
#include "nftl_internal.h"

void nftl_recent_init(struct rt_mtd_nand_device* device)
{
    struct nftl_layer* layer;
    layer = NFTL_LAYER(device);

    RT_ASSERT(layer != RT_NULL);

    rt_memset(layer->recent_blocks, 0x00, sizeof(layer->recent_blocks));
    layer->recent_index = 0;
}

void nftl_recent_push(struct rt_mtd_nand_device* device, rt_uint16_t block)
{
    struct nftl_layer* layer;

    layer = NFTL_LAYER(device);

    if (layer->recent_index > NFTL_BLOCKS_RECENT_MAX)
    {
        log_trace(LOG_TRACE_ERROR NFTL_MOD"recent queue error: %d\n", layer->recent_index);
        RT_ASSERT(0);
    }

    if (layer->recent_index == NFTL_BLOCKS_RECENT_MAX)
    {
        /* flush mapping table and flush recent queue */
        log_trace(LOG_TRACE_DEBUG NFTL_MOD"flush mapping table because recent queue full.\n");
        nftl_mapping_flush(device, RT_FALSE);
        log_trace(LOG_TRACE_DEBUG NFTL_MOD"recent queue index: %d\n", layer->recent_index);
    }

    log_trace(LOG_TRACE_DEBUG NFTL_MOD"push %d to recent block queue @ %d.\n", block,
              layer->recent_index);
    layer->recent_blocks[layer->recent_index] = block;
    layer->recent_index ++;
}

void nftl_recent_flush(struct rt_mtd_nand_device* device)
{
    rt_uint32_t index;
    rt_uint32_t prefix;
    struct nftl_layer* layer;
    struct nftl_mapping* mapping;

    layer = NFTL_LAYER(device);
    mapping = NFTL_MAPPING(device);
    RT_ASSERT(mapping != RT_NULL);

    if (layer->recent_index < NFTL_BLOCKS_RECENT_MAX/4)
    {
        log_trace(LOG_TRACE_DEBUG NFTL_MOD"recent block queue is too short.\n");
        return;
    }

    if (layer->recent_index > NFTL_BLOCKS_RECENT_MAX)
    {
        log_trace(LOG_TRACE_ERROR NFTL_MOD"recent queue error: %d\n", layer->recent_index);
        RT_ASSERT(0);
    }

    prefix = (layer->recent_index * 3)/4;
    log_trace(LOG_TRACE_DEBUG NFTL_MOD"flush recent block queue, chunk = %d.\n", prefix);

    for (index = 0; index < prefix; index ++)
    {
        mapping->phyical_status[layer->recent_blocks[index]].free = 0x01;
        mapping->phyical_status[layer->recent_blocks[index]].used = 0x00;
        mapping->phyical_status[layer->recent_blocks[index]].recent = 0x00;

        log_trace(LOG_TRACE_DEBUG NFTL_MOD"pop %d from recent block queue.\n", layer->recent_blocks[index]);
    }
    rt_memmove(&layer->recent_blocks[0], &layer->recent_blocks[prefix],
               (layer->recent_index - prefix) * sizeof(rt_uint16_t));
    layer->recent_index = layer->recent_index - (rt_uint16_t)prefix;

    log_trace(LOG_TRACE_DEBUG NFTL_MOD "recent index=%d\n", layer->recent_index);
}

