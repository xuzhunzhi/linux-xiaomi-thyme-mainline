# 2026-10-18 晚间：相机打不开与黑屏恢复

本记录晚于 `SESSION-2026-10-18.md`，续修先读这里。

## 本轮发现与恢复

- 手机本次重新开机仍运行 #17。OV13B10 在1.400秒 CCI master1 queue0
  超时，probe失败 -5；另外两路已绑定，但CAMSS等不到OV，全部相机无法枚举。
- 手动写 `23-0036` 到 `/sys/bus/i2c/drivers/ov13b10/bind` 成功，
  随即出现27个v4l-subdev。重启桌面ubuntu的PipeWire/WirePlumber后枚举恢复。
- OV13B10 经PipeWire的640×480、60帧成功；主摄1280×720、60帧成功。
  Snapshot随后实际启动了OV的1920×1080预览，日志显示处理30帧。
- 前摄仍有第一条0x6028写超时 -110。autosuspend_delay_ms改0也失败，
  已恢复1000。保持前摄power/control=on的后续测试因Snapshot抢占、
  后来桌面退出而无有效结论，已恢复auto；CCI power/control仍为auto。
  不要把这些电源诊断当作有效修复。

## 新安装的开机恢复配置

仅重试未绑定的传感器，不主动解绑已绑定设备；最多5轮、间隔1秒，
仅匹配 `Xiaomi Mi 10S (thyme)`。

- 脚本本地 `thyme-camera-probe-retry.py`，手机 `/usr/local/sbin/thyme-camera-probe-retry`。
- 服务本地 `thyme-camera-probe-retry.service`，手机 `/etc/systemd/system/thyme-camera-probe-retry.service`。
- 已enable，排序在systemd-user-sessions/display-manager之前。
- systemd-analyze verify通过，当前启动服务成功（全部已绑定分支）。
  实际缺失设备的手动bind恢复已通过，完整下次冷启动服务路径尚未验证。
- 这是开机故障恢复措施，**不等于解决CCI超时内核根因**。

WirePlumber配置 `91-thyme-camera-names.conf` 已装到
`/home/ubuntu/.config/wireplumber/wireplumber.conf.d/91-thyme-camera-names.conf`，
三路名称和优先级已在pw-cli中确认：

| sensor | node.description | priority.session |
|---|---|---|
| S5KHMX | Xiaomi Mi 10S Main (S5KHMX) | 1000 |
| OV13B10 | Xiaomi Mi 10S Ultrawide (OV13B10) | 750 |
| S5K3T2 | Xiaomi Mi 10S Front (S5K3T2) | 700 |

原Snapshot last-camera-id是“内置后摄像头”，两个后摄以前名称重复。
现在设为 `Xiaomi Mi 10S Main (S5KHMX)`，避免默认选中有启动故障的前摄。
没有禁用或隐藏前摄。

## 19:20 黑屏

用户在测试中报告突然黑屏。日志确认19:20:39 GNOME Shell PID2476
`crashed with signal 11`。随后GDM greeter的临时用户运行目录UID不匹配，
登录界面也启动失败，ubuntu图形会话于19:20:51退出。
手机内核及SSH一直在运行，没有重启，也没有新的镜像刷写。

崩溃前有ubuntu-dock挂载卷的JS异常，但仅凭时间先后不能确定崩溃根因；
目前没有可定位原生SIGSEGV的堆栈，不应断言某个扩展或相机代码导致。

执行 `sudo -n systemctl restart display-manager.service` 恢复自动登录。
新的GNOME PID8427、ubuntu图形会话seat0/tty2正常，DSI connected。
没有更改GDM、GNOME扩展或系统GPU配置。

## 本轮结束状态（手机19:22:50）

- GNOME PID8427运行，Snapshot PID9880运行。
- 桌面PipeWire PID8135 active，NRestarts=0。
- Snapshot已选择主摄，1920×1080-XRGB8888预览启动；
  19:22:38软件ISP报告已处理30帧，证明应用链路收到帧。
- 前摄power/control=auto，autosuspend_delay_ms=1000；CCI=auto。
- 三路已绑定，开机probe重试服务已启用。
- 没有修改内核源码、重新打包镜像、刷写或重启整机。
- 未完成：前摄稳定启动/切换，CCI根因，下一次冷启动验证，
  桌面SIGSEGV根因（显示管理器重启已恢复会话）。

## 证据

- `device-reopen-boot-dmesg.log`：本次开机OV探测失败。
- `device-reopen-gnome-crash.log`：黑屏前后完整系统journal片段。
- `device-reopen-recovery.log`：恢复服务、会话和电源参数。
- `device-reopen-preview.log`：恢复后主摄Snapshot预览处理帧证据。

若撤销新增用户态配置：disable开机retry服务，移走对应service/script，
移走用户WirePlumber的91-thyme-camera-names.conf并重启WP；
Snapshot last-camera-id可恢复原值。不要误删此前修好的90-thyme-camera.conf
（PipeWire使用/opt/thyme-camera/lib）或libcamera CPU配置。
