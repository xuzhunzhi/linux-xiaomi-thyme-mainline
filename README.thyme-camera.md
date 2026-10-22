# Xiaomi Mi 10S（thyme）相机

相机相关内核改动已直接合入本源码树，包含 S5KHMX 主摄、S5K3T2 前摄、
OV13B10 超广角、AK7377 对焦马达、SM8250 C-PHY 和 GPIO hog 修复。

完整的用户态源码、配置、补丁、构建方法、实机记录与 #19 启动镜像见
[thyme-camera/README.zh-CN.md](thyme-camera/README.zh-CN.md)。

主摄颜色修正及自动对焦算法需要配套 libcamera，单独更新内核不能替换它们。
最新驱动已在 #18 上热加载验证；#19 完整镜像尚待启动验证。
