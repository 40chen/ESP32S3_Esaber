# ESABER 动捕协议 v1（PROTOCOL.md · 唯一真相源）

_补录于 2026-10-05 · 全栈开发（协议 v1 原定者）。此前 README/MotionTelemetry.h 引用本文件但从未落盘，属 round2 交付漏项，本文件为补交正本。_
_固件侧权威实现：`src/services/MotionTelemetry.h` / `MotionTelemetry.cpp`（行号以 no-display 分支 HEAD 为准）。参考接收端：同目录 `blender_receiver.py`。_

---

## 1. 传输层

| 项 | 值 | 说明 |
|---|---|---|
| 协议 | UDP 单播 | 无连接、无重试，丢帧靠 seq 检测 |
| 方向 | 设备（AP 网关 192.168.4.1）→ 电脑 | 默认目标 192.168.4.2（AP 首个客户端），控制台「动捕地址」可改 |
| 端口 | **5005** | `HardwareConfig::TelemetryPort` |
| 节拍 | 标称 50Hz（20ms） | `TelemetryInterval`；主循环驱动，实际有抖动，接收端不得假设恒定间隔 |
| 本地端口 | 设备侧随机 | `udp_.begin(0)`，只发不收 |

## 2. 帧格式（31 字节定长）

```
偏移      长度   类型                  字段
[0]       1     uint8                 version = 0x01（接收端遇未知版本必须整帧丢弃）
[1..4]    4     float32 小端 IEEE-754  quat.w
[5..8]    4     float32 小端           quat.x
[9..12]   4     float32 小端           quat.y
[13..16]  4     float32 小端           quat.z
[17..20]  4     float32 小端           pos.x（米，±2.0 钳位）
[21..24]  4     float32 小端           pos.y
[25..28]  4     float32 小端           pos.z
[29]      1     uint8                 seq，从 0 起模 256 回绕
[30]      1     uint8                 checksum = frame[0..29] 逐字节异或（XOR）
```

字段顺序沿用旧 CSV 流（`w,x,y,z,px,py,pz`）。ESP32-S3 与 x86 均为小端 IEEE-754，免逐字段转换；**大端平台接收端必须 byteswap**。

## 3. 数据语义

- **四元数 (w,x,y,z)**：Madgwick `updateIMU`（6 轴，**无磁力计**）输出。世界系 **Z 轴朝上**（静止时重力沿 −Z）；roll/pitch 有重力锚定，**yaw 无绝对参考、必然缓慢漂移**（协议层面已知，接收端应提供 yaw 归零）。
- **位置 (x,y,z)**：加速度计世界系旋转 → 去重力 → 死区（0.15 m/s²）→ 速度/位置双积分，±2m 钳位。**单 6 轴 IMU 双积分物理上限极低（零偏二次发散），仅暗示性跟随；接收端应默认提供位置开关，推荐默认关闭。**
- **seq**：接收端按 `(seq − last_seq − 1) mod 256` 统计丢帧，>128 视为回绕/乱序。

## 4. 接收端消费契约（违反必现"越用越拖"）

1. **每个消费节拍把 socket 缓冲读空，只处理最新一帧**。固件 50Hz 发、Blender ~24fps 消费，若每帧只读一个包，每秒净积压 26 帧 ≈ 每分钟多 31 秒延迟。
2. 校验失败（长度≠31 / 版本≠0x01 / XOR 不符）**整帧丢弃**，绝不使用半帧。
3. 不要按固定 sleep(20ms) 读——跟随自己的渲染节拍 drain。

## 5. 已知遗留（固件侧，待 P0 修复后此处更新）

- 陀螺量程现配 ±250°/s（`MotionSensor.cpp` L48），挥剑 500~2000°/s 必削顶 → 四元数欠积分；
- 开机无陀螺零偏校准；四元数经欧拉角往返重建（万向锁敏感）；Madgwick 用名义 100Hz 不吃实际 dt；
- 详见 `shared/review/ESP32S3_Esaber_V2/ESABER_动捕漂移与延迟根因分析.md`（症状→根因指纹对照表）。
