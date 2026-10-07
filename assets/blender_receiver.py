# -*- coding: utf-8 -*-
"""ESABER Blender 接收脚本 · 协议 v1（31B 定长 UDP 帧，见 PROTOCOL.md）

使用方法：
  1. Blender → Scripting 工作区 → 新建文本 → 粘贴本脚本 → Run Script（Alt+P）
  2. 遥测实时驱动 TARGET_OBJ（留空则取运行时的活动物体），rotation_mode 自动设为 QUATERNION
  3. 联调：另开终端运行 esaber_sim.py（模式 static/spin/swing/tumble）
  4. 停止：在 Python 控制台执行 bpy.app.timers.unregister(ESABER_TICK)

配置区按装机方向调整 AXIS_MAP / AXIS_SIGN；AP 模式下电脑连设备热点，
设备端的 Blender IP 要填这台电脑在热点网段的地址（192.168.4.x）。
"""
import bpy
import socket
import struct

from mathutils import Matrix, Quaternion, Vector

# ── 配置区 ────────────────────────────────────────────────────────────────
UDP_PORT = 5005          # TelemetryPort，与固件一致
TARGET_OBJ = ""          # 物体名；留空 = 运行时的活动物体
APPLY_POSITION = False   # 位置为加速度积分（漂移大），默认只跟姿态
POS_SCALE = 0.01         # 位置缩放（米）
AXIS_MAP = (0, 1, 2)     # 设备 x,y,z → Blender 轴编号（Blender 为 Z-up）
AXIS_SIGN = (1.0, 1.0, 1.0)  # 各设备轴在 Blender 系的符号
# 例：设备 Y 轴朝前、Z 轴朝上装机 → 常用 AXIS_MAP=(0, 2, 1), AXIS_SIGN=(1.0, -1.0, 1.0)
# ─────────────────────────────────────────────────────────────────────────

VERSION = 0x01
FRAME_LEN = 31
HEADER_FMT = "<B7fB"     # version + qw,qx,qy,qz,px,py,pz + seq（小端）

_sock = None
_stats = {"frames": 0, "bad": 0, "drops": 0, "last_seq": None}
_target_name = TARGET_OBJ


def _get_socket():
    global _sock
    if _sock is None:
        _sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        _sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        _sock.bind(("0.0.0.0", UDP_PORT))
        _sock.setblocking(False)
        print(f"[ESABER] 监听 UDP :{UDP_PORT}（协议 v1, {FRAME_LEN}B）")
    return _sock


def _get_target():
    global _target_name
    if _target_name:
        return bpy.data.objects.get(_target_name)
    obj = bpy.context.view_layer.objects.active if bpy.context.view_layer else None
    if obj is not None:
        _target_name = obj.name
        print(f"[ESABER] 目标物体: {obj.name}")
    return obj


def _remap_rotation(quat):
    """设备系旋转 → Blender 系：R_B = M · R_D · M⁻¹（精确坐标变换，非分量重排）。"""
    m = Matrix.Identity(3)
    for dev_axis, blender_axis in enumerate(AXIS_MAP):
        m[blender_axis][dev_axis] = AXIS_SIGN[dev_axis]
    rot_device = Quaternion((quat[0], quat[1], quat[2], quat[3])).to_matrix().to_3x3()
    return (m @ rot_device @ m.inverted()).to_quaternion()


def _apply(obj, quat, pos):
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = _remap_rotation(quat)
    if APPLY_POSITION:
        p = Vector((pos[0], pos[1], pos[2])) * POS_SCALE
        obj.location = (p[AXIS_MAP[0]] * AXIS_SIGN[0],
                        p[AXIS_MAP[1]] * AXIS_SIGN[1],
                        p[AXIS_MAP[2]] * AXIS_SIGN[2])


def ESABER_TICK():
    """bpy.app.timers 回调：每 20ms 清空 socket 只保留最新帧并应用。"""
    sock = _get_socket()
    latest = None
    while True:
        try:
            data, _addr = sock.recvfrom(128)
        except BlockingIOError:
            break
        if len(data) != FRAME_LEN:
            _stats["bad"] += 1
            continue
        csum = 0
        for b in data[:FRAME_LEN - 1]:
            csum ^= b
        if csum != data[-1]:
            _stats["bad"] += 1
            continue
        version = data[0]
        if version != VERSION:
            _stats["bad"] += 1
            continue
        values = struct.unpack(HEADER_FMT, data[:30])
        seq = values[-1]
        if _stats["last_seq"] is not None:
            _stats["drops"] = (_stats["drops"] + (seq - _stats["last_seq"] - 1) % 256)
        _stats["last_seq"] = seq
        latest = values[1:8]  # qw,qx,qy,qz,px,py,pz

    if latest is not None:
        obj = _get_target()
        if obj is not None:
            _apply(obj, latest[0:4], latest[4:7])
            _stats["frames"] += 1
            if _stats["frames"] % 250 == 0:
                print(f"[ESABER] 已应用 {_stats['frames']} 帧 | 丢帧 {_stats['drops']}"
                      f" | 坏帧 {_stats['bad']}")
    return 0.02  # 20ms


bpy.app.timers.register(ESABER_TICK, first_interval=0.05)
print("[ESABER] 接收脚本已启动；停止: bpy.app.timers.unregister(ESABER_TICK)")
