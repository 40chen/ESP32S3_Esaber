# -*- coding: utf-8 -*-
"""
ESABER 动捕接收器（Blender 端）
================================

接收 ESP32-S3 光剑通过 UDP 发来的姿态数据，实时驱动 Blender 中的 Empty 物体，
用于动画录制 / 实时预览。

数据契约（与固件 src/services/MotionTelemetry.cpp 逐字段对应）：
    UDP 单播，每包一行 CSV，共 7 个字段，10 Hz（TelemetryInterval = 100ms）：
        q0,q1,q2,q3,px,py,pz
      - q0..q3 ：Madgwick AHRS 输出的四元数（w, x, y, z），已归一化
      - px..pz ：世界系位置（米），加速度积分 + 死区去漂移，已被固件钳制

用法（Blender 内）：
    1. 确认场景里有一个名为 "Empty" 的空物体（驱动目标）。
       名字不同就改下方 EMPTY_OBJ_NAME。
    2. Scripting 工作区 → 打开本文件 → Run Script（快捷键 Alt+P）。
    3. 保证电脑与光剑在同一网络；若光剑开的是热点模式，电脑连上 Esaber-Setup
       热点后，把本机 IP 填到光剑控制台「动捕地址」里。
    4. 停止：在 Blender 视口按 ESC（模态操作器会停掉监听线程和定时器）。

依赖：仅 Blender 自带模块（bpy / numpy），无需额外安装。
"""

import socket
import threading
import time

import bpy
import numpy as np

# ------------------------------------------------------------------ 配置区域
EMPTY_OBJ_NAME = "Empty"   # 驱动目标：场景中的空物体名
UDP_PORT = 5005            # 与固件 HardwareConfig::TelemetryPort 一致，勿单方面改动
HOST_IP = "0.0.0.0"        # 监听本机所有网卡（热点 / 局域网均可）
FPS = 60                   # Blender 侧应用频率；固件 10Hz 发包，这里 60Hz 消费即可
LOG_INTERVAL_S = 5.0       # 接收统计日志的节流间隔，避免 10Hz 数据刷屏

# 传感器 → Blender 坐标系映射。固件发的是 IMU 原始系，Blender 轴向不同，
# 方向不对时调这里的符号 / 顺序：
#   - 位置：三个分量逐项缩放，负号 = 轴向翻转
#   - 四元数：(w, x, y, z) 四个分量逐项取符号，用于翻转旋转轴
SENSOR_POS_SCALE = np.array([1.0, 1.0, -1.0], dtype=float)
SENSOR_QUAT_SIGN = np.array([1.0, 1.0, -1.0, -1.0], dtype=float)

# ------------------------------------------------------------------ 共享状态
# 监听线程写、Blender 定时器读。GIL 保证“整块引用赋值”原子，无需加锁。
latest_quat = np.array([1.0, 0.0, 0.0, 0.0], dtype=float)   # (w, x, y, z)
latest_pos = np.array([0.0, 0.0, 0.0], dtype=float)          # 米
is_running = False


def udp_listener():
    """后台线程：收 UDP 包、校验、写入共享状态。is_running 置 False 后退出。"""
    global latest_quat, latest_pos

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind((HOST_IP, UDP_PORT))
    except OSError as e:
        # 端口被占（多半是上次没按 ESC、旧线程还活着）——给出可操作的提示
        print(f"❌ 无法监听端口 {UDP_PORT}：{e}")
        print("   处理：① 先按 ESC 停掉旧接收器再重新运行；"
              "② 仍不行就重启 Blender（旧线程随进程结束释放）。")
        is_running = False
        return

    sock.settimeout(1.0)   # 周期性醒来检查退出标志，保证 ESC 后线程能退干净

    print(f"📡 UDP 监听已启动：{HOST_IP}:{UDP_PORT}，等待光剑数据…")
    print("   停止方式：Blender 视口内按 ESC")

    packets = 0        # 距上次统计的新增包数
    last_log = time.monotonic()
    try:
        while is_running:
            try:
                data, addr = sock.recvfrom(1024)
            except socket.timeout:
                continue   # 没数据也定期回头检查 is_running

            try:
                message = data.decode("utf-8").strip()
            except UnicodeDecodeError:
                continue   # 半截包 / 乱码包直接丢弃

            parts = message.split(",")
            if len(parts) != 7:
                continue   # 与固件契约不符的包（调试输出等）忽略

            try:
                values = np.array(list(map(float, parts)), dtype=float)
            except ValueError:
                continue   # 数值字段解析失败，丢弃

            if not np.all(np.isfinite(values)):
                continue   # NaN / Inf 防御：坏包不污染场景

            quat = values[0:4].copy()
            norm = np.linalg.norm(quat)
            if norm < 1e-6:
                continue   # 全零四元数非法
            quat /= norm   # 固件已归一化，这里再兜底一次（浮点截断容错）

            latest_quat = quat
            latest_pos = values[4:7].copy()
            packets += 1

            # 日志节流：只报首包来源 + 每 LOG_INTERVAL_S 一条速率统计
            if packets == 1:
                print(f"✅ 已连上光剑 {addr[0]}，数据开始流入")
            elif time.monotonic() - last_log >= LOG_INTERVAL_S:
                rate = packets / (time.monotonic() - last_log)
                print(f"   …持续接收中：{rate:.1f} 包/秒")
                packets = 0
                last_log = time.monotonic()
    finally:
        sock.close()
        print("📡 UDP 监听已停止，socket 已释放")


class UDPModalOperator(bpy.types.Operator):
    """UDP 动捕接收器：后台线程收包 + Blender 定时器消费，姿态写到 Empty"""

    bl_idname = "wm.udp_modal_operator"
    bl_label = "UDP 动捕接收器"
    _timer = None

    def modal(self, context, event):
        if event.type == "ESC":
            return self.cancel(context)

        if event.type == "TIMER":
            empty_obj = bpy.data.objects.get(EMPTY_OBJ_NAME)
            if empty_obj is None:
                # 目标被删/改名：低频提示一次，不在每帧刷屏
                if not getattr(self, "_warned", False):
                    print(f"⚠️ 场景中找不到物体「{EMPTY_OBJ_NAME}」，请创建空物体后重试")
                    self._warned = True
                return {"PASS_THROUGH"}

            mapped_quat = latest_quat * SENSOR_QUAT_SIGN
            mapped_pos = latest_pos * SENSOR_POS_SCALE

            empty_obj.rotation_mode = "QUATERNION"
            empty_obj.rotation_quaternion = mapped_quat.tolist()
            empty_obj.location = mapped_pos.tolist()

        return {"PASS_THROUGH"}

    def execute(self, context):
        global is_running
        is_running = True
        threading.Thread(target=udp_listener, daemon=True).start()
        self._timer = context.window_manager.event_timer_add(
            1.0 / FPS, window=context.window)
        context.window_manager.modal_handler_add(self)
        print("🚀 接收器启动中…（ESC 停止）")
        return {"RUNNING_MODAL"}

    def cancel(self, context):
        global is_running
        is_running = False
        if self._timer is not None:
            context.window_manager.event_timer_remove(self._timer)
            self._timer = None
        print("⏹️ 接收器已停止")
        return {"CANCELLED"}


def register():
    bpy.utils.register_class(UDPModalOperator)


def unregister():
    bpy.utils.unregister_class(UDPModalOperator)


if __name__ == "__main__":
    register()
    bpy.ops.wm.udp_modal_operator()
