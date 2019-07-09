/*
 * File      : nftl_blk.c
 *             block device interface for NandFlash FTL
 * COPYRIGHT (C) 2012-2014, Shanghai Real-Thread Electronic Technology Co.,Ltd
 *
 * Change Logs:
 * Date           Author       Notes
 * 2012-03-01     Bernard      the first version
 * 2012-11-01     Bernard      Add page mapping
 */

#include <rtthread.h>
#include "nftl_internal.h"

#ifdef RTTHREAD_VERSION /* with RT-Thread RTOS */

static rt_err_t  nftl_blk_init(rt_device_t dev)
{
    rt_uint32_t tick;
    struct rt_mtd_nand_device* device;

    device = RT_MTD_NAND_DEVICE(dev);
    RT_ASSERT(device != RT_NULL);

    /* check the total number of block in MTD nand device */
    if (device->block_end - device->block_start > NFTL_BLOCKS_MAX)
    {
        log_trace(LOG_TRACE_INFO NFTL_MOD "NOTICE: the total number of block is great than the NFTL blocks\n");
        device->block_end = device->block_start + NFTL_BLOCKS_MAX;
    }

    tick = rt_tick_get();
    /* find the mapping block */
    if (nftl_layer_init(device) != RT_EOK)
    {
        /* initialization failed */
        log_trace(LOG_TRACE_ERROR NFTL_MOD "initialize NFTL mapping failed\n");
        return -RT_ERROR;
    }
    tick = rt_tick_get() - tick;

    /* dump duration time */
    log_trace(LOG_TRACE_INFO NFTL_MOD "Initialize NFTL mapping table done, duration: %d second.\n",
              tick/RT_TICK_PER_SECOND);

    return RT_EOK;
}

static rt_err_t nftl_blk_open(rt_device_t dev, rt_uint16_t oflag)
{
    return RT_EOK;
}

static rt_err_t nftl_blk_close(rt_device_t dev)
{
    return RT_EOK;
}

static rt_size_t nftl_blk_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    rt_err_t result;
    rt_uint8_t *ptr;
    rt_uint16_t block_offset, page_offset;
    struct rt_mtd_nand_device* device;

    device = RT_MTD_NAND_DEVICE(dev);
    RT_ASSERT(device != RT_NULL);
    RT_ASSERT(size != 0);

    block_offset = (rt_uint16_t)(pos / device->pages_per_block);
    page_offset  = (rt_uint16_t)(pos % device->pages_per_block);

    log_trace(LOG_TRACE_DEBUG NFTL_MOD"blkread, position: %d, size: %d\n", pos, size * device->page_size);

    if (size > 1)
    {
        rt_size_t chunk, rest;

        ptr = (rt_uint8_t*) buffer;
        rest = size;

        /* multi-page read */
        while (rest > 0)
        {
            if (page_offset + rest > device->pages_per_block)
                chunk = device->pages_per_block - page_offset;
            else
                chunk = rest;

            result = nftl_read_multi_page(device, (rt_uint16_t)block_offset, (rt_uint16_t)page_offset, ptr, chunk);
            if (result != RT_EOK)
            {
                /* set errno */
                log_trace(LOG_TRACE_ERROR NFTL_MOD"Read multi-page failed: %d\n", result);
                rt_set_errno(result);

                return 0;
            }

            ptr += chunk * device->page_size;
            rest = rest - chunk;
            page_offset = 0;
            block_offset ++;
        }

        return size;
    }

    /* read one page */
    result = nftl_read_page(device, (rt_uint16_t)block_offset, (rt_uint16_t)page_offset, (rt_uint8_t*)buffer);
    if (result == RT_EOK)
    {
        return size;
    }

    /* set errno */
    log_trace(LOG_TRACE_ERROR NFTL_MOD"Read page failed: %d\n", result);
    rt_set_errno(result);

    return 0;
}

static rt_size_t nftl_blk_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    rt_err_t result;
    rt_uint8_t *ptr;
    rt_uint16_t block_offset, page_offset;
    struct rt_mtd_nand_device* device;

    device = RT_MTD_NAND_DEVICE(dev);
    RT_ASSERT(device != RT_NULL);

    block_offset = (rt_uint16_t) (pos / device->pages_per_block);
    page_offset  = (rt_uint16_t) (pos % device->pages_per_block);

    log_trace(LOG_TRACE_DEBUG NFTL_MOD"blkwrite, position: %d, size: %d\n", pos, size * device->page_size);

    if (size > 1)
    {
        rt_size_t chunk, rest;

        ptr = (rt_uint8_t*) buffer;
        rest = size;

        /* multi-page write */
        while (rest > 0)
        {
            if (page_offset + rest > device->pages_per_block)
                chunk = device->pages_per_block - page_offset;
            else
                chunk = rest;

            result = nftl_write_multi_page(device, (rt_uint16_t)block_offset, (rt_uint16_t)page_offset, ptr, chunk);
            if (result != RT_EOK)
            {
                /* set errno */
                log_trace(LOG_TRACE_ERROR NFTL_MOD"Write multi-page failed: %d\n", result);
                rt_set_errno(result);
                return 0;
            }

            ptr += chunk * device->page_size;
            rest = rest - chunk;
            page_offset = 0;
            block_offset ++;
        }

        return size;
    }

    /* write one page */
    result = nftl_write_page(device, (rt_uint16_t)block_offset, (rt_uint16_t)page_offset, (rt_uint8_t*)buffer);
    if (result == RT_EOK)
        return size;

    /* set errno */
    log_trace(LOG_TRACE_ERROR NFTL_MOD"Write page failed: %d\n", result);
    rt_set_errno(result);
    return 0;
}

static rt_err_t nftl_blk_control(rt_device_t dev, rt_uint8_t cmd, void *args)
{
    int pages;
    struct rt_mtd_nand_device *device;
    struct rt_device_blk_geometry *geometry;
    struct rt_device_blk_sectors *sectors;

    device = RT_MTD_NAND_DEVICE(dev);
    RT_ASSERT(device != RT_NULL);

    switch (cmd)
    {
    case RT_DEVICE_CTRL_BLK_GETGEOME:
        geometry = (struct rt_device_blk_geometry*) args;
        RT_ASSERT(geometry != RT_NULL);

        geometry->block_size = device->pages_per_block * device->page_size;
        geometry->bytes_per_sector = device->page_size;
        pages = device->block_total * device->pages_per_block;
        geometry->sector_count = (rt_uint32_t)(pages * 0.8);
        break;

    case RT_DEVICE_CTRL_BLK_SYNC:
        log_trace(LOG_TRACE_DEBUG NFTL_MOD"NFTL does synchronous operation\n");
        nftl_mapping_flush(device, RT_TRUE);
        break;

    case RT_DEVICE_CTRL_BLK_ERASE:
        sectors = (struct rt_device_blk_sectors*) args;
        log_trace(LOG_TRACE_DEBUG NFTL_MOD"erase sector[%d - %d]\n", sectors->sector_begin,
                  sectors->sector_end);
        nftl_erase_pages(device, sectors->sector_begin, sectors->sector_end);
        break;
    }

    return RT_EOK;
}

/**
 * This function attaches the NFTL layer to a MTD device
 *
 * @param mtd_device the name of MTD device
 *
 * @return -RT_EOK on successfully attached,
 * 		   -RT_ERROR on not found MTD device,
 *         -RT_ENOMEM on out of memory.
 */
rt_err_t nftl_attach(const char* mtd_device)
{
    rt_device_t device;
    rt_err_t result = RT_EOK;
    struct nftl_layer *layer;

    log_trace("NFTL version %d.%d.%d, attach to device: %s\n",
              NFTL_VERSION, NFTL_SUBVERSION, NFTL_REVISION, mtd_device);
    log_trace("Shanghai Real-Thread Electronic Technology Co.,Ltd\n");

    device = rt_device_find(mtd_device);
    if (device != RT_NULL)
    {
        if (device->type == RT_Device_Class_MTD)
        {
            /* set device interface */
            device->init	= nftl_blk_init;
            device->open	= nftl_blk_open;
            device->read	= nftl_blk_read;
            device->write	= nftl_blk_write;
            device->close	= nftl_blk_close;
            device->control = nftl_blk_control;

            log_trace(LOG_TRACE_INFO NFTL_MOD"NFTL layer size: %d\n", sizeof(struct nftl_layer));
            log_trace(LOG_TRACE_INFO NFTL_MOD"the maximum number of block: %d\n", NFTL_BLOCKS_MAX);
            log_trace(LOG_TRACE_INFO NFTL_MOD"the maximum number of recent block: %d\n", NFTL_BLOCKS_RECENT_MAX);

            /* allocate mapping table */
            layer = (struct nftl_layer*) NFTL_MALLOC (sizeof(struct nftl_layer));
            if (layer == RT_NULL)
                return -RT_ENOMEM;

            /* set mapping table as user data */
            device->user_data = (void*)layer;
        }
    }
    else
    {
        result = -RT_ERROR;
    }

    return result;
}
#endif

