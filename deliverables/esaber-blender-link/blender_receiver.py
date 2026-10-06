#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESABER 动捕协议 v1 —— Blender 接收端参考实现
补交于 2026-10-05（全栈开发）。契约：同目录 PROTOCOL.md（唯一真相源）；
固件侧权威实现：src/services/MotionTelemetry.{h,cpp}。

用法
----
A. 诊断台（任何 python3 ≥3.7，不在 Blender 里跑）：
     python3 blender_receiver.py                 # 监听 0.0.0.0:5005
     python3 blender_receiver.py --port 5005
   每 2 秒打印：帧率 / 累计丢帧 / 单次 drain 最大包数。
   **单次 drain >1 说明 UDP 队列积压过 —— 这是"越用越拖"的直接证据。**
   终端按键：z = yaw 归零（以当前朝向为正前）  p = 位置归零  q = 退出

B. Blender 内直驱活动对象（Blender 自带 Python 里跑，Text Editor → Run Script，
   或命令行 `blender --python blender_receiver.py -- --apply`）：
     python3 blender_receiver.py --apply
   把姿态四元数写到当前活动对象（rotation_mode=QUATERNION）。位置默认关闭
   （POSITION_ENABLED=False，理由见 PROTOCOL.md §3）。

接收端两条铁律（违反必现"越用越拖"）
------------------------------------
1. 每个消费节拍把 socket 缓冲【读空】只留最新一帧（poll() 已实现）：
   固件 50Hz 发、Blender ~24fps 消费，每帧只读一个包 = 每秒净积压 26 帧
   ≈ 每分钟多 31 秒延迟；
2. 校验失败（长度≠31 / 版本≠0x01 / XOR 不符）整帧丢弃，绝不使用半帧。

坐标系注意（第一次使用必校）
----------------------------
固件 Madgwick 世界系 Z 轴朝上（静止重力沿 −Z），yaw 无磁力计参考会缓慢漂移
（按 z 归零）。Blender 场景也是 Z-up。若模型呈"镜像 / 恒定 90° 偏转"，
改下方 AXIS_MAP / INVERT 两个常量即可，无需动固件。
"""

import argparse
import math
import socket
import struct
import sys
import threading
import time
from collections import deque

# ---------------------------------------------------------------- 常量
FRAME_LEN = 31
PROTO_VER = 0x01
_HDR = struct.Struct("<B7fBB")   # version + w,x,y,z,px,py,pz + seq + checksum，无填充恰 31B

# IMU 传感系 → 模型系轴向映射（按实物校一次）：
#   模型第 i 轴取 IMU 第 AXIS_MAP[i] 轴，INVERT[i]=-1 表示取反
AXIS_MAP = (0, 1, 2)
INVERT = (1.0, 1.0, 1.0)
POSITION_ENABLED = False   # 单 6 轴 IMU 双积分位置物理上限极低，默认关闭


# ---------------------------------------------------------------- 解码
def checksum(data: bytes) -> int:
    c = 0
    for b in data:
        c ^= b
    return c


def parse_frame(buf):
    """31B 帧 → {"q":(w,x,y,z), "p":(x,y,z), "seq":int}；任何校验不过返回 None。"""
    if buf is None or len(buf) != FRAME_LEN or buf[0] != PROTO_VER:
        return None
    if checksum(buf[:30]) != buf[30]:
        return None
    vals = _HDR.unpack_from(buf, 0)
    return {"q": vals[1:5], "p": vals[5:8], "seq": vals[8]}


# ---------------------------------------------------------------- 四元数工具
def qmul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return (w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
            w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2)


def yaw_of(q):
    w, x, y, z = q
    return math.atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z))


def quat_to_mat3(q):
    w, x, y, z = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
            [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
            [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]]


def mat_to_quat(m):
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0.0:
        s = math.sqrt(tr + 1.0) * 2.0
        return (0.25 * s, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s)
    if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0
        return ((m[2][1] - m[1][2]) / s, 0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s)
    if m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0
        return ((m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s)
    s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0
    return ((m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s)


def remap_axes_quat(q):
    """姿态旋转向量做基变换 R_model = A·R_imu·Aᵀ（A = 置换+符号矩阵）。"""
    r = quat_to_mat3(q)
    a = [[INVERT[i] if AXIS_MAP[i] == j else 0.0 for j in range(3)] for i in range(3)]
    # t = A·R
    t = [[sum(a[i][k] * r[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    # R' = t·Aᵀ
    m = [[sum(t[i][k] * a[j][k] for k in range(3)) for j in range(3)] for i in range(3)]
    return mat_to_quat(m)


def remap_vec(p):
    return tuple(INVERT[i] * p[AXIS_MAP[i]] for i in range(3))


# ---------------------------------------------------------------- 接收流
class MotionStream:
    """协议 v1 接收流。核心契约在 poll()：读空缓冲只留最新帧。"""

    def __init__(self, host="0.0.0.0", port=5005):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.setblocking(False)
        self.last_seq = None
        self.frames = 0
        self.dropped = 0
        self.last_burst = 0     # 最近一次 drain 读到的包数
        self.max_burst = 0      # 历史最大 drain 包数（>1 即发生过积压）
        self.last = None        # 最近有效帧（归零命令用）
        self.zero_rot = (1.0, 0.0, 0.0, 0.0)
        self.zero_pos = (0.0, 0.0, 0.0)

    def poll(self):
        """读空 socket 缓冲返回最新有效帧；无新帧返回 None。
        drain 丢弃的中间包也逐包计入丢帧统计（seq 不丢账）。"""
        newest = None
        burst = 0
        while True:
            try:
                buf, _ = self.sock.recvfrom(2048)
                burst += 1
            except (BlockingIOError, InterruptedError):
                break
            except ConnectionResetError:      # Windows：ICMP port unreachable
                continue
            frame = parse_frame(buf)
            if frame is None:
                continue                      # 坏帧丢弃，但 drain 不停
            if self.last_seq is not None:
                gap = (frame["seq"] - self.last_seq - 1) & 0xFF
                if 0 < gap < 128:
                    self.dropped += gap
            self.last_seq = frame["seq"]
            newest = frame
        self.last_burst = burst
        self.max_burst = max(self.max_burst, burst)
        if newest is not None:
            self.frames += 1
            self.last = newest
        return newest

    def zero_yaw(self, frame):
        """以 frame 的朝向为正前：补偿 = 绕世界 Z 转 −yaw₀，输出 = zero_rot ∘ q。"""
        y = yaw_of(frame["q"])
        self.zero_rot = (math.cos(-y / 2.0), 0.0, 0.0, math.sin(-y / 2.0))

    def zero_position(self, frame):
        self.zero_pos = tuple(frame["p"])

    def oriented(self, frame):
        q = qmul(self.zero_rot, frame["q"])
        p = tuple(frame["p"][i] - self.zero_pos[i] for i in range(3))
        return q, p


# ---------------------------------------------------------------- 模式 A：诊断台
def console_main(host, port):
    stream = MotionStream(host, port)
    stop = threading.Event()
    print(f"[esaber] 诊断台监听 {host}:{port}（协议 v1 / 31B 帧）")
    print("按键：z = yaw 归零   p = 位置归零   q = 退出")

    def cmd_loop():
        while not stop.is_set():
            try:
                line = sys.stdin.readline()
            except Exception:
                break
            if not line:
                break
            c = line.strip().lower()
            if c == "z" and stream.last:
                stream.zero_yaw(stream.last)
                print("[esaber] yaw 已归零")
            elif c == "p" and stream.last:
                stream.zero_position(stream.last)
                print("[esaber] 位置已归零")
            elif c == "q":
                stop.set()
                break
        # stdin EOF（后台运行/管道）：仅退出命令线程，主循环继续收帧。
        # 交互终端下 stdin 恒开不会走到这里；Ctrl+C / q 才是停止方式。

    threading.Thread(target=cmd_loop, daemon=True).start()

    t0 = time.time()
    f0 = 0
    while not stop.is_set():
        stream.poll()
        now = time.time()
        if now - t0 >= 2.0:
            fps = (stream.frames - f0) / (now - t0)
            warn = "  ← 积压！接收端消费跟不上" if stream.max_burst > 1 else ""
            print(f"[{now - t0:6.1f}s] {fps:5.1f} fps · 收 {stream.frames} · 丢 {stream.dropped}"
                  f" · 单次 drain 峰值 {stream.max_burst} 包{warn}")
            t0, f0 = now, stream.frames
        time.sleep(0.004)
    print("[esaber] bye")


# ---------------------------------------------------------------- 模式 B：Blender 直驱
def blender_main(host, port):
    import bpy

    stream = MotionStream(host, port)
    target_name = bpy.context.active_object.name if bpy.context.active_object else None
    if not target_name:
        print("[esaber] 没有活动对象：先在 3D 视图选中要驱动的模型再运行本脚本")
        return

    def tick():
        frame = stream.poll()
        if frame:
            q, p = stream.oriented(frame)
            obj = bpy.context.active_object or bpy.data.objects.get(target_name)
            if obj:
                obj.rotation_mode = "QUATERNION"
                obj.rotation_quaternion = remap_axes_quat(q)
                if POSITION_ENABLED:
                    obj.location = remap_vec(p)
        return 1.0 / 120.0   # Blender 主线程定时器，2 次回调间隔

    bpy.app.timers.register(tick, first_interval=0.0, persistent=True)
    print(f"[esaber] 监听 :{port} → 驱动对象「{target_name}」（位置 {'开' if POSITION_ENABLED else '关'}）")
    print(f"[esaber] drain 峰值/丢帧查看：bpy.app.timers 已注册；归零请用诊断台模式验证")


# ---------------------------------------------------------------- 自测
def selftest():
    def pack(seq, q, p, ver=PROTO_VER):
        f = bytearray()
        f.append(ver)
        f += struct.pack("<7f", *q, *p)
        f.append(seq)
        f.append(checksum(bytes(f)))
        return bytes(f)

    q90 = (math.sqrt(0.5), 0.0, 0.0, math.sqrt(0.5))          # 绕世界 Z 转 90°（yaw=90°，非奇异）
    buf = pack(5, q90, (0.1, 0.2, 0.3))

    fr = parse_frame(buf)
    assert fr and fr["seq"] == 5 and abs(fr["q"][0] - q90[0]) < 1e-6, "正常帧解析失败"

    bad = bytearray(buf); bad[30] ^= 1
    assert parse_frame(bytes(bad)) is None, "XOR 校验未拦截坏帧"
    bad = bytearray(buf); bad[0] = 2; bad[30] = checksum(bytes(bad[:30]))
    assert parse_frame(bytes(bad)) is None, "版本号未拦截"
    assert parse_frame(buf[:-1]) is None, "长度校验未拦截"

    st = MotionStream("127.0.0.1", 0)
    port = st.sock.getsockname()[1]
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tx.sendto(pack(0, q90, (0.0, 0.0, 0.0)), ("127.0.0.1", port))
    tx.sendto(pack(1, q90, (0.0, 0.0, 0.0)), ("127.0.0.1", port))
    tx.sendto(pack(4, q90, (0.0, 0.0, 0.0)), ("127.0.0.1", port))   # 丢 2,3
    time.sleep(0.05)
    f1 = st.poll()
    assert f1 and f1["seq"] == 4, f"drain-latest 破缺：拿到 {f1 and f1['seq']}"
    assert st.dropped == 2, f"丢帧统计错误：{st.dropped}"
    assert st.last_burst == 3, f"burst 统计错误：{st.last_burst}"
    assert st.poll() is None, "缓冲未读空"

    assert abs(math.degrees(yaw_of(f1["q"])) - 90.0) < 0.5, "yaw 提取错误"
    st.zero_yaw(f1)
    qz, _ = st.oriented(f1)
    assert abs(math.degrees(yaw_of(qz))) < 0.5, "yaw 归零失败"

    st.zero_position(f1)
    _, pz = st.oriented(f1)
    assert all(abs(v) < 1e-6 for v in pz), "位置归零失败"

    q0 = (0.9239, 0.2209, 0.2209, 0.2209)
    qi = mat_to_quat(quat_to_mat3(q0))
    assert all(abs(a - b) < 1e-5 for a, b in zip(q0, qi)), "四元数↔矩阵往返破缺"

    print("selftest OK：解析/校验/drain-latest/丢帧统计/归零/映射往返 全部通过")


# ---------------------------------------------------------------- 入口
def main():
    ap = argparse.ArgumentParser(description="ESABER 动捕协议 v1 接收端（参考实现）")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=5005)
    ap.add_argument("--apply", action="store_true", help="Blender 内运行：直驱活动对象")
    ap.add_argument("--selftest", action="store_true", help="离线自测（不需要设备）")
    args = ap.parse_args()
    if args.selftest:
        selftest()
    elif args.apply:
        blender_main(args.host, args.port)
    else:
        console_main(args.host, args.port)


if __name__ == "__main__":
    main()
