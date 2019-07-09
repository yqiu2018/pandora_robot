
# rt_ota API

## OTA 初始化

> int rt_ota_init(void);

OTA 全局初始化函数，属于应用层函数，需要在使用 OTA 功能前调用。**rt_ota_init** 函数接口内部集成了 **FAL** （FAL: Flash abstraction layer，Flash 抽象层）功能的初始化。

| 参数     | 描述    |
| :-----   | :-----  |
|无        | 无   |
| **返回**    | **描述**  |
|`>= 0`       | 成功 |
|-1        | 分区表未找到     |
|-2        | 下载分区未找到   |

## OTA 固件校验

> int rt_ota_part_fw_verify(const struct fal_partition *part);

对指定分区内的固件进行完整性和合法性校验。

| 参数     | 描述    |
| :-----   | :-----  |
|part        | 指向待校验的分区的指针  |
| **返回**    | **描述**  |
|`>= 0`       | 成功 |
|-1        | 校验失败     |

## OTA 升级检查

> int rt_ota_check_upgrade(void);

检查设备是否需要升级。该函数接口首先会通过校验下载分区（Download 分区）固件头信息的方式检查下载分区是否存在固件，如果下载分区存在固件，则对比检查下载分区和目标分区（app 分区）中固件的固件头信息，如果固件头信息不一致，则需要进行升级。

| 参数     | 描述    |
| :-----   | :-----  |
|无        | 无   |
| **返回**    | **描述**  |
|1       | 需要升级   |
|0        | 不需要升级     |

## 固件擦除

> int rt_ota_erase_fw(const struct fal_partition *part, size_t new_fw_size);

擦除目标分区固件信息。该接口会将目标分区内的固件擦除，使用前，请确认目标分区的正确性。

| 参数     | 描述    |
| :-----   | :-----  |
|part        | 指向待擦除分区的指针   |
|new_fw_size        | 指定擦除区域为新固件的大小   |
| **返回**    | **描述**  |
|`>= 0`       | 实际擦除的大小 |
|< 0        | 错误     |

## 查询固件版本号

> const char *rt_ota_get_fw_version(const struct fal_partition *part);

获取指定分区内的固件的版本。

| 参数     | 描述    |
| :-----   | :-----  |
|part        | 指向 Flash 分区的指针   |
| **返回**    | **描述**  |
|!= NULL       | 成功获取版本号，返回指向版本号的指针 |
| NULL        | 失败     |

## 查询固件时间戳

> uint32_t rt_ota_get_fw_timestamp(const struct fal_partition *part);

获取指定分区内的固件的时间戳信息。

| 参数     | 描述    |
| :-----   | :-----  |
|part        | 指向 Flash 分区的指针   |
| **返回**    | **描述**  |
|!= 0       | 成功，返回时间戳 |
|0        | 失败     |

## 查询固件大小

> uint32_t rt_ota_get_fw_size(const struct fal_partition *part);

获取指定分区内的固件的大小信息。

| 参数     | 描述    |
| :-----   | :-----  |
|part        | 指向 Flash 分区的指针   |
| **返回**    | **描述**  |
|!= 0       | 成功，返回固件大小 |
| 0        | 失败     |

## 查询原始固件大小

> uint32_t rt_ota_get_raw_fw_size(const struct fal_partition *part);

获取指定分区内的固件的原始大小信息。如下载分区（download 分区）里存储的固件可能是经过压缩加密后的固件，通过该接口获取压缩加密前的原始固件大小。

| 参数     | 描述    |
| :-----   | :-----  |
|part        | 指向 Flash 分区的指针   |
| **返回**    | **描述**  |
|!= 0       | 成功，返回固件大小 |
| 0        | 失败     |

## 获取目标分区名字

> const char *rt_ota_get_fw_dest_part_name(const struct fal_partition *part);

获取指定分区内的目标分区的名字。如下载分区（download 分区）中目标分区可能是 `app` 或者是其他分区（如参数区、文件系统区）。

| 参数     | 描述    |
| :-----   | :-----  |
|part        | 指向 Flash 分区的指针   |
| **返回**    | **描述**  |
|!= 0       | 成功，返回固件大小 |
| 0        | 失败     |

## 获取固件加密压缩方式

> rt_ota_algo_t rt_ota_get_fw_algo(const struct fal_partition *part);

获取指定分区内固件的加密压缩方式。

| 参数     | 描述    |
| :-----   | :-----  |
|part        | 指向 Flash 分区的指针   |
| **返回**    | **描述**  |
| RETURN_VALUE       | 返回固件加密压缩类型 |

获取加密类型： RETURN_VALUE & RT_OTA_CRYPT_STAT_MASK

获取压缩类型： RETURN_VALUE & RT_OTA_CMPRS_STAT_MASK

| 加密压缩类型     |描述 |
| :-----   | :----- |
|RT_OTA_CRYPT_ALGO_NONE        | 不加密不压缩   |
|RT_OTA_CRYPT_ALGO_XOR         | XOR 加密方式   |
|RT_OTA_CRYPT_ALGO_AES256      | AES256 加密方式   |
|RT_OTA_CMPRS_ALGO_GZIP        | GZIP 压缩方式   |
|RT_OTA_CMPRS_ALGO_QUICKLZ     | Quicklz 压缩方式   |
|RT_OTA_CMPRS_ALGO_FASTLZ      | FastLz 压缩方式   |

## 开始 OTA 升级

> int rt_ota_upgrade(void);

启动固件升级，将 OTA 固件从下载分区搬运到目标分区（app 分区）。

| 参数     | 描述    |
| :-----   | :-----  |
|无        | 无   |
| **返回**    | **描述**  |
|rt_ota_err_t 类型错误      | 详细的错误类型查看 rt_ota_err_t 定义 |

| 错误类型     | 值    |
| :-----   | -----:  |
|RT_OTA_NO_ERR        | 0   |
|RT_OTA_GENERAL_ERR        | -1   |
|RT_OTA_CHECK_FAILED        | -2   |
|RT_OTA_ALGO_NOT_SUPPORTED        | -3   |
|RT_OTA_COPY_FAILED        | -4   |
|RT_OTA_FW_VERIFY_FAILED        | -5   |
|RT_OTA_NO_MEM_ERR        | -6   |
|RT_OTA_PART_READ_ERR        | -7   |
|RT_OTA_PART_WRITE_ERR        | -8   |
|RT_OTA_PART_ERASE_ERR        | -9   |

## 获取固件加密信息

> void rt_ota_get_iv_key(uint8_t * iv_buf, uint8_t * key_buf);

移植接口，需要用户自行实现，从用户指定的地方获取固件加密使用的 iv 和 key。

| 参数     | 描述    |
| :-----   | :-----  |
|iv_buf        | 指向存放固件加密 iv 的指针，不能为空  |
|key_buf        | 指向存放固件加密 key 的指针，不能为空  |
| **返回**    | **描述**  |
|无       | 无 |

## 自定义校验

> int rt_ota_custom_verify(const struct fal_partition *cur_part, long offset, const uint8_t *buf, size_t len);

用户自定义校验接口，该接口用于扩展用户自定义的固件校验方法，需用用户重新实现。

该接口通过 **buf** 参数拿到 **len** 参数大小的 OTA 固件内容，固件的偏移地址为 **offset**，用户如果需要对这部分固件做自定义的操作，可以实现该接口来进行处理。

> 注意，用户不能在该接口内修改 **buf** 指向的缓冲区里的内容。

| 参数     | 描述    |
| :-----   | :-----  |
|cur_part        | OTA 固件下载分区   |
|offset        | OTA 固件的偏移地址   |
|buf        | 指向存放 OTA 固件的临时缓冲区，不能修改   |
|len        | OTA 固件缓冲区中的固件大小   |
| **返回**    | **描述**  |
|`>= 0`       | 成功 |
|< 0        | 失败     |
