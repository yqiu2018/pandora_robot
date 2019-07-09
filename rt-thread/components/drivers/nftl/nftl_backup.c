/*
 * File      : nftl_bakcup.c
 * COPYRIGHT (C) 2012-2014, Shanghai Real-Thread Electronic Technology Co.,Ltd
 *
 * Change Logs:
 * Date           Author       Notes
 * 2014-01-21     Bernard      Add COPYRIGHT file header.
 */

#include "nftl_internal.h"

#ifdef NFTL_USING_BACKUP_MAPPING_BLOCK

/* Only for EFM32 */
#include <em_burtc.h>

/*
 * NFTL block number of mapping table backup region
 *
 * There is a region on chip to save the block number of mapping table.
 */

rt_uint32_t nftl_backup_get_block(int index)
{
    return BURTC_RetRegGet(index);
}

void nftl_backup_save_block(int block)
{
    rt_uint32_t index;
    rt_uint32_t prev_block;
    rt_uint32_t checksum;
    rt_uint32_t signature;

    /* set initial checksum value */
    checksum = block;

    for(index = NFTL_BACKUP_MAPPING_BLOCKS - 1; index > 0; index --)
    {
        prev_block = BURTC_RetRegGet(index - 1);
        /* calculate checksum */
        checksum += prev_block;

        /* move to next */
        BURTC_RetRegSet(index, prev_block);
    }
    /* set the first block */
    BURTC_RetRegSet(0, block);

    signature = 0xAEEA0000 | (checksum & 0x0000FFFF);
    /* set signature on backup register */
    BURTC_RetRegSet(NFTL_BACKUP_MAPPING_BLOCKS, signature);
}

rt_bool_t nftl_backup_check_signature(void)
{
    rt_uint32_t signature;
    rt_uint32_t index;
    rt_uint32_t checksum;

    checksum = 0;
    for (index = 0; index < NFTL_BACKUP_MAPPING_BLOCKS; index ++)
    {
        checksum += BURTC_RetRegGet(index);
    }

    if (checksum == 0)
        return RT_FALSE;

    /* get signature on backup register */
    signature = BURTC_RetRegGet(NFTL_BACKUP_MAPPING_BLOCKS);
    if (signature & 0xAEEA0000 == 0xAEEA0000)
    {
        if ((signature & 0x0000FFFF) == (checksum & 0x0000FFFF))
            return RT_TRUE;
    }

    return RT_FALSE;
}

#endif
