/*
 * File      : nftl_util.c
 * COPYRIGHT (C) 2012-2014, Shanghai Real-Thread Electronic Technology Co.,Ltd
 *
 * Change Logs:
 * Date           Author       Notes
 * 2014-01-21     Bernard      Add COPYRIGHT file header.
 */

#include "nftl_internal.h"
#include <stdarg.h>

// #define RT_USING_FINSH
#ifdef RT_USING_FINSH
#include <finsh.h>
#else
#define FINSH_FUNCTION_EXPORT(name,desc)
#define FINSH_FUNCTION_EXPORT_ALIAS(name,alias,desc)
#endif

#define __is_print(ch) ((unsigned int)((ch) - ' ') < 127u - ' ')

#ifndef RTTHREAD_VERSION
rt_uint8_t log_trace_level = LOG_TRACE_LEVEL_WARNING;

/******************************************************************************
 *
 * Log Trace API
 *
 ******************************************************************************/
void log_trace_set_level(int level)
{
    log_trace_level = level;
}
FINSH_FUNCTION_EXPORT_ALIAS(log_trace_set_level, log_level, set log level);

extern void log_vprintf(const char * format, va_list arg);
void log_trace(const char *fmt, ...)
{
    va_list args;

    if (fmt[0] == '<' && fmt[2] == '>')
    {
        rt_uint8_t  level;
        level = fmt[1] - '0';
        if (level > log_trace_level)
            return;
    }

    va_start(args, fmt);
    log_vprintf(fmt, args);
    va_end(args);
}
#else
#ifdef log_trace
#undef log_trace
#endif

/* use rt_kprintf for dump */
#define log_trace rt_kprintf
#endif

rt_uint16_t nftl_get_logic_block(struct rt_mtd_nand_device* device, rt_uint16_t phy_block)
{
    rt_uint16_t index;
    struct nftl_mapping* mapping;

    mapping = NFTL_MAPPING(device);
    for (index = 0; index < device->block_total; index ++)
    {
        if (mapping->logic_blocks[index] == phy_block)
            return index;
    }

    return RT_UINT16_MAX;
}

rt_bool_t nftl_is_empty_page(struct rt_mtd_nand_device* device, rt_uint32_t* ptr)
{
    rt_uint32_t page_index;

    for (page_index = 0; page_index < (rt_uint32_t)device->page_size/4; page_index ++)
    {
        if (*ptr != 0x00)
            return RT_FALSE;
        ptr ++;
    }

    return RT_TRUE;
}

struct nftl_log_item
{
    rt_uint8_t action;
    rt_uint8_t resv;

    rt_uint8_t src_page;
    rt_uint8_t dst_page;

    rt_uint16_t src_block;
    rt_uint16_t dst_block;
};

static struct nftl_log_item items[NFTL_LOG_ITEMS];	/* 8 x 512 = 4096 kB */
void nftl_log_action(struct rt_mtd_nand_device* device, int action, int src, int dst)
{
    struct nftl_log_item *item;
    struct nftl_mapping* mapping;

    mapping = NFTL_MAPPING(device);
    item = &items[mapping->log_index % NFTL_LOG_ITEMS];
    mapping->log_index ++;

    item->action = action;
    item->src_block = src / device->pages_per_block;
    item->src_page  = src % device->pages_per_block;

    item->dst_block = dst / device->pages_per_block;
    item->dst_page  = dst % device->pages_per_block;
}

void nftl_log_session_done(struct rt_mtd_nand_device* device, int sn)
{
    struct nftl_log_item *item;
    struct nftl_mapping* mapping;

    mapping = NFTL_MAPPING(device);
    item = &items[mapping->log_index % NFTL_LOG_ITEMS];
    mapping->log_index ++;

    item->action = NFTL_ACTION_SESSION_DONE;
    item->resv = 0;
    item->src_page = item->dst_page = 0;
    item->src_block = (sn & 0xfff);
    item->dst_block = (sn & 0xfff0000) >> 16;
}

void nftl_log_init(struct rt_mtd_nand_device* device, int block, int page)
{
    rt_uint8_t *ptr;
    struct nftl_layer *layer;
    struct nftl_mapping* mapping;

    layer = NFTL_LAYER(device);
    mapping = NFTL_MAPPING(device);
    layer->log_ptr = (rt_uint8_t *)&items[0];
    if (mapping->version == 0x00 || mapping->version == 0xffffffff) /* old version */
    {
        mapping->log_index = 0;
        memset(items, 0x00, sizeof(items));
    }
    else
    {
        ptr = layer->log_ptr;
        rt_mtd_nand_read(device, block * device->pages_per_block + page, ptr, device->page_size, RT_NULL, device->oob_size);

        ptr += device->page_size;
        rt_mtd_nand_read(device, block * device->pages_per_block + page + 1, ptr, device->page_size, RT_NULL, device->oob_size);
    }
}

void nftl_log_write(struct rt_mtd_nand_device* device, int block, int page)
{
    rt_uint8_t *ptr;
    struct nftl_layer *layer;

    layer = NFTL_LAYER(device);

    ptr = layer->log_ptr;
    rt_mtd_nand_write(device, block * device->pages_per_block + page, ptr, device->page_size, RT_NULL, device->oob_size);

    ptr += device->page_size;
    rt_mtd_nand_write(device, block * device->pages_per_block + page + 1, ptr, device->page_size, RT_NULL, device->oob_size);
}

void nftl_dump_mapping(const char* nand_device)
{
    rt_device_t dev;
    struct rt_mtd_nand_device* device;
    struct nftl_mapping* mapping;
    rt_uint32_t index;
    rt_uint16_t logic_block;

    dev = rt_device_find(nand_device);
    if (dev == RT_NULL)
    {
        log_trace("No such NAND device\n");
        return;
    }

    device = RT_MTD_NAND_DEVICE(dev);
    mapping = NFTL_MAPPING(device);

    log_trace("u => current used block\n");
    log_trace("f => free block\n");
    log_trace("r => recent used block\n");
    log_trace("b => bad block\n");

    log_trace("+---------------------------+---+---+---+---+--------------+\n");
    log_trace("| phy block <== logic block | u | f | r | b | erase times  |\n");
    log_trace("+---------------------------+---+---+---+---+--------------+\n");

    for (index = 0; index < device->block_total; index ++)
    {
        logic_block = nftl_get_logic_block(device, (rt_uint16_t)index);
        if (logic_block != RT_UINT16_MAX)
        {
            log_trace("| %9d <== %-11d | %d | %d | %d | %d |  %11d |\n", index, logic_block,
                      mapping->phyical_status[index].used,
                      mapping->phyical_status[index].free,
                      mapping->phyical_status[index].recent,
                      mapping->phyical_status[index].bad,
                      mapping->phyical_status[index].erase_times);
        }
        else
        {
            if (NFTL_MAPPING_BLOCK(device) == index)
            {
                log_trace("| %9d (mapping table) | %d | %d | %d | %d |  %11d |\n", index,
                          mapping->phyical_status[index].used,
                          mapping->phyical_status[index].free,
                          mapping->phyical_status[index].recent,
                          mapping->phyical_status[index].bad,
                          mapping->phyical_status[index].erase_times);
            }
            else if (mapping->phyical_status[index].used)
            {
                log_trace("| %9d (bad)           | %d | %d | %d | %d |  %11d |\n", index,
                          mapping->phyical_status[index].used,
                          mapping->phyical_status[index].free,
                          mapping->phyical_status[index].recent,
                          mapping->phyical_status[index].bad,
                          mapping->phyical_status[index].erase_times);
            }
            else
            {
                if (mapping->phyical_status[index].free)
                    continue;

                if (mapping->phyical_status[index].bad)
                {
                    log_trace("| %9d <bad block>     | %d | %d | %d | %d |  %11d |\n", index,
                              mapping->phyical_status[index].used,
                              mapping->phyical_status[index].free,
                              mapping->phyical_status[index].recent,
                              mapping->phyical_status[index].bad,
                              mapping->phyical_status[index].erase_times);
                }
                else
                {
                    log_trace("| %9d                 | %d | %d | %d | %d |  %11d |\n", index,
                              mapping->phyical_status[index].used,
                              mapping->phyical_status[index].free,
                              mapping->phyical_status[index].recent,
                              mapping->phyical_status[index].bad,
                              mapping->phyical_status[index].erase_times);
                }
            }
        }
    }
    log_trace("+---------------------------+---+---+---+---+--------------+\n");
}
FINSH_FUNCTION_EXPORT_ALIAS(nftl_dump_mapping, dump, dump mapping on the flash);

void nftl_dump_mapping_all(const char* nand_device)
{
    rt_device_t dev;
    struct rt_mtd_nand_device* device;
    struct nftl_mapping* mapping;
    rt_uint32_t index;
    rt_uint16_t logic_block;

    dev = rt_device_find(nand_device);
    if (dev == RT_NULL)
    {
        log_trace("No such NAND device\n");
        return;
    }

    device = RT_MTD_NAND_DEVICE(dev);
    mapping = NFTL_MAPPING(device);

    log_trace("SN: 0x%08x\n", mapping->sn);
    log_trace("u => current used block\n");
    log_trace("f => free block\n");
    log_trace("r => recent used block\n");
    log_trace("b => bad block\n");

    log_trace("+---------------------------+---+---+---+---+--------------+\n");
    log_trace("| phy block <== logic block | u | f | r | b | erase times  |\n");
    log_trace("+---------------------------+---+---+---+---+--------------+\n");

    for (index = 0; index < device->block_total; index ++)
    {
        logic_block = nftl_get_logic_block(device, (rt_uint16_t)index);
        if (logic_block != RT_UINT16_MAX)
        {
            log_trace("| %9d <== %-11d | %d | %d | %d | %d |  %11d |\n", index, logic_block,
                      mapping->phyical_status[index].used,
                      mapping->phyical_status[index].free,
                      mapping->phyical_status[index].recent,
                      mapping->phyical_status[index].bad,
                      mapping->phyical_status[index].erase_times);
        }
        else
        {
            if (NFTL_MAPPING_BLOCK(device) == index)
            {
                log_trace("| %9d (mapping table) | %d | %d | %d | %d |  %11d |\n", index,
                          mapping->phyical_status[index].used,
                          mapping->phyical_status[index].free,
                          mapping->phyical_status[index].recent,
                          mapping->phyical_status[index].bad,
                          mapping->phyical_status[index].erase_times);
            }
            else if (mapping->phyical_status[index].used)
            {
                log_trace("| %9d (bad)           | %d | %d | %d | %d |  %11d |\n", index,
                          mapping->phyical_status[index].used,
                          mapping->phyical_status[index].free,
                          mapping->phyical_status[index].recent,
                          mapping->phyical_status[index].bad,
                          mapping->phyical_status[index].erase_times);
            }
            else
            {
                // if (mapping->phyical_status[index].free) continue;

                if (mapping->phyical_status[index].bad)
                {
                    log_trace("| %9d <bad block>     | %d | %d | %d | %d |  %11d |\n", index,
                              mapping->phyical_status[index].used,
                              mapping->phyical_status[index].free,
                              mapping->phyical_status[index].recent,
                              mapping->phyical_status[index].bad,
                              mapping->phyical_status[index].erase_times);
                }
                else
                {
                    log_trace("| %9d                 | %d | %d | %d | %d |  %11d |\n", index,
                              mapping->phyical_status[index].used,
                              mapping->phyical_status[index].free,
                              mapping->phyical_status[index].recent,
                              mapping->phyical_status[index].bad,
                              mapping->phyical_status[index].erase_times);
                }
            }
        }
    }
    log_trace("+---------------------------+---+---+---+---+--------------+\n");
}
FINSH_FUNCTION_EXPORT_ALIAS(nftl_dump_mapping_all, dump_all, dump all block mapping on the flash);

void nftl_dump_page_mapping(struct nftl_page_mapping *page_mapping)
{
    rt_uint32_t index;

    log_trace("+--------------------------------------+\n");
    log_trace("| physical block: %4d, nfree:%3d      |\n", page_mapping->phy_block, page_mapping->next_free);
    log_trace("+--------------------------------------+\n");
    log_trace("| logic page => physical page          |\n");
    log_trace("+--------------------------------------+\n");
    for (index = 0; index < NFTL_PAGE_IN_BLOCK_MAX; index ++)
    {
        if (page_mapping->logic_pages[index] != 0xff)
            log_trace("| %3d ==> %3d                          |\n", index, page_mapping->logic_pages[index]);
    }
    log_trace("+--------------------------------------+\n");
}

void nftl_dump_block(const char* nand_device, int block)
{
    rt_device_t dev;
    struct nftl_page_mapping page_mapping;

    dev = rt_device_find(nand_device);
    if (dev == RT_NULL)
    {
        log_trace("No NAND Device\n");
        return;
    }

    nftl_page_get_mapping(RT_MTD_NAND_DEVICE(dev), block, &page_mapping);
    nftl_dump_page_mapping(&page_mapping);
}
FINSH_FUNCTION_EXPORT_ALIAS(nftl_dump_block, dump_block, dump block with device and block);

void dump_hex(void *ptr, int buflen)
{
    unsigned char *buf = (unsigned char*)ptr;
    int i, j;
    for (i=0; i<buflen; i+=16)
    {
        log_trace("%06x: ", i);
        for (j=0; j<16; j++)
            if (i+j < buflen)
                log_trace("%02x ", buf[i+j]);
            else
                log_trace("   ");
        log_trace(" ");
        for (j=0; j<16; j++)
            if (i+j < buflen)
                log_trace("%c", __is_print(buf[i+j]) ? buf[i+j] : '.');
        log_trace("\n");
    }
}

void nftl_check_mapping(const char* nand_device)
{
    rt_uint16_t block_index, logic_block;
    rt_device_t dev;
    struct rt_mtd_nand_device* device;
    struct nftl_mapping* mapping;

    dev = rt_device_find(nand_device);
    if (dev == RT_NULL)
    {
        log_trace("No NAND Device\n");
        return;
    }

    device = RT_MTD_NAND_DEVICE(dev);
    mapping = NFTL_MAPPING(device);

    for (block_index = 0; block_index < device->block_total; block_index ++)
    {
        if (mapping->phyical_status[block_index].used)
        {
            logic_block = nftl_get_logic_block(device, block_index);
            if (logic_block == RT_UINT16_MAX)
                log_trace("bad mapping, block: %d\n", block_index);
        }
    }
}
FINSH_FUNCTION_EXPORT_ALIAS(nftl_check_mapping, check_mapping, check_mapping with device);

void nftl_check_block(const char* nand_device)
{
    rt_device_t dev;
    rt_err_t result;
    rt_uint32_t block_index;
    rt_uint32_t page_index;
    struct rt_mtd_nand_device* device;
    struct nftl_mapping* mapping;
    struct nftl_page_mapping page_mapping;
    rt_uint8_t *page_buffer = RT_NULL;
    rt_uint8_t *oob_buffer  = RT_NULL;

    dev = rt_device_find(nand_device);
    if (dev == RT_NULL)
    {
        log_trace("No NAND Device\n");
        return;
    }

    device = RT_MTD_NAND_DEVICE(dev);
    mapping = NFTL_MAPPING(device);

    for (block_index = 0; block_index < device->block_total; block_index ++)
    {
        if (mapping->phyical_status[block_index].used == 1)
        {
            log_trace("Checking block[%4d] - ", block_index);
            nftl_page_get_mapping(device, (rt_uint16_t)block_index, &page_mapping);

            page_buffer = NFTL_PAGE_BUFFER_GET(device);
            oob_buffer  = NFTL_OOB_BUFFER_GET(device);
            for (page_index = 0; page_index < device->pages_per_block; page_index ++)
            {
                if (page_mapping.logic_pages[page_index] != NFTL_PAGE_INVALID)
                {
                    result = rt_mtd_nand_read(device, block_index * device->pages_per_block + page_mapping.logic_pages[page_index],
                                              page_buffer, device->page_size, oob_buffer, device->oob_size);
                    if (result != RT_EOK && result != -RT_MTD_EECC_CORRECT)
                    {
                        log_trace("check failed. physical block: %d, physical page: %d\n",
                                  block_index, page_mapping.logic_pages[page_index]);
                    }

                    if (page_index & 0x01)
                        log_trace(".");
                }
            }
            NFTL_PAGE_BUFFER_PUT(device, page_buffer);
            NFTL_OOB_BUFFER_PUT(device, oob_buffer);

            log_trace("done\n");
        }
    }
}
FINSH_FUNCTION_EXPORT_ALIAS(nftl_check_block, check_block, check_block with device);

int nftl_log_item_dump(struct nftl_log_item *item)
{
    if (!item)
        return -1;

    switch (item->action)
    {
    case NFTL_ACTION_WRITE_PAGE:
        rt_kprintf("write logic block[%d:%d] to physical block[%d:%d]\n", item->src_page, item->src_block, item->dst_page, item->dst_block);
        break;
    case NFTL_ACTION_READ_PAGE:
        rt_kprintf("read  logic block[%d:%d] from physical block[%d:%d]\n", item->src_page, item->src_block, item->dst_page, item->dst_block);
        break;
    case NFTL_ACTION_COPY_BLOCK:
        rt_kprintf("copy block[%d] to block %d\n", item->src_block, item->dst_block);
        break;
    case NFTL_ACTION_ALLOC_BLOCK:
        rt_kprintf("allocate block %d\n", item->src_block);
        break;
    case NFTL_ACTION_SW_COPY_BLOCK:
        rt_kprintf("do sw copy physical block[%d] to block %d\n", item->src_block, item->dst_block);
        break;
    case NFTL_ACTION_FLUSH_MAPPING:
        rt_kprintf("flush, sn=%d\n", (item->src_block) | (item->dst_block << 16));
        break;
    case NFTL_ACTION_MAP_BLOCK:
        rt_kprintf("map logic block %d => physical block %d\n", item->src_block, item->dst_block);
        break;
    case NFTL_ACTION_UNMAP_BLOCK:
        rt_kprintf("unmap physical block %d\n", item->src_block);
        break;

    case NFTL_ACTION_WEAR_LEVELING:
        rt_kprintf("do wear leveling: physical block %d => block %d\n", item->src_block, item->dst_block);
        break;
    case NFTL_ACTION_ERASE_LBLOCK:
        rt_kprintf("erase logic block[%d:%d] - block[%d:%d]\n", item->src_page, item->src_block, item->dst_page, item->dst_block);
        break;
    case NFTL_ACTION_FLUSH_RECENT:
        rt_kprintf("flush recent\n");
        break;
    case NFTL_ACTION_SESSION_DONE:
        rt_kprintf("session done, sn=0x%08x\n", (item->src_block) | (item->dst_block << 16));
        break;
    case NFTL_ACTION_COPY_FAILED:
        rt_kprintf("copy failed! physical block %d => block %d\n", item->src_block, item->dst_block);
        break;
    case NFTL_ACTION_READ_FAILED:
        rt_kprintf("block[%d:%d]: read  failed!\n", item->src_page, item->src_block);
        break;
    case NFTL_ACTION_WRITE_FAILED:
        rt_kprintf("block[%d:%d]: write failed!\n", item->src_page, item->src_block);
        break;
    case NFTL_ACTION_BIT_INVERT:
        rt_kprintf("block[%d:%d]: bit invert!!!\n", item->src_page, item->src_block);
        break;
    case NFTL_ACTION_PAGETAG_FAILED:
        rt_kprintf("block[%d:%d]: read page tag failed!\n", item->src_page, item->src_block);
        break;
    case NFTL_ACTION_PMAPPING_FAILED:
        rt_kprintf("block[%d:%d]: read page mapping failed!\n", item->src_page, item->src_block);
        break;
    case NFTL_ACTION_COPYPAGE_FAILED:
        rt_kprintf("block[%d:%d]: copy from block[%d:%d] failed!\n", item->dst_page, item->dst_block, item->src_page, item->src_block);
        break;
    case NFTL_ACTION_ESRC:
        rt_kprintf("block[%d:%d]: copy source page failed!\n", item->src_page, item->src_block);
        break;
    }

    return 0;
}

int nftl_dump_log(const char* nand_device)
{
    int index;
    rt_device_t dev;
    struct rt_mtd_nand_device* device;
    struct nftl_mapping* mapping;
    struct nftl_log_item *item;

    dev = rt_device_find(nand_device);
    if (dev == RT_NULL)
    {
        log_trace("No NAND Device\n");
        return -1;
    }

    device = RT_MTD_NAND_DEVICE(dev);
    mapping = NFTL_MAPPING(device);

    rt_kprintf("log index=%d, with tag block[page:block]\n", mapping->log_index);
    for (index = mapping->log_index % NFTL_LOG_ITEMS; index < NFTL_LOG_ITEMS; index ++)
    {
        item = &items[index];
        if (item->action != 0xff && item->action != 0x00)
            rt_kprintf("%3d: ", index);
        nftl_log_item_dump(item);
    }

    for (index = 0; index < mapping->log_index % NFTL_LOG_ITEMS; index ++)
    {
        item = &items[index];
        if (item->action != 0xff && item->action != 0x00)
            rt_kprintf("%3d: ", index);
        nftl_log_item_dump(item);
    }

    return 0;
}

int nftl_clean_log(const char* nand_device)
{
    rt_device_t dev;
    struct rt_mtd_nand_device* device;
    struct nftl_mapping* mapping;

    dev = rt_device_find(nand_device);
    if (dev == RT_NULL)
    {
        log_trace("No NAND Device\n");
        return -1;
    }

    device = RT_MTD_NAND_DEVICE(dev);
    mapping = NFTL_MAPPING(device);

    mapping->log_index = 0;
    memset(items, 0x0, sizeof(items));

    nftl_mapping_flush(device, RT_TRUE);

    return 0;
}

