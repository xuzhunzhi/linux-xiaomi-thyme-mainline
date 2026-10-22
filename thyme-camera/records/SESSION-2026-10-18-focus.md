# 主摄对焦与第二次黑屏（2026-10-18）

本记录晚于 SESSION-2026-10-18-evening.md。用户反馈主摄偏紫、模糊，
并明确指出对焦问题；修复中再次报告整屏黑屏。

## 主摄对焦硬件已验证，自动对焦尚待新内核启动

移植遗漏了主摄 VCM：目标树没有 lens-focus，CONFIG_VIDEO_AK7375 未启用。
apollo donor 的 AK7375 节点不适合直接照搬：thyme 原厂模块指定
umi_ak7377_s5khmx，寄存器参数是地址 0、1 字节地址、2 字节数据、左移 6 位。
原厂 init 为寄存器 2 写 0、延时 1000 us，上电后延时 10 ms。

原厂 thyme DTS 主摄 cell 1 引用 actuator_ultra_wide（名称容易误导），
实际位于 CCI0 master 1，使用 PM8009 L6；不能按 wide 名称误选 L5/master 0。
live Linux 总线 21、地址 0x0c，应答成功。寄存器 2 初始读到 0x20，
写 0 后读回 0；位置 343 左移 6 位为 0x55c0，分别读寄存器 0/1 为 0x55/0xc0。
不要用一次连续读取来判断寄存器值，此器件的连续读结果与逐寄存器读不同。

旧内核 #17 下仅用临时 I2C 诊断，单条主摄管线依次测试
0、200、343、450、600、750、900。位置约 200 时星形边缘、键盘细节明显
优于 0；750/900 明显失焦，证实马达能够改变焦平面。
诊断帧只存 /tmp，没有放入持久记录。保存的数字指标见
device-focus-manual-sweep.json；其简单高频分数受暗部噪声影响，
不能用最后一个最高分声称 900 是正确焦点。

测试曾设 21-003d/power/control=on 保持供电；19:47 已恢复 auto。
最后一次显式手动位置是 200，但恢复 auto/下电后不能保证保持该位置。

## 内核与镜像

- ak7375.c 新增独立 AK7377 10-bit/shift 6 变体，不冒充 AK7375。
- 不使用未核实的 standby 指令；释放时沿用渐进归零后关闭 regulator。
- 修正 runtime resume 的 I2C 失败路径：返回错误并释放 regulator 引用。
- thyme 主摄添加 lens-focus；同一 CCI0 master 1 下 lens@c 使用 L6/VIO L7。
- 两份 thyme 配置及当前 build .config 启用 CONFIG_VIDEO_AK7375=y。
- Image.gz、两个 DTB 完整编译，无编译 warning/error；版本 #18。
- 编译 DTB 的地址、compatible、lens phandle、共享 VDD/VIO 已实际检查。

boot-images/thyme-camera-menu-focus.img：
425df03a5445e06b07e0cca1088271f1579751eace9e260cec3f84da60c5c3f1

menu/logdump/display-test 三种 -focus 镜像及 SHA256SUMS-camera-focus 均已生成，
pack-images-focus.log 记录头、命令行、内核、DTB、gzip 与填充验证。
旧 #17 gpiofix 镜像保持原散列，未刷写分区。

完整 thyme-camera.patch 已更新，反向 dry-run 通过；
thyme-ak7377-focus.patch 是相对 #17 源码的本轮增量；before-focus/ 可回退本轮。
checkpatch-focus.log 的 Signed-off-by/提交拆分提示属于本地补丁打包格式，
不是编译失败；没有虚构作者签署。

## libcamera

旧 /opt/thyme-camera 保留。新库单独安装在 /opt/thyme-camera-focus。
基准仍为 v0.7.0，保留此前首帧参数初始化及未启动 worker 的 stop 修复。
从本机 hotdog 工程的 0005-libcamera-software-isp-add-contrast-autofocus.patch
适配简单管线 lens controls、IPA、统计及粗扫/细扫 AF。

本轮另外适配：

- v0.7.0 的 YamlObject、单统计缓冲区 API。
- HMX 专用 tuning，AF 扫描限制为原厂边界 900；其他相机不启用 AF 算法。
- AE 连续稳定三次后开始扫描；扫描期间固定曝光/增益。
- 每四个 green 样本先平均，再计算边缘能量，减少噪声影响。
- 没有校准 DAC 与屈光度，所以不冒充标准 LensPosition 的单位。
- S5KHMX gain helper：原厂 sensor.so 反汇编确认 code=gain*32，1x..32x。
- RAW10 暗场集中在约 64 DN，固定 blackLevel=4096（16 位尺度）。
- AGC 的 unity/default 阈值使用 helper->gain(againDef)，避免把数值 1
  当成原始寄存器增益码导致曝光降不下来。

新库 native arm64 构建、独立安装及 ldd -r 检查通过。
#17 下直接 cam 主摄 1280x960，120 帧、约 30 fps、正常结束；gain 显示 1..32。
同时明确警告 No focus lens control available，这是旧内核缺少 lens 节点，
不能把本次收帧当作自动对焦已验证。

95-thyme-focus.conf 指向新库，原 90-thyme-camera.conf 保留供回退。
需要核对手机端 drop-in 已安装；daemon-reload 本身不会替换运行中的 PipeWire。
后续新内核启动后检查 PipeWire maps 实际路径及 AF 日志。

持久保存：libcamera-0.7-focus.patch（含此前两处修复的累计补丁）、
libcamera-0.7.0-thyme-focus-source.tar.gz、构建/安装日志。
原先 libcamera-0.7-softisp-fixes.patch 和旧源码归档保持可用。
原厂配置/现场寄存器及增益证据见 s5khmx-focus-evidence.json。

## 第二次黑屏：已有原生堆栈，绕开桌面 Zink/Turnip

19:44:57 GNOME Shell PID8427 打印 signal 11，19:45:19 用户会话退出。
内核 #17、SSH 和 uptime 一直正常；当时没有启动 #18，也未激活新相机库。
GDM greeter 又遇到随机 UID60578 与实际 UID995 不匹配，登录界面未恢复。

19:47 重启 display-manager 后恢复桌面 PID18742。
解包手机 /var/crash/_usr_bin_gnome-shell.1001.crash 到
/tmp/thyme-gnome-crash-1945，用 gdb 获得原生线程堆栈：
崩溃线程在 signal handler 之前落入 libvulkan_freedreno.so，随后是
libgallium-26.0.3；主线程在 libmutter-cogl 的 swap_buffers_with_damage。
这次有依据定位到桌面 Zink/Turnip 路径，不能归咎于 camera kernel 或
仅凭 ubuntu-dock 的 JS 告警认定扩展是根因。

原 GNOME 环境 MESA_LOADER_DRIVER_OVERRIDE=zink、TU_DEBUG=noconform。
先独立验证 native Freedreno EGL OpenGL 3.3 context，实际报告
FD650 / OpenGL4.6 Mesa26.0.3，清屏读回 RGBA=[64,128,191,255]，通过。

安装用户 drop-in：
/home/ubuntu/.config/systemd/user/org.gnome.Shell@ubuntu.service.d/90-thyme-native-gl.conf
只把 GNOME Shell 的 MESA_LOADER_DRIVER_OVERRIDE 设为 msm，
GALLIUM_DRIVER 设为 freedreno；未改 /etc/environment 的全局设置。

第一次生效重启撞到旧 gnome-session stop 与新 autologin start 事务冲突，
并非 native GL 启动崩溃。旧会话彻底退出后再启动显示服务，19:52 桌面
PID21830 正常，service Environment 已核对，/proc/maps 无 libvulkan_freedreno。
随后新相机库 120 帧测试时桌面仍正常；这只是短时验证，需继续观察稳定性。

证据：device-focus-second-black-screen.log、gnome-second-crash-backtrace.log、
egl-freedreno-probe.log、gnome-native-gl-start.log、device-focus-lib-smoke.log。
core 和 crash 原件留手机 /tmp、/var/crash，未复制到外部。
撤销桌面改动：移走该 Shell drop-in，daemon-reload，并重新登录。

## 待继续

1. #18 临时 fastboot boot，随后确认 AK7377 media ancillary link 与 focus_absolute。
2. 确认 PipeWire 使用新库，实际 AF 扫描/收敛、不同距离画面及重开镜头。
3. 主摄偏紫尚未完成 RAW/软件 ISP 对比；gain/黑电平修正不等于色彩已修好。
4. 前摄 CCI 首写超时仍未解决，开机 OV probe retry 仍需冷启动验证。
5. 继续观察 native Freedreno 桌面稳定性。

已向用户询问是否现在重启临时启动 #18；收到答复前不重启手机。
