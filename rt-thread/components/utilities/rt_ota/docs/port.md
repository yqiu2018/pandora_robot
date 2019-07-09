# RT-Thread OTA 包移植说明

本文主要介绍拿到 rt_ota 软件包后，需要做的移植工作。

rt_ota 软件包已经将硬件平台相关的特性剥离出去，因此 rt_ota 本身的移植工作非常少。

## 移植过程中主要涉及的文件

|文件                                    | 说明 |
|:-----                                  |:----|
|/ports/temp/rt_ota_key_port.c           | OTA 固件加密密钥获取接口的移植 |

rt_ota 软件包仅有一个移植文件 `rt_ota_key_port.c` 文件主要是实现获取 OTA 固件加密密钥信息的 API 接口，接口定义如下所示：

```
/**
 * Get the decryption key & iv
 *
 * @param iv_buf initialization vector
 * @param key_buf aes key
 */
void rt_ota_get_iv_key(uint8_t * iv_buf, uint8_t * key_buf)
{
    /* Get the decryption key & iv */
}
```

开发者只需要在该接口内，将 OTA 固件用到的加密密钥（key）和加密iv分别拷贝到 `key_buf` 和 `iv_buf` 即可。

**Note：**  
加密密钥（key）和加密 iv 是使用 **OTA 打包工具** 打包 OTA 固件所使用的密钥和 iv，直接关系到 OTA 固件的安全，请妥善保管。
