/*
 * File      : rt_ota.c
 * COPYRIGHT (C) 2012-2018, Shanghai Real-Thread Technology Co., Ltd
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-01-23     armink       the first version
 */

#include <rt_ota.h>
#include <string.h>
#include <stdlib.h>

#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
#include <tiny_aes.h>
#endif

#ifdef RT_OTA_CMPRS_ALGO_USING_QUICKLZ
#include <quicklz.h>
#endif

#ifdef RT_OTA_CMPRS_ALGO_USING_FASTLZ
#include <fastlz.h>
#endif

#ifdef RT_OTA_CMPRS_ALGO_USING_GZIP
#include <zlib.h>
#endif

/* rbl file decrypt or cmp block size */
#define DECRYPT_DCMPRS_BUFFER_SIZE     4096

/* OTA utils fnv-hash http://create.stephan-brumme.com/fnv-hash/ */
#define RT_OTA_HASH_FNV_SEED           0x811C9DC5

/**
 * OTA '.rbl' file header, also as firmware information header
 */
struct rt_ota_rbl_hdr
{
    char magic[4];

    rt_ota_algo_t algo;
    uint32_t timestamp;
    char name[16];
    char version[24];

    char sn[24];

    /* crc32(aes(zip(rtt.bin))) */
    uint32_t crc32;
    /* hash(rtt.bin) */
    uint32_t hash;

    /* len(rtt.bin) */
    uint32_t size_raw;
    /* len(aes(zip(rtt.bin))) */
    uint32_t size_package;

    /* crc32(rbl_hdr - info_crc32) */
    uint32_t info_crc32;
};
typedef struct rt_ota_rbl_hdr *rt_ota_rbl_hdr_t;

static uint8_t init_ok = 0;
/* partitions total number */
static size_t parts_num = 0;
/* rbl file header length */
static const size_t rbl_hdr_len = sizeof(struct rt_ota_rbl_hdr);

extern uint32_t rt_ota_calc_crc32(uint32_t crc, const void *buf, size_t len);
extern uint32_t rt_ota_calc_hash(uint32_t hash, const void *buf, size_t len);
extern int PKCS7Padding_len(const uint8_t *buf, uint32_t block_size);
extern uint32_t PKCS7Padding(uint8_t *buf, uint32_t buf_len, uint32_t block_size);
/**
 * OTA initialization
 *
 * @return -1: partition table not found
 *         -2: download partition not found
 *        >=0: initialize success
 */
int rt_ota_init(void)
{
    int result = 0;

    if (init_ok)
    {
        return 0;
    }

    parts_num = fal_init();

    if (parts_num <= 0)
    {
        log_e("Initialize failed! Don't found the partition table.");
        result = -1;
        goto __exit;
    }

    if (!fal_partition_find(RT_OTA_DL_PART_NAME))
    {
        log_e("Initialize failed! The download partition(%s) not found.", RT_OTA_DL_PART_NAME);
        result = -2;
        goto __exit;
    }

__exit:

    if (!result){
        log_i("RT-Thread OTA package(V%s) initialize success.", RT_OTA_SW_VERSION);
    } else {
        log_e("RT-Thread OTA package(V%s) initialize failed(%d).", RT_OTA_SW_VERSION, result);
    }

    init_ok = 1;

    return result;
}

/**
 * get firmware header on this partition
 *
 * @param part partition
 * @param hdr firmware header
 *
 * @return -1: get firmware header has an error, >=0: success
 */
static int get_fw_hdr(const struct fal_partition *part, struct rt_ota_rbl_hdr *hdr)
{
    uint32_t crc32 = 0;

    assert(init_ok);
    assert(part);
    assert(hdr);

    if (!strcmp(part->name, RT_OTA_DL_PART_NAME))
    {
        /* firmware header is on OTA download partition top */
        fal_partition_read(part, 0, (uint8_t *) hdr, rbl_hdr_len);
    }
    else
    {
        /* firmware header is on other partition bottom */
        fal_partition_read(part, part->len - rbl_hdr_len, (uint8_t *) hdr, rbl_hdr_len);
    }

    if (hdr->info_crc32 != (crc32 = rt_ota_calc_crc32(0, hdr, rbl_hdr_len - sizeof(uint32_t))))
    {
        log_e("Get firmware header occur CRC32(calc.crc: %08lx != hdr.info_crc32: %08lx) error on '%s' partition!", crc32,
                hdr->info_crc32, part->name);
        return -1;
    }

    return 0;
}

/**
 * get downloaded firmware address in '.rbl' file on OTA download partition
 *
 * @return firmware relative address for OTA download partition
 */
static uint32_t get_save_rbl_body_addr(void)
{
    assert(init_ok);

    return rbl_hdr_len;
}

/**
 * save firmware header on this partition
 *
 * @param part_name partition name
 * @param hdr firmware header
 *
 * @note MUST erase partition before save firmware header
 *
 * @return -1: save failed, >=0: success
 */
static int save_fw_hdr(const char *part_name, struct rt_ota_rbl_hdr *hdr)
{
    const struct fal_partition *part = fal_partition_find(part_name);

    assert(init_ok);
    assert(part_name);
    assert(strcmp(part_name, RT_OTA_DL_PART_NAME));
    assert(hdr);

    if (!part)
    {
        log_e("Save failed. The partition %s was not found.", part_name);
        return -1;
    }

    /* save firmware header on partition bottom */
    return fal_partition_write(part, part->len - rbl_hdr_len, (uint8_t *) hdr, rbl_hdr_len);
}

/**
 * custom verify RAW firmware by user. It's a weak function, the user can be reimplemented it.
 *
 * @param cur_part current partition
 * @param offset RAW firmware offset
 * @param buf firmware buffer
 * @param len buffer length
 *
 * @return >=0: success
 *          <0: failed
 */
RT_OTA_WEAK int rt_ota_custom_verify(const struct fal_partition *cur_part, long offset, const uint8_t *buf, size_t len)
{
    (void)(cur_part);
    (void)(offset);
    (void)(buf);
    (void)(len);

    return 0;
}

/**
 * verify firmware hash code on this partition. It will used custom verify RAW firmware.
 *
 * @param part partition @note this partition is not 'OTA download' partition
 * @param hdr firmware header
 *
 * @return -1: failed, >=0: success
 */
static int part_fw_hash_verify(const struct fal_partition *part, const struct rt_ota_rbl_hdr *hdr)
{
    uint32_t fw_start_addr, fw_end_addr, hash = RT_OTA_HASH_FNV_SEED, i;
    uint8_t buf[32], remain_size;

    assert(strcmp(part->name, RT_OTA_DL_PART_NAME));

    fw_start_addr = 0;
    fw_end_addr = fw_start_addr + hdr->size_raw;
    /* calculate hash */
    for (i = fw_start_addr; i <= fw_end_addr - sizeof(buf); i += sizeof(buf))
    {
        fal_partition_read(part, i, buf, sizeof(buf));
        hash = rt_ota_calc_hash(hash, buf, sizeof(buf));
        /* custom check */
       if (rt_ota_custom_verify(part, i, buf, sizeof(buf)) < 0)
       {
           log_e("Custom verify RAW firmware failed!");
           return -1;
       }
    }
    /* align process */
    if (i != fw_end_addr - sizeof(buf))
    {
        remain_size = fw_end_addr - i;
        fal_partition_read(part, i, buf, remain_size);
        hash = rt_ota_calc_hash(hash, buf, remain_size);
        /* custom check */
       if (rt_ota_custom_verify(part, i, buf, remain_size) < 0)
       {
           log_e("Custom verify RAW firmware failed!");
           return -1;
       }
    }

    if (hash != hdr->hash)
    {
        log_e("Verify firmware hash(calc.hash: %08lx != hdr.hash: %08lx) failed on partition '%s'.", hash, hdr->hash,
                part->name);
        return -1;
    }

    return 0;
}

/**
 * firmware encryption algorithm and compression algorithm support check
 *
 * @param cur_algo current algorithm
 *
 * @return >= 0: pass
 *          < 0: failed
 */
static int fw_algo_support_check(uint32_t cur_algo)
{
    if ((cur_algo & RT_OTA_CMPRS_STAT_MASK) == RT_OTA_CMPRS_ALGO_QUICKLZ)
    {
#ifndef RT_OTA_CMPRS_ALGO_USING_QUICKLZ
        log_e("Not supported QuickLZ compress firmware, please check you configuration!");
        return -1;
#endif
    }
    else if ((cur_algo & RT_OTA_CMPRS_STAT_MASK) == RT_OTA_CMPRS_ALGO_FASTLZ)
    {
#ifndef RT_OTA_CMPRS_ALGO_USING_FASTLZ
        log_e("Not supported FastLZ compress firmware, please check you configuration!");
        return -1;
#endif
    }
    else if ((cur_algo & RT_OTA_CMPRS_STAT_MASK) == RT_OTA_CMPRS_ALGO_GZIP)
    {
#ifndef RT_OTA_CMPRS_ALGO_USING_GZIP
        log_e("Not supported GZip compress firmware, please check you configuration!");
        return -1;
#endif
    }
    else if (cur_algo & RT_OTA_CMPRS_STAT_MASK)
    {
        log_e("Not supported this compression algorithm(%ld)", cur_algo & RT_OTA_CMPRS_STAT_MASK);
        return -1;
    }

    if ((cur_algo & RT_OTA_CRYPT_STAT_MASK) == RT_OTA_CRYPT_ALGO_XOR)
    {
        log_e("Not supported XOR firmware, please check you configuration!");
        return -1;
    }
    else if ((cur_algo & RT_OTA_CRYPT_STAT_MASK) == RT_OTA_CRYPT_ALGO_AES256)
    {
#ifndef RT_OTA_CRYPT_ALGO_USING_AES256
        log_e("Not supported AES256 firmware, please check you configuration!");
        return -1;
#endif
    }
    else if (cur_algo & RT_OTA_CRYPT_STAT_MASK)
    {
        log_e("Not supported this encryption algorithm(%ld)", cur_algo & RT_OTA_CRYPT_STAT_MASK);
        return -1;
    }

    return 0;
}

/**
 * get firmware encryption algorithm and compression algorithm string
 *
 * @param cur_algo current algorithm
 *
 * @return != NULL: algorithm string
 *         == NULL: get failed, maybe not supported
 */
static const char *fw_algo_get_string(uint32_t cur_algo)
{
    if (fw_algo_support_check(cur_algo) < 0)
    {
        /* check failed */
        return NULL;
    }
    switch(cur_algo)
    {
    case RT_OTA_CRYPT_ALGO_NONE:
    {
        return "NONE";
    }
    case RT_OTA_CRYPT_ALGO_AES256:
    {
        return "AES256";
    }
    case RT_OTA_CMPRS_ALGO_QUICKLZ:
    {
        return "QuickLZ";
    }
    case RT_OTA_CMPRS_ALGO_GZIP:
    {
        return "GZip";
    }
    case RT_OTA_CMPRS_ALGO_GZIP | RT_OTA_CRYPT_ALGO_AES256:
    {
        return "GZip and AES256";
    }
    case RT_OTA_CMPRS_ALGO_QUICKLZ | RT_OTA_CRYPT_ALGO_AES256:
    {
        return "QuickLZ and AES256";
    }
    case RT_OTA_CMPRS_ALGO_FASTLZ:
    {
        return "FastLZ";
    }
    case RT_OTA_CMPRS_ALGO_FASTLZ | RT_OTA_CRYPT_ALGO_AES256:
    {
        return "FastLZ and AES256";
    }
    default: 
        return NULL;
    }
}

/**
 * verify firmware on this partition
 *
 * @param part partition
 *
 * @return -1: failed, >=0: success
 */
int rt_ota_part_fw_verify(const struct fal_partition *part)
{
    struct rt_ota_rbl_hdr hdr;
    uint32_t fw_start_addr, fw_end_addr, i, crc32 = 0;
    uint8_t buf[128], remain_size;

    assert(init_ok);
    assert(part);

    if (get_fw_hdr(part, &hdr) < 0)
    {
        return -1;
    }

    if (!strcmp(part->name, RT_OTA_DL_PART_NAME))
    {
        /* on OTA download partition */
        assert(hdr.size_package >= sizeof(buf));

        fw_start_addr = get_save_rbl_body_addr();
        fw_end_addr = fw_start_addr + hdr.size_package;
        /* calculate CRC32 */
        for (i = fw_start_addr; i <= fw_end_addr - sizeof(buf); i += sizeof(buf))
        {
            fal_partition_read(part, i, buf, sizeof(buf));
            crc32 = rt_ota_calc_crc32(crc32, buf, sizeof(buf));
        }
        /* align process */
        if (i != fw_end_addr - sizeof(buf))
        {
            remain_size = fw_end_addr - i;
            fal_partition_read(part, i, buf, remain_size);
            crc32 = rt_ota_calc_crc32(crc32, buf, remain_size);
        }

        if (crc32 != hdr.crc32)
        {
            log_e("Verify firmware CRC32(calc.crc: %08lx != hdr.crc: %08lx) failed on partition '%s'.", crc32, hdr.crc32, part->name);
            return -1;
        }
    }
    else if (part_fw_hash_verify(part, &hdr) < 0)
    {
        return -1;
    }

    log_i("Verify '%s' partition(fw ver: %s, timestamp: %ld) success.", part->name, hdr.version, hdr.timestamp);

    return 0;
}

/**
 * check need to upgrade
 *
 * @note please run `rt_ota_part_fw_verify` before upgrade
 *
 * @return 1: need upgrade, 0: don't need upgrade
 */
int rt_ota_check_upgrade(void)
{
    struct rt_ota_rbl_hdr dl_hdr, dest_hdr;
    const struct fal_partition *part = fal_partition_find(RT_OTA_DL_PART_NAME);

    assert(init_ok);

    if (!part)
    {
        log_e("The download partition %s was not found", RT_OTA_DL_PART_NAME);
        return -1;
    }

    if (get_fw_hdr(part, &dl_hdr) < 0)
    {
        log_e("Get OTA download partition firmware header failed!");
        return 0;
    }
    /* get destination partition */
    part = fal_partition_find(dl_hdr.name);
    if (!part)
    {
        log_e("The partition %s was not found", dl_hdr.name);
        return -1;
    }

    if (get_fw_hdr(part, &dest_hdr) < 0)
    {
        log_e("Get '%s' partition firmware header failed! This partition will be forced to upgrade.", dl_hdr.name);
        return 1;
    }
    /* compare firmware header */
    if (!memcmp(&dl_hdr, &dest_hdr, sizeof(struct rt_ota_rbl_hdr)))
    {
        return 0;
    }

    return 1;
}

static void print_progress(size_t cur_size, size_t total_size)
{
    static unsigned char progress_sign[100 + 1];
    uint8_t i, per = cur_size * 100 / total_size;

    if (per > 100)
    {
        per = 100;
    }

    for (i = 0; i < 100; i++)
    {
        if (i < per)
        {
            progress_sign[i] = '=';
        }
        else if (per == i)
        {
            progress_sign[i] = '>';
        }
        else
        {
            progress_sign[i] = ' ';
        }
    }

    progress_sign[sizeof(progress_sign) - 1] = '\0';

    if (cur_size != 0)
    {
        log_i("\033[2A");
    }
    log_i("OTA Write: [%s] %d%%", progress_sign, per);
}

#ifdef RT_OTA_CMPRS_ALGO_USING_GZIP

static int gzip_copy_fw_from_dl_part(const struct fal_partition *part, struct rt_ota_rbl_hdr *hdr)
{
    const struct fal_partition *dl_part;
    uint8_t *buffer_in = NULL, *buffer_out = NULL, *buffer_cmprs = NULL, verify_only = 1;
    size_t len, i, total_len = 0, decompress_len;
    size_t gzip_read_offset = 0;
    int ret = 0;
    uint32_t hash = RT_OTA_HASH_FNV_SEED;

    z_stream d_stream = {0}; /* decompression stream */
    int err;
    
    assert(part);
    assert(hdr);
    if ((dl_part = fal_partition_find(RT_OTA_DL_PART_NAME)) == NULL)
    {
        log_e("Copy firmware from download partition failed! Partition(%s) not found!", RT_OTA_DL_PART_NAME);
        ret = RT_OTA_CHECK_FAILED;
        goto __exit;
    }

    buffer_in = (uint8_t *)RT_OTA_CALLOC(1, DECRYPT_DCMPRS_BUFFER_SIZE);
    buffer_cmprs = (uint8_t *)RT_OTA_CALLOC(1, DECRYPT_DCMPRS_BUFFER_SIZE);
    buffer_out = (uint8_t *)RT_OTA_CALLOC(1, DECRYPT_DCMPRS_BUFFER_SIZE * 2);
    if (!buffer_in || !buffer_cmprs || !buffer_out)
    {
        log_e("Copy firmware from download partition failed! No memory for buffer_in or buffer_cmprs or buffer_out!");
        ret = RT_OTA_NO_MEM_ERR;
        goto __exit;
    }

#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
    tiny_aes_context ctx;
    uint8_t iv[16 + 1];
    uint8_t private_key[32 + 1];
#endif /* RT_OTA_CRYPT_ALGO_USING_AES256 */

    /* will restart when RAW firmware is verify OK */
__restart:
    total_len = 0;
    d_stream.zalloc = (alloc_func)0;
    d_stream.zfree = (free_func)0;
    err = inflateInit2(&d_stream, MAX_WBITS + 16);
    if(err != Z_OK)
    {
        log_e("inflateInit2 error: %d\n", err);
        ret = -RT_OTA_NO_MEM_ERR;
        goto __exit;
    }
    if (!verify_only)
    {
        /* erase destination partition data */
        rt_ota_erase_fw(part, hdr->size_raw);
    }

#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
    rt_ota_get_iv_key(iv, private_key);
    iv[sizeof(iv) - 1] = '\0';
    private_key[sizeof(private_key) - 1] = '\0';
    tiny_aes_setkey_dec(&ctx, (uint8_t *) private_key, 256);
#endif /* RT_OTA_CRYPT_ALGO_USING_AES256 */

    for (i = 0; i < hdr->size_package; i += DECRYPT_DCMPRS_BUFFER_SIZE)
    {
        if ((hdr->size_package - i) < DECRYPT_DCMPRS_BUFFER_SIZE)
        {
            len = hdr->size_package - i;
        }
        else
        {
            len = DECRYPT_DCMPRS_BUFFER_SIZE;
        }

        /* read the partition data from the header's length address */
        if (fal_partition_read(dl_part, i + sizeof(struct rt_ota_rbl_hdr), buffer_in, len) < 0)
        {
            log_e("Copy firmware from download partition failed! OTA partition(%s) read error!", dl_part->name);

            ret = RT_OTA_PART_READ_ERR;
            break;
        }

        if (hdr->algo & RT_OTA_CRYPT_ALGO_AES256)
        {
#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
            tiny_aes_crypt_cbc(&ctx, AES_DECRYPT, len, iv, buffer_in, buffer_cmprs);
            if((i+len) == hdr->size_package)
            {
                int padlen = PKCS7Padding_len((const uint8_t *)buffer_cmprs + len - 16, 16);
                log_i("last package padlen %d", padlen);
                len -= padlen;
            }
#endif
        }
        else
        {
            memcpy(buffer_cmprs, (const uint8_t *) buffer_in, len);
        }
        
        d_stream.next_in  = buffer_cmprs;
        d_stream.avail_in = len;
        
        while(((i + DECRYPT_DCMPRS_BUFFER_SIZE) > hdr->size_package) ? 1 : d_stream.avail_in)
        {
            
            d_stream.next_out = buffer_out + gzip_read_offset;
            d_stream.avail_out = DECRYPT_DCMPRS_BUFFER_SIZE * 2 - gzip_read_offset;

            int res = inflate(&d_stream, Z_NO_FLUSH);
            
            //if(d_stream.avail_out < ((DECRYPT_DCMPRS_BUFFER_SIZE * 2) - gzip_read_offset))
            {
                decompress_len = ((DECRYPT_DCMPRS_BUFFER_SIZE * 2) - gzip_read_offset) - d_stream.avail_out;
                
                gzip_read_offset += decompress_len;

                while(gzip_read_offset >= DECRYPT_DCMPRS_BUFFER_SIZE)
                {
                    if (verify_only)
                    {
                        /* custom check */
                        if (rt_ota_custom_verify(dl_part, total_len, buffer_out, DECRYPT_DCMPRS_BUFFER_SIZE) < 0)
                        {
                            log_e("Custom verify RAW firmware failed!");
                            ret = RT_OTA_FW_VERIFY_FAILED;
                            goto __exit;
                        }
                        hash = rt_ota_calc_hash(hash, buffer_out, DECRYPT_DCMPRS_BUFFER_SIZE);
                    }
                    else
                    {
                        if (fal_partition_write(part, total_len, buffer_out, DECRYPT_DCMPRS_BUFFER_SIZE) < 0)
                        {
                            log_e("Copy firmware from download partition failed! OTA partition (%s) write error!", part->name);
                            ret = RT_OTA_PART_WRITE_ERR;
                            break;
                        }
                        print_progress(i, hdr->size_package);
                    }

                    total_len += DECRYPT_DCMPRS_BUFFER_SIZE;
                    gzip_read_offset -= DECRYPT_DCMPRS_BUFFER_SIZE;

                    if(gzip_read_offset)
                    {
                        memcpy(buffer_out, buffer_out + DECRYPT_DCMPRS_BUFFER_SIZE, gzip_read_offset);
                    }
                }
                if((i + DECRYPT_DCMPRS_BUFFER_SIZE) > hdr->size_package)
                {
                    if (verify_only)
                    {
                        /* custom check */
                        if (rt_ota_custom_verify(dl_part, total_len, buffer_out, gzip_read_offset) < 0)
                        {
                            log_e("Custom verify RAW firmware failed!");
                            ret = RT_OTA_FW_VERIFY_FAILED;
                            goto __exit;
                        }
                        hash = rt_ota_calc_hash(hash, buffer_out, gzip_read_offset);
                    }
                    else
                    {
                        if (fal_partition_write(part, total_len, buffer_out, gzip_read_offset) < 0)
                        {
                            log_e("Copy firmware from download partition failed! OTA partition (%s) write error!", part->name);
                            ret = RT_OTA_PART_WRITE_ERR;
                            break;
                        }
                        print_progress(i, hdr->size_package);
                    }

                    total_len += gzip_read_offset;
                    gzip_read_offset = 0;
                }
                
            }
            if(res == Z_STREAM_END)
            {
                log_i("Z_STREAM_END");
                goto _Z_STREAM_END;
            }
            else if(res != Z_OK)
            {
                log_e("decomprs error %s", d_stream.msg);
                ret = RT_OTA_COPY_FAILED;
                goto _Z_STREAM_END;
            }
        }
    }
    
_Z_STREAM_END:
    log_i("total_len: %d", total_len);
    err = inflateEnd(&d_stream);
    if(err != Z_OK)
    {
        log_e("decomprs error %s", d_stream.msg);
        ret = RT_OTA_COPY_FAILED;
        goto __exit;
    }
    
    if (verify_only)
    {
        /* hash check */
        if (hash != hdr->hash)
        {
            log_e("Verify downloaded firmware hash (calc.hash: %08lx != hdr.hash: %08lx) failed.", hash, hdr->hash);
            ret = RT_OTA_FW_VERIFY_FAILED;
            goto __exit;
        }
        else
        {
            log_d("Verify downloaded firmware hash success!");
        }
        verify_only = 0;
        goto __restart;
    }
    else
    {
        print_progress(i, hdr->size_package);
    }

__exit:
    if (buffer_in)
    {
        RT_OTA_FREE(buffer_in);
    }
    
    if (buffer_cmprs)
    {
        RT_OTA_FREE(buffer_cmprs);
    }

    if (buffer_out)
    {
        RT_OTA_FREE(buffer_out);
    }

#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
    if (ret < 0)
    {
        log_d("AES KEY:%s IV:%s", private_key, iv);
    }
#endif

    return ret;
}


#endif /* RT_OTA_CMPRS_ALGO_USING_GZIP */

/**
 * copy download data to the specified partition
 * @note it will verify RAW firmware on download partition first.
 *
 * @param part partition
 * @param hdr firmware header
 *
 * @return @see rt_ota_err_t
 */
static int copy_fw_from_dl_part(const struct fal_partition *part, struct rt_ota_rbl_hdr *hdr)
{
    const struct fal_partition *dl_part;
    uint8_t *buffer_in = NULL, *buffer_out = NULL, verify_only = 1;
    size_t len, i, total_len = 0;
    int ret = 0;
    uint32_t hash = RT_OTA_HASH_FNV_SEED;

    assert(part);
    assert(hdr);

    if ((dl_part = fal_partition_find(RT_OTA_DL_PART_NAME)) == NULL)
    {
        log_e("Copy firmware from download partition failed! Partition(%s) not found!", RT_OTA_DL_PART_NAME);
        ret = RT_OTA_CHECK_FAILED;
        goto __exit;
    }

    buffer_in = (uint8_t *) RT_OTA_CALLOC(1, DECRYPT_DCMPRS_BUFFER_SIZE);
    buffer_out = (uint8_t *) RT_OTA_CALLOC(1, DECRYPT_DCMPRS_BUFFER_SIZE);
    if (!buffer_in || !buffer_out)
    {
        log_e("Copy firmware from download partition failed! No memory for buffer_in or buffer_out!");
        ret = RT_OTA_NO_MEM_ERR;
        goto __exit;
    }

#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
    tiny_aes_context ctx;
    uint8_t iv[16 + 1];
    uint8_t private_key[32 + 1];
#endif /* RT_OTA_CRYPT_ALGO_USING_AES256 */

    /* will restart when RAW firmware is verify OK */
__restart:

    if (!verify_only)
    {
        /* erase destination partition data */
        rt_ota_erase_fw(part, hdr->size_raw);
    }

#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
    rt_ota_get_iv_key(iv, private_key);
    iv[sizeof(iv) - 1] = '\0';
    private_key[sizeof(private_key) - 1] = '\0';
    tiny_aes_setkey_dec(&ctx, (uint8_t *) private_key, 256);
#endif /* RT_OTA_CRYPT_ALGO_USING_AES256 */

    for (i = 0; i < hdr->size_package; i += DECRYPT_DCMPRS_BUFFER_SIZE)
    {
        if ((hdr->size_package - i) < DECRYPT_DCMPRS_BUFFER_SIZE)
        {
            len = hdr->size_package - i;
        }
        else
        {
            len = DECRYPT_DCMPRS_BUFFER_SIZE;
        }

        /* read the partition data from the header's length address */
        if (fal_partition_read(dl_part, i + sizeof(struct rt_ota_rbl_hdr), buffer_in, len) < 0)
        {
            log_e("Copy firmware from download partition failed! OTA partition(%s) read error!", dl_part->name);

            ret = RT_OTA_PART_READ_ERR;
            break;
        }

        if (hdr->algo & RT_OTA_CRYPT_ALGO_AES256)
        {
#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
            tiny_aes_crypt_cbc(&ctx, AES_DECRYPT, len, iv, buffer_in, buffer_out);
#endif
        }
        else
        {
            memcpy(buffer_out, (const uint8_t *) buffer_in, len);
        }

        if (verify_only)
        {
            /* custom check */
           if (rt_ota_custom_verify(dl_part, total_len, buffer_out, len) < 0)
           {
               log_e("Custom verify RAW firmware failed!");
               ret = RT_OTA_FW_VERIFY_FAILED;
               goto __exit;
           }

           total_len += len;

           if (hdr->size_raw < total_len)
           {
               len = len - (total_len - hdr->size_raw);
           }
           hash = rt_ota_calc_hash(hash, buffer_out, len);
        }
        else
        {
            if (fal_partition_write(part, i, buffer_out, len) < 0)
            {
                log_e("Copy firmware from download partition failed! OTA partition (%s) write error!", part->name);
                ret = RT_OTA_PART_WRITE_ERR;
                break;
            }
            print_progress(i, hdr->size_package);
        }
    }

    if (verify_only)
    {
        /* hash check */
        if (hash != hdr->hash)
        {
            log_e("Verify downloaded firmware hash (calc.hash: %08lx != hdr.hash: %08lx) failed.", hash, hdr->hash);
            ret = RT_OTA_FW_VERIFY_FAILED;
            goto __exit;
        }
        else
        {
            log_d("Verify downloaded firmware hash success!");
        }
        verify_only = 0;
        goto __restart;
    }
    else
    {
        print_progress(i, hdr->size_package);
    }

__exit:
    if (buffer_in)
    {
        RT_OTA_FREE(buffer_in);
    }

    if (buffer_out)
    {
        RT_OTA_FREE(buffer_out);
    }

#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
    if (ret < 0)
    {
        log_d("AES KEY:%s IV:%s", private_key, iv);
    }
#endif

    return ret;
}

#define CRYPT_ALIGN_SIZE               16                  /* AES decryption block size requires 16-byte alignment */
#define CMPRS_HEADER_SIZE              4                   /* The header before cmpress block is used to store the block size that needs to be decompressed */

#ifdef RT_OTA_CMPRS_ALGO_USING_QUICKLZ
#define QLZ_CMPRS_PADDING_SIZE         QLZ_BUFFER_PADDING  /* Padding is used to provide a buffer for decompressing data */
#define QLZ_CMPRS_BUFFER_SIZE          (CMPRS_HEADER_SIZE + DECRYPT_DCMPRS_BUFFER_SIZE + QLZ_CMPRS_PADDING_SIZE + CRYPT_ALIGN_SIZE)
#endif

#ifdef RT_OTA_CMPRS_ALGO_USING_FASTLZ
#define FLZ_CMPRS_PADDING_SIZE         (FASTLZ_BUFFER_PADDING(DECRYPT_DCMPRS_BUFFER_SIZE))  /* Padding is used to provide a buffer for decompressing data */
#define FLZ_CMPRS_BUFFER_SIZE          (CMPRS_HEADER_SIZE + DECRYPT_DCMPRS_BUFFER_SIZE + FLZ_CMPRS_PADDING_SIZE + CRYPT_ALIGN_SIZE)
#endif

/**
 * Decrypt & decompress & copy download data to the specified partition
 *
 * @param part partition @note it will verify RAW firmware only when part is download partition
 * @param hdr firmware header
 *
 * @return @see rt_ota_err_t
 */
static int decrypt_dcmprs_fw_from_dl_part(const struct fal_partition *part, struct rt_ota_rbl_hdr *hdr)
{
    uint8_t *buffer_in = NULL, *buffer_out = NULL, *buffer_cmprs = NULL, verify_only = 1;
    const struct fal_partition *dl_part;
    int ret = 0;
    uint32_t hash = RT_OTA_HASH_FNV_SEED;

#ifdef RT_OTA_CMPRS_ALGO_USING_QUICKLZ
    qlz_state_decompress *dcmprs_state = NULL;
#endif
    
    /* compression block header get the size of block */
    uint8_t cmprs_block_header[4] = { 0 };

    /* the size of the decrypt block in every cycle*/
    size_t decrypt_block_size = 0;

    /* The size of the data that has been decompress, Compared with the "hdr->size_package" to judge the cycle execute or not*/
    size_t already_dcmprs_size = 0;

    /* crmps_buffer_size : the size of the currently available compressed data that read from the flash, length range 4096 - 4109.
     * crmps_buffer_remain_size : remained the length of the compressed data after the current decompression .
     * */
    size_t crmps_buffer_size = 0, crmps_buffer_remain_size = 0;

    size_t crmps_block_size = 0, dcrmps_block_size = 0;
    size_t flash_already_read_size = 0, flash_already_write_size = 0;

    assert(part);
    assert(hdr);

    if ((dl_part = fal_partition_find(RT_OTA_DL_PART_NAME)) == NULL)
    {
        log_e("decrypt & decompress firmware from download partition failed! Partition(%s) not found!", RT_OTA_DL_PART_NAME);
        ret = RT_OTA_CHECK_FAILED;
        goto __exit;
    }

    if ((hdr->algo & RT_OTA_CMPRS_STAT_MASK) == RT_OTA_CMPRS_ALGO_QUICKLZ)
    {
#ifdef RT_OTA_CMPRS_ALGO_USING_QUICKLZ

        buffer_in = (uint8_t *) RT_OTA_CALLOC(1, QLZ_CMPRS_BUFFER_SIZE);
        buffer_out = (uint8_t *) RT_OTA_CALLOC(1, QLZ_CMPRS_BUFFER_SIZE);
        buffer_cmprs =  (uint8_t *) RT_OTA_CALLOC(1, QLZ_CMPRS_BUFFER_SIZE);
        if (!buffer_in || !buffer_out || !buffer_cmprs)
        {
            log_e("decrypt & decompress firmware from download partition failed! No memory for quicklz buffer_in or buffer_out!");
            ret = RT_OTA_NO_MEM_ERR;
            goto __exit;
        }

        dcmprs_state = (qlz_state_decompress *) RT_OTA_CALLOC(1, sizeof(qlz_state_decompress));
        if (!dcmprs_state)
        {
            log_e("decrypt & decompress firmware from download partition failed! No memory for qlz_state_decompress struct !");
            ret = RT_OTA_NO_MEM_ERR;
            goto __exit;
        }
#endif
    }
    else if((hdr->algo & RT_OTA_CMPRS_STAT_MASK) == RT_OTA_CMPRS_ALGO_FASTLZ)
    {
#ifdef RT_OTA_CMPRS_ALGO_USING_FASTLZ
        buffer_in = (uint8_t *) RT_OTA_CALLOC(1, FLZ_CMPRS_BUFFER_SIZE);
        buffer_out = (uint8_t *) RT_OTA_CALLOC(1, FLZ_CMPRS_BUFFER_SIZE);
        buffer_cmprs =  (uint8_t *) RT_OTA_CALLOC(1, FLZ_CMPRS_BUFFER_SIZE);
        if (!buffer_in || !buffer_out || !buffer_cmprs)
        {
            log_e("decrypt & decompress firmware from download partition failed! No memory for fastlz buffer_in or buffer_out!");
            ret = RT_OTA_NO_MEM_ERR;
            goto __exit;
        }
#endif
    }

#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
    tiny_aes_context ctx;
    uint8_t iv[16 + 1];
    uint8_t private_key[32 + 1];
#endif /* RT_OTA_CRYPT_ALGO_USING_AES256 */

    /* will restart when RAW firmware is verify OK */
__restart:

    if (!verify_only)
    {
        /* erase destination partition data */
        rt_ota_erase_fw(part, hdr->size_raw);
    }

#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
    rt_ota_get_iv_key(iv, private_key);
    iv[sizeof(iv) - 1] = '\0';
    private_key[sizeof(private_key) - 1] = '\0';
    tiny_aes_setkey_dec(&ctx, (uint8_t *) private_key, 256);
#endif /* RT_OTA_CRYPT_ALGO_USING_AES256 */

    decrypt_block_size= 0;
    already_dcmprs_size = 0;
    crmps_buffer_size = 0;
    crmps_buffer_remain_size = 0;
    crmps_block_size = 0;
    dcrmps_block_size = 0;
    flash_already_read_size = 0;
    flash_already_write_size = 0;

    do
    {
        if (already_dcmprs_size == 0)
        {
            if ((hdr->algo & RT_OTA_CMPRS_STAT_MASK) == RT_OTA_CMPRS_ALGO_QUICKLZ)
            {
#ifdef RT_OTA_CMPRS_ALGO_USING_QUICKLZ
                crmps_buffer_size = QLZ_CMPRS_BUFFER_SIZE - (QLZ_CMPRS_BUFFER_SIZE % CRYPT_ALIGN_SIZE);
#endif
            }
            else if ((hdr->algo & RT_OTA_CMPRS_STAT_MASK) == RT_OTA_CMPRS_ALGO_FASTLZ)
            {
#ifdef RT_OTA_CMPRS_ALGO_USING_FASTLZ
                crmps_buffer_size = FLZ_CMPRS_BUFFER_SIZE - (FLZ_CMPRS_BUFFER_SIZE % CRYPT_ALIGN_SIZE);
#endif
            }
            decrypt_block_size = crmps_buffer_size;
        }
        else
        {
            /* The size of the currently need to decrypt that is the last compress size of 16 bytes aligned(AES256) */
            if (hdr->algo == (RT_OTA_CRYPT_ALGO_AES256 | RT_OTA_CMPRS_ALGO_QUICKLZ))
            {
#ifdef RT_OTA_CMPRS_ALGO_USING_QUICKLZ
                decrypt_block_size = QLZ_CMPRS_BUFFER_SIZE - crmps_buffer_remain_size
                        - (QLZ_CMPRS_BUFFER_SIZE - crmps_buffer_remain_size) % CRYPT_ALIGN_SIZE;
#endif
            }
            else if(hdr->algo == (RT_OTA_CRYPT_ALGO_AES256 | RT_OTA_CMPRS_ALGO_FASTLZ))
            {
#ifdef RT_OTA_CMPRS_ALGO_USING_FASTLZ
                decrypt_block_size = FLZ_CMPRS_BUFFER_SIZE - crmps_buffer_remain_size
                        - (FLZ_CMPRS_BUFFER_SIZE - crmps_buffer_remain_size) % CRYPT_ALIGN_SIZE;
#endif
            }
            else
            {
                decrypt_block_size = crmps_block_size + CMPRS_HEADER_SIZE;
            }

            crmps_buffer_size = crmps_buffer_remain_size + decrypt_block_size;
        }

        if ((hdr->size_package - flash_already_read_size) <= decrypt_block_size)
        {
            decrypt_block_size = hdr->size_package - flash_already_read_size;
        }

        if (decrypt_block_size > 0)
        {
            /* read the partition data from the header's length address */
            if (fal_partition_read(dl_part, sizeof(struct rt_ota_rbl_hdr) + flash_already_read_size , buffer_in,
                    decrypt_block_size) < 0)
            {
                log_e("Decrypt & decompress firmware from download partition failed! OTA partition(%s) read error!", dl_part->name);
                ret = RT_OTA_PART_READ_ERR;
                break;
            }
            flash_already_read_size += decrypt_block_size;

            if ((hdr->algo & RT_OTA_CRYPT_STAT_MASK) == RT_OTA_CRYPT_ALGO_AES256)
            {
#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
                tiny_aes_crypt_cbc(&ctx, AES_DECRYPT, decrypt_block_size, iv, buffer_in, buffer_cmprs + crmps_buffer_remain_size);
#endif
            }
            else
            {
                memcpy(buffer_cmprs + crmps_buffer_remain_size, buffer_in, decrypt_block_size);
            }
        }

        /* read the padding data , end decompression and decryption process */
        memcpy(cmprs_block_header, buffer_cmprs, CMPRS_HEADER_SIZE);
        if (cmprs_block_header[0] != 0)
        {
            if (!verify_only)
            {
                print_progress(hdr->size_package, hdr->size_package);
                log_d("Decrypt & decompress firmware from download partition success!");
            }
            break;
        }

        /* get the compression block size by the compression block header(4 byte) */
        crmps_block_size = cmprs_block_header[0] * (1 << 24) + cmprs_block_header[1] * (1 << 16)
                + cmprs_block_header[2] * (1 << 8) + cmprs_block_header[3];
        assert(crmps_block_size <= crmps_buffer_size);

        /* get the decompression block size */
        if ((hdr->algo & RT_OTA_CMPRS_STAT_MASK) == RT_OTA_CMPRS_ALGO_QUICKLZ)
        {
#ifdef RT_OTA_CMPRS_ALGO_USING_QUICKLZ
            memset(buffer_in, 0x00, QLZ_CMPRS_BUFFER_SIZE);
            memcpy(buffer_in, buffer_cmprs + CMPRS_HEADER_SIZE, crmps_block_size);

            dcrmps_block_size = qlz_decompress((const char *) buffer_in, buffer_out, dcmprs_state);
#endif
        }
        else if ((hdr->algo & RT_OTA_CMPRS_STAT_MASK) == RT_OTA_CMPRS_ALGO_FASTLZ)
        {
#ifdef RT_OTA_CMPRS_ALGO_USING_FASTLZ
            memset(buffer_in, 0x00, FLZ_CMPRS_BUFFER_SIZE);
            memcpy(buffer_in, buffer_cmprs + CMPRS_HEADER_SIZE, crmps_block_size);

            dcrmps_block_size = fastlz_decompress((void *) buffer_in, crmps_block_size, buffer_out,
                    FLZ_CMPRS_PADDING_SIZE + DECRYPT_DCMPRS_BUFFER_SIZE);
#endif
        }

        if (verify_only)
        {
            /* custom check */
            if (rt_ota_custom_verify(dl_part, flash_already_write_size, buffer_out, dcrmps_block_size) < 0)
            {
                log_e("Custom verify RAW firmware failed!");
                ret = RT_OTA_FW_VERIFY_FAILED;
                goto __exit;
            }
            /* hash check */
            hash = rt_ota_calc_hash(hash, buffer_out, dcrmps_block_size);
        }
        else
        {
            if (fal_partition_write(part, flash_already_write_size, buffer_out, dcrmps_block_size) < 0)
            {
                log_e("Decrypt & decompress firmware from download partition failed! OTA partition(%s) write error!",
                        part->name);
                ret = RT_OTA_PART_WRITE_ERR;
                break;
            }
            print_progress(already_dcmprs_size, hdr->size_package);
        }
        flash_already_write_size += dcrmps_block_size;

        /* copy the remain compression buffer size to the buffer header */
        crmps_buffer_remain_size = crmps_buffer_size - crmps_block_size - CMPRS_HEADER_SIZE;

        if((hdr->algo & RT_OTA_CMPRS_STAT_MASK) == RT_OTA_CMPRS_ALGO_QUICKLZ)
        {
#ifdef RT_OTA_CMPRS_ALGO_USING_QUICKLZ
            memset(buffer_in, 0x00, QLZ_CMPRS_BUFFER_SIZE);
            memcpy(buffer_in, buffer_cmprs + crmps_block_size + CMPRS_HEADER_SIZE, crmps_buffer_remain_size);
            memset(buffer_cmprs, 0x00, QLZ_CMPRS_BUFFER_SIZE);
            memcpy(buffer_cmprs, buffer_in, crmps_buffer_remain_size);
#endif
        }
        else if((hdr->algo & RT_OTA_CMPRS_STAT_MASK) == RT_OTA_CMPRS_ALGO_FASTLZ)
        {
#ifdef RT_OTA_CMPRS_ALGO_USING_FASTLZ
            memset(buffer_in, 0x00, FLZ_CMPRS_BUFFER_SIZE);
            memcpy(buffer_in, buffer_cmprs + crmps_block_size + CMPRS_HEADER_SIZE, crmps_buffer_remain_size);
            memset(buffer_cmprs, 0x00, FLZ_CMPRS_BUFFER_SIZE);
            memcpy(buffer_cmprs, buffer_in, crmps_buffer_remain_size);
#endif
        }

        already_dcmprs_size += crmps_block_size + CMPRS_HEADER_SIZE;
    } while (already_dcmprs_size < hdr->size_package);

    if (verify_only)
    {
        /* hash check */
        if (hash != hdr->hash)
        {
            log_e("Verify downloaded firmware hash (calc.hash: %08lx != hdr.hash: %08lx) failed.", hash, hdr->hash);
            ret = RT_OTA_FW_VERIFY_FAILED;
            goto __exit;
        }
        else
        {
            log_d("Verify downloaded firmware hash success!");
        }
        verify_only = 0;
        goto __restart;
    }
    else if (already_dcmprs_size == hdr->size_package)
    {
        print_progress(already_dcmprs_size, hdr->size_package);
        log_d("Decrypt & decompress firmware from download partition success!");
    }

__exit:
    if (buffer_in)
    {
        RT_OTA_FREE(buffer_in);
        buffer_in = NULL;
    }

    if (buffer_out)
    {
        RT_OTA_FREE(buffer_out);
        buffer_out = NULL;
    }

    if (buffer_cmprs)
    {
        RT_OTA_FREE(buffer_cmprs);
        buffer_cmprs = NULL;
    }

#ifdef RT_OTA_CRYPT_ALGO_USING_AES256
    if (ret < 0)
    {
        rt_ota_get_iv_key(iv, private_key);
        iv[sizeof(iv) - 1] = '\0';
        private_key[sizeof(private_key) - 1] = '\0';        

        log_d("AES KEY:%s IV:%s", private_key, iv);
    }
#endif

    return ret;
}

/**
 * erase the partition for new firmware write
 *
 * @note it will be called by rt_ota_upgrade() or before application download starting
 *
 * @param part erase partition object
 * @param new_fw_size the new firmware size
 *
 * @return result
 */
int rt_ota_erase_fw(const struct fal_partition *part, size_t new_fw_size)
{
    const struct fal_flash_dev *flash = fal_flash_device_find(part->flash_name);
    int result;

    assert(part);
    assert(flash);

    log_i("The partition '%s' is erasing.", part->name);

    /* erase destination partition data for new firmware */
    result = fal_partition_erase(part, 0, new_fw_size);

    if (result >= 0 && new_fw_size <= part->len - flash->blk_size)
    {
        /* erase the last block for firmware header */
        result = fal_partition_erase(part, part->len - flash->blk_size, flash->blk_size);
    }

    if (result >= 0)
    {
        log_i("The partition '%s' erase success.", part->name);
    }
    else
    {
        log_e("The partition '%s' erase failed.", part->name);
    }

    return result;
}

/**
 * get firmware version on this partition
 * @note this function is not supported reentrant
 *
 * @param part partition
 *
 * @return != NULL: version name
 *         == NULL: get failed
 */
const char *rt_ota_get_fw_version(const struct fal_partition *part)
{
    static struct rt_ota_rbl_hdr hdr;

    if (get_fw_hdr(part, &hdr) >= 0)
    {
        return hdr.version;
    }

    return NULL;
}

/**
 * get firmware timestamp on this partition
 *
 * @param part partition
 *
 * @return != 0: firmware timestamp
 *         == 0: get failed
 */
uint32_t rt_ota_get_fw_timestamp(const struct fal_partition *part)
{
    struct rt_ota_rbl_hdr hdr;

    if (get_fw_hdr(part, &hdr) >= 0)
    {
        return hdr.timestamp;
    }

    return 0;
}

/**
 * get firmware size on this partition
 * @note This firmware size is after compression or encryption.
 *       If you want to get RAW firmware size, please using rt_ota_get_raw_fw_size().
 *
 * @param part partition
 *
 * @return != 0: firmware size
 *         == 0: get failed
 */
uint32_t rt_ota_get_fw_size(const struct fal_partition *part)
{
    struct rt_ota_rbl_hdr hdr;

    if (get_fw_hdr(part, &hdr) >= 0)
    {
        return hdr.size_package;
    }

    return 0;
}

/**
 * get RAW firmware size on this partition
 *
 * @param part partition
 *
 * @return != 0: RAW firmware size
 *         == 0: get failed
 */
uint32_t rt_ota_get_raw_fw_size(const struct fal_partition *part)
{
    struct rt_ota_rbl_hdr hdr;

    if (get_fw_hdr(part, &hdr) >= 0)
    {
        return hdr.size_raw;
    }

    return 0;
}

/**
 * get firmware upgraded destination partition name on this partition
 * @note this function is not supported reentrant
 *
 * @param part partition
 *
 * @return != NULL: destination partition name
 *         == NULL: get failed
 */
const char *rt_ota_get_fw_dest_part_name(const struct fal_partition *part)
{
    static struct rt_ota_rbl_hdr hdr;

    if (get_fw_hdr(part, &hdr) >= 0)
    {
        return hdr.name;
    }

    return NULL;
}

/**
 * get firmware firmware encryption algorithm and compression algorithm on this partition
 *
 * @param part partition
 *
 * @return != 0: algorithm
 *         == 0: no algorithm or get failed
 */
rt_ota_algo_t rt_ota_get_fw_algo(const struct fal_partition *part)
{
    static struct rt_ota_rbl_hdr hdr;

    if (get_fw_hdr(part, &hdr) >= 0)
    {
        return hdr.algo;
    }

    return RT_OTA_CRYPT_ALGO_NONE;
}

/**
 * upgrade firmware from OTA download partition
 *
 * 1. decrypt, dcmprs, copy firmware to destination partition
 * 2. hash verify on destination partition
 *
 * @note please run `rt_ota_check_upgrade` before upgrade
 *
 * @return @see rt_ota_err_t
 */
int rt_ota_upgrade(void)
{
    struct rt_ota_rbl_hdr dl_hdr, old_hdr;
    const struct fal_partition *dl_part = NULL, *dest_part = NULL;
    int result = RT_OTA_NO_ERR;
    const char *algo_string = NULL;


    assert(init_ok);
    dl_part = fal_partition_find(RT_OTA_DL_PART_NAME);
    if (!dl_part)
    {
        log_e("The download partition %s was not found", RT_OTA_DL_PART_NAME);
        return -1;
    }

    if (get_fw_hdr(dl_part, &dl_hdr) < 0)
    {
        log_e("Get OTA download partition firmware header failed!");
        return RT_OTA_CHECK_FAILED;
    }

    if ((dest_part = fal_partition_find(dl_hdr.name)) == NULL)
    {
        log_e("The partition(%s) was not found!", dl_hdr.name);
        return RT_OTA_CHECK_FAILED;
    }

    if (dl_hdr.size_raw + sizeof(struct rt_ota_rbl_hdr) > dest_part->len)
    {
        log_e("The partition (%s) has no space (unless %d) for upgrade.", dest_part->flash_name,
                dl_hdr.size_raw + sizeof(struct rt_ota_rbl_hdr));
        return RT_OTA_CHECK_FAILED;
    }

    if (get_fw_hdr(dest_part, &old_hdr) >= 0)
    {
        log_i("OTA firmware(%s) upgrade(%s->%s) startup.", dl_hdr.name, old_hdr.version, dl_hdr.version);
        log_d("The original firmware version: %s, timestamp: %ld", old_hdr.version, old_hdr.timestamp);
    }
    else
    {
        log_i("OTA firmware(%s) upgrade startup.", dl_hdr.name);
    }

    /* get firmware encryption algorithm and compression algorithm string.
     * It will get failed when algorithm is not supported */
    if ((algo_string = fw_algo_get_string(dl_hdr.algo)) == NULL)
    {
        return RT_OTA_ALGO_NOT_SUPPORTED;
    }
    
    algo_string = algo_string;   /* make compiler happy */

    log_d("The      new firmware version: %s, timestamp: %ld, algo: %s", dl_hdr.version, dl_hdr.timestamp, algo_string);

    /* write firmware to destination partition by different algorithm */
    switch((uint16_t)dl_hdr.algo)
    {
    case (uint16_t)RT_OTA_CRYPT_ALGO_NONE:
    case (uint16_t)RT_OTA_CRYPT_ALGO_AES256:
        
        result = copy_fw_from_dl_part(dest_part, &dl_hdr);
        
        break;
#ifdef RT_OTA_CMPRS_ALGO_USING_GZIP
    case (uint16_t)RT_OTA_CMPRS_ALGO_GZIP:
    case (uint16_t)RT_OTA_CMPRS_ALGO_GZIP | (uint16_t)RT_OTA_CRYPT_ALGO_AES256:
        
        result = gzip_copy_fw_from_dl_part(dest_part, &dl_hdr);
    
        break;
#endif /* RT_OTA_CMPRS_ALGO_USING_GZIP */
    case (uint16_t)RT_OTA_CMPRS_ALGO_QUICKLZ:
    case (uint16_t)RT_OTA_CMPRS_ALGO_QUICKLZ | (uint16_t)RT_OTA_CRYPT_ALGO_AES256:
    case (uint16_t)RT_OTA_CMPRS_ALGO_FASTLZ:
    case (uint16_t)RT_OTA_CMPRS_ALGO_FASTLZ | (uint16_t)RT_OTA_CRYPT_ALGO_AES256:
        
        result = decrypt_dcmprs_fw_from_dl_part(dest_part, &dl_hdr);
    
        break;
    default:
        
        log_e("OTA upgrade failed! Download header algo(0x%x) is not supported!", dl_hdr.algo);
        result = RT_OTA_ALGO_NOT_SUPPORTED;
    
        break;
    }

    if (result < 0)
    {
        log_e("OTA upgrade failed! Download data copy to partition(%s) error!", dest_part->name);
        return result;
    }

    /* verify destination partition firmware hash code */
    if (part_fw_hash_verify(dest_part, &dl_hdr) < 0)
    {
        return RT_OTA_FW_VERIFY_FAILED;
    }

    return save_fw_hdr(dl_hdr.name, &dl_hdr);
}
