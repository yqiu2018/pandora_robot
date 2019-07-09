/*
 * File      : nftl_layer.c
 * COPYRIGHT (C) 2012-2014, Shanghai Real-Thread Electronic Technology Co.,Ltd
 *
 * Change Logs:
 * Date           Author       Notes
 * 2014-01-21     Bernard      Add COPYRIGHT file header.
 */

#include "nftl_internal.h"
#include <string.h>

#ifdef RTTHREAD_VERSION
#ifdef RT_USING_LOGTRACE
struct log_trace_session nftl_log_session = {{"NFTL"}, LOG_TRACE_LEVEL_INFO};
#else
#define log_trace_register_session(...)
#endif
#else

#ifdef NFTL_USING_STATIC
#define PRAGMA(x)                   _Pragma(#x)
#define ALIGN(n)                    PRAGMA(data_alignment=n)

ALIGN(8192)
struct nftl_layer _nftl_layer =
{
    .resv0 = 1,
    .resv1 = 2,
    0
};

/* set the initial value to put .data section */
#endif

rt_err_t nftl_layer_attach_nand(struct rt_mtd_nand_device* device)
{
    struct nftl_layer *layer;

    if(!cpu_check())
    {
        log_trace("unsupported CPU\n");
        return -RT_EIO;
    }

    layer = NFTL_LAYER(device);

    log_trace("NFTL version %d.%d.%d released for wasion, attach to device: %s\n",
              NFTL_VERSION, NFTL_SUBVERSION, NFTL_REVISION, device->parent.name);
    log_trace("COPYRIGHT (C) 2013-2016, Shanghai Real-Thread Technology Co., Ltd\n");
    log_trace("build date: %s\n", __DATE__);

#ifdef NFTL_USING_STATIC
    layer = &_nftl_layer;
#else
    /* allocate mapping table */
    layer = (struct nftl_layer*) NFTL_MALLOC (sizeof(struct nftl_layer));
    if (layer == RT_NULL)
        return -RT_ENOMEM;
#endif

    /* set mapping table as user data */
    device->parent.user_data = (rt_uint32_t)layer;

    return RT_EOK;
}
#endif

/* initialize NFTL layer */
rt_err_t nftl_layer_init(struct rt_mtd_nand_device* device)
{
    struct nftl_layer* layer = NFTL_LAYER(device);

    if(!cpu_check())
    {
        log_trace("unsupported CPU\n");
        return -RT_EIO;
    }

    /* clear to zero */
    memset(layer, 0, sizeof(struct nftl_layer));

    layer->mapping_table_block = 0;
    layer->resv0 = 0xff;
    layer->resv1 = 0xff;
    // memset(&(layer->page_mapping_cache), 0xff, sizeof(struct nftl_page_mapping_cache));
    nftl_recent_init(device);

    log_trace_register_session(&nftl_log_session);

    /* initialize mapping table */
    return nftl_mapping_init(device);
}

/* reset NFTL layer */
rt_err_t nftl_layer_reset(const char* device_name)
{
    rt_device_t dev;
    struct rt_mtd_nand_device* device;
    struct nftl_layer* layer;
    struct nftl_mapping *mapping;
    rt_uint16_t block_index;

    dev = rt_device_find(device_name);
    if (dev == RT_NULL)
    {
        log_trace(LOG_TRACE_ERROR NFTL_MOD "no device found.\n");
        return -RT_ERROR;
    }

    device = RT_MTD_NAND_DEVICE(dev);
    RT_ASSERT(device != RT_NULL);

    layer = NFTL_LAYER(device);
    layer->mapping_table_block = 0;
    layer->resv0 = 0xff;
    layer->resv1 = 0xff;

    // memset(&(layer->page_mapping_cache), 0xff, sizeof(struct nftl_page_mapping_cache));
    nftl_recent_init(device);

    mapping = NFTL_MAPPING(device);

    /* zero mapping table */
    memset(mapping, 0, sizeof(struct nftl_mapping));

    /* set to 0xff for phy_block */
    memset(mapping->logic_blocks, 0xff, sizeof(mapping->logic_blocks));

    for (block_index = 0; block_index < device->block_total; block_index ++)
    {
        mapping->phyical_status[block_index].free = 0x01;
    }
    NFTL_MAPPING_BLOCK(device) = RT_UINT16_MAX;

    return RT_EOK;
}
