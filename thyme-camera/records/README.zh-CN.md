# thyme 摄像头源码移植

2026-10-18 后续：主摄对焦驱动、#18 镜像、自动对焦库，以及第二次黑屏的
Turnip 原生堆栈和桌面 Freedreno 配置见 `SESSION-2026-10-18-focus.md`。
**#18 镜像已生成，尚不能据此宣称实机自动对焦已验证。**

最新实机状态见 `SESSION-2026-10-18-evening.md`：已恢复开机探测失败导致的
全部相机缺失，加入有限次数的开机probe重试，主摄Snapshot预览已收到帧。
前摄启动超时仍未解决；本轮GNOME崩溃黑屏已通过重启显示管理器恢复。

2026-10-18：将 `/home/ubuntu/os/linux-apollo-7.1` 的 S5KHMX、S5K3T2
驱动及 SM8250 C-PHY 支持移植到 `../kernel-source`（7.2.0-rc5）。
OV13B10 使用目标树已有的较新驱动，补上源树中的 OF 匹配能力及 thyme
辅助供电管理。未覆盖目标树的通话、显示或 modem 驱动。

## 板级连接

配置集中在：
`arch/arm64/boot/dts/qcom/sm8250-xiaomi-thyme-camera.dtsi`。
由 thyme 主 DTS **在文件末尾**包含，确保 L3 电压覆盖和 GPIO hog 删除生效。

| Sensor | CCI 控制器/master | 地址（7 位） | CSIPHY | MCLK | RESET |
|---|---|---|---|---|---|
| S5KHMX 主摄 | 0/1 | 0x3d | 3，3-trio C-PHY | 1，19.2 MHz | TLMM 62 |
| S5K3T2 前置 | 1/0 | 0x10 | 4，4-lane D-PHY | 4，19.2 MHz | TLMM 109 |
| OV13B10 超广角 | 1/1 | 0x36 | 2，4-lane D-PHY | 3，19.2 MHz | PM8009 3 |

CCI0、CCI1 的实际控制器地址分别为 0xac4f000、0xac50000。
接线依据用户提供的下游 `thyme-sm8250-camera-sensor-mtp.dtsi`，没有照搬
apollo 的主摄 CSIPHY0、CCI master、RESET 或 PM8008 电源。

- 三路 VIO 接 PM8009 L7，1.8 V。
- 主摄 VDIG 接 L1，1.104 V；VANA 是 PM8150L GPIO3 使能的 2.8 V 电源。
- 前置 VDIG 新增 L4，1.104 V；VANA 是 PM8150L GPIO4 使能的 2.8 V 电源。
- 超广角 VDIG 接 L3，约束从目标原来的 1.056 V 改为 1.2 V；VANA 由
  PM8150L GPIO5 使能。L3 的旧标签含 `1p05`，实际电压以新约束为准。
- 按下游 CUSTOM_REG1/2 接入 PM8150L GPIO1、GPIO4 对应的辅助 2.8 V
  电源；三个 sensor 驱动均管理其辅助电源的启停和错误回滚。
- 主摄同时管理 `afvdd`（PM8009 L6，2.856 V）。
- 删除占用超广角 RESET 的 PM8009 GPIO3 hog。GPIO1、2、4 的 hog 保留，
  让暂未支持的 GC02M1 微距/景深保持关闭及复位。
- OV13B10 增加 `ovti,ov13b10` OF 匹配，并在获取 RESET 时先保持复位。

## 前置时钟为何采用 19.2 MHz

下游 DTSI 声明 24 MHz，但 thyme 实际的
`com.qti.sensormodule.umi_sunny_s5k3t2.bin` 上电序列指定 **19200000 Hz**。
其 5184×3880、2592×1940、1280×720 三组模式寄存器表与源树 S5K3T2
驱动逐项一致，包括 `0x0136=0x1300`、`0x013e=0x00c8` 和 PLL 寄存器。
因此使用模块配置的 19.2 MHz；没有仅放宽驱动检查后把 19.2 MHz 的 PLL
表运行在 24 MHz。

thyme HMX 的 3008×2256 模式（模块内第 10 组）也与源驱动模式表逐项一致。
核对工具为源树 `tools/remoteproc/qti_sensormodule.py`。
公开模块数据来源、下载文件散列和核对结果见 `vendor-evidence.json`；这些
二进制仅用于核对，未复制到内核或作为运行时依赖。

## 构建与验证

`thyme_modem_defconfig`、`thyme-modem.config` 均已启用相应选项。
现用 `../kernel-build/.config` 仅新增 S5KHMX、S5K3T2 两项；其余相机基础
选项原先已开启，已有 XFRM/ESP 和通话修复配置保留。

```sh
cd ../kernel-source
make O=../kernel-build ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make O=../kernel-build ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j6 \
  Image.gz qcom/sm8250-xiaomi-thyme.dtb qcom/sm8250-xiaomi-thyme-logdump.dtb
```

验证结果：

- 三个 sensor 和整个 CAMSS 目录交叉编译通过。
- Image.gz 完整链接及两个 DTB 编译通过，无编译 warning/error。
- 检查生成 DTB 的三路 CCI/master、RESET 控制器/编号、VDIG 电压、时钟、
  CSIPHY、双向 endpoint，以及 GPIO hog 状态，通过；见 `dtb-check.log`。
- 保留 7.2 的 TPG、SM6350、虚拟通道及图像行对齐实现，仅移入所需
  C-PHY 增量；C-PHY 入口限制为 SM8250。

产物位于：

- `../kernel-build/arch/arm64/boot/Image.gz`
- `../kernel-build/arch/arm64/boot/dts/qcom/sm8250-xiaomi-thyme.dtb`
- `../kernel-build/arch/arm64/boot/dts/qcom/sm8250-xiaomi-thyme-logdump.dtb`

## 已打包的启动镜像

当前应使用 GPIO 修复版 **#17**：`../boot-images/thyme-camera-menu-gpiofix.img`。
初版摄像头镜像已实测有下述 GPIO core 问题，请勿用初版验证摄像头。

- `../boot-images/thyme-camera-menu-gpiofix.img`：修复版启动菜单。
- `../boot-images/thyme-camera-logdump-gpiofix.img`：修复版日志镜像。
- `../boot-images/thyme-camera-display-test-gpiofix.img`：修复版显示测试镜像。
- `../boot-images/SHA256SUMS-camera-gpiofix`：修复版校验值。

初版产物（保留用于对照）：

- `../boot-images/thyme-camera-menu.img`：常用启动菜单版，包含三路摄像头驱动和 thyme 摄像头设备树。
- `../boot-images/thyme-camera-logdump.img`：日志版，使用 logdump 设备树。
- `../boot-images/thyme-camera-display-test.img`：显示测试版，保留原显示测试启动参数。
- `../boot-images/SHA256SUMS-camera`：上述三个镜像的 SHA256 校验值。

沿用现有镜像的 Android boot header v1、2048 字节页面、加载地址和各自启动参数，
将新 `Image.gz` 与相应 DTB 拼接打包；initramfs 已内嵌在内核中。
逐项校验镜像头、启动参数、内核/DTB 内容、gzip 解压和页面填充通过。
原有 `thyme-menu.img` 等镜像保留。打包脚本为 `pack-images.py`，
验证记录见 `pack-images.log`。用户已启动 GPIO 修复版 #17，三路 sensor 均已绑定；本次操作没有刷写分区。

临时启动菜单版：

```sh
fastboot boot '/home/ubuntu/桌面/etc/thyme-call-repair/boot-images/thyme-camera-menu-gpiofix.img'
```

## 实机发现：GPIO hog 丢失编号转换

初版摄像头内核 #15 已在手机启动，S5KHMX、S5K3T2 已成功读 ID 并绑定。
但 PM8009 GPIO 控制器启动失败：

```text
gpiochip_add_data_with_key: GPIOs 546..549 (c440000.spmi:pmic@a:gpio@c000) failed to register, -22
i2c 23-0036: deferred probe pending: i2c: wait for supplier /soc@0/spmi@c440000/pmic@a/gpio@c000/camera-ultra-reset-state
```

CAMSS 的 `pending_async_subdevices` 仅剩 OV13B10，三路传感器尚未挂入完整
媒体图，没有生成 `/dev/v4l-subdev*`。以桌面用户 ubuntu 运行 libcamera 0.7.0
也确认 `No sensor found for /dev/media0`；该用户可访问 media 设备并使用
udmabuf，所以不能将这次问题归为相机应用的权限故障。

根因是 GPIO core 的 `gpiochip_add_hog()`：OF helper 已调用控制器的
`of_xlate`，却丢弃返回的描述符，只保留 flags，随后以 DTS 原始数字重新
获取描述符。PMIC GPIO 的 DTS 编号从 1 开始，而描述符从 0 开始，导致
GPIO1/2 hog 错位，GPIO4 越界，进而使四引脚控制器整体注册失败。

修复 `drivers/gpio/gpiolib.c`、`gpiolib-of.c`、`gpiolib-of.h`，让 OF helper
直接返回已转换的描述符，hog 使用该描述符；非 OF 分支沿用原来的取值方式。
GPIO1、2、4 的设备树编号和关闭 GC02M1 的意图保持不变。
独立修复补丁为 `gpio-hog-fix.patch`，完整 `thyme-camera.patch` 也已更新。
原始实机日志为 `device-dmesg-first.log`；编译记录 `build-gpio-fix.log`，
修复版镜像打包校验记录 `pack-images-gpiofix.log`。

用户已启动修复版 #17：PM8009 GPIO 控制器注册成功，三个 sensor 均已绑定，
`devices_deferred` 为空，CAMSS 不再有 pending endpoint，并已创建 27 个
`/dev/v4l-subdev*`。桌面 libcamera/PipeWire 成功枚举三路摄像头。

出图时发现第二个问题：libcamera 0.7 默认使用 EGL GPU debayer，但桌面
PipeWire 继承了 `MESA_LOADER_DRIVER_OVERRIDE=zink`，出现 EGL image 创建
失败、`debayerGPU failed`，最终 PipeWire SIGABRT。手机当时剩余内存约
4.6 GiB，不能仅凭驱动的分配失败消息认定系统物理内存不足。

已核对 libcamera v0.7.0 `software_isp.cpp` 和 `global_configuration.cpp`：
支持 `LIBCAMERA_SOFTISP_MODE=cpu`，或配置 `configuration.software_isp.mode: cpu`。
`libcamera-cpu.yaml` 已应用到手机桌面用户 ubuntu 的
`/home/ubuntu/.config/libcamera/configuration.yaml`。三路直接 libcamerasrc
收帧已通过，但 PipeWire 路径发现首帧未初始化参数崩溃及启动失败清理死锁，
已修补独立 libcamera 并安装。仍有前摄反复启停超时，尚未全部修好。

**下次继续先读 `SESSION-2026-10-18.md`，包含最新实机状态、补丁和复现步骤。**

## 尚需上机验证

修复版已验证三路 probe、枚举及直接收帧。桌面链路单次可出帧，
前摄反复启停仍会超时；应用预览、色彩及稳定切换尚待完成。
没有刷写手机分区。

支持范围是 V4L2 raw Bayer 采集。源 HMX 驱动仅提供 3008×2256 模式；没有
新增 GC02M1 驱动、AF/VCM、OIS、EEPROM 校准或 Android ISP 处理。
原厂图像处理效果和完整 108MP 模式不属于本次驱动的能力。

上机后先查看 sensor probe 和 media graph，再配置 RAW pipeline：

```sh
dmesg | grep -Ei 's5khmx|s5k3t2|ov13b10|camss|csiphy|cci'
media-ctl -p
v4l2-ctl --list-devices
```

## 补丁与备份

- `thyme-camera.patch`：相对于本次操作前目标树的源码补丁。
- `before/`：本次修改前已存在文件的原件。
- `changed-files.txt`：本次源码改动清单（含新增文件）。
- `build-config.before`、`Image.gz.before`：旧构建配置与旧压缩内核。
- `build-image-final.log`：最终镜像构建日志。

目标目录不是 Git 仓库，因此另行保存上述补丁和备份。撤销可在
`kernel-source` 中先执行 `patch --dry-run -R -p1 < ../camera-port/thyme-camera.patch`，
核对后再去掉 `--dry-run`；恢复构建配置后需要重新编译。
