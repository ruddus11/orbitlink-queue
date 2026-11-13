#!/usr/bin/env python3
"""
설계/전체구조도.png (1920x1080) 를 그린다.

실행: python3 설계/tools/make_architecture.py   (어느 폴더에서 실행해도 된다)

태스크 우선순위, 큐 길이, 시나리오 2 실측값은 소스와 결과 파일에서 읽어 온다. 값이
바뀌면 다시 실행만 하면 그림도 따라간다. 값을 찾지 못하면 그리지 않고 멈춘다.

10/20/40 tick backoff는 코드에 없는 제안값이라 아래 PROPOSED_BACKOFF 에 직접 적었다.
"""
import re
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch, Rectangle

ROOT = Path(__file__).resolve().parents[2]
QUEUE = ROOT / "OrbitLink_Queue"
OUTPUT = ROOT / "설계" / "전체구조도.png"

WIDTH, HEIGHT = 1920, 1080

PROPOSED_BACKOFF = (10, 20, 40)     # 제안값. 데모 코드는 50/100/200 이다

plt.rcParams["font.family"] = ["Arial", "DejaVu Sans"]

INK = "#0b0b0b"
MUTED = "#52514e"
LINE = "#8a8983"
BOX = "#f1f0ec"
WHITE = "#ffffff"
MONO = ["Menlo", "DejaVu Sans Mono"]

# 색은 docs/tools/make_diagrams.py 와 맞춘다: Emergency=파랑, System=주황, Normal=청록
EMERGENCY = "#2a78d6"
SYSTEM = "#eb6834"
NORMAL = "#1baf7a"
TINT = {EMERGENCY: "#dceafa", SYSTEM: "#fce4da", NORMAL: "#d8f2e8"}
FAIL = "#c0392b"
FAIL_TINT = "#fbe9e7"
LOGGING = "#6b5bb5"
LOGGING_TINT = "#ece9f7"


def find_define(path, name):
    """#define NAME 값 에서 값을 정수로 돌려준다. tskIDLE_PRIORITY + n 형식도 읽는다."""
    match = re.search(rf"#define\s+{name}\s+(.+)", path.read_text(encoding="utf8"))
    if match is None:
        raise SystemExit(f"{path} 에서 {name} 을 찾지 못했습니다.")
    value = match.group(1)
    relative = re.search(r"tskIDLE_PRIORITY\s*\+\s*(\d+)", value)
    return int(relative.group(1)) if relative else int(re.match(r"\d+", value).group())


def read_s2_retry():
    """시나리오 결과의 CSV,S2_RETRY 줄: 콜백 수, 재전송 성공, 한도 초과 포기, 대기함 넘침"""
    text = (QUEUE / "결과" / "시나리오_결과.txt").read_text(encoding="utf8")
    match = re.search(r"^CSV,S2_RETRY,(\d+),(\d+),(\d+),(\d+)$", text, re.M)
    if match is None:
        raise SystemExit("결과 파일에 CSV,S2_RETRY 줄이 없습니다. 먼저 make result 를 실행하세요.")
    return [int(v) for v in match.groups()]


def text(ax, x, y, s, size=13, color=INK, weight="normal", ha="center", va="center", family=None):
    ax.text(x, y, s, fontsize=size, color=color, fontweight=weight, ha=ha, va=va, family=family)


def panel(ax, x, y, w, h, fill=BOX, edge=LINE, lw=1.4, style="-"):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=12",
                                facecolor=fill, edgecolor=edge, linewidth=lw, linestyle=style))


def badge(ax, right, top, priority):
    """태스크 상자 오른쪽 위의 FreeRTOS 태스크 우선순위 표시"""
    w, h = 92, 28
    ax.add_patch(FancyBboxPatch((right - w - 10, top - h - 10), w, h, boxstyle="round,pad=0,rounding_size=14",
                                facecolor=INK, edgecolor=INK, linewidth=0))
    text(ax, right - w / 2 - 10, top - h / 2 - 10, f"Priority {priority}", size=12, color=WHITE, weight="bold")


def arrow(ax, points, color=LINE, style="-", lw=2.4):
    """꺾인 선. 마지막 구간에만 화살촉을 단다."""
    if len(points) > 2:
        xs, ys = zip(*points[:-1])
        ax.plot(xs, ys, color=color, linewidth=lw, linestyle=style, solid_capstyle="butt")
    ax.add_patch(FancyArrowPatch(points[-2], points[-1], arrowstyle="-|>", mutation_scale=20, color=color,
                                 linewidth=lw, linestyle=style, shrinkA=0, shrinkB=0))


def label(ax, x, y, s, size=13, color=MUTED, ha="center", mono=True):
    ax.text(x, y, s, fontsize=size, color=color, ha=ha, va="center", family=MONO if mono else None,
            bbox=dict(facecolor=WHITE, edgecolor="none", pad=2.5))


def queue_row(ax, x, y, name, slots, color, order):
    """큐 1개: 이름 + 칸 + 꺼내는 순서 번호. 칸 폭이 같으므로 길이 차이가 그대로 보인다."""
    cell_w, cell_h = 19, 32
    text(ax, x, y + 49, f"{name}  ({slots} slots)", size=14, weight="bold", ha="left", color=color)
    for i in range(slots):
        ax.add_patch(Rectangle((x + i * cell_w, y), cell_w, cell_h, facecolor=TINT[color], edgecolor=color, linewidth=1.1))

    cx, cy = x + 16 * cell_w + 28, y + cell_h / 2
    ax.add_patch(plt.Circle((cx, cy), 17, facecolor=color, edgecolor=color))
    text(ax, cx, cy, str(order), size=14, color=WHITE, weight="bold")


def sender_chip(ax, x, y, w, name, color):
    ax.add_patch(FancyBboxPatch((x, y), w, 44, boxstyle="round,pad=0,rounding_size=8",
                                facecolor=TINT[color], edgecolor=color, linewidth=1.6))
    text(ax, x + w / 2, y + 22, name, size=14, weight="bold", color=INK)


def legend(ax, x, y):
    entries = [(LINE, "-", "Normal message flow"), (FAIL, "--", "Failure and retry path"), (LOGGING, ":", "Monitoring and logging")]
    for i, (color, style, name) in enumerate(entries):
        row = y - i * 26
        ax.plot([x, x + 52], [row, row], color=color, linewidth=2.4, linestyle=style)
        text(ax, x + 64, row, name, size=12.5, color=MUTED, ha="left")


def draw():
    prio_sender = find_define(QUEUE / "src" / "main_demo.c", "PRIO_SENDER")
    prio_receiver = find_define(QUEUE / "src" / "main_demo.c", "PRIO_RECEIVER")
    prio_retry = find_define(QUEUE / "src" / "main_demo.c", "PRIO_RETRY")
    retry_max = find_define(QUEUE / "src" / "main_demo.c", "RETRY_MAX_COUNT")
    demo_inbox = find_define(QUEUE / "src" / "main_demo.c", "RETRY_INBOX_LENGTH")
    demo_backoff = find_define(QUEUE / "src" / "main_demo.c", "RETRY_BACKOFF_TICKS")
    len_e = find_define(QUEUE / "include" / "msg_queue.h", "MQ_LEN_EMERGENCY")
    len_s = find_define(QUEUE / "include" / "msg_queue.h", "MQ_LEN_SYSTEM")
    len_n = find_define(QUEUE / "include" / "msg_queue.h", "MQ_LEN_NORMAL")
    calls, recovered, gave_up, dropped = read_s2_retry()

    fig = plt.figure(figsize=(WIDTH / 100, HEIGHT / 100), dpi=100)
    fig.patch.set_facecolor(WHITE)
    ax = fig.add_axes((0, 0, 1, 1))
    ax.set_xlim(0, WIDTH)
    ax.set_ylim(0, HEIGHT)
    ax.axis("off")

    text(ax, 50, 1022, "OrbitLink System Architecture", size=31, weight="bold", ha="left")
    text(ax, 50, 978, "Spacecraft Communication System on FreeRTOS (POSIX simulator)", size=16, color=MUTED, ha="left")
    legend(ax, 1590, 1034)

    # ---------- 윗줄: 정상 흐름 ----------
    mid = 720    # 윗줄 화살표 높이

    panel(ax, 50, 570, 290, 300)
    text(ax, 70, 840, "Sender Tasks", size=17, weight="bold", ha="left")
    badge(ax, 340, 870, prio_sender)
    for i, (name, color) in enumerate([("Emergency", EMERGENCY), ("System", SYSTEM), ("Normal", NORMAL)]):
        sender_chip(ax, 75, 756 - i * 56, 240, name, color)
    text(ax, 195, 618, "Create message", size=13.5, color=MUTED)
    text(ax, 195, 594, "mq_msg_init()", size=13, color=MUTED, family=MONO)

    panel(ax, 420, 620, 210, 200)
    text(ax, 525, 780, "XOR Encrypt", size=17, weight="bold")
    text(ax, 525, 738, "Before the queue", size=13.5, color=MUTED)
    text(ax, 525, 712, "Payload only", size=13.5, color=MUTED)
    text(ax, 525, 686, "encrypted = 1", size=13, color=MUTED, family=MONO)
    text(ax, 525, 648, "Runs inside Sender", size=12.5, color=MUTED)

    panel(ax, 760, 545, 430, 350, fill="#f7fafe", edge=EMERGENCY, lw=2.4)
    text(ax, 975, 866, "Priority Queues", size=17, weight="bold")
    text(ax, 975, 838, "Always taken in order: Emergency, System, Normal", size=12.5, color=MUTED)
    queue_x = 790
    queue_row(ax, queue_x, 750, "Emergency", len_e, EMERGENCY, 1)
    queue_row(ax, queue_x, 665, "System", len_s, SYSTEM, 2)
    queue_row(ax, queue_x, 580, "Normal", len_n, NORMAL, 3)

    panel(ax, 1340, 570, 330, 300)
    text(ax, 1360, 840, "Receiver Task", size=17, weight="bold", ha="left")
    badge(ax, 1670, 870, prio_receiver)
    for i, (head, body) in enumerate([("1", "Take next message"), ("2", "XOR decrypt"), ("3", "Process")]):
        y = 748 - i * 62
        ax.add_patch(plt.Circle((1385, y), 16, facecolor=INK, edgecolor=INK))
        text(ax, 1385, y, head, size=13, color=WHITE, weight="bold")
        text(ax, 1416, y, body, size=15, ha="left")

    arrow(ax, [(340, mid), (420, mid)])
    arrow(ax, [(630, mid), (760, mid)])
    label(ax, 695, mid + 24, "mq_send()")
    arrow(ax, [(1190, mid), (1340, mid)])
    label(ax, 1265, mid + 24, "mq_receive()")

    # ---------- 아랫줄: 실패와 재전송 (오른쪽에서 왼쪽으로 돈다) ----------
    panel(ax, 880, 150, 290, 190, fill=FAIL_TINT, edge=FAIL)
    text(ax, 1025, 300, "Failure Callback", size=17, weight="bold")
    text(ax, 1025, 258, "Runs inside the caller", size=13.5, color=MUTED)
    text(ax, 1025, 232, "Must not block", size=13.5, color=MUTED)
    text(ax, 1025, 192, "xQueueSend(inbox, msg, 0)", size=11.5, color=MUTED, family=MONO)

    panel(ax, 530, 150, 260, 190, fill=FAIL_TINT, edge=FAIL)
    text(ax, 660, 300, "Retry Inbox", size=17, weight="bold")
    text(ax, 660, 258, "Holds failed messages", size=13.5, color=MUTED)
    text(ax, 660, 232, "Emergency first", size=13.5, color=MUTED)
    text(ax, 660, 192, "Still encrypted", size=13.5, color=MUTED)

    panel(ax, 50, 150, 390, 190, fill=FAIL_TINT, edge=FAIL)
    text(ax, 70, 310, "Retry & Backoff Task", size=17, weight="bold", ha="left")
    badge(ax, 440, 340, prio_retry)
    backoff = " / ".join(str(t) for t in PROPOSED_BACKOFF)
    text(ax, 70, 262, f"Max {retry_max} retries", size=14.5, ha="left")
    text(ax, 70, 232, f"Backoff {backoff} ticks (exponential)", size=14.5, ha="left")
    text(ax, 70, 202, "Emergency first", size=14.5, ha="left")
    text(ax, 70, 172, "Over the limit: give up and log", size=13, color=MUTED, ha="left")

    arrow(ax, [(1025, 545), (1025, 340)], color=FAIL, style="--")
    label(ax, 1025, 448, "Queue full: MQ_ERR_FULL", color=FAIL)
    arrow(ax, [(880, 245), (790, 245)], color=FAIL, style="--")
    arrow(ax, [(530, 245), (440, 245)], color=FAIL, style="--")
    arrow(ax, [(245, 340), (245, 448), (850, 448), (850, 545)], color=FAIL, style="--")
    label(ax, 545, 448, "Re-send: retry_count + 1, mq_send()", color=FAIL)

    # ---------- 오른쪽 아래: 로그 ----------
    panel(ax, 1340, 150, 530, 280, fill=LOGGING_TINT, edge=LOGGING)
    text(ax, 1362, 396, "Logger Task", size=17, weight="bold", ha="left")
    text(ax, 1362, 352, "Queue statistics", size=14.5, weight="bold", ha="left")
    text(ax, 1362, 324, "sent, received, failed, length, peak, wait ticks", size=13.5, color=MUTED, ha="left")
    text(ax, 1362, 280, "Structured log", size=14.5, weight="bold", ha="left")
    text(ax, 1362, 252, "time, type, id, result", size=13.5, color=MUTED, ha="left", family=MONO)
    text(ax, 1362, 208, "Log events from", size=14.5, weight="bold", ha="left")
    text(ax, 1362, 180, "Sender, Receiver, Retry", size=13.5, color=MUTED, ha="left")

    arrow(ax, [(1505, 570), (1505, 430)], color=LOGGING, style=":")
    label(ax, 1505, 500, "Log events", mono=False, color=LOGGING)
    arrow(ax, [(1190, 620), (1255, 620), (1255, 290), (1340, 290)], color=LOGGING, style=":")
    label(ax, 1255, 525, "mq_get_status()", color=LOGGING)

    # ---------- 바닥글 ----------
    ax.plot([50, 1870], [112, 112], color="#e4e3df", linewidth=1.2)
    text(ax, 50, 84, f"Scenario 2 (measured, stand-in Retry, {demo_inbox}-slot inbox): {calls} failure callbacks, "
         f"{dropped} dropped at the inbox, {recovered} re-sent OK, {gave_up} gave up.", size=14, ha="left")
    demo = "/".join(str(demo_backoff << i) for i in range(retry_max))
    text(ax, 50, 54, f"Task priorities are from main_demo.c (larger = higher). Logger has no priority in code yet. "
         f"Retry rule is a proposal; the demo uses {demo} ticks.", size=13, color=MUTED, ha="left")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUTPUT, dpi=100, facecolor=WHITE)
    plt.close(fig)
    print("저장:", OUTPUT)


if __name__ == "__main__":
    draw()
