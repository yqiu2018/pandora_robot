/*
 * File      : ota.c
 * COPYRIGHT (C) 2012-2018, Shanghai Real-Thread Technology Co., Ltd
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-01-27     MurphyZhao   the first version
 */

#include <fal.h>
#include <rt_ota.h>
#include <string.h>

#define log_ota(...)                     printf("\033[36;22m");printf(__VA_ARGS__);printf("\033[0m\r\n")

#define BOOT_OTA_DEBUG (1) // 1: open debug; 0: close debug

int ota_main(void)
{
    int result = 0;
    size_t i, part_table_size;
    const struct fal_partition *dl_part = NULL;
    const struct fal_partition *part_table = NULL;

    if (rt_ota_init() >= 0)
    {
        /* verify bootloader partition 
         * 1. Check if the BL partition exists
         * 2. CRC BL FW HDR
         * 3. HASH BL FW
         * */
        if (rt_ota_part_fw_verify(fal_partition_find(RT_BK_BL_PART_NAME)) < 0)
        {
            //TODO upgrade bootloader to safe image
            // firmware HDR crc failed or hash failed. if boot verify failed, may not jump to app running
            #if !BOOT_OTA_DEBUG // close debug
                return -1;
            #endif
        }

        // 4. Check if the download partition exists
        dl_part = fal_partition_find(RT_BK_DL_PART_NAME);
        if (!dl_part)
        {
            log_e("download partition is not exist, please check your configuration!");
            return -1;
        }

        /* do upgrade when check upgrade OK 
         * 5. CRC DL FW HDR
         * 6. Check if the dest partition exists
         * 7. CRC APP FW HDR
         * 8. Compare DL and APP HDR, containning fw version
         */
        log_d("check upgrade...");
        if ((result = rt_ota_check_upgrade()) == 1) // need to upgrade
        {
            /* verify OTA download partition 
            * 9. CRC DL FW HDR
            * 10. CRC DL FW
            */
            if (rt_ota_part_fw_verify(dl_part) == 0)
            {
                // 11. rt_ota_custom_verify
                // 12. upgrade
                if (rt_ota_upgrade() < 0)
                {
                    log_e("OTA upgrade failed!");
                    /*
                     *  upgrade failed, goto app check. If success, jump to app to run, otherwise goto recovery factory firmware.
                     **/
                    goto _app_check;
                }
            }
            else
            {
                goto _app_check;
            }
        }
        else if (result == 0)
        {
            log_d("No firmware upgrade!");
        }
        else
        {
            log_e("OTA upgrade failed! Need to recovery factory firmware.");
            return -1;
        }

_app_check:
        part_table = fal_get_partition_table(&part_table_size);
        /* verify all partition */
        for (i = 0; i < part_table_size; i++)
        {
            /* ignore bootloader partition and OTA download partition */
            if (!strncmp(part_table[i].name, RT_BK_APP_NAME, FAL_DEV_NAME_MAX))
            {
                // verify app firmware
                if (rt_ota_part_fw_verify(&part_table[i]) < 0)
                {
                    // TODO upgrade to safe image
                    log_e("App verify failed! Need to recovery factory firmware.");
                    return -1;
                }
                else
                {
                    result = 0;
                }
            }
        }
    }
    else
    {
        result = -1;
    }

    return result;
}

int rt_ota_custom_verify(const struct fal_partition *cur_part, long offset, const uint8_t *buf, size_t len)
{
    uint16_t crc1, crc2;
    uint32_t link_addr;

    /* Do not modify buf content */
    uint8_t *buffer_out = (uint8_t*)buf;

    /* Add others custom validation rules. User TODO */

    return 0;
}
