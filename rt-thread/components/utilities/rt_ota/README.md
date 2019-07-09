# rt_ota

## 1、介绍

**rt_ota** 是 RT-Thread 开发的跨 OS、跨芯片平台的固件空中升级技术（Firmware Over-the-Air Technology），轻松实现对设备端固件的管理、升级与维护。

RT-Thread 提供的 OTA 固件升级技术具有以下优势：

- 固件防篡改 ： 自动检测固件签名，保证固件安全可靠
- 固件加密 ： 支持 AES-256 加密算法，提高固件下载、存储安全性
- 固件压缩 ： 高效压缩算法，降低固件大小，减少 Flash 空间占用，节省传输流量，降低下载时间
- 差分升级 ： 根据版本差异生成差分包，进一步节省 Flash 空间，节省传输流量，加快升级速度
- 断电保护 ： 断电后保护，重启后继续升级
- 智能还原 ： 固件损坏时，自动还原至出厂固件，提升可靠性
- 高度可移植 ： 可跨 OS、跨芯片平台、跨 Flash 型号使用

更多详细介绍，参考 [rt_ota 介绍](docs/introduction.md) 文档。

### 1.1 目录结构

> 说明：参考下面表格，整理出 packages 的目录结构

| 名称 | 说明 |
| ---- | ---- |
| docs  | 文档目录 |
| inc  | 头文件目录 |
| src | 源码                 |
| port | 移植代码目录 |
| samples| 软件包应用示例 |
| tools | OTA 打包工具存储目录 |

### 1.2 许可证

**rt_ota** package 以库文件的方式输出给开发者使用，严禁一切个人、组织、团体以任何形式传播源代码。

### 1.3 依赖

- RT-Thread 3.0+
- [FAL](https://github.com/RT-Thread-packages/fal)
- [Quicklz](https://github.com/RT-Thread-packages/quicklz) (可选)
- [tinycrypt](https://github.com/RT-Thread-packages/tinycrypt) (可选)

## 2、如何打开 rt_ota

**rt_ota** 通常是以基础组件包的形式自动被其它组件依赖，但开发者也可以使用 RT-Thread 的包管理器中选择使用它，具体路径如下：

```
Privated Packages of RealThread
    [*] OTA: The RT-Thread Over-the-air Programming package
    [ ]   Enable debug log output
    [ ]   Enable encryption
    [ ]   Enable compression
        version (latest)  --->
```

开发者可以根据需要选择启用调试日志输出、启用加密、启用压缩。

保存后等待 RT-Thread 的包管理器自动更新完成，或者使用 `pkgs --update` 命令手动更新包到 BSP 中。

## 3、使用 rt_ota

**rt_ota** package 是一个基础组件包，需要依赖 `1.3 依赖` 章节提到的组件包。因此，在使用 **rt_ota** 前，需要完成相关依赖的组件包的引入。

同时，在使用 **rt_ota** 前，需要开发者进行简单的移植操作，移植示例文件存放在 `./ports/temp/` 目录下，如下所示：

| 文件 | 说明 |
| ---- | ---- |
| rt_ota_key_port.c   | 文件中定义了获取 rt_ota 升级文件使用的加密 IV 和加密密钥的函数接口 |

其中，加密 IV 和加密密钥是开发者对固件进行加密的密钥，加密 IV 长度最大 16 个字符，加密密钥长度最大 32 个字符。

* 参考 rt_ota [API 手册](docs/api.md)
* 参考 rt_ota [移植文档](docs/port.md)
* 更多文档位于 `/docs` 下，使用前务必查看

## 4、注意事项

- 加密 IV 和加密密钥在 OTA 固件的安全性上起着关键性的作用，请妥善保管。

## 5、联系方式 & 感谢

* 维护：Armink
* 主页：
