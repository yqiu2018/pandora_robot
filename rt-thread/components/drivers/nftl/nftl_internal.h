/*
 * File      : nftl_internal.h
 * COPYRIGHT (C) 2012-2014, Shanghai Real-Thread Electronic Technology Co.,Ltd
 *
 * Change Logs:
 * Date           Author       Notes
 * 2012-10-09     Bernard      Move the internal declaration to this file.
 * 2012-11-01     Bernard      Add page mapping
 * 2012-12-28     Bernard      Use log trace for NFTL log
 */

#ifndef __NFTL_INTERNAL_H__
#define __NFTL_INTERNAL_H__

#include "nftl.h"
#if defined(RTTHREAD_VERSION) && !defined(RT_USING_LOGTRACE)

#define LOG_TRACE_ERROR             "<1>"
#define LOG_TRACE_WARNING           "<3>"
#define LOG_TRACE_INFO              "<5>"
#define LOG_TRACE_VERBOSE           "<7>"
#define LOG_TRACE_DEBUG             "<9>"

#define LOG_TRACE_LEVEL_MASK        0x0f
#define LOG_TRACE_LEVEL_NOTRACE     0x00
#define LOG_TRACE_LEVEL_ERROR       0x01
#define LOG_TRACE_LEVEL_WARNING     0x03
#define LOG_TRACE_LEVEL_INFO        0x05
#define LOG_TRACE_LEVEL_VERBOSE     0x07
#define LOG_TRACE_LEVEL_DEBUG       0x09
#define LOG_TRACE_LEVEL_ALL         0x0f

void log_trace_set_level(int level);
// void log_trace(const char *fmt, ...);
#define log_trace(...)

#else
#include <log_trace.h>
#endif

#define NFTL_MOD					"[NFTL]"

#define NFTL_PAGE_MAGIC				0xA55A
#define NFTL_PAGE_INVALID			0xff

#define NFTL_PAGE_STATUS_EMPTY		0xff
#define NFTL_PAGE_STATUS_USED		0x55

#define NFTL_PAGE_STATUS_MAPPING	0xee
#define NFTL_PAGE_LP_TAG			0xee

#define NFTL_LAYER(device)				((struct nftl_layer*)(device->parent.user_data))
#define NFTL_MAPPING(device)			(&(NFTL_LAYER(device)->mapping))
#define NFTL_MAPPING_BLOCK(device)		((NFTL_LAYER(device)->mapping_table_block))
#define NFTL_PAGE_MAPPING_CACHE(device)	(&(NFTL_LAYER(device)->page_mapping_cache))

#define NFTL_PAGE_BUFFER_GET(device)	(NFTL_LAYER(device)->page_buffer)
#define NFTL_PAGE_BUFFER_PUT(device, buf_ptr)
#define NFTL_OOB_BUFFER_GET(device)		(NFTL_LAYER(device)->oob_buffer)
#define NFTL_OOB_BUFFER_PUT(device,  buf_ptr)

/* the status of block */
struct nftl_block_status
{
    rt_uint32_t erase_times:28;
    rt_uint32_t used:1;
    rt_uint32_t bad:1;
    rt_uint32_t free:1;
    rt_uint32_t recent:1;
};

struct nftl_page_tag
{
    rt_uint16_t magic;
    rt_uint8_t  status;
    rt_uint8_t  logic_page;

    rt_uint32_t sn;
};

/* page mapping */
struct nftl_page_mapping
{
    rt_uint8_t  reserv;
    rt_uint8_t  next_free;
    rt_uint16_t phy_block;		/* physical block */

    rt_uint8_t  logic_pages[NFTL_PAGE_IN_BLOCK_MAX];
};
struct nftl_page_mapping_cache
{
    struct nftl_page_mapping items[NFTL_PM_CACHE_MAX];
};

struct nftl_mapping
{
    rt_uint32_t sn;                    /* sequence number */
    rt_uint32_t crc;                   /* crc */

    rt_uint16_t logic_blocks[NFTL_BLOCKS_MAX];
    struct nftl_block_status phyical_status[NFTL_BLOCKS_MAX];

    rt_uint32_t version;
    rt_uint32_t log_index;
};

struct nftl_layer
{
    rt_uint16_t mapping_table_block;
    rt_uint16_t resv0;
    struct nftl_mapping mapping;

    rt_uint16_t recent_blocks[NFTL_BLOCKS_RECENT_MAX];
    rt_uint16_t recent_index;
    rt_uint16_t resv1;			/* reserved for alignment */

    rt_uint8_t page_buffer[NFTL_PAGE_MAX];
    rt_uint8_t oob_buffer[NFTL_OOB_MAX];

    rt_uint8_t *log_ptr;
    rt_uint8_t padding[36];
};

rt_err_t nftl_layer_init(struct rt_mtd_nand_device* device);
rt_uint8_t* nftl_layer_get_page_buffer(struct rt_mtd_nand_device* device);
void nftl_layer_release_page_buffer(struct rt_mtd_nand_device* device, rt_uint8_t *page);

rt_err_t nftl_mapping_init(struct rt_mtd_nand_device* device);
rt_err_t nftl_mapping_flush(struct rt_mtd_nand_device* device, rt_bool_t protect);

int nftl_page_get_mapping(struct rt_mtd_nand_device* device,
                          rt_uint16_t phy_block, struct nftl_page_mapping *page_mapping);
int nftl_page_get_clean_mapping(struct rt_mtd_nand_device* device,
                                rt_uint16_t phy_block, struct nftl_page_mapping *page_mapping);
rt_uint32_t nftl_crc32(rt_uint8_t *s, rt_uint32_t len);
rt_uint16_t nftl_get_logic_block(struct rt_mtd_nand_device* device, rt_uint16_t phy_block);

void nftl_recent_init(struct rt_mtd_nand_device* device);
void nftl_recent_push(struct rt_mtd_nand_device* device, rt_uint16_t block);
void nftl_recent_flush(struct rt_mtd_nand_device* device);

rt_bool_t nftl_is_empty_page(struct rt_mtd_nand_device* device, rt_uint32_t* ptr);

#ifdef NFTL_USING_BACKUP_MAPPING_BLOCK
/* NFTL backup region routines */
rt_uint32_t nftl_backup_get_block(int index);
void nftl_backup_save_block(int block);
rt_bool_t nftl_backup_check_signature(void);
#endif

void nftl_dump_mapping(const char* nand_device);

/* NFTL log on the flash */
#define NFTL_ACTION_WRITE_PAGE		1
#define NFTL_ACTION_READ_PAGE		2
#define NFTL_ACTION_COPY_BLOCK		3
#define NFTL_ACTION_ALLOC_BLOCK		4
#define NFTL_ACTION_SW_COPY_BLOCK	5
#define NFTL_ACTION_FLUSH_MAPPING	6
#define NFTL_ACTION_MAP_BLOCK		7
#define NFTL_ACTION_UNMAP_BLOCK		8

#define NFTL_ACTION_WEAR_LEVELING	10
#define NFTL_ACTION_ERASE_LBLOCK	11
#define NFTL_ACTION_FLUSH_RECENT	12

#define NFTL_ACTION_SESSION_DONE	128

#define NFTL_ACTION_COPY_FAILED		(128 + 1)
#define NFTL_ACTION_READ_FAILED		(128 + 2)
#define NFTL_ACTION_WRITE_FAILED	(128 + 3)
#define NFTL_ACTION_BIT_INVERT		(128 + 4)
#define NFTL_ACTION_PAGETAG_FAILED	(128 + 5)
#define NFTL_ACTION_PMAPPING_FAILED	(128 + 6)
#define NFTL_ACTION_ESRC			(128 + 7)
#define NFTL_ACTION_COPYPAGE_FAILED (128 + 8)

#define NFTL_LOG_ITEMS	   512

#define NFTL_LOG_UINT32L(value)				(((value) & 0xffff) << 6)
#define NFTL_LOG_UINT32H(value)				((((value) & 0xffff0000) >> 16) << 6)
#define NFTL_LOG_BLOCK(block)				((block) << 6)
#define NFTL_LOG_BLOCK_PAGE(block, page)	((block) << 6 | (page))

void nftl_log_action(struct rt_mtd_nand_device* device, int action, int src, int dst);
void nftl_log_session_done(struct rt_mtd_nand_device* device, int sn);

void nftl_log_init (struct rt_mtd_nand_device* device, int block, int page);
void nftl_log_write(struct rt_mtd_nand_device* device, int block, int page);

/*
return: 0: failed, 1:success.
*/
rt_inline int cpu_check(void)
{
    /* not use CPU checking in source code release */
    return 1;
}

#endif
