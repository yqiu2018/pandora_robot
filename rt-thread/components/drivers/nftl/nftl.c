/*
 * File      : nftl.c
 * COPYRIGHT (C) 2012-2014, Shanghai Real-Thread Electronic Technology Co.,Ltd
 *
 * Change Logs:
 * Date           Author       Notes
 * 2012-03-01     Bernard      the first version
 * 2012-11-01     Bernard      Added page mapping
 * 2012-11-18     Bernard      Fixed block copy when nftl_mapping_init found block
 *                             is dirty.
 * 2012-12-03     Bernard      Fixed multi-block write issue.
 * 2012-12-21     Bernard      Fixed empty page issue;
 */

#include "nftl_internal.h"
#include <string.h>

/* it's an offical version for NFTL */
// #define NFTL_DEMO_VERSION

#define NFTL_MAPPING_PAGES(device)      ((sizeof(struct nftl_mapping) + device->page_size - 1)/device->page_size)
#define NFTL_MAPPING_PATTERN(index)     ((index << 1) & 0x55)

#define BLOCK_INVALID                   (RT_UINT16_MAX)
#define BLOCK_IS_INVALID(block)         (block == RT_UINT16_MAX)
#define BLOCK_IS_BAD(mapping, block)    (mapping->block_status[block].bad == 0x01)
#define BLOCK_IS_FREE(mapping, block)   (mapping->block_status[block].free== 0x01)
#define BLOCK_IS_USED(mapping, block)   (mapping->block_status[block].used== 0x01)

#define BLOCK_USED(block)               (mapping->phyical_status[block].used)
#define BLOCK_FREE(block)               (mapping->phyical_status[block].free)
#define BLOCK_ERASETIMES(block)         (mapping->phyical_status[block].erase_times)
#define BLOCK_BAD(block)                (mapping->phyical_status[block].bad)
#define BLOCK_RECENT(block)             (mapping->phyical_status[block].recent)

#define OOB_FREE(nand, oob, n)          (&((rt_uint8_t*)(oob))[nand->oob_size - nand->oob_free + n])

static rt_uint16_t nftl_block_allocate_force(struct rt_mtd_nand_device* device, rt_uint16_t refer_block);
static rt_uint16_t nftl_block_allocate(struct rt_mtd_nand_device* device, rt_uint16_t refer_block);
static rt_uint8_t nftl_page_allocate(struct rt_mtd_nand_device* device, struct nftl_page_mapping* mapping);
static rt_err_t nftl_page_software_copy(struct rt_mtd_nand_device* device, rt_uint16_t src_block, rt_uint16_t src_page,
                                        rt_uint16_t dst_block, rt_uint16_t dst_page);
static rt_err_t nftl_page_copy(struct rt_mtd_nand_device* device, rt_uint16_t src_block, rt_uint16_t src_page,
                               rt_uint16_t dst_block, rt_uint16_t dst_page);
rt_inline rt_err_t nftl_page_domapping(struct nftl_page_mapping* mapping, rt_uint8_t logic_page, rt_uint8_t phy_page);
rt_inline void nftl_page_init_mapping(struct rt_mtd_nand_device* device, struct nftl_page_mapping *page_mapping);

// struct nftl_layer *_layer; /* for debug */

static rt_uint16_t nftl_find_youest_block(struct rt_mtd_nand_device* device, rt_uint16_t refer_block, rt_uint16_t* used_youest)
{
    rt_uint16_t index;
    rt_uint16_t youest_index, uyouest_index;
    rt_uint16_t begin, step;
    rt_uint16_t max_erase_block;
    struct nftl_mapping *mapping = NFTL_MAPPING(device);

    log_trace(LOG_TRACE_DEBUG NFTL_MOD"search youest block, the refer block is %d.\n", refer_block);

    /* set initial value */
    max_erase_block = 0;
    if (used_youest != RT_NULL)
        *used_youest = RT_UINT16_MAX;

    if (BLOCK_IS_INVALID(refer_block))
    {
        begin = 0;
        step  = 1;
    }
    else
    {
        begin = refer_block & (device->plane_num - 1);
        step  = device->plane_num;
    }

    youest_index = uyouest_index = RT_UINT16_MAX;
    for (index = begin; index < device->block_total; index += step)
    {
        /* not find in the refer block */
        if (index == refer_block)
            continue;

        if (!BLOCK_BAD(index))
        {
            if (BLOCK_ERASETIMES(index) > BLOCK_ERASETIMES(max_erase_block))
                max_erase_block = index;
        }

        if (BLOCK_FREE(index))
        {
            if (youest_index == RT_UINT16_MAX)
                youest_index = index;
            else if (BLOCK_ERASETIMES(index) < BLOCK_ERASETIMES(youest_index))
            {
                youest_index = index;
            }
        }
        else if (BLOCK_USED(index))
        {
            if (uyouest_index == RT_UINT16_MAX)
                uyouest_index = index;
            if (BLOCK_ERASETIMES(index) < BLOCK_ERASETIMES(uyouest_index))
                uyouest_index = index;
        }
    }

    if (uyouest_index != RT_UINT16_MAX &&
            BLOCK_ERASETIMES(max_erase_block) - BLOCK_ERASETIMES(uyouest_index) > NFTL_ERASE_BLANCE)
    {
        if (used_youest != RT_NULL)
            *used_youest = uyouest_index;
    }

    log_trace(LOG_TRACE_DEBUG NFTL_MOD"result: refer %d, youest %d, used youest %d\n", refer_block,
              youest_index, uyouest_index);

    return youest_index;
}

rt_inline rt_uint16_t nftl_block_mapping(struct nftl_mapping *mapping, rt_uint16_t logic_block)
{
    return mapping->logic_blocks[logic_block];
}

rt_inline void nftl_block_set_bad(struct nftl_mapping *mapping, rt_uint16_t block)
{
    log_trace(LOG_TRACE_INFO NFTL_MOD"set %d as bad block\n", block);
    BLOCK_USED(block) = 0x00;
    BLOCK_FREE(block) = 0x00;
    BLOCK_BAD (block) = 0x01;
}

rt_inline void nftl_block_unmap(struct nftl_mapping *mapping, rt_uint16_t block)
{
    BLOCK_USED(block)   = 0x00;
    BLOCK_FREE(block)   = 0x00;
    BLOCK_RECENT(block) = 0x01;
}

rt_inline void nftl_block_set_used(struct nftl_mapping *mapping, rt_uint16_t block)
{
    BLOCK_USED(block)   = 0x01;
    BLOCK_FREE(block)   = 0x00;
    BLOCK_RECENT(block) = 0x00;
    BLOCK_BAD(block)    = 0x00;
}

rt_inline void nftl_block_set_free(struct nftl_mapping *mapping, rt_uint16_t block)
{
    BLOCK_USED(block)   = 0x00;
    BLOCK_FREE(block)   = 0x01;
    BLOCK_RECENT(block) = 0x00;
    BLOCK_BAD(block)    = 0x00;
}

rt_inline void nftl_block_domapping(struct nftl_mapping *mapping, rt_uint16_t logic_block,
                                    rt_uint16_t physical_block)
{
    if (physical_block == RT_UINT16_MAX)
    {
        log_trace(LOG_TRACE_INFO NFTL_MOD"remove block[%d] mapping, old is %d\n", logic_block,
                  mapping->logic_blocks[logic_block]);
    }
    else if (mapping->logic_blocks[logic_block] == RT_UINT16_MAX)
    {
        log_trace(LOG_TRACE_INFO NFTL_MOD"mapping empty block %d => %d\n", logic_block, physical_block);
    }
    else
    {
        log_trace(LOG_TRACE_INFO NFTL_MOD"mapping block %d => %d, old is %d\n", logic_block, physical_block,
                  mapping->logic_blocks[logic_block]);
    }
    mapping->logic_blocks[logic_block] = physical_block;
}

/*
 * copy the data from the physical block of a logic block to another physical block
 */
static void nftl_block_copy(struct rt_mtd_nand_device* device, rt_uint16_t logic_block)
{
    rt_uint8_t new_page;
    rt_err_t result;
    struct nftl_mapping *mapping;
    rt_bool_t block_is_bad = RT_FALSE;
    rt_bool_t using_sw_copy = RT_FALSE;
    struct nftl_page_mapping old_page_mapping;
    struct nftl_page_mapping new_page_mapping;
    rt_uint16_t old_phyblock, page_index, new_phyblock;

    RT_ASSERT(device != RT_NULL);
    mapping = NFTL_MAPPING(device);

    old_phyblock = nftl_block_mapping(mapping, logic_block);
    /* get clean page mapping */
    nftl_page_get_clean_mapping(device, old_phyblock, &old_page_mapping);

    using_sw_copy = RT_FALSE;
    while (1)
    {
        /* allocate a block */
        // new_phyblock = nftl_block_allocate(device, old_phyblock);
        new_phyblock = nftl_block_allocate_force(device, old_phyblock);
        if (BLOCK_IS_INVALID(new_phyblock))
        {
            log_trace(LOG_TRACE_ERROR NFTL_MOD"Out of block in the flash.\n");
            return;
        }
        block_is_bad = RT_FALSE;

        if (using_sw_copy == RT_TRUE)
        {
            nftl_log_action(device, NFTL_ACTION_SW_COPY_BLOCK, NFTL_LOG_BLOCK(old_phyblock), NFTL_LOG_BLOCK(new_phyblock));
        }
        else
        {
            if (mapping->logic_blocks[0] == old_phyblock)
                nftl_log_action(device, NFTL_ACTION_COPY_BLOCK, NFTL_LOG_BLOCK(old_phyblock), NFTL_LOG_BLOCK(new_phyblock));
        }

        /* initialize page mapping */
        nftl_page_init_mapping(device, &new_page_mapping);
        for (page_index = 0; page_index < device->pages_per_block; page_index ++)
        {
            if (old_page_mapping.logic_pages[page_index] != 0xff)
            {
                /* allocate a new page */
                new_page = nftl_page_allocate(device, &new_page_mapping);

                /* made a page copy */
                if (using_sw_copy == RT_TRUE)
                {
                    result = nftl_page_software_copy(device, old_phyblock, old_page_mapping.logic_pages[page_index],
                                                     new_phyblock, new_page);
                }
                else
                {
                    result = nftl_page_copy(device, old_phyblock, old_page_mapping.logic_pages[page_index],
                                            new_phyblock, new_page);
                    if (result == -RT_MTD_ESRC)
                    {
                        /* source page error */
                        using_sw_copy = RT_TRUE;
                        block_is_bad = RT_TRUE;
                        break;
                    }

                    if (result == -RT_MTD_EECC_CORRECT)
                    {
                        nftl_log_action(device, NFTL_ACTION_COPY_BLOCK, NFTL_LOG_BLOCK(old_phyblock), NFTL_LOG_BLOCK(new_phyblock));
                        nftl_log_action(device, NFTL_ACTION_BIT_INVERT, NFTL_LOG_BLOCK_PAGE(new_phyblock, new_page), 0);
                    }
                }

                if (result == -RT_MTD_ESRC || result == RT_MTD_EOK)
                {
                    /* we think that the source page error is a pass case. */
                    if (result == -RT_MTD_ESRC)
                    {
                        log_trace(LOG_TRACE_WARNING NFTL_MOD"copy source page error: %d:%d\n", old_page_mapping.logic_pages[page_index],
                                  old_phyblock);
                    }

                    /* copy OK */
                    nftl_page_domapping(&new_page_mapping, (rt_uint8_t)page_index, new_page);
                }
                else /* error case */
                {
                    nftl_block_set_bad(mapping, new_phyblock);
                    block_is_bad = RT_TRUE;
                    break;
                }
            }
        }

        if (block_is_bad == RT_FALSE)
        {
            /* made a block mapping */
            nftl_block_domapping(mapping, logic_block, new_phyblock);

            nftl_block_unmap(mapping, old_phyblock);
            nftl_recent_push(device, old_phyblock);
            break;
        }
    }
}

#ifdef NFTL_USING_BACKUP_MAPPING_BLOCK
rt_uint16_t nftl_mapping_search_backup(struct rt_mtd_nand_device* device, rt_uint8_t *page_buffer, rt_uint8_t *oob_buffer)
{
    rt_err_t result;
    rt_uint16_t index, page_index;
    rt_uint16_t block_index;
    rt_uint32_t mapping_sn = 0;
    struct nftl_mapping *mapping = NFTL_MAPPING(device);
    rt_uint16_t mapping_block = RT_UINT16_MAX;

    /* try to load the block number of mapping table from backup register */
    for (index = 0; index < NFTL_BACKUP_MAPPING_BLOCKS; index ++)
    {
        rt_uint8_t  *ptr;
        rt_uint32_t crc;
        rt_bool_t   found = RT_TRUE;

        block_index = nftl_backup_get_block(index);
        /* ignore the invalid block and the zero block */
        if (block_index > device->block_total || block_index == 0)
            continue;

        /* read page#0 from block */
        result = rt_mtd_nand_read(device, block_index * device->pages_per_block,
                                  page_buffer, device->page_size,
                                  oob_buffer, device->oob_size);
        if (result != RT_EOK)
        {
            log_trace(LOG_TRACE_INFO NFTL_MOD"read block:%d failed\n", block_index);
            continue;
        }

        /* page#0 is used for saving signature */
        for (page_index = 0; page_index < device->page_size; page_index ++)
        {
            if (page_buffer[page_index] != NFTL_MAPPING_PATTERN(page_index))
            {
                found = RT_FALSE;
                break;
            }
        }

        if (found == RT_TRUE)
        {
            log_trace(LOG_TRACE_INFO NFTL_MOD"found signature on block: %d\n", block_index);

            /* found the signature page, try to read mapping table */
            ptr = (rt_uint8_t*) mapping;
            for (page_index = 1; page_index < NFTL_MAPPING_PAGES(device) + 1; page_index ++)
            {
                /* read page and save the data in page buffer */
                result = rt_mtd_nand_read(device, block_index * device->pages_per_block + page_index,
                                          page_buffer, device->page_size,
                                          RT_NULL, 0);
                if (result != RT_MTD_EOK)
                    continue;

                /* copy to mapping */
                memcpy(ptr, page_buffer,
                       page_index == NFTL_MAPPING_PAGES(device)?
                       sizeof(struct nftl_mapping)%device->page_size : device->page_size);
                ptr += device->page_size;
            }

            crc = mapping->crc;
            mapping->crc = 0;

            /* verify mapping table according to crc */
            mapping->crc = nftl_crc32((rt_uint8_t*)mapping, sizeof(struct nftl_mapping));
            if (mapping->crc != crc)
                continue;
            if (mapping->sn >= mapping_sn)
            {
                mapping_sn = mapping->sn;
                mapping_block = block_index;
            }
        }
    }

    return mapping_block;
}
#endif

rt_uint16_t nftl_mapping_search_full(struct rt_mtd_nand_device* device, rt_uint8_t *page_buffer, rt_uint8_t *oob_buffer)
{
    rt_err_t result;
    rt_uint16_t page_index;
    rt_uint16_t block_index;
    rt_uint32_t mapping_sn = 0;
    struct nftl_mapping *mapping = NFTL_MAPPING(device);
    rt_uint16_t mapping_block = RT_UINT16_MAX;

    /* look for block map from every block */
    for (block_index = 0; block_index < device->block_total; block_index ++)
    {
        rt_uint8_t  *ptr;
        rt_uint32_t crc;
        rt_bool_t   found = RT_TRUE;

        /* read page#0 from block */
        result = rt_mtd_nand_read(device, block_index * device->pages_per_block,
                                  page_buffer, device->page_size,
                                  oob_buffer, device->oob_size);
        if (result != RT_EOK && result != -RT_MTD_EECC_CORRECT)
        {
            log_trace(LOG_TRACE_INFO NFTL_MOD"read block:%d failed\n", block_index);
            continue;
        }

        /* page#0 is used for saving signature */
        for (page_index = 0; page_index < device->page_size; page_index ++)
        {
            if (page_buffer[page_index] != NFTL_MAPPING_PATTERN(page_index))
            {
                found = RT_FALSE;
                break;
            }
        }

        if (found == RT_TRUE)
        {
            log_trace(LOG_TRACE_INFO NFTL_MOD"found signature block: %d\n", block_index);

            /* found the signature page, try to read mapping table */
            ptr = (rt_uint8_t*) mapping;
            for (page_index = 1; page_index < NFTL_MAPPING_PAGES(device) + 1; page_index ++)
            {
                /* read page and save the data in page buffer */
                result = rt_mtd_nand_read(device, block_index * device->pages_per_block + page_index,
                                          page_buffer, device->page_size,
                                          RT_NULL, 0);
                if (result != RT_MTD_EOK && result != -RT_MTD_EECC_CORRECT)
                    continue;

                /* copy to mapping */
                memcpy(ptr, page_buffer,
                       page_index == NFTL_MAPPING_PAGES(device)?
                       sizeof(struct nftl_mapping)%device->page_size : device->page_size);
                ptr += device->page_size;
            }

            crc = mapping->crc;
            mapping->crc = 0;

            /* verify mapping table according to crc */
            mapping->crc = nftl_crc32((rt_uint8_t*)mapping, sizeof(struct nftl_mapping));
            if (mapping->crc != crc)
                continue;
            if (mapping->sn >= mapping_sn)
            {
                mapping_sn = mapping->sn;
                mapping_block = block_index;
            }
        }
    }

    return mapping_block;
}

#define MAPPING_SEARCH_MAX  8

rt_uint16_t nftl_mapping_search(struct rt_mtd_nand_device* device, rt_uint8_t *page_buffer, rt_uint8_t *oob_buffer)
{
    rt_err_t result;
    rt_uint32_t index;
    rt_uint16_t page_index;
    rt_uint16_t block_index;
    rt_uint16_t mapping_block = RT_UINT16_MAX;
    struct nftl_mapping *mapping = NFTL_MAPPING(device);
    struct nftl_page_tag* tag;

    struct mapping_block
    {
        rt_uint32_t block_index;
        rt_uint32_t mapping_sn;
    };
    struct mapping_block mapping_blocks[MAPPING_SEARCH_MAX];

    tag = (struct nftl_page_tag*) OOB_FREE(device, oob_buffer, 0);

    /* initialize mapping blocks */
    for (index = 0; index < MAPPING_SEARCH_MAX; index ++)
    {
        mapping_blocks[index].block_index = RT_UINT16_MAX;
        mapping_blocks[index].mapping_sn = 0;
    }

    /* search page tag */
    for (block_index = 0; block_index < device->block_total; block_index ++)
    {
        memset(oob_buffer, 0x00, device->oob_size);
        result = rt_mtd_nand_read(device, block_index * device->pages_per_block,
                                  RT_NULL, device->page_size,
                                  oob_buffer, device->oob_size);
        if (result != RT_EOK && result != -RT_MTD_EECC_CORRECT)
        {
            log_trace(LOG_TRACE_INFO NFTL_MOD"read block:%d failed\n", block_index);
            continue;
        }

        /* check the signature tag */
        if (tag->magic == NFTL_PAGE_MAGIC &&
                tag->status == NFTL_PAGE_STATUS_MAPPING &&
                tag->logic_page == NFTL_PAGE_LP_TAG)
        {
            /* insert to mapping blocks with decrease order */
            for (index = 0; index < 8; index ++)
            {
                /* found the signature tag */
                if (tag->sn > mapping_blocks[index].mapping_sn)
                {
                    rt_uint32_t descrease;

                    /* make a sort array */
                    for (descrease = 7; descrease > index ; descrease --)
                    {
                        mapping_blocks[descrease].mapping_sn  = mapping_blocks[descrease - 1].mapping_sn;
                        mapping_blocks[descrease].block_index = mapping_blocks[descrease - 1].block_index;
                    }

                    mapping_blocks[index].mapping_sn = tag->sn;
                    mapping_blocks[index].block_index = block_index;

                    break;
                }
            }
        }
    }

#ifdef NFTL_USING_COMPATIBLE /* whether compatible with <1.0.14 version */
    if (mapping_blocks[0].block_index == RT_UINT16_MAX) /* never found a tag */
    {
        /* search the data signature */
        for (block_index = 0; block_index < device->block_total; block_index ++)
        {
            rt_uint8_t  *ptr;
            rt_bool_t   found = RT_TRUE;

            /* read page #0 for signature */
            result = rt_mtd_nand_read(device, block_index * device->pages_per_block,
                                      page_buffer, device->page_size,
                                      oob_buffer, device->oob_size);
            if (result != RT_EOK && result != -RT_MTD_EECC_CORRECT)
            {
                log_trace(LOG_TRACE_INFO NFTL_MOD"read block:%d failed\n", block_index);
                continue;
            }

            /* page#0 is used for saving signature */
            for (page_index = 0; page_index < device->page_size; page_index ++)
            {
                if (page_buffer[page_index] != NFTL_MAPPING_PATTERN(page_index))
                {
                    found = RT_FALSE;
                    break;
                }
            }

            if (found == RT_TRUE)
            {
                /* found the signature page, try to read header of mapping table (page #1) */
                ptr = (rt_uint8_t*) mapping;
                result = rt_mtd_nand_read(device, block_index * device->pages_per_block + 1,
                                          page_buffer, device->page_size, RT_NULL, 0);
                if (result == RT_MTD_EOK || result == -RT_MTD_EECC_CORRECT)
                {
                    /*
                     * Only to get the first page of mapping table,
                     * therefore, it's not the whole mapping table.
                     */
                    memcpy(ptr, page_buffer, device->page_size);
                    /* insert to mapping blocks with decrease order */
                    for (index = 0; index < 8; index ++)
                    {
                        if (mapping->sn > mapping_blocks[index].mapping_sn)
                        {
                            rt_uint32_t descrease;
                            for (descrease = 7; descrease > index ; descrease --)
                            {
                                mapping_blocks[descrease].mapping_sn  = mapping_blocks[descrease - 1].mapping_sn;
                                mapping_blocks[descrease].block_index = mapping_blocks[descrease - 1].block_index;
                            }

                            mapping_blocks[index].mapping_sn = mapping->sn;
                            mapping_blocks[index].block_index = block_index;
                            break;
                        }
                    }
                }
            }
        }
    }
#endif

    /* look for block map from mapping blocks */
    for (index = 0; index < 8; index ++)
    {
        rt_uint8_t  *ptr;
        rt_uint32_t crc;

        block_index = (rt_uint16_t)mapping_blocks[index].block_index;
        if (block_index >= RT_UINT16_MAX)
            continue;
        log_trace(LOG_TRACE_INFO NFTL_MOD"found signature block: %d, sn 0x%08x\n",
                  block_index,
                  mapping_blocks[index].mapping_sn);

        /* read page #0 for data signature */
        result = rt_mtd_nand_read(device, block_index * device->pages_per_block,
                                  page_buffer, device->page_size,
                                  oob_buffer, device->oob_size);
        if (result != RT_MTD_EOK && result != -RT_MTD_EECC_CORRECT)
        {
            log_trace(LOG_TRACE_INFO NFTL_MOD"read block:%d failed\n", block_index);
            continue;
        }
        /* check page#0 for data signature */
        for (page_index = 0; page_index < device->page_size; page_index ++)
        {
            if (page_buffer[page_index] != NFTL_MAPPING_PATTERN(page_index))
            {
                result = -RT_MTD_EIO;
                break;
            }
        }
        if (result != RT_MTD_EOK)
            continue;

        /* try to read mapping table */
        ptr = (rt_uint8_t*) mapping;
        for (page_index = 1; page_index < NFTL_MAPPING_PAGES(device) + 1; page_index ++)
        {
            /* read page and save the data in page buffer */
            result = rt_mtd_nand_read(device, block_index * device->pages_per_block + page_index,
                                      page_buffer, device->page_size,
                                      RT_NULL, 0);
            if (result != RT_MTD_EOK && result != -RT_MTD_EECC_CORRECT)
                continue;

            /* copy to mapping */
            memcpy(ptr, page_buffer,
                   page_index == NFTL_MAPPING_PAGES(device)?
                   sizeof(struct nftl_mapping)%device->page_size : device->page_size);
            ptr += device->page_size;
        }

        crc = mapping->crc;
        mapping->crc = 0;

        /* verify mapping table according to crc */
        mapping->crc = nftl_crc32((rt_uint8_t*)mapping, sizeof(struct nftl_mapping));
        if (mapping->crc != crc)
            continue;

        /* found mapping block */
        mapping_block = block_index;

        if (mapping->sn != mapping_blocks[index].mapping_sn)
        {
            log_trace(LOG_TRACE_WARNING NFTL_MOD "the SN is not mached!\n");
        }

        break;
    }

    /* the CRC of all found block is not correct. */
    if (mapping_block == RT_UINT16_MAX && mapping_blocks[0].block_index != RT_UINT16_MAX)
    {
        /* try to search all block */
        mapping_block = nftl_mapping_search_full(device, page_buffer, oob_buffer);
    }

    log_trace(LOG_TRACE_INFO NFTL_MOD "the returned mapping block is %d in quickly tag search.\n", mapping_block);

    return mapping_block;
}

/* initialize mapping table from MTD device */
rt_err_t nftl_mapping_init(struct rt_mtd_nand_device* device)
{
    rt_err_t result;
    rt_uint16_t block_index, page_index;
    rt_uint16_t mapping_block = RT_UINT16_MAX;
    struct nftl_mapping *mapping = NFTL_MAPPING(device);
    rt_uint8_t *page_buffer;
    rt_uint8_t *oob_buffer;

    // _layer = NFTL_LAYER(device); /* for debug */
    result = -RT_ERROR;
    page_buffer = RT_NULL;

    /* allocate buffer */
    page_buffer = NFTL_PAGE_BUFFER_GET(device);
    oob_buffer = NFTL_OOB_BUFFER_GET(device);
    if (page_buffer == RT_NULL || oob_buffer == RT_NULL)
    {
        NFTL_PAGE_BUFFER_PUT(device, page_buffer);
        NFTL_OOB_BUFFER_PUT(device, oob_buffer);
        result = -RT_ENOMEM;
        return result;
    }

#ifdef NFTL_USING_BACKUP_MAPPING_BLOCK
    if (nftl_backup_check_signature() == RT_TRUE)
    {
        mapping_block = nftl_mapping_search_backup(device, page_buffer, oob_buffer);
        if (mapping_block != RT_UINT16_MAX)
        {
            log_trace(LOG_TRACE_INFO NFTL_MOD"found mapping table block:%d on backup region.\n", mapping_block);
        }
    }
    else
    {
        log_trace(LOG_TRACE_INFO NFTL_MOD"The signature of backup region is not correct.\n");
    }
#endif

    if (mapping_block == RT_UINT16_MAX)
    {
        /* search mapping block by oob tag */
        mapping_block = nftl_mapping_search(device, page_buffer, oob_buffer);

#ifdef NFTL_USING_BACKUP_MAPPING_BLOCK
        /* save to backup region */
        if (mapping_block != RT_UINT16_MAX)
            nftl_backup_save_block(mapping_block);
#endif
    }

    /* release page buffer */
    NFTL_PAGE_BUFFER_PUT(device, page_buffer);

    if (mapping_block != RT_UINT16_MAX)
    {
        rt_uint8_t* ptr;
        rt_uint32_t mapping_sn;
        struct nftl_page_tag* tag;
        rt_bool_t flush_mapping = RT_FALSE;

        ptr = (rt_uint8_t*) mapping;
        /* allocate a page buffer */
        page_buffer = (rt_uint8_t*) NFTL_PAGE_BUFFER_GET(device);
        if (page_buffer == RT_NULL)
        {
            result = -RT_ENOMEM;
            return result;
        }

        /* read the newest mapping table */
        for (page_index = 1; page_index < NFTL_MAPPING_PAGES(device) + 1; page_index ++)
        {
            result = rt_mtd_nand_read(device, mapping_block * device->pages_per_block + page_index,
                                      page_buffer, device->page_size, RT_NULL, device->oob_size);
            if (result != RT_MTD_EOK && result != -RT_MTD_EECC_CORRECT)
                continue;

            /* copy to mapping */
            memcpy(ptr, page_buffer,
                   page_index == NFTL_MAPPING_PAGES(device)?
                   sizeof(struct nftl_mapping)%device->page_size : device->page_size);
            ptr += device->page_size;
        }

        /* release page buffer */
        NFTL_PAGE_BUFFER_PUT(device, page_buffer);
        page_buffer = RT_NULL;

        mapping->crc = 0;
        /* save mapping block */
        NFTL_MAPPING_BLOCK(device) = mapping_block;
        mapping->sn ++;
        log_trace(LOG_TRACE_INFO NFTL_MOD"load mapping table on %d block\n", mapping_block);

        mapping_sn = mapping->sn;
        tag = (struct nftl_page_tag*) OOB_FREE(device,oob_buffer,0);

        /* initialize NFTL log */
        nftl_log_init(device, mapping_block, NFTL_MAPPING_PAGES(device) + 1);
        /* set NFTL version */
        mapping->version = 0x00010018;

        /* check recent block */
        for (block_index = 0; block_index < device->block_total; block_index ++)
        {
            if (mapping->phyical_status[block_index].recent)
            {
                nftl_recent_push(device, block_index);
            }
        }

        /* check bad page mapping on each logic block */
        for (block_index = 0; block_index < device->block_total; block_index ++)
        {
            if (!BLOCK_IS_INVALID(mapping->logic_blocks[block_index]))
            {
                for (page_index = 0; page_index < device->pages_per_block; page_index ++)
                {
                    result = rt_mtd_nand_read(device, mapping->logic_blocks[block_index] * device->pages_per_block + page_index,
                                              RT_NULL, device->page_size,
                                              oob_buffer, device->oob_size);
                    if ((result == RT_EOK || result == -RT_MTD_EECC_CORRECT) && (tag->magic == NFTL_PAGE_MAGIC) && (tag->sn >= mapping_sn))
                    {
                        /* move this block */
                        log_trace(LOG_TRACE_INFO NFTL_MOD"found dirty block:%d, copy data on block\n", block_index);
                        nftl_block_copy(device, block_index);
                        flush_mapping = RT_TRUE;
                        break;
                    }
                }
            }
        }

        if (flush_mapping == RT_TRUE)
        {
            /* flush mapping table */
            nftl_mapping_flush(device, RT_FALSE);
        }
    }
    else
    {
        /* this is a NAND flash has no NFTL layer */
        log_trace(LOG_TRACE_INFO NFTL_MOD"not found mapping table on the flash\n");

        /* zero mapping table */
        memset(mapping, 0, sizeof(struct nftl_mapping));
        /* set the initialize value of SN to 1 */
        mapping->sn = 0x01;

        /* set to 0xff for phy_block */
        memset(mapping->logic_blocks, 0xff, sizeof(mapping->logic_blocks));

        for (block_index = 0; block_index < device->block_total; block_index ++)
        {
            mapping->phyical_status[block_index].free = 0x01;
        }
        NFTL_MAPPING_BLOCK(device) = RT_UINT16_MAX;

        mapping->version = 0x00;
        nftl_log_init(device, 0, 0);
        mapping->version = 0x00010018;
    }

    return RT_EOK;
}

/* flush mapping table to MTD device */
rt_err_t nftl_mapping_flush(struct rt_mtd_nand_device* device, rt_bool_t protect)
{
    rt_uint8_t *ptr, *pattern_page, *page_ptr;
    rt_err_t result;
    rt_uint16_t next_block;
    rt_uint32_t crc_value, index;
    struct nftl_layer *layer = NFTL_LAYER(device);
    struct nftl_mapping *mapping = NFTL_MAPPING(device);
    rt_uint8_t oob_buffer[NFTL_OOB_MAX];
    struct nftl_page_tag *tag;

    /* discard old mapping block */
    if (!BLOCK_IS_INVALID(NFTL_MAPPING_BLOCK(device)))
    {
        nftl_block_unmap(mapping, NFTL_MAPPING_BLOCK(device));
    }

    /* prepare OOB buffer */
    tag = (struct nftl_page_tag*) OOB_FREE(device, oob_buffer, 0);
    memset(oob_buffer, 0xff, sizeof(oob_buffer));
    tag->magic = NFTL_PAGE_MAGIC;
    tag->status = NFTL_PAGE_STATUS_MAPPING;
    tag->logic_page = NFTL_PAGE_LP_TAG;
    tag->sn = mapping->sn;

    while (1)
    {
        /* allocate an empty block */
        next_block = nftl_block_allocate_force(device, BLOCK_INVALID);
        if (BLOCK_IS_INVALID(next_block))
        {
            result = -RT_ERROR; /* no empty block yet */

            /* allocate failed, revert the old mapping block */
            if (!BLOCK_IS_INVALID(NFTL_MAPPING_BLOCK(device)))
            {
                nftl_block_set_used(mapping, NFTL_MAPPING_BLOCK(device));
            }
            break;
        }

        /* prepare signature page */
        pattern_page = (rt_uint8_t*) NFTL_PAGE_BUFFER_GET(device);
        if (pattern_page == RT_NULL)
        {
            nftl_block_set_free(mapping, next_block);

            /* allocate failed, revert the old mapping block */
            if (!BLOCK_IS_INVALID(NFTL_MAPPING_BLOCK(device)))
            {
                nftl_block_set_used(mapping, NFTL_MAPPING_BLOCK(device));
            }
            result = -RT_EFULL;
            break;
        }
        for (index = 0; index < device->page_size; index ++)
        {
            pattern_page[index] = NFTL_MAPPING_PATTERN(index);
        }
        /* write to NAND flash */
        result = rt_mtd_nand_write(device, next_block * device->pages_per_block,
                                   pattern_page, device->page_size, oob_buffer, device->oob_size);
        if (result == RT_MTD_EOK)
        {
            /* read back for ECC check */
            result = rt_mtd_nand_read(device, next_block * device->pages_per_block,
                                      pattern_page, device->page_size, RT_NULL, device->oob_size);
        }
        /* release buffer */
        NFTL_PAGE_BUFFER_PUT(device, pattern_page);

        /* check the result of write */
        if (result != RT_MTD_EOK && result != -RT_MTD_EECC_CORRECT)
        {
            /* this block is a bad block */
            nftl_block_set_bad(mapping, next_block);
            continue;
        }

        /* update mapping crc and flush to disk */
        mapping->crc = 0;
        crc_value = nftl_crc32((rt_uint8_t*)mapping, sizeof(struct nftl_mapping));
        mapping->crc = crc_value;

        page_ptr = NFTL_PAGE_BUFFER_GET(device);
        ptr = (rt_uint8_t*) mapping;
        for (index = 1; index < NFTL_MAPPING_PAGES(device) + 1; index++)
        {
            memcpy(page_ptr, ptr, device->page_size);
            result = rt_mtd_nand_write(device, next_block * device->pages_per_block + index,
                                       page_ptr, device->page_size, oob_buffer, device->oob_size);
            if (result == RT_MTD_EOK)
            {
                result = rt_mtd_nand_read(device, next_block * device->pages_per_block + index,
                                          page_ptr, device->page_size, RT_NULL, device->oob_size);
            }
            if (result != RT_MTD_EOK && result != -RT_MTD_EECC_CORRECT)
            {
                nftl_block_set_bad(mapping, next_block);
                break;
            }

            /* move to next page */
            ptr += device->page_size;
        }

        /* write NFTL log */
        nftl_log_write(device, next_block, index);

        NFTL_PAGE_BUFFER_PUT(device, page_ptr);

        /* bad block, retry */
        if (result != RT_MTD_EOK && result != -RT_MTD_EECC_CORRECT)
            continue;

        /* write mapping table successfully */
        /* increase mapping version */
        mapping->sn ++;
        mapping->crc = 0x00;

        /* set current mapping block */
        NFTL_MAPPING_BLOCK(device) = next_block;
#ifdef NFTL_USING_BACKUP_MAPPING_BLOCK
        /* save to backup region */
        nftl_backup_save_block(next_block);
#endif
        log_trace(LOG_TRACE_INFO NFTL_MOD"flush mapping table done, mapping table block: %d\n", NFTL_MAPPING_BLOCK(device));
        // nftl_log_action(device, NFTL_ACTION_FLUSH_MAPPING, NFTL_LOG_UINT32L(mapping->sn), NFTL_LOG_UINT32H(mapping->sn));

        /* flush recent block */
        nftl_recent_flush(device);
        /* put the old mapping block to the recent block queue */
        nftl_recent_push(device, NFTL_MAPPING_BLOCK(device));

        break;
    }

    return result;
}

/*
 * do wear leveling on a used block
 */
rt_bool_t nftl_block_wear_leveling(struct rt_mtd_nand_device* device, rt_uint16_t *youest_ref, rt_uint16_t used_youest)
{
    rt_err_t result;
    rt_uint8_t  new_page;
    rt_uint16_t index, youest;
    rt_bool_t copy_failed = RT_FALSE;
    rt_uint16_t logic_block = 0xffff;
    rt_bool_t using_sw_copy = RT_FALSE;
    struct nftl_mapping *mapping = NFTL_MAPPING(device);
    struct nftl_page_mapping old_page_mapping, new_page_mapping;

    youest = *youest_ref;

    log_trace(LOG_TRACE_INFO NFTL_MOD"wear-leveling: used youest block:%d, youest block: %d\n",
              used_youest, youest);

    if (mapping->logic_blocks[0] == used_youest)
        nftl_log_action(device, NFTL_ACTION_WEAR_LEVELING, NFTL_LOG_BLOCK(used_youest), NFTL_LOG_BLOCK(youest));

    if (NFTL_MAPPING_BLOCK(device) == used_youest)
    {
        log_trace(LOG_TRACE_DEBUG NFTL_MOD"used youest block is the mapping block.\n");

        /* erase the youest block */
        result = rt_mtd_nand_erase_block(device, youest);
        if (result != RT_EOK)
        {
            /* destination block is a bad block */
            nftl_block_set_bad(mapping, youest);
        }
        else
        {
            nftl_block_set_used(mapping, youest);
            BLOCK_ERASETIMES(youest) ++;
        }

        /* initialize the new page mapping */
        nftl_page_init_mapping(device, &new_page_mapping);
        /* copy the mapping page to the destination block */
        for (index = 0; index < NFTL_MAPPING_PAGES(device) + 1; index++)
        {
            /* allocate a new page */
            new_page = nftl_page_allocate(device, &new_page_mapping);

            /* copy the page */
            if (using_sw_copy == RT_TRUE)
            {
                result = nftl_page_software_copy(device, used_youest, index, youest, new_page);
            }
            else
            {
                result = nftl_page_copy(device, used_youest, index, youest, new_page);
                if (result == -RT_MTD_ESRC)
                {
                    /* source page error */
                    using_sw_copy = RT_TRUE;
                    copy_failed = RT_TRUE;
                    break;
                }
            }

            if (result == -RT_MTD_ESRC)
            {
                log_trace(LOG_TRACE_WARNING NFTL_MOD"source page error: %d:%d when wear leveling\n", index,
                          used_youest);
            }
            else if (result == -RT_MTD_EECC)
            {
                /* set block as bad block */
                nftl_block_set_bad(mapping, youest);
                copy_failed = RT_TRUE;
                break;
            }
        }
        if (copy_failed == RT_TRUE)
            return RT_FALSE; /* re-find youest block */

        /* set the new mapping block */
        NFTL_MAPPING_BLOCK(device) = youest;
    }
    else
    {
        /* find the logic block which mapping to this used youest block */
        for (index = 0; index < device->block_total; index ++)
        {
            if (mapping->logic_blocks[index] == used_youest)
            {
                logic_block = index;
                break;
            }
        }

        if (BLOCK_IS_INVALID(logic_block))
        {
            log_trace(LOG_TRACE_WARNING NFTL_MOD"warning: the youest used block[%d] has bad logic mapping.\n",
                      used_youest);
        }
        else
        {
            log_trace(LOG_TRACE_DEBUG NFTL_MOD"release used physical block: %d, which logic block is:%d\n",
                      used_youest, logic_block);

__retry:
            /* erase the youest block */
            result = rt_mtd_nand_erase_block(device, youest);
            if (result != RT_EOK)
            {
                /* destination block is a bad block */
                nftl_block_set_bad(mapping, youest);
                /* it's a bad block */
                return RT_FALSE;
            }
            else
            {
                nftl_block_set_used(mapping, youest);
                mapping->phyical_status[youest].erase_times ++;
            }

            /* copy the block, from used block to the youest block */
            nftl_page_get_mapping(device, used_youest, &old_page_mapping);
            /* initialize the new page mapping */
            nftl_page_init_mapping(device, &new_page_mapping);

            /* copy each logic page to the destination block */
            for (index = 0; index < device->pages_per_block; index ++)
            {
                if (old_page_mapping.logic_pages[index] != NFTL_PAGE_INVALID)
                {
                    /* allocate a new page */
                    new_page = nftl_page_allocate(device, &new_page_mapping);

                    if (using_sw_copy == RT_TRUE)
                    {
                        result = nftl_page_software_copy(device, used_youest, old_page_mapping.logic_pages[index],
                                                         youest, new_page);
                    }
                    else
                    {
                        result = nftl_page_copy(device, used_youest, old_page_mapping.logic_pages[index],
                                                youest, new_page);
                        if (result == -RT_MTD_ESRC)
                        {
                            /* try to use software copy */
                            using_sw_copy = RT_TRUE;
                            nftl_block_set_free(mapping, youest);
                            nftl_log_action(device, NFTL_ACTION_SW_COPY_BLOCK, NFTL_LOG_BLOCK(used_youest), NFTL_LOG_BLOCK(youest));
                            goto __retry;
                        }
                    }

                    if (result == -RT_MTD_EOK || result == -RT_MTD_ESRC)
                    {
                        if (result == -RT_MTD_ESRC)
                        {
                            log_trace(LOG_TRACE_WARNING NFTL_MOD"source page error: %d:%d when wear leveling\n", index,
                                      used_youest);
                        }

                        /* update page */
                        nftl_page_domapping(&new_page_mapping, (rt_uint8_t)index, new_page);
                    }
                    else // if (result == -RT_MTD_EECC)
                    {
                        /* set block as bad block */
                        nftl_block_set_bad(mapping, youest);
                        copy_failed = RT_TRUE;
                        break;
                    }
                }
            }

            if (copy_failed == RT_TRUE)
                return RT_FALSE; /* re-find youest block */

            /* set new mapping */
            nftl_block_domapping(mapping, logic_block, youest);
        }
    }

    /* switch to used youest block */
    *youest_ref = used_youest;

    return RT_TRUE;
}

/* force to allocate an empty block, without wear leveling */
static rt_uint16_t nftl_block_allocate_force(struct rt_mtd_nand_device* device, rt_uint16_t refer_block)
{
    rt_err_t result;
    rt_uint16_t used_youest, youest;
    struct nftl_mapping *mapping = NFTL_MAPPING(device);

    while (1)
    {
        youest = nftl_find_youest_block(device, refer_block, &used_youest);
        if (youest == RT_UINT16_MAX)
        {
            if (device->plane_num == 1)
            {
                log_trace(LOG_TRACE_WARNING NFTL_MOD"No free block yet.\n");
                return RT_UINT16_MAX;
            }
            else
            {
                log_trace(LOG_TRACE_INFO NFTL_MOD"Try to find the youest block on the other plane.\n");
                /* re-search youest block on the other plane */
                youest = nftl_find_youest_block(device, RT_UINT16_MAX, &used_youest);
                if (youest == RT_UINT16_MAX)
                {
                    log_trace(LOG_TRACE_WARNING NFTL_MOD"No free block yet.\n");
                    return RT_UINT16_MAX;
                }
            }
        }

        /* allocate this block */
        nftl_block_set_used(mapping, youest);
        /* erase this block */
        result = rt_mtd_nand_erase_block(device, youest);
        if (result != RT_EOK)
        {
            /* destination block is a bad block */
            nftl_block_set_bad(mapping, youest);
            /* re-search empty block */
        }
        else
        {
            mapping->phyical_status[youest].erase_times ++;
            break;
        }
    } /* end of while (1) */

    log_trace(LOG_TRACE_DEBUG NFTL_MOD"allocate block: %d, erase time: %d\n", youest,
              mapping->phyical_status[youest].erase_times);
    // nftl_log_action(device, NFTL_ACTION_ALLOC_BLOCK, NFTL_LOG_BLOCK(youest), 0);

    return youest;
}

/* allocate an empty block */
static rt_uint16_t nftl_block_allocate(struct rt_mtd_nand_device* device, rt_uint16_t refer_block)
{
    rt_err_t result;
    rt_uint16_t used_youest, youest;
    struct nftl_mapping *mapping = NFTL_MAPPING(device);

    while (1)
    {
        youest = nftl_find_youest_block(device, refer_block, &used_youest);
        if (youest == RT_UINT16_MAX)
        {
            if (device->plane_num == 1)
            {
                log_trace(LOG_TRACE_WARNING NFTL_MOD"No free block yet.\n");
                return RT_UINT16_MAX;
            }
            else
            {
                log_trace(LOG_TRACE_INFO NFTL_MOD"Try to find the youest block on the other plane.\n");
                /* re-search youest block on the other plane */
                youest = nftl_find_youest_block(device, RT_UINT16_MAX, &used_youest);
                if (youest == RT_UINT16_MAX)
                {
                    log_trace(LOG_TRACE_WARNING NFTL_MOD"No free block yet.\n");
                    return RT_UINT16_MAX;
                }
            }
        }

        /* made a wear-leveling */
        if (used_youest != RT_UINT16_MAX && used_youest != NFTL_MAPPING_BLOCK(device))
        {
            if (nftl_block_wear_leveling(device, &youest, used_youest) == RT_FALSE)
                continue;

            /* push to the recent queue */
            nftl_block_unmap(mapping, used_youest);
            nftl_recent_push(device, used_youest);
            continue;

            /* switch to used youest block */
            // youest = used_youest;
        } /* end of made a wear-leveling */

        // nftl_log_action(device, NFTL_ACTION_ALLOC_BLOCK, NFTL_LOG_BLOCK(youest), 0);

        /* allocate this block */
        nftl_block_set_used(mapping, youest);
        /* erase this block */
        result = rt_mtd_nand_erase_block(device, youest);
        if (result != RT_EOK)
        {
            /* destination block is a bad block */
            nftl_block_set_bad(mapping, youest);
            /* re-search empty block */
        }
        else
        {
            mapping->phyical_status[youest].erase_times ++;
            break;
        }
    } /* end of while (1) */

    log_trace(LOG_TRACE_DEBUG NFTL_MOD"allocate block: %d, erase time: %d\n", youest,
              mapping->phyical_status[youest].erase_times);

    return youest;
}

static rt_err_t nftl_page_software_copy(struct rt_mtd_nand_device* device, rt_uint16_t src_block, rt_uint16_t src_page,
                                        rt_uint16_t dst_block, rt_uint16_t dst_page)
{
    rt_err_t result;
    rt_uint32_t src_pages, dst_pages;
    rt_uint8_t  *oob_buffer  = RT_NULL;
    rt_uint8_t  *page_buffer = RT_NULL;

    page_buffer = (rt_uint8_t*) NFTL_PAGE_BUFFER_GET(device);
    if (page_buffer == RT_NULL)
    {
        result = -RT_MTD_ENOMEM;
        goto __exit;
    }
    oob_buffer = (rt_uint8_t*) NFTL_OOB_BUFFER_GET(device);
    if (oob_buffer == RT_NULL)
    {
        result = -RT_MTD_ENOMEM;
        goto __exit;
    }

    src_pages = src_block * device->pages_per_block + src_page;
    dst_pages = dst_block * device->pages_per_block + dst_page;

    log_trace(LOG_TRACE_DEBUG NFTL_MOD"swcopy page:block %d:%d => %d:%d\n", src_page, src_block,
              dst_page, dst_block);

    /* software copy */
    {
        result = rt_mtd_nand_read(device, src_pages, page_buffer, device->page_size, oob_buffer, device->oob_size);
        if (result == RT_MTD_EOK || result == -RT_MTD_EECC || result == -RT_MTD_EECC_CORRECT)
        {
            if (result == -RT_MTD_EECC)
            {
                log_trace(LOG_TRACE_WARNING NFTL_MOD "swcopy page: source page ecc error, %d:%d\n", src_page, src_block);
            }

            result = rt_mtd_nand_write(device, dst_pages, page_buffer, device->page_size, oob_buffer, device->oob_size);
        }
        else
        {
            log_trace(LOG_TRACE_ERROR NFTL_MOD"read bad @ nftl_page_swcopy source read, src %d:%d\n", src_page, src_block);
            /* source page issue */
            result = -RT_MTD_ESRC;
        }
    }

    if (result == RT_EOK)
    {
        /* read back */
        result = rt_mtd_nand_read(device, dst_pages, page_buffer, device->page_size, RT_NULL, device->oob_size);
        if (result != RT_EOK && result != -RT_MTD_EECC_CORRECT)
        {
            log_trace(LOG_TRACE_ERROR NFTL_MOD"read bad @ nftl_page_swcopy read back, %d:%d\n", dst_page, dst_block);
        }

        if (result == -RT_MTD_EECC_CORRECT)
        {
            log_trace(LOG_TRACE_INFO NFTL_MOD"swcopy page: read ecc corrected after write.\n");
            result = RT_MTD_EOK;
        }
    }

__exit:
    NFTL_PAGE_BUFFER_PUT(device, page_buffer);
    NFTL_OOB_BUFFER_PUT(device, oob_buffer);

    return result;
}

static rt_err_t nftl_page_copy(struct rt_mtd_nand_device* device, rt_uint16_t src_block, rt_uint16_t src_page,
                               rt_uint16_t dst_block, rt_uint16_t dst_page)
{
    rt_err_t result;
    rt_uint32_t src_pages, dst_pages;
    rt_uint8_t  *oob_buffer  = RT_NULL;
    rt_uint8_t  *page_buffer = RT_NULL;

    page_buffer = (rt_uint8_t*) NFTL_PAGE_BUFFER_GET(device);
    if (page_buffer == RT_NULL)
    {
        result = -RT_MTD_ENOMEM;
        goto __exit;
    }
    oob_buffer = (rt_uint8_t*) NFTL_OOB_BUFFER_GET(device);
    if (oob_buffer == RT_NULL)
    {
        result = -RT_MTD_ENOMEM;
        goto __exit;
    }

    src_pages = src_block * device->pages_per_block + src_page;
    dst_pages = dst_block * device->pages_per_block + dst_page;

    log_trace(LOG_TRACE_DEBUG NFTL_MOD"Copy page:block %d:%d => %d:%d\n", src_page, src_block,
              dst_page, dst_block);

    if (device->ops->move_page != RT_NULL) /* hardware copy */
    {
        rt_uint32_t mask;

        mask = device->plane_num - 1;
        if ((src_block & mask) != (dst_block & mask))
        {
            /* which is not on the same plane, use software copy */
            log_trace(LOG_TRACE_INFO NFTL_MOD"Use software copy %d => %d\n", src_pages, dst_pages);
            result = rt_mtd_nand_read(device, src_pages, page_buffer, device->page_size, oob_buffer, device->oob_size);
            if (result == RT_EOK || result == -RT_MTD_EECC_CORRECT)
            {
                result = rt_mtd_nand_write(device, dst_pages, page_buffer, device->page_size, oob_buffer, device->oob_size);
            }
            else
            {
                log_trace(LOG_TRACE_ERROR NFTL_MOD"read bad @ nftl_page soft read, %d:%d\n", src_page, src_block);
                result = -RT_MTD_ESRC;
            }
        }
        else
        {
            result = rt_mtd_nand_move_page(device, src_pages, dst_pages);
        }
    }
    else /* software copy */
    {
        log_trace(LOG_TRACE_INFO NFTL_MOD"Use software copy %d => %d\n", src_pages, dst_pages);
        result = rt_mtd_nand_read(device, src_pages, page_buffer, device->page_size, oob_buffer, device->oob_size);
        if (result == RT_EOK || result == -RT_MTD_EECC_CORRECT)
        {
            result = rt_mtd_nand_write(device, dst_pages, page_buffer, device->page_size, oob_buffer, device->oob_size);
        }
        else
        {
            log_trace(LOG_TRACE_ERROR NFTL_MOD"read bad @ nftl_page_copy soft read, %d:%d\n", dst_page, dst_block);
            result = -RT_MTD_ESRC;
        }
    }

    if (result == RT_EOK)
    {
        /* read back */
        result = rt_mtd_nand_read(device, dst_pages, page_buffer, device->page_size, RT_NULL, device->oob_size);
        if (result != RT_MTD_EOK && result != -RT_MTD_EECC_CORRECT)
        {
            /* try to read the source page */
            result = rt_mtd_nand_read(device, src_pages, page_buffer, device->page_size, oob_buffer, device->oob_size);
            if (result != RT_EOK && result != -RT_MTD_EECC_CORRECT)
            {
                log_trace(LOG_TRACE_ERROR NFTL_MOD"source page error, %d:%d\n", src_page, src_block);
                result = -RT_MTD_ESRC; /* the destination page dirty, should be retried with software copy */
            }
            else
            {
                log_trace(LOG_TRACE_ERROR NFTL_MOD"read bad @ nftl_page_copy read back, %d:%d\n", dst_page, dst_block);
                result = -RT_MTD_EECC;
            }
        }

        if (result == -RT_MTD_EECC_CORRECT)
        {
            log_trace(LOG_TRACE_INFO NFTL_MOD"copy page: read ecc corrected after write.\n");
            result = RT_MTD_EOK;
        }
    }

__exit:
    NFTL_PAGE_BUFFER_PUT(device, page_buffer);
    NFTL_OOB_BUFFER_PUT(device, oob_buffer);

    return result;
}

rt_inline rt_err_t nftl_page_domapping(struct nftl_page_mapping* mapping, rt_uint8_t logic_page, rt_uint8_t phy_page)
{
    RT_ASSERT(mapping != RT_NULL);

    mapping->logic_pages[logic_page] = phy_page;

    /* increase next freed page */
    if (mapping->next_free == phy_page)
        mapping->next_free += 1;

    return RT_EOK;
}

/* allocate a physical page from a block */
rt_inline rt_uint8_t nftl_page_allocate(struct rt_mtd_nand_device* device, struct nftl_page_mapping* mapping)
{
    rt_uint8_t phy_page;

    RT_ASSERT(device  != RT_NULL);
    RT_ASSERT(mapping != RT_NULL);

    if (mapping->next_free >= device->pages_per_block)
        return NFTL_PAGE_INVALID;

    phy_page = mapping->next_free;

    return phy_page;
}

rt_inline void nftl_page_init_mapping(struct rt_mtd_nand_device* device, struct nftl_page_mapping *page_mapping)
{
    /* initialize new page mapping */
    memset(page_mapping, 0xff, sizeof(struct nftl_page_mapping));
    page_mapping->next_free = 0;
}

int nftl_page_get_mapping(struct rt_mtd_nand_device* device,
                          rt_uint16_t phy_block,
                          struct nftl_page_mapping *page_mapping)
{
    rt_uint8_t page_index;
    rt_uint8_t *oob_buffer;
    struct nftl_page_tag *tag;
    rt_err_t result;
    rt_uint32_t page_position;

    RT_ASSERT(page_mapping != RT_NULL);

    oob_buffer = (rt_uint8_t*) NFTL_OOB_BUFFER_GET(device);
    if (oob_buffer == RT_NULL)
        return -RT_MTD_ENOMEM;

    tag = (struct nftl_page_tag*) OOB_FREE(device, oob_buffer, 0);
    page_position = phy_block * device->pages_per_block;

    /* initialize page mapping */
    nftl_page_init_mapping(device, page_mapping);
    page_mapping->phy_block = phy_block;

    /* read page mapping in this block */
    for (page_index = 0; page_index < device->pages_per_block; page_index++)
    {
        result = rt_mtd_nand_read(device, page_position + page_index, RT_NULL, device->page_size,
                                  oob_buffer, device->oob_size);
        if (result != RT_EOK && result != -RT_MTD_EECC_CORRECT)
        {
            log_trace(LOG_TRACE_ERROR NFTL_MOD"read page mapping failed: %d:%d, rc=%d\n",
                      page_index, phy_block, result);
            nftl_log_action(device, NFTL_ACTION_PMAPPING_FAILED, NFTL_LOG_BLOCK(phy_block), page_index);

            NFTL_OOB_BUFFER_PUT(device, oob_buffer);
            return result;
        }

        if (tag->magic != NFTL_PAGE_MAGIC)
        {
            page_mapping->next_free = page_index;
            if (tag->magic != 0x00 && tag->magic != 0xffff)
            {
                nftl_log_action(device, NFTL_ACTION_PAGETAG_FAILED, NFTL_LOG_BLOCK_PAGE(phy_block, page_index), tag->logic_page);
            }

            break;
        }

        if (tag->status == NFTL_PAGE_STATUS_USED)
        {
            if (tag->logic_page >= 64)
            {
                nftl_log_action(device, NFTL_ACTION_PAGETAG_FAILED, NFTL_LOG_BLOCK_PAGE(phy_block, page_index), tag->logic_page);
                RT_ASSERT(0);
            }

            page_mapping->logic_pages[tag->logic_page] = page_index;
            page_mapping->next_free = page_index + 1;
        }
    }

    /* check whether this block is full */
    if (page_mapping->next_free >= device->pages_per_block)
        page_mapping->next_free = NFTL_PAGE_INVALID;

    NFTL_OOB_BUFFER_PUT(device, oob_buffer);
    return RT_MTD_EOK;
}

int nftl_page_get_clean_mapping(struct rt_mtd_nand_device* device,
                                rt_uint16_t phy_block,
                                struct nftl_page_mapping *page_mapping)
{
    rt_uint8_t page_index;
    rt_uint8_t *oob_buffer;
    struct nftl_page_tag *tag;
    rt_err_t result;
    rt_uint32_t page_position;
    struct nftl_mapping *mapping = NFTL_MAPPING(device);

    RT_ASSERT(page_mapping != RT_NULL);

    oob_buffer = (rt_uint8_t*) NFTL_OOB_BUFFER_GET(device);
    if (oob_buffer == RT_NULL)
        return -RT_MTD_ENOMEM;

    tag = (struct nftl_page_tag*) OOB_FREE(device, oob_buffer, 0);
    page_position = phy_block * device->pages_per_block;

    /* initialize page mapping */
    nftl_page_init_mapping(device, page_mapping);
    page_mapping->phy_block = phy_block;

    /* read page mapping in this block */
    for (page_index = 0; page_index < device->pages_per_block; page_index++)
    {
        result = rt_mtd_nand_read(device, page_position + page_index, RT_NULL, device->page_size,
                                  oob_buffer, device->oob_size);
        if (result != RT_EOK && result != -RT_MTD_EECC_CORRECT)
        {
            log_trace(LOG_TRACE_ERROR NFTL_MOD"read page mapping failed: %d:%d, rc=%d\n",
                      page_index, phy_block, result);
            NFTL_OOB_BUFFER_PUT(device, oob_buffer);
            return result;
        }

        if (tag->magic != NFTL_PAGE_MAGIC)
        {
            page_mapping->next_free = page_index;
            if (tag->magic != 0x00 && tag->magic != 0xffff)
            {
                nftl_log_action(device, NFTL_ACTION_PAGETAG_FAILED, NFTL_LOG_BLOCK_PAGE(phy_block, page_index), tag->logic_page);
            }

            break;
        }

        if (tag->sn >= mapping->sn)
        {
            /* dirty page, skip it */
            continue;
        }

        if (tag->status == NFTL_PAGE_STATUS_USED)
        {
            if (tag->logic_page >= 64)
            {
                nftl_log_action(device, NFTL_ACTION_PAGETAG_FAILED, NFTL_LOG_BLOCK_PAGE(phy_block, page_index), tag->logic_page);
                RT_ASSERT(0);
            }

            page_mapping->logic_pages[tag->logic_page] = page_index;
            page_mapping->next_free = page_index + 1;
        }
    }

    /* check whether this block is full */
    if (page_mapping->next_free >= device->pages_per_block)
        page_mapping->next_free = NFTL_PAGE_INVALID;

    NFTL_OOB_BUFFER_PUT(device, oob_buffer);
    return RT_MTD_EOK;
}

static rt_uint8_t nftl_page_to_physical(struct rt_mtd_nand_device* device, rt_uint16_t phy_block, rt_uint16_t logic_page)
{
    int result;
    struct nftl_page_mapping page_mapping;

    /* get mapping */
    result = nftl_page_get_mapping(device, phy_block, &page_mapping);
    if (result != RT_MTD_EOK)
    {
        log_trace(LOG_TRACE_ERROR NFTL_MOD "get page mapping error\n");
        RT_ASSERT(0);
    }

    return page_mapping.logic_pages[logic_page];
}

rt_err_t nftl_erase_pages(struct rt_mtd_nand_device* device, rt_uint32_t page_begin, rt_uint32_t page_end)
{
    rt_uint16_t logic_block;
    rt_uint8_t page_chunk;
    rt_uint32_t page_index;
    struct nftl_layer *layer = NFTL_LAYER(device);
    struct nftl_mapping *mapping = NFTL_MAPPING(device);

    log_trace(LOG_TRACE_DEBUG NFTL_MOD"page erase: %d - %d\n", page_begin, page_end);

    nftl_log_action(device, NFTL_ACTION_ERASE_LBLOCK, page_begin, page_end);

    page_chunk = 0;
    for (page_index = page_begin; page_index < page_end; page_index += page_chunk)
    {
        if (page_index + (device->pages_per_block - page_index % device->pages_per_block) > page_end)
            page_chunk = (rt_uint8_t)(page_end % device->pages_per_block - page_index % device->pages_per_block);
        else
            page_chunk = (rt_uint8_t)(device->pages_per_block - page_index % device->pages_per_block);

        /* get block */
        logic_block = (rt_uint16_t)(page_index / device->pages_per_block);

        if (page_chunk == device->pages_per_block)
        {
            rt_uint16_t old_phyblock;

            /* get physical block */
            old_phyblock = nftl_block_mapping(mapping, logic_block);
            if (old_phyblock == RT_UINT16_MAX)
                continue; /* it's an unmapped block */

            /* erase a block */
            nftl_block_domapping(mapping, logic_block, RT_UINT16_MAX);

            /* set the old block as free */
            nftl_block_unmap(mapping, old_phyblock);
            nftl_recent_push(device, old_phyblock);
        }
        else
        {
            /* copy the rest pages to another block */
            rt_uint8_t index;
            rt_uint8_t new_page;
            rt_uint16_t old_phyblock;
            rt_uint16_t new_phyblock;
            rt_err_t result;
            rt_bool_t block_is_bad = RT_FALSE;
            rt_bool_t using_sw_copy = RT_FALSE;
            struct nftl_page_tag *tag;
            rt_uint8_t oob_buffer[NFTL_OOB_MAX], *oob_ptr;

            struct nftl_page_mapping old_page_mapping;
            struct nftl_page_mapping new_page_mapping;

            /* get physical block */
            old_phyblock = nftl_block_mapping(mapping, logic_block);
            if (old_phyblock == RT_UINT16_MAX)
                continue; /* it's an unmapped block */

            log_trace(LOG_TRACE_DEBUG NFTL_MOD"page erase: erase page chunk %d - %d\n", page_index, page_index + page_chunk);

            oob_ptr = &oob_buffer[0];
            tag = (struct nftl_page_tag*) OOB_FREE(device, oob_ptr, 0);

            /* read old page mapping */
            memset(&old_page_mapping, 0xff, sizeof(struct nftl_page_mapping));
            for (index = 0; index < device->pages_per_block; index ++)
            {
                result = rt_mtd_nand_read(device, old_phyblock * device->pages_per_block + index, RT_NULL, device->page_size,
                                          oob_ptr, device->oob_size);
                if (result == RT_EOK || result == -RT_MTD_EECC_CORRECT)
                {
                    if (tag->magic != NFTL_PAGE_MAGIC)
                        break;
                    if (tag->status == NFTL_PAGE_STATUS_USED && (tag->sn <= mapping->sn))
                    {
                        /* set mapping */
                        old_page_mapping.logic_pages[tag->logic_page] = (rt_uint8_t)index;
                    }
                }
                else
                {
                    log_trace(LOG_TRACE_ERROR NFTL_MOD"page erase: read page failed(%d:%d).\n", index, old_phyblock);

                    return -RT_EIO;
                }
            }

            /* un-set page mapping */
            for (index = (rt_uint8_t)(page_index % device->pages_per_block);
                    index < (rt_uint8_t)(page_index % device->pages_per_block + page_chunk);
                    index ++)
            {
                old_page_mapping.logic_pages[index] = RT_UINT8_MAX;
            }

            for (index = 0; index < device->pages_per_block; index ++)
            {
                if (old_page_mapping.logic_pages[index] != RT_UINT8_MAX)
                    break;
            }
            /* erase a block */
            if (index == device->pages_per_block)
            {
                nftl_block_domapping(mapping, logic_block, RT_UINT16_MAX);

                /* set the old block as free */
                nftl_block_unmap(mapping, old_phyblock);
                nftl_recent_push(device, old_phyblock);
                continue;
            }

            while (1)
            {
                /* allocate a block */
                new_phyblock = nftl_block_allocate(device, old_phyblock);
                if (BLOCK_IS_INVALID(new_phyblock))
                {
                    log_trace(LOG_TRACE_ERROR NFTL_MOD"Out of block in the flash.\n");

                    return -RT_EIO;
                }
                block_is_bad = RT_FALSE;

                /* initialize page mapping */
                nftl_page_init_mapping(device, &new_page_mapping);

                /* copy to the new block */
                for (index = 0; index < device->pages_per_block; index ++)
                {
                    if (old_page_mapping.logic_pages[index] != RT_UINT8_MAX)
                    {
                        /* allocate a new page */
                        new_page = nftl_page_allocate(device, &new_page_mapping);
                        /* made a page copy */
                        if (using_sw_copy == RT_TRUE)
                        {
                            result = nftl_page_software_copy(device, old_phyblock, old_page_mapping.logic_pages[index], new_phyblock, new_page);
                        }
                        else
                        {
                            result = nftl_page_copy(device, old_phyblock, old_page_mapping.logic_pages[index], new_phyblock, new_page);
                            if (result == -RT_MTD_ESRC)
                            {
                                /* source page error */
                                using_sw_copy = RT_TRUE;
                                block_is_bad = RT_TRUE; /* continue to find an empty block */
                                break;
                            }
                        }

                        if (result == -RT_MTD_ESRC)
                        {
                            log_trace(LOG_TRACE_WARNING NFTL_MOD"source page error: %d:%d when erase\n", old_page_mapping.logic_pages[index],
                                      old_phyblock);
                        }
                        else if (result != RT_MTD_EOK)
                        {
                            nftl_block_set_bad(mapping, new_phyblock);
                            block_is_bad = RT_TRUE;
                            break;
                        }

                        /* copy OK */
                        nftl_page_domapping(&new_page_mapping, (rt_uint8_t)index, new_page);
                    }
                }

                if (block_is_bad == RT_FALSE)
                {
                    /* made a block mapping */
                    nftl_block_domapping(mapping, logic_block, new_phyblock);

                    nftl_block_unmap(mapping, old_phyblock);
                    nftl_recent_push(device, old_phyblock);
                    break;
                }
            }
        }
    }

    return RT_EOK;
}

rt_err_t nftl_read_page(struct rt_mtd_nand_device* device, rt_uint16_t block_offset, rt_uint16_t page_offset,
                        rt_uint8_t *buffer)
{
    rt_err_t result;
    rt_uint16_t phy_block;
    rt_uint8_t  phy_page;
    struct nftl_layer *layer = NFTL_LAYER(device);
    struct nftl_mapping *mapping = NFTL_MAPPING(device);

#ifdef NFTL_DEMO_VERSION
    static rt_uint32_t nftl_paging = 0;
    if (nftl_paging > ((1024 * 1024 * 1024)/2048))
    {
        log_trace(LOG_TRACE_ERROR NFTL_MOD "This is a debug version!\n");
        return -RT_EIO;
    }
#endif

    phy_block = nftl_block_mapping(mapping, block_offset);
    if (BLOCK_IS_INVALID(phy_block))
    {
        /* no this block on physical, return 0x00 */
        log_trace(LOG_TRACE_DEBUG NFTL_MOD"return empty data on %d:%d\n", page_offset, block_offset);
        memset(buffer, 0x00, device->page_size);

        return RT_EOK;
    }

    phy_page = nftl_page_to_physical(device, phy_block, page_offset);
    if (phy_page == NFTL_PAGE_INVALID)
    {
        /* no this page on physical device, return 0x00 */
        log_trace(LOG_TRACE_DEBUG NFTL_MOD"return empty data on %d:%d\n", page_offset, block_offset);
        memset(buffer, 0x00, device->page_size);

        return RT_EOK;
    }

    log_trace(LOG_TRACE_DEBUG NFTL_MOD"Read single logic page %d:%d at %d:%d\n", page_offset, block_offset,
              phy_page, phy_block);

    /* read one page on physical page */
    result = rt_mtd_nand_read(device, phy_block * device->pages_per_block + phy_page, buffer,
                              device->page_size, RT_NULL, device->oob_size);
    if (result != RT_EOK && result != -RT_MTD_EECC_CORRECT)
    {
        log_trace(LOG_TRACE_ERROR NFTL_MOD"Read page failed: %d, %d:%d\n", result, phy_page, phy_block);

        nftl_log_action(device, NFTL_ACTION_READ_PAGE, NFTL_LOG_BLOCK_PAGE(block_offset, page_offset), NFTL_LOG_BLOCK_PAGE(phy_block, phy_page));
        nftl_log_action(device, NFTL_ACTION_READ_FAILED, NFTL_LOG_BLOCK_PAGE(phy_block, phy_page), 0);

        return -RT_EIO;
    }

    if (result == -RT_MTD_EECC_CORRECT)
    {
        log_trace(LOG_TRACE_INFO NFTL_MOD "Read page with corrected ECC, %d:%d\n", phy_page, phy_block);

        nftl_log_action(device, NFTL_ACTION_READ_PAGE, NFTL_LOG_BLOCK_PAGE(block_offset, page_offset), NFTL_LOG_BLOCK_PAGE(phy_block, phy_page));
        nftl_log_action(device, NFTL_ACTION_BIT_INVERT, NFTL_LOG_BLOCK_PAGE(phy_block, phy_page), 0);

        /* copy this block to another block */
        nftl_block_copy(device, block_offset);
    }

#ifdef NFTL_DEMO_VERSION
    nftl_paging += 1;
#endif

    return result;
}

rt_err_t nftl_read_multi_page(struct rt_mtd_nand_device* device, rt_uint16_t block_offset, rt_uint16_t page_offset,
                              rt_uint8_t *buffer, rt_size_t count)
{
    rt_err_t result;
    rt_uint16_t index;
    rt_uint16_t phy_block;
    rt_uint8_t  phy_page;
    rt_bool_t need_copy = RT_FALSE;
    struct nftl_layer *layer = NFTL_LAYER(device);
    struct nftl_mapping *mapping = NFTL_MAPPING(device);
    struct nftl_page_mapping page_mapping;

    result = RT_EOK;
    phy_block = nftl_block_mapping(mapping, block_offset);
    if (BLOCK_IS_INVALID(phy_block))
    {
        /* no this block on physical, return 0x00 */
        memset(buffer, 0x00, device->page_size * count);

        return RT_EOK;
    }

    result = nftl_page_get_mapping(device, phy_block, &page_mapping);
    if (result != RT_MTD_EOK)
    {
        log_trace(LOG_TRACE_ERROR NFTL_MOD "get page mapping error in reading multi-page.\n");

        return -RT_EIO;
    }

    for (index = 0; index < count; index ++)
    {
        phy_page = page_mapping.logic_pages[page_offset + index];
        if (phy_page == NFTL_PAGE_INVALID)
        {
            /* no this page on physical device, return 0x00 */
            memset(buffer, 0x00, device->page_size);
        }
        else
        {
            log_trace(LOG_TRACE_DEBUG NFTL_MOD"Read chunk-logic page %d:%d at %d:%d\n", page_offset + index, block_offset,
                      phy_page, phy_block);

            /* read one page on physical page */
            result = rt_mtd_nand_read(device, phy_block * device->pages_per_block + phy_page, buffer,
                                      device->page_size, RT_NULL, device->oob_size);
            if (result != RT_EOK && result != -RT_MTD_EECC_CORRECT)
            {
                log_trace(LOG_TRACE_ERROR NFTL_MOD"Read page failed: %d, %d:%d\n", result, phy_page, phy_block);
                nftl_log_action(device, NFTL_ACTION_READ_FAILED, NFTL_LOG_BLOCK_PAGE(phy_block, phy_page), 0);

                return -RT_MTD_EIO;
            }

            if (result == -RT_MTD_EECC_CORRECT)
                need_copy = RT_TRUE;
        }

        buffer += device->page_size;
    }

    if (need_copy == RT_TRUE)
    {
        log_trace(LOG_TRACE_INFO NFTL_MOD "Read multi-page with corrected ECC, block %d\n", phy_block);

        /* copy this block to another block */
        nftl_block_copy(device, block_offset);
    }

    return result;
}

static rt_err_t nftl_page_write_with_tag(struct rt_mtd_nand_device* device, rt_uint16_t block,
        rt_uint16_t page, rt_uint16_t logic_page, const rt_uint8_t *buffer)
{
    rt_uint8_t *oob_buffer;
    rt_uint8_t *page_buffer;

    rt_err_t result;
    struct nftl_page_tag* page_tag;
    struct nftl_mapping *mapping = NFTL_MAPPING(device);

    oob_buffer = (rt_uint8_t*) NFTL_OOB_BUFFER_GET(device);
    if (oob_buffer == RT_NULL)
        return -RT_MTD_ENOMEM;
    page_buffer = (rt_uint8_t*) NFTL_PAGE_BUFFER_GET(device);
    if (page_buffer == RT_NULL)
        return -RT_MTD_ENOMEM;

    /* write to the page */
    memset(oob_buffer, 0xff, NFTL_OOB_MAX);
    page_tag = (struct nftl_page_tag*)OOB_FREE(device, oob_buffer, 0);
    page_tag->logic_page = (rt_uint8_t)logic_page;
    page_tag->magic = NFTL_PAGE_MAGIC;
    page_tag->status = NFTL_PAGE_STATUS_USED;
    page_tag->sn = mapping->sn;

    result = rt_mtd_nand_write(device, block * device->pages_per_block + page,
                               buffer, device->page_size,
                               oob_buffer, device->oob_size);
    if (result == RT_MTD_EOK)
    {
        /* read back to check status */
        result = rt_mtd_nand_read(device, block * device->pages_per_block + page,
                                  page_buffer, device->page_size, oob_buffer, device->oob_size);
        if (result != RT_MTD_EOK && result != -RT_MTD_EECC_CORRECT)
        {
            nftl_log_action(device, NFTL_ACTION_WRITE_FAILED, NFTL_LOG_BLOCK_PAGE(block, page), 0);
        }
    }

    NFTL_PAGE_BUFFER_PUT(device, page_buffer);
    NFTL_OOB_BUFFER_PUT(device, oob_buffer);
    return result;
}

rt_err_t nftl_write_page(struct rt_mtd_nand_device* device, rt_uint16_t block_offset, rt_uint16_t page_offset,
                         const rt_uint8_t *buffer)
{
    rt_err_t result;
    rt_bool_t using_sw_copy;
    rt_uint8_t new_page;
    rt_uint16_t page_index;
    rt_uint16_t old_phyblock, new_phyblock;
    struct nftl_layer *layer = NFTL_LAYER(device);
    struct nftl_mapping *mapping = NFTL_MAPPING(device);
    struct nftl_page_mapping old_page_mapping, new_page_mapping;

#ifdef NFTL_DEMO_VERSION
    static rt_uint32_t nftl_paging = 0;
    if (nftl_paging > ((1024 * 1024 * 1024)/2048))
    {
        log_trace(LOG_TRACE_ERROR NFTL_MOD "This is a debug version!\n");
        return -RT_EIO;
    }
#endif

    result = RT_EOK;

    /* get old physical block */
    old_phyblock = nftl_block_mapping(mapping, block_offset);
    if (!BLOCK_IS_INVALID(old_phyblock))
    {
        /* get page mapping table */
        nftl_page_get_mapping(device, old_phyblock, &old_page_mapping);

        /* check whether it is an empty page */
        if (old_page_mapping.logic_pages[page_offset] == NFTL_PAGE_INVALID)
        {
            if (nftl_is_empty_page(device, (rt_uint32_t*)buffer) == RT_TRUE)
            {
                return RT_EOK;
            }
        }

        /* allocate a new page */
        new_page = nftl_page_allocate(device, &old_page_mapping);
        if (new_page != NFTL_PAGE_INVALID)
        {
            /* write to the page */
            log_trace(LOG_TRACE_DEBUG NFTL_MOD"Write single-logic page %d:%d to %d:%d\n",
                      page_offset, block_offset, new_page, old_phyblock);

            if (block_offset == 0 && (page_offset >= 50 && page_offset <= 53))
            {
                nftl_log_action(device, NFTL_ACTION_WRITE_PAGE, NFTL_LOG_BLOCK_PAGE(block_offset, page_offset), NFTL_LOG_BLOCK_PAGE(old_phyblock, new_page));
            }

            result = nftl_page_write_with_tag(device, old_phyblock, new_page, page_offset, buffer);
            if (result == RT_EOK || result == -RT_MTD_EECC_CORRECT)
            {
                /* write OK */
                nftl_page_domapping(&old_page_mapping, (rt_uint8_t)page_offset, new_page);

                if (result == -RT_MTD_EECC_CORRECT)
                {
                    nftl_log_action(device, NFTL_ACTION_WRITE_PAGE, NFTL_LOG_BLOCK_PAGE(block_offset, page_offset), NFTL_LOG_BLOCK_PAGE(old_phyblock, new_page));
                    nftl_log_action(device, NFTL_ACTION_BIT_INVERT, NFTL_LOG_BLOCK(old_phyblock), 0);
                    /* move block */
                    nftl_block_copy(device, block_offset);
                }

                return RT_EOK;
            }
            else
            {
                log_trace(LOG_TRACE_WARNING NFTL_MOD"bad block[%d] when writing\n", old_phyblock);
                /* destination block is a bad block, continue and allocate a new block */
                nftl_block_set_bad(mapping, old_phyblock);

                nftl_log_action(device, NFTL_ACTION_WRITE_PAGE, NFTL_LOG_BLOCK_PAGE(block_offset, page_offset), NFTL_LOG_BLOCK_PAGE(old_phyblock, new_page));
                nftl_log_action(device, NFTL_ACTION_WRITE_FAILED, NFTL_LOG_BLOCK_PAGE(old_phyblock, new_page), 0);
            }
        }
        /* else the block is full, we need to allocate a new block */
    }
    else
    {
        /* check whether it is an empty page */
        if (nftl_is_empty_page(device, (rt_uint32_t*)buffer) == RT_TRUE)
        {
            return RT_EOK;
        }

        /* write to a new block */
        while (1)
        {
            /* allocate an empty physical block */
            new_phyblock = nftl_block_allocate(device, old_phyblock);
            if (BLOCK_IS_INVALID(new_phyblock))
            {
                return -RT_EFULL; /* no block */
            }

            /* initialize new page mapping */
            nftl_page_init_mapping(device, &new_page_mapping);
            /* allocate a new page */
            new_page = nftl_page_allocate(device, &new_page_mapping);
            if (new_page != NFTL_PAGE_INVALID)
            {
                /* write to the page */
                log_trace(LOG_TRACE_DEBUG NFTL_MOD"Write signle-logic page %d:%d to %d:%d\n",
                          page_offset, block_offset, new_page, new_phyblock);

                result = nftl_page_write_with_tag(device, new_phyblock, new_page, page_offset, buffer);
                if (result == RT_EOK || result == -RT_MTD_EECC_CORRECT)
                {
                    /* write OK */
                    /* do a block mapping */
                    nftl_block_domapping(mapping, block_offset, new_phyblock);
                    if (block_offset == 0)
                        nftl_log_action(device, NFTL_ACTION_MAP_BLOCK, NFTL_LOG_BLOCK(block_offset), NFTL_LOG_BLOCK(new_phyblock));

                    if (result == -RT_MTD_EECC_CORRECT)
                    {
                        nftl_log_action(device, NFTL_ACTION_WRITE_PAGE, NFTL_LOG_BLOCK_PAGE(block_offset, page_offset), NFTL_LOG_BLOCK_PAGE(new_phyblock, new_page));
                        nftl_log_action(device, NFTL_ACTION_BIT_INVERT, NFTL_LOG_BLOCK(new_phyblock), 0);

                        /* move block */
                        nftl_block_copy(device, block_offset);
                    }

                    return RT_EOK;
                }
                else
                {
                    log_trace(LOG_TRACE_WARNING NFTL_MOD"bad block[%d] when writing\n", new_phyblock);
                    nftl_log_action(device, NFTL_ACTION_WRITE_PAGE, NFTL_LOG_BLOCK_PAGE(block_offset, page_offset), NFTL_LOG_BLOCK_PAGE(new_phyblock, new_page));
                    nftl_log_action(device, NFTL_ACTION_WRITE_FAILED, NFTL_LOG_BLOCK_PAGE(new_phyblock, new_page), 0);

                    /* destination block is a bad block, FIXME: allocate a new block */
                    nftl_block_set_bad(mapping, new_phyblock);
                }
            }
        }
    }

    using_sw_copy = RT_FALSE;

    /* try to allocate a new block and then copy the exist page */
    while (1)
    {
        rt_bool_t is_bad_block = RT_FALSE;

        /* allocate an empty physical block */
        new_phyblock = nftl_block_allocate(device, old_phyblock);
        if (BLOCK_IS_INVALID(new_phyblock))
            return -RT_EFULL; /* no block */

        /* copy the old block page to the new allocated block */
        if (block_offset == 0 && (page_offset >= 50 && page_offset <= 53))
        {
            nftl_log_action(device, NFTL_ACTION_WRITE_PAGE, NFTL_LOG_BLOCK_PAGE(block_offset, page_offset), NFTL_LOG_BLOCK_PAGE(new_phyblock, 0));
        }
        if (block_offset == 0)
            nftl_log_action(device, NFTL_ACTION_COPY_BLOCK, NFTL_LOG_BLOCK(old_phyblock), NFTL_LOG_BLOCK(new_phyblock));

        /* initialize new page mapping */
        nftl_page_init_mapping(device, &new_page_mapping);
        /* re-get the old page mapping */
        nftl_page_get_mapping(device, old_phyblock, &old_page_mapping);

        /* for each logic page */
        for (page_index = 0; page_index < device->pages_per_block; page_index ++)
        {
            if (page_index == page_offset)
            {
                /* perform page write */
                new_page = nftl_page_allocate(device, &new_page_mapping);

                log_trace(LOG_TRACE_DEBUG NFTL_MOD"Write single-logic page %d:%d to %d:%d\n",
                          page_index, block_offset, new_page, new_phyblock);

                result = nftl_page_write_with_tag(device, new_phyblock, new_page, page_offset, buffer);
                if (result == RT_EOK || result == -RT_MTD_EECC_CORRECT)
                {
                    /* ignore the corrected ECC */
                    if (result == -RT_MTD_EECC_CORRECT)
                        result = RT_MTD_EOK;
                    nftl_page_domapping(&new_page_mapping, (rt_uint8_t)page_offset, new_page);
                }
                else
                {
                    /* bad block */
                    is_bad_block = RT_TRUE;

                    log_trace(LOG_TRACE_WARNING NFTL_MOD"bad block[%d] when writing\n", new_phyblock);
                    /* destination block is a bad block. */
                    nftl_block_set_bad(mapping, new_phyblock);

                    nftl_log_action(device, NFTL_ACTION_WRITE_PAGE, NFTL_LOG_BLOCK_PAGE(block_offset, page_offset), NFTL_LOG_BLOCK_PAGE(new_phyblock, new_page));
                    nftl_log_action(device, NFTL_ACTION_WRITE_FAILED, NFTL_LOG_BLOCK_PAGE(new_phyblock, new_page), 0);

                    break;
                }
            }
            else if (old_page_mapping.logic_pages[page_index] != NFTL_PAGE_INVALID)
            {
                /* allocate a new physical page */
                new_page = nftl_page_allocate(device, &new_page_mapping);

                /* perform page copy */
                if (using_sw_copy == RT_TRUE)
                {
                    result = nftl_page_software_copy(device, old_phyblock, old_page_mapping.logic_pages[page_index],
                                                     new_phyblock, new_page);
                }
                else
                {
                    result = nftl_page_copy(device, old_phyblock, old_page_mapping.logic_pages[page_index],
                                            new_phyblock, new_page);
                    if (result == -RT_MTD_ESRC)
                    {
                        /* source page error */
                        using_sw_copy = RT_TRUE; /* use software copy */
                        is_bad_block = RT_TRUE;
                        break;
                    }
                }

                if (result == -RT_MTD_ESRC)
                {
                    log_trace(LOG_TRACE_WARNING NFTL_MOD"source page error: %d:%d when writing\n", old_page_mapping.logic_pages[page_index],
                              old_phyblock);
                    nftl_log_action(device, NFTL_ACTION_ESRC, NFTL_LOG_BLOCK_PAGE(old_phyblock, old_page_mapping.logic_pages[page_index]), 0);
                }
                else if (result != RT_EOK)
                {
                    /* bad block */
                    is_bad_block = RT_TRUE;

                    log_trace(LOG_TRACE_WARNING NFTL_MOD"bad block[%d] when writing\n", new_phyblock);
                    nftl_log_action(device, NFTL_ACTION_COPYPAGE_FAILED, NFTL_LOG_BLOCK_PAGE(old_phyblock, old_page_mapping.logic_pages[page_index]),
                                    NFTL_LOG_BLOCK_PAGE(new_phyblock, new_page));

                    /* destination block is a bad block */
                    nftl_block_set_bad(mapping, new_phyblock);

                    break;
                }
                nftl_page_domapping(&new_page_mapping, (rt_uint8_t)page_index, new_page);
            }
        }

        if (is_bad_block == RT_FALSE)
        {
            /* update page mapping */
            // memcpy(&old_page_mapping, &new_page_mapping, sizeof(struct nftl_page_mapping));

            /* do a block mapping */
            nftl_block_domapping(mapping, block_offset, new_phyblock);
            if (block_offset == 0)
                nftl_log_action(device, NFTL_ACTION_MAP_BLOCK, NFTL_LOG_BLOCK(block_offset), NFTL_LOG_BLOCK(new_phyblock));

            /* mark old block as not used */
            nftl_block_unmap(mapping, old_phyblock);
            // nftl_log_action(device, NFTL_ACTION_UNMAP_BLOCK, NFTL_LOG_BLOCK(old_phyblock), 0);

            nftl_recent_push(device, old_phyblock);

            break;
        }
        /* else continue to allocate a new block */
    }

#ifdef NFTL_DEMO_VERSION
    nftl_paging += 1;
#endif

    return result;
}

rt_err_t nftl_write_multi_page(struct rt_mtd_nand_device* device, rt_uint16_t block_offset, rt_uint16_t page_offset,
                               const rt_uint8_t *buffer, rt_size_t count)
{
    rt_err_t result;
    rt_uint8_t new_page;
    rt_bool_t using_sw_copy;
    rt_uint16_t index, page_index;
    rt_uint16_t old_phyblock, new_phyblock;
    struct nftl_layer *layer = NFTL_LAYER(device);
    struct nftl_mapping *mapping = NFTL_MAPPING(device);
    struct nftl_page_mapping old_page_mapping, new_page_mapping;

    result = RT_EOK;

    /* get old physical block */
    old_phyblock = nftl_block_mapping(mapping, block_offset);

    if (!BLOCK_IS_INVALID(old_phyblock))
    {
        /* get page mapping table */
        result = nftl_page_get_mapping(device, old_phyblock, &old_page_mapping);
        if (result != RT_MTD_EOK)
        {
            log_trace(LOG_TRACE_ERROR NFTL_MOD "get page mapping failed @ write multi-page.\n");

            return result;
        }

        if ((device->pages_per_block > old_page_mapping.next_free) &&
                ((rt_uint8_t)device->pages_per_block - old_page_mapping.next_free) > (rt_uint8_t)count - 1)
        {
            rt_bool_t need_copy = RT_FALSE;
            rt_bool_t is_bad_block = RT_FALSE;

            /* we has enough empty pages on the block */
            for (page_index = 0; page_index < count; page_index ++)
            {
                new_page = nftl_page_allocate(device, &old_page_mapping);
                RT_ASSERT(new_page < device->pages_per_block);

                /* write to the page */
                log_trace(LOG_TRACE_DEBUG NFTL_MOD"Write chunk-logic page %d:%d to %d:%d\n",
                          page_offset + page_index, block_offset, new_page, old_phyblock);

                result = nftl_page_write_with_tag(device, old_phyblock, new_page, page_offset + page_index,
                                                  buffer + page_index * device->page_size);
                if (result == RT_EOK || result == -RT_MTD_EECC_CORRECT)
                {
                    if (result == -RT_MTD_EECC_CORRECT)
                        need_copy = RT_TRUE;
                    /* write OK */
                    nftl_page_domapping(&old_page_mapping, (rt_uint8_t)(page_offset + page_index), new_page);
                }
                else
                {
                    is_bad_block = RT_TRUE;
                    log_trace(LOG_TRACE_WARNING NFTL_MOD"bad block[%d] when writing\n", old_phyblock);
                    /* destination block is a bad block */
                    nftl_block_set_bad(mapping, old_phyblock);
                    break;
                }
            }

            if (is_bad_block == RT_FALSE)
            {
                if (need_copy == RT_TRUE)
                {
                    /* copy the block to the another block */
                    nftl_block_copy(device, block_offset);
                }

                return RT_EOK;
            }
        }
    }
    else
    {
        /* write to a new block */
        while (1)
        {
            rt_bool_t need_copy = RT_FALSE;
            rt_bool_t is_bad_block = RT_FALSE;

            /* allocate an empty physical block */
            new_phyblock = nftl_block_allocate(device, old_phyblock);
            if (BLOCK_IS_INVALID(new_phyblock))
            {
                return -RT_EFULL; /* no block */
            }

            /* initialize new page mapping */
            nftl_page_init_mapping(device, &new_page_mapping);
            for (page_index = 0; page_index < count; page_index ++)
            {
                /* allocate a new page */
                new_page = nftl_page_allocate(device, &new_page_mapping);
                if (new_page != NFTL_PAGE_INVALID)
                {
                    /* write to the page */
                    log_trace(LOG_TRACE_DEBUG NFTL_MOD"Write chunk-logic page %d:%d to %d:%d\n",
                              page_offset + page_index, block_offset, new_page, new_phyblock);

                    result = nftl_page_write_with_tag(device, new_phyblock, new_page, page_offset + page_index,
                                                      buffer + page_index * device->page_size);
                    if (result == RT_EOK || result == -RT_MTD_EECC_CORRECT)
                    {
                        if (result == -RT_MTD_EECC_CORRECT)
                            need_copy = RT_TRUE;
                        /* write OK */
                        nftl_page_domapping(&new_page_mapping, (rt_uint8_t)(page_offset + page_index), new_page);
                    }
                    else
                    {
                        is_bad_block = RT_TRUE;
                        log_trace(LOG_TRACE_WARNING NFTL_MOD"bad block[%d] when writing\n", old_phyblock);
                        /* destination block is a bad block, FIXME: allocate a new block */
                        nftl_block_set_bad(mapping, new_phyblock);

                        break;
                    }
                }
            }

            if (is_bad_block == RT_FALSE)
            {
                /* do a block mapping */
                nftl_block_domapping(mapping, block_offset, new_phyblock);

                if (need_copy == RT_TRUE)
                {
                    /* copy the block to the another block */
                    nftl_block_copy(device, block_offset);
                }

                return RT_EOK;
            }
        }
    }

    using_sw_copy = RT_FALSE;
    /* try to allocate a new block and then copy the page */
    while (1)
    {
        rt_bool_t is_bad_block = RT_FALSE;

        /* allocate an empty physical block */
        new_phyblock = nftl_block_allocate(device, old_phyblock);
        if (BLOCK_IS_INVALID(new_phyblock))
        {
            return -RT_EFULL; /* no block */
        }

        /* copy the old block page to the new allocated block */

        /* initialize new page mapping */
        nftl_page_init_mapping(device, &new_page_mapping);

        /* re-get the old page mapping */
        nftl_page_get_mapping(device, old_phyblock, &old_page_mapping);

        /* for each logic page */
        for (page_index = 0; page_index < device->pages_per_block; page_index ++)
        {
            if (page_index == page_offset)
            {
                /* perform multi-pages write */
                for (index = 0; index < count; index ++)
                {
                    /* allocate a new page */
                    new_page = nftl_page_allocate(device, &new_page_mapping);

                    /* build a page tag and then write to page */
                    log_trace(LOG_TRACE_DEBUG NFTL_MOD"Write chunk-logic page %d:%d to %d:%d\n",
                              page_offset + index, block_offset, new_page, new_phyblock);

                    result = nftl_page_write_with_tag(device, new_phyblock, new_page, page_offset + index,
                                                      buffer + index * device->page_size);
                    if (result == RT_EOK || result == -RT_MTD_EECC_CORRECT)
                    {
                        /* ignore the corrected ECC */
                        if (result == -RT_MTD_EECC_CORRECT)
                            result = RT_MTD_EOK;
                        nftl_page_domapping(&new_page_mapping, page_offset + index, new_page);
                    }
                    else
                    {
                        /* bad block */
                        is_bad_block = RT_TRUE;

                        log_trace(LOG_TRACE_WARNING NFTL_MOD"bad block[%d] when writing\n", new_phyblock);
                        /* destination block is a bad block, break and allocate a new block */
                        nftl_block_set_bad(mapping, new_phyblock);

                        break;
                    }
                }

                if (is_bad_block == RT_TRUE)
                    break;
                else
                {
                    page_index += (rt_uint16_t)count - 1;
                    continue;
                }
            }
            else if (old_page_mapping.logic_pages[page_index] != NFTL_PAGE_INVALID)
            {
                /* allocate a new physical page */
                new_page = nftl_page_allocate(device, &new_page_mapping);

                /* perform page copy */
                if (using_sw_copy == RT_TRUE)
                {
                    result = nftl_page_software_copy(device, old_phyblock, old_page_mapping.logic_pages[page_index],
                                                     new_phyblock, new_page);
                }
                else
                {
                    result = nftl_page_copy(device, old_phyblock, old_page_mapping.logic_pages[page_index],
                                            new_phyblock, new_page);
                    if (result == -RT_MTD_ESRC)
                    {
                        /* source page error */
                        using_sw_copy = RT_TRUE; /* using software copy */
                        is_bad_block = RT_TRUE;
                        break;
                    }
                }

                if (result == -RT_MTD_ESRC)
                {
                    log_trace(LOG_TRACE_WARNING NFTL_MOD"source page error: %d:%d when multi-writing\n", old_page_mapping.logic_pages[page_index],
                              old_phyblock);
                }
                else if (result != RT_EOK)
                {
                    /* bad block */
                    is_bad_block = RT_TRUE;

                    log_trace(LOG_TRACE_WARNING NFTL_MOD"bad block[%d] when copying\n", new_phyblock);
                    /* destination block is a bad block, FIXME: allocate a new block */
                    nftl_block_set_bad(mapping, new_phyblock);

                    break;
                }

                nftl_page_domapping(&new_page_mapping, (rt_uint8_t)page_index, new_page);
            }
        }

        if (is_bad_block == RT_FALSE)
        {
            /* update page mapping */
            // memcpy(&old_page_mapping, &new_page_mapping, sizeof(struct nftl_page_mapping));

            /* do a block mapping */
            nftl_block_domapping(mapping, block_offset, new_phyblock);

            /* mark old block as not used */
            nftl_block_unmap(mapping, old_phyblock);
            nftl_recent_push(device, old_phyblock);

            break;
        }
        /* else continue to allocate a new block */
    }

    return result;
}
