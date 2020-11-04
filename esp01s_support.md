
## ESP01s模块支持

1. 更改分区文件为 [partitions_esp01s.csv](partitions_esp01s.csv)

![](assets/XSL2020-11-03_08-55-56.png)

2. 更改Flash Size 为 1MB,  Flash SPI Mode为DOUT

![](assets/XSL2020-11-03_09-07-31.png)

3. 文件镜像大小40K
```
mkspiffs -c esp-html/ -a -s 0xA000 ./spiffs_esp01.bin
```