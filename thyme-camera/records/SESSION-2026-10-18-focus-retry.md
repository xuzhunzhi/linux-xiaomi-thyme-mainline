# #18 实机对焦恢复、颜色修正及 #19 镜像

本记录接续 SESSION-2026-10-18-focus.md，更新至 2026-10-18 20:35 CST。

## 启动与现状

用户进入 fastboot 后，手机实际运行 #18，uname 已确认。未刷写分区。
fastboot boot 的 sending 返回 unknown command，随后设备消失；不能仅凭该
返回值断言启动失败，也不能断言一定是该命令完成了启动。

#18 内建旧马达驱动存在首次初始化失败问题，目前已加载
thyme_ak7377_retry 模块接管 21-000c，代码与 #19 的新 ak7375.c 相同，
仅 driver.name 改为 thyme-ak7377-retry，方便无需重启验证。
此模块未安装为开机服务。重启旧 #18 会丢失这次热加载修复；应测试 #19。

桌面 GNOME Shell 仍为 PID2545，使用此前 native Freedreno override，
本轮未再次出现桌面崩溃。不能据此保证长期稳定。

## 本轮用户确认与颜色修正

用户明确反馈“颜色正常了，对焦又失效了”。颜色修正版已独立安装至
/opt/thyme-camera-focus-color，旧 /opt/thyme-camera 与 -focus 保留。

CPU ISP 原实现先乘白平衡增益，再由 gamma LUT 扣黑电平，导致黑电平
偏置也被红、蓝增益放大。修正为对扣除黑电平后的信号应用增益/CCM，
再加回 gamma LUT 将扣除的偏置；CCM 与非 CCM 分支均处理。
另外，unpacked RAW10/12 的颜色 sum 先除 4/16，与 8-bit histogram
和 black level 统一尺度。原 helper 的 blackLevel4096 仍有效，
它通过 camera_sensor_helper 设置，不在 s5khmx.yaml 中重复设置。

native arm64 编译、安装、ldd -r 通过。PipeWire 和 WirePlumber 均添加
96-thyme-focus-color.conf，指向新库，实际运行环境已检查。
本机保留同名 conf、累计 libcamera-0.7-focus-color.patch 和完整源码归档。
临时 DEBUG drop-in 已在收尾删除并重启服务，避免持续大量写 journal。

## 对焦丢失的实证与修正

#18 在开机约16秒初始化 AK7377 时 I2C 返回 -ENXIO(-6)。runtime PM
锁存为 error，此后 open 返回 EINVAL。切换颜色库时再次发生，libcamera
日志明确 No focus lens control available；有预览不代表 AF 已接入。
清除 PM 错误、重新连接后，实际扫描到 424，硬件控制恢复。

电源实验并没有证明某一个单独电源或 CAMSS 常开可以永久解决问题。
早期“主摄/CAMSS 都 auto 后仍可打开”只是短时成功，随后仍会 -6；
不能将此短时结果写成不存在供电依赖。只开 CCI、只开 CAMSS 和逐项
启用 dvdd/avdd/avdd2/avdd3 的结果见 power-matrix/power-rails 日志，
没有得到稳定单轨归因，不据此添加猜测的 regulator 依赖。

修正 ak7375.c：

- 先初始化 runtime PM 为 suspended，再注册异步 subdevice，消除暴露窗口。
- AK7377 open/resume 先取得 vdd/vio，仅枚举控制范围不做 I2C 初始化。
- 实际 focus 请求时才激活马达并写位置。失败由该次控制返回，后续请求
  可重试，不再把访问过早造成的 NACK 锁存在 runtime resume 状态中。
- 尚未初始化的马达关闭时直接释放电源；初始化过的仍按原逻辑渐进归零。
- AK7345/AK7375 保留恢复位置的现有行为。

热加载同份代码后：主摄/CAMSS power/control=auto；无传感器采集时
单独打开/关闭 lens 5 次全部成功并恢复 suspended。随后主摄完整重新
打开采集3轮，每轮210帧、约30fps，三轮均取得 focus controls 并完成
粗扫/细扫，分别到334、200、222。桌面恢复后仍有AF与马达位置变化。
这验证控制链和重复打开；近远距离的最终主观清晰度仍需用户反馈。

## #19 镜像与持久文件

完整 Image.gz 和 thyme/menu、logdump DTB 编译通过，编译日志无 warning/error。
ak7375.c checkpatch：0 errors、0 warnings。累计内核补丁反向 dry-run 通过。
boot header、命令行、kernel、DTB、gzip、填充与旧镜像保护检查均通过。

boot-images/thyme-camera-menu-focus-retry.img
SHA256: 27ce7fb2ed6324e44217d5f987ef890f5eee5152d8627b29f9ff60cafba316aa

另有 logdump/display-test 的 -focus-retry 镜像和 SHA256SUMS-camera-focus-retry。
#19 尚未临时启动或进行冷启动验证；当前仍是 #18 + 热加载驱动。

thyme-camera.patch 已更新为累计版本；thyme-ak7377-focus-retry.patch 是
相对 #18 的驱动增量。before-focus-retry/ 与旧累计补丁备份可回退。
live-driver-test/ 保存热加载源码、Makefile 与实测 .ko；PM reset 辅助源码
仅供诊断，其模块已经卸载，不作为开机修复。

## 收尾与待验证

- 两个用户的相机/音频服务 runtime masks 均解除，服务已恢复。
- 主摄、CAMSS 均恢复 auto，未保留常供电脚本。
- 桌面能列出前摄、主摄和 OV13B10 三个节点，Snapshot 已重新打开。
- 图片仅暂存手机/主机 /tmp，未加入持久源码/记录归档。
- 下一步：用户确认近远目标实际清晰度；必要时改进扫描指标，不能只凭
  AfStateFocused 宣称对焦品质达标。#19 需临时启动，验证冷开机与重新枚举。
- 前摄 CCI 首写 timeout、OV13B10 selection ioctl 警告仍属未完成事项。
