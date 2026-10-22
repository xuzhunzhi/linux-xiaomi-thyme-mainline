# thyme 相机完整资料与源码

2026-10-18 整理。内核修改已经位于上一级 kernel-source 的正式驱动、
设备树和配置文件中。这里收齐配套用户态实现、配置、原始记录和构建产物。
入口以本文件和 records/SESSION-2026-10-18-focus-retry.md 为准；
records 内较早的 README、SESSION 和日志保留当时状态，不能当作最新结论。

## 内核实现

| 功能 | 源码位置（相对 kernel-source） |
|---|---|
| S5KHMX 主摄 | drivers/media/i2c/s5khmx.c |
| S5K3T2 前摄 | drivers/media/i2c/s5k3t2.c |
| OV13B10 超广角 | drivers/media/i2c/ov13b10.c |
| AK7377 10-bit 马达及延后初始化 | drivers/media/i2c/ak7375.c |
| 三路板级供电、接线、lens-focus | arch/arm64/boot/dts/qcom/sm8250-xiaomi-thyme-camera.dtsi |
| SM8250 C-PHY | drivers/media/platform/qcom/camss/ |
| PMIC GPIO hog 编号转换修复 | drivers/gpio/gpiolib.c、gpiolib-of.c、gpiolib-of.h |
| 内核配置 | arch/arm64/configs/thyme_modem_defconfig、thyme-modem.config |

主 DTS 末尾包含相机 dtsi。两份配置均启用 S5KHMX、S5K3T2、OV13B10、
VIDEO_AK7375 和 QCOM_CAMSS。完整改动清单见 records/changed-files.txt，
对应文件校验值见 KERNEL-SOURCES.sha256。

内核累计补丁 records/thyme-camera.patch 已经应用，不要再次应用到此树。
records/before/ 保存修改前文件，before-focus/ 和 before-focus-retry/
保存后续修复前的备份，增量补丁用于审阅及回退。

## 配套用户态

userspace/thyme-libcamera-v070/ 是最新完整 libcamera v0.7.0 源码，包含：

- simple pipeline、software ISP、IPA 的马达控制传递与连续自动对焦。
- AE 稳定后粗扫/细扫，扫描期间冻结曝光，限制最大马达位置为900。
- S5KHMX 增益转换、黑电平4096、专用 tuning。
- 对扣除黑电平后的信号应用白平衡/CCM，修正暗部偏紫。
- RAW10/RAW12 颜色统计尺度修正。
- 软件 ISP 首帧参数初始化与启动失败清理死锁修复。

records/libcamera-0.7-focus-color.patch 是上述累计补丁；同名最新源码归档
保留，较早版本也保留在 records 中用于对比。

手机现用 /opt/thyme-camera-focus-color。records/96-thyme-focus-color.conf
应分别放在桌面用户的以下两个目录，随后 daemon-reload 并重启服务：

```text
~/.config/systemd/user/pipewire.service.d/
~/.config/systemd/user/wireplumber.service.d/
```

records/libcamera-cpu.yaml 对应 ~/.config/libcamera/configuration.yaml；
records/91-thyme-camera-names.conf 是 WirePlumber 的相机名称配置。
records/90-thyme-native-gl.conf 是 GNOME Shell 服务的独立显示修复，
其安装目录是 ~/.config/systemd/user/org.gnome.Shell@ubuntu.service.d/。
历史 90/95 相机 drop-in 使用旧库路径，当前以96版本为准。

records/thyme-camera-probe-retry.py 与同名 service 是已有的有限次数开机
探测重试方案：仅重试未绑定 sensor，不解绑正常设备；不是前摄反复启动
超时的根本修复。安装位置分别见 service 的 ExecStart 与相关 SESSION。
这些配置和服务仅保存于本目录，本次整理没有更改手机设置。

## 构建

在 kernel-source 根目录构建已有配置：

```sh
mkdir -p ../kernel-build
cp thyme-camera/config/kernel-build.config ../kernel-build/.config
make O=../kernel-build ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make O=../kernel-build ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j6 \
  Image.gz qcom/sm8250-xiaomi-thyme.dtb qcom/sm8250-xiaomi-thyme-logdump.dtb
python3 thyme-camera/tools/pack-images.py --suffix=-focus-retry
```

config/kernel-build.config 保存 #19 构建使用的完整配置，其中固件、
initramfs 等外部路径仍按当前工作区设置；迁移机器时需核对这些路径。
打包脚本、原始 boot header 模板均在本目录，支持 --build-dir 和
--output-dir；无需依赖外部 camera-port 目录。

在手机 arm64 环境构建用户态（需 Meson、Ninja、C/C++ 编译器、libudev、
libyaml、OpenSSL 开发依赖及 Python 构建依赖；详见构建日志）：

```sh
meson setup /tmp/thyme-libcamera-color-build \
  userspace/thyme-libcamera-v070 \
  --prefix=/opt/thyme-camera-focus-color --libdir=lib --buildtype=debugoptimized \
  -Dpipelines=simple -Dipas=simple -Dcam=disabled -Dqcam=disabled \
  -Dgstreamer=disabled -Dpycamera=disabled -Ddocumentation=disabled \
  -Dlc-compliance=disabled -Dv4l2=disabled -Dtest=false \
  -Dtracing=disabled -Dlibunwind=disabled
ninja -C /tmp/thyme-libcamera-color-build -j4
```

构建成功后可用 meson install 安装；替换手机已加载的库之前需要先停止
相机客户端及使用该库的 PipeWire/WirePlumber，安装后恢复。

## 镜像和验证状态

boot-images/ 内包含 #19 三个 -focus-retry 镜像及 SHA256 清单。
常用 thyme-camera-menu-focus-retry.img 的 SHA256 为：

```text
27ce7fb2ed6324e44217d5f987ef890f5eee5152d8627b29f9ff60cafba316aa
```

当前手机仍为 #18 加热加载修复。最新驱动单独打开/关闭马达5次、主摄
重开采集3轮均通过；桌面已恢复新库与三路节点，用户已确认颜色正常。
#19 镜像编译与打包校验通过，尚待完整启动验证；最终近远距离对焦清晰度
仍需验证。前摄反复启停 CCI 超时、OV13B10 selection ioctl 警告仍未解决。
GC02M1、OIS、EEPROM 校准与108MP完整模式未实现。

records/live-driver-test/ 是仅供 #18 热加载验证的模块源码和 .ko，
不应作为 #19 的额外安装依赖。PM reset 辅助模块也仅供诊断。

核对本资料包与内核源码：

```sh
cd thyme-camera
sha256sum -c SHA256SUMS
cd ..
sha256sum -c thyme-camera/KERNEL-SOURCES.sha256
patch --dry-run -R -p1 -i thyme-camera/records/thyme-camera.patch
```

records 为原 camera-port 的完整副本，包含历史路径及历史散列清单；
本包的完整性以此处新生成的 SHA256SUMS 为准。
