### Fatfs 文件系统 OTA 打包工具 ###

> design by: word天

- 修改文件系统大小
  编辑 fatdisk.xml 文件的 disk_size 条目，单位 Kbytes

- 修改目标分区
  编辑 create.bat 中 PART 的值为目标分区的分区名
```
set PART=fs
```

- 打包
  复制需要打包的文件至 root 目录，运行 create.bat 即可

