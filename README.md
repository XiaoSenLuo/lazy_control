# ESP8266 MQTT

## 特性:

1. MQTT 协议远程控制, 连接MQTT Broker, 无论在哪里都可以控制.
2. 两路继电器控制市电220V负载, 最大可控制 8A 电流.
3. 内置 220V 转 5V 1200mA 超稳定降压模块.
4. ESP 12F模块作为控制中心, 内置http server用于配置用户设定:
   - WiFi SSID
   - WiFi Password
   - MQTT Broker Host
   - MQTT Broker Port
   - MQTT Broker URI
   - MQTT Broker Username
   - MQTT Broker Password

5. 设置掉电不丢失
6. 快速上电掉电3次(每次控制在3S内), 恢复默认设置
7. 手机控制


![](assets/1.png)
![](assets/2.jpg)