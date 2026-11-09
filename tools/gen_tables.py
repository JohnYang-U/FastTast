#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_tables.py -- 从 META 官方公开头文件/源码中提取两张表, 生成给 METAQuickCheck 用的 .inc

  1) FlagTable.inc   : COMMON_CFG_TW 里全部 bool 测试项开关的 (名字 -> 字节偏移)
  2) TaskCatalog.inc : 任务枚举 ID -> (枚举名, 显示名, 前置依赖, 校准/综测属性)

这样工具里的"按名字设置任意测试项开关"和"测试项树"都不需要手写,
并且偏移量用 offsetof 在编译期由编译器校验。

用法:
    python tools\\gen_tables.py                      自动找头文件(优先 sdk/META/include)
    python tools\\gen_tables.py --src-dir <META源码树>  指定 META_DLL 源码树

  FlagTable.inc   只需要公开头文件, 自包含包里就能重新生成。
  TaskCatalog.inc 需要读官方源码里的任务映射表, 只有拿到源码树时
                  才会重新生成; 否则保留现有的 .inc 并给出提示。
"""
import argparse
import re
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR_DEFAULT = os.path.normpath(os.path.join(HERE, "..", "src", "gen"))

# 依次尝试: 自包含包的 include 目录 -> 本工程上一级(META_DLL 根) -> 上两级
HEADER_ROOTS = [
    os.path.normpath(os.path.join(HERE, "..", "sdk", "META", "include")),
    os.path.normpath(os.path.join(HERE, "..")),
    os.path.normpath(os.path.join(HERE, "..", "..")),
]

HDR_REL = os.path.join("METACalibrationLibrary", "Interface", "METACalibrationLibrary.h")
CPP_REL = os.path.join("METACalibrationLibrary", "Handler",
                       "METACalibrationLibrary_Handle.cpp")

CFG_T = "METACalibrationLibrary_COMMON_CFG_TW"

# 与 METAQuickCheck/CMakeLists.txt 里的 META_DEFINES 保持一致。
# 生成器按这套宏对结构体里的 #if/#ifdef 求值, 从而只保留 DLL 里真实存在的成员
# (例如 b_wcn_fm_test 在 #if defined(__WCN_FM_SUPPORT__) 里, 不属于本版本)。
KNOWN_DEFINES = {
    "__META_TD_A__", "__META_DLL_TD_A__", "__META_LTE__", "__META_WCDMA__",
    "__META_IOTNTN__", "__INVERSE_ORDER__", "__CHECK_REENTRANCY__",
    "__META_C2K__", "__META_LAB_TUNING__", "_CRT_SECURE_NO_WARNINGS",
}


def eval_cond(cond):
    """对 #if 条件做简单求值(只支持 defined()/&&/||, 足够应付头文件)"""
    c = cond.strip()
    # 去掉行尾注释
    c = re.sub(r"//.*$", "", c).strip()
    # 去掉外层多余括号
    while c.startswith("(") and c.endswith(")"):
        c = c[1:-1].strip()

    if "||" in c:
        return any(eval_cond(p) for p in c.split("||"))
    if "&&" in c:
        return all(eval_cond(p) for p in c.split("&&"))

    m = re.match(r"^(!?)\s*defined\s*\(\s*(\w+)\s*\)$", c)
    if m:
        v = m.group(2) in KNOWN_DEFINES
        return (not v) if m.group(1) else v

    m = re.match(r"^!\s*(\w+)$", c)
    if m:
        return m.group(1) not in KNOWN_DEFINES

    m = re.match(r"^(\w+)$", c)
    if m:
        return m.group(1) in KNOWN_DEFINES

    if c in ("0", "false"):
        return False
    if c in ("1", "true"):
        return True

    # 看不懂的条件: 保守当作成立(宁可多一个成员, 编译期 offsetof 会报错提醒)
    return True


def iter_active(lines):
    """按 #if/#else/#endif 过滤, 只产出处于有效分支的行"""
    stack = []          # 每层: dict(parent_active, active, taken)
    active = True
    for raw in lines:
        s = raw.strip()
        if s.startswith("#if"):
            m = re.match(r"#if(n?def)?\s*(.*)$", s)
            kind = m.group(1) or ""
            expr = m.group(2).strip()
            if kind == "def":
                cond = expr in KNOWN_DEFINES
            elif kind == "ndef":
                cond = expr not in KNOWN_DEFINES
            else:
                cond = eval_cond(expr)
            stack.append({"parent": active})
            active = active and cond
            continue
        if s.startswith("#elif"):
            if stack:
                expr = s[5:].strip()
                active = stack[-1]["parent"] and eval_cond(expr)
            continue
        if s.startswith("#else"):
            if stack:
                active = stack[-1]["parent"] and not active
            continue
        if s.startswith("#endif"):
            if stack:
                active = stack.pop()["parent"]
            continue
        if active:
            yield raw


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def find_struct(text, name):
    """提取 typedef struct {...} NAME; 的 body

    用括号配平向前回溯, 避免正则 .*? 跨过前面若干 struct 吞掉大段内容。
    """
    end = re.search(r"\}\s*" + re.escape(name) + r"\s*;", text)
    if end is None:
        return None
    close = end.start()          # 指向 '}'
    depth = 0
    i = close
    while i >= 0:
        ch = text[i]
        if ch == '}':
            depth += 1
        elif ch == '{':
            depth -= 1
            if depth == 0:
                return text[i + 1:close]
        i -= 1
    return None


def parse_bool_members(body):
    """返回 [(member_name, comment)] , 只保留 bool 成员(已按预处理条件过滤)"""
    out = []
    for line in iter_active(body.splitlines()):
        s = line.strip()
        if not s.startswith("bool "):
            continue
        m = re.match(r"bool\s+(\w+)\s*;(.*)$", s)
        if not m:
            continue
        member = m.group(1)
        rest = m.group(2)
        cm = re.search(r"/\*\*<\s*(.*?)\s*\*/", rest)
        comment = cm.group(1) if cm else ""
        out.append((member, comment))
    return out


def locate(src_dir):
    """找到头文件(必需)和 Handle.cpp(可选)"""
    roots = []
    if src_dir:
        roots.append(os.path.normpath(src_dir))
    roots.extend(HEADER_ROOTS)

    for r in roots:
        h = os.path.join(r, HDR_REL)
        if os.path.exists(h):
            c = os.path.join(r, CPP_REL)
            return h, (c if os.path.exists(c) else None), r
    return None, None, None


def main():
    ap = argparse.ArgumentParser(description="生成 METAQuickCheck 的开关表/任务表")
    ap.add_argument("--src-dir", default="", help="META_DLL 源码树根目录(可选)")
    ap.add_argument("--out-dir", default=OUT_DIR_DEFAULT, help="输出目录(默认 ../src/gen)")
    args = ap.parse_args()

    HDR, HANDLE_CPP, root = locate(args.src_dir)
    if HDR is None:
        sys.exit(
            "找不到公开头文件。已尝试:\n  " +
            "\n  ".join(os.path.join(r, HDR_REL) for r in HEADER_ROOTS) +
            "\n请把 sdk/META/include 补齐, 或用 --src-dir 指定 META_DLL 源码树。")

    out_dir = os.path.abspath(args.out_dir)
    print("头文件      : %s" % HDR)
    print("映射表源    : %s" % (HANDLE_CPP if HANDLE_CPP else "(无, 跳过 TaskCatalog 重生成)"))
    print("输出目录    : %s" % out_dir)

    hdr = read(HDR)

    cfg_body = find_struct(hdr, CFG_T)
    if cfg_body is None:
        sys.exit("找不到 %s 定义" % CFG_T)

    # CFG 里成员: "S_METACalibrationLibrary_XXX_T   memberName;" (按预处理条件过滤)
    cfg_members = []
    for line in iter_active(cfg_body.splitlines()):
        m = re.match(r"\s*(S_METACalibrationLibrary_\w+_T)\s+(\w+)\s*;", line)
        if m:
            cfg_members.append((m.group(1), m.group(2)))

    # 枚举名 -> id
    enum_body = None
    m = re.search(r"typedef\s+enum\s*\{(.*?)E_METACalibrationLibrary_TestTaskContext_NUM(.*?)\}\s*"
                  r"E_METACalibrationLibrary_TestTaskContext\s*;", hdr, re.S)
    if m:
        enum_body = m.group(1)

    enum_id = {}
    if enum_body:
        idx = 0
        for line in enum_body.splitlines():
            s = line.strip().strip(",")
            if not s:
                continue
            s = re.sub(r"//.*$", "", s).strip().strip(",").strip()
            if not s:
                continue
            mm = re.match(r"^(\w+)", s)
            if mm:
                name = mm.group(1)
                if name == "MULTIMODE_PRE_SELF_CALIBRATION":
                    enum_id[name] = 0
                    idx = 1
                elif name.startswith("E_METACalibrationLibrary"):
                    continue
                else:
                    enum_id[name] = idx
                    idx += 1

    # 从官方任务映射表提取显示名与依赖
    tasks = {}   # enum_id -> dict
    if HANDLE_CPP:
        hc = read(HANDLE_CPP)
        # 第 4/5 组二选一: 有处理函数(=实现了) 或 NULL(=表里有但没实现, desc 通常是
        # "Not support now")。supported 就是据此推导的, 不需要手写例外表。
        entry_re = re.compile(
            r"\{\s*(\w+)\s*,\s*\"([^\"]*)\"\s*,\s*\"([^\"]*)\"\s*,\s*"
            r"(?:&\s*METACalibrationLibrary_HANDLE\s*::\s*(\w+)|(NULL))\s*,\s*\"([^\"]*)\"\s*,\s*"
            r"([A-Z_0-9|]+)\s*\}")
        for m in entry_re.finditer(hc):
            enum_name, disp, desc, func, null_kw, dep, attr = m.groups()
            if enum_name not in enum_id:
                continue
            tid = enum_id[enum_name]
            is_test = 1 if "E_TASK_IS_TEST_ITEM" in attr else 0
            tasks[tid] = dict(enum=enum_name, name=disp, desc=desc, deps=dep,
                              is_test=is_test, supported=0 if null_kw else 1)

    # ---------------- FlagTable.inc ----------------
    os.makedirs(out_dir, exist_ok=True)
    lines = []
    lines.append("// ==== 本文件由 tools/gen_tables.py 自动生成, 请勿手工修改 ====")
    lines.append("// 来源: METACalibrationLibrary/Interface/METACalibrationLibrary.h")
    lines.append("// 用途: 测试项开关名 -> COMMON_CFG_TW 内的字节偏移(offsetof, 编译期校验)")
    lines.append("")
    total = 0
    for typ, mem in cfg_members:
        body = find_struct(hdr, typ)
        if body is None:
            continue
        members = parse_bool_members(body)
        if not members:
            continue
        lines.append("// ---- %s (%s) : %d 项 ----" % (mem, typ, len(members)))
        for name, comment in members:
            c = comment.replace('"', "'")
            lines.append(
                '    { "%s.%s", "%s", "%s", '
                "offsetof(%s, %s) + offsetof(%s, %s) },"
                % (mem, name, mem, c, CFG_T, mem, typ, name))
            total += 1
        lines.append("")
    # 去掉末尾空行
    while lines and lines[-1] == "":
        lines.pop()
    with open(os.path.join(out_dir, "FlagTable.inc"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("FlagTable.inc   : %d 个开关, %d 个分组" % (total, len(cfg_members)))

    # ---------------- TaskCatalog.inc ----------------
    if not tasks:
        print("TaskCatalog.inc : 跳过(没有 Handle.cpp, 保留现有文件)")
        return

    tl = []
    tl.append("// ==== 本文件由 tools/gen_tables.py 自动生成, 请勿手工修改 ====")
    tl.append("// 来源: METACalibrationLibrary/Handler/METACalibrationLibrary_Handle.cpp")
    tl.append("//       (来自 META 源码树的任务映射表与接口枚举)")
    tl.append("// 用途: 测试项树(ID / 显示名 / 前置依赖 / 校准或综测 / DLL 是否实现 / 说明)")
    tl.append("// supported=0 表示映射表里的 entry 为 NULL(表里有但 DLL 没实现, 例如")
    tl.append("// FR2_POST_SELF_CALIBRATION / C2K_POST_SELF_CALIBRATION), 选了会被静默跳过。")
    tl.append("")
    for tid in sorted(tasks.keys()):
        t = tasks[tid]
        tl.append('    { %d, "%s", "%s", "%s", %d, %d, "%s" },' % (
            tid, t["enum"], t["name"].replace('"', "'"),
            t["deps"], t["is_test"], t["supported"],
            t["desc"].replace('"', "'")))
    with open(os.path.join(out_dir, "TaskCatalog.inc"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(tl) + "\n")

    print("TaskCatalog.inc : %d 个任务项" % len(tasks))
    missing = [i for i in range(max(tasks) + 1) if i not in tasks]
    if missing:
        print("提示: 以下 taskId 在映射表中未出现(仅枚举存在): %s" % missing)


if __name__ == "__main__":
    main()
