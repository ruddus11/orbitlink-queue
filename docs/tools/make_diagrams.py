#!/usr/bin/env python3
"""
docs/ 의 다이어그램 PNG 4장을 그린다.

실행: python3 docs/tools/make_diagrams.py   (프로젝트 폴더에서)

그림 4의 숫자는 결과/시나리오_결과.txt 의 CSV 줄을 읽어서 그리므로, 실험을 다시
돌리고 이 스크립트를 실행하면 그래프도 실측값으로 바뀐다.
"""
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch, Rectangle

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"
RESULT = ROOT / "결과" / "시나리오_결과.txt"

plt.rcParams["font.family"] = "AppleGothic"
plt.rcParams["axes.unicode_minus"] = False

INK = "#0b0b0b"
MUTED = "#52514e"
LINE = "#8a8983"
GRID = "#e4e3df"
SURFACE = "#fcfcfb"

# 색은 유형을 따라간다: 모든 그림에서 Emergency=파랑, System=주황, Normal=청록
EMERGENCY = "#2a78d6"
SYSTEM = "#eb6834"
NORMAL = "#1baf7a"
TINT = {EMERGENCY: "#dceafa", SYSTEM: "#fce4da", NORMAL: "#d8f2e8"}
BOX = "#f1f0ec"
OURS = "#dceafa"


def canvas(width, height):
    fig, ax = plt.subplots(figsize=(width / 10, height / 10), dpi=200)
    fig.patch.set_facecolor(SURFACE)
    ax.set_xlim(0, width)
    ax.set_ylim(0, height)
    ax.axis("off")
    return fig, ax


def box(ax, x, y, w, h, text, fill=BOX, edge=LINE, size=11, lw=1.0, color=INK):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=1.0",
                                facecolor=fill, edgecolor=edge, linewidth=lw))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=size, color=color, linespacing=1.35)


def arrow(ax, start, end, text=None, color=LINE, style="-", offset=(0, 1.2), size=9.5, rad=0.0, ha="center"):
    ax.add_patch(FancyArrowPatch(start, end, arrowstyle="-|>", mutation_scale=13, color=color, linewidth=1.5,
                                 linestyle=style, connectionstyle=f"arc3,rad={rad}", shrinkA=0, shrinkB=0))
    if text:
        ax.text((start[0] + end[0]) / 2 + offset[0], (start[1] + end[1]) / 2 + offset[1], text,
                ha=ha, va="center", fontsize=size, color=MUTED, linespacing=1.3)


def title(ax, x, y, ko, en):
    ax.text(x, y, ko, fontsize=15, color=INK, va="center")
    ax.text(x, y - 3.2, en, fontsize=10.5, color=MUTED, va="center")


def save(fig, name):
    fig.savefig(DOCS / name, facecolor=SURFACE, bbox_inches="tight", pad_inches=0.15)
    plt.close(fig)
    print("저장:", DOCS / name)


def diagram1():
    fig, ax = canvas(156, 76)
    title(ax, 2, 72, "그림 1. OrbitLink 전체 구조와 큐 모듈의 위치", "Figure 1. OrbitLink architecture and where the queue module sits")

    box(ax, 3, 38, 20, 14, "Sender 태스크\nSender Tasks\n(RTOS & Task)")
    box(ax, 29, 38, 18, 14, "XOR 암호화\nEncrypt\n(Encryption)")

    ax.add_patch(FancyBboxPatch((62, 28), 30, 36, boxstyle="round,pad=0,rounding_size=1.2",
                                facecolor=OURS, edgecolor=EMERGENCY, linewidth=2.0))
    ax.text(77, 59.5, "메시지 큐 모듈 (경연 담당)\nmsg_queue module", ha="center", va="center", fontsize=11, color=INK, linespacing=1.3)
    for i, (label, color) in enumerate([("Emergency 큐", EMERGENCY), ("System 큐", SYSTEM), ("Normal 큐", NORMAL)]):
        box(ax, 66, 47 - i * 6.2, 22, 4.8, label, fill="white", edge=color, size=10, lw=1.4)
    ax.text(77, 31.5, "유형 분리 · 우선순위 · 통계", ha="center", va="center", fontsize=9.5, color=MUTED)

    box(ax, 107, 38, 19, 14, "Receiver 태스크\nReceiver Task\n+ XOR 복호화")
    box(ax, 132, 38, 21, 14, "Logger 태스크\nLogger Task\n(Encryption\n& Logging)", size=9.5)

    box(ax, 22, 8, 30, 12, "Retry 태스크 + Backoff\nRetry Task (Retry & Backoff)", size=10)
    box(ax, 104, 8, 26, 12, "Monitor 태스크\nMonitor Task", size=10.5)

    arrow(ax, (23, 45), (29, 45))
    arrow(ax, (47, 45), (62, 45), "mq_send()", offset=(0, 1.8))
    arrow(ax, (92, 45), (107, 45), "mq_receive()", offset=(0, 1.8))
    arrow(ax, (126, 45), (132, 45))

    arrow(ax, (70, 28), (46, 20), "꽉 참 → 실패 콜백\nmq_set_full_callback()", offset=(10.5, -5), size=9)
    arrow(ax, (30, 20), (62, 34), "재전송 mq_send()", style="--", offset=(-11, 3), size=9)
    arrow(ax, (112, 20), (88, 28), "mq_get_status()\n큐 길이 · 대기 tick", offset=(15, 2.5), style="--", size=9)

    ax.text(78, 3, "XOR를 큐 앞뒤에 두는 배치는 제안안이며 팀 협의가 필요하다. 큐 모듈은 payload 내용을 건드리지 않는다.",
            ha="center", fontsize=9.5, color=MUTED)
    save(fig, "diagram1_architecture.png")


def cells(ax, x, y, cell_w, cell_h, total, filled, color):
    for i in range(total):
        fill = color if i < filled else "white"
        ax.add_patch(Rectangle((x + i * cell_w, y), cell_w, cell_h, facecolor=fill,
                               edgecolor="white" if i < filled else color, linewidth=0.9))


def diagram2():
    fig, ax = canvas(130, 68)
    title(ax, 2, 64, "그림 2. 유형별 큐 3개와 Receiver가 꺼내는 순서",
          "Figure 2. Three typed queues and the order the Receiver drains them")

    rows = [("Emergency", 8, 1, EMERGENCY, 44, "1순위 · 8칸"), ("System", 8, 3, SYSTEM, 30, "2순위 · 8칸"),
            ("Normal", 16, 14, NORMAL, 16, "3순위 · 16칸")]
    for name, total, filled, color, y, note in rows:
        box(ax, 3, y, 16, 8, f"Sender\n{name}", fill=TINT[color], edge=color, size=10.5, lw=1.2)
        arrow(ax, (19, y + 4), (28, y + 4), "mq_send", offset=(0, 1.8), size=9)
        cells(ax, 28, y + 1, 2.6, 6, total, filled, color)
        ax.text(28, y + 8.6, note, fontsize=9.5, color=MUTED)

    box(ax, 92, 25, 21, 18, "Receiver\nmq_receive()", size=11.5, fill="white", edge=INK, lw=1.3)
    arrow(ax, (50, 49), (92, 39), "1. 가장 먼저 확인", color=EMERGENCY, offset=(4, 3.2), size=10)
    arrow(ax, (50, 34), (92, 34), "2. Emergency가 비었으면", color=SYSTEM, offset=(0, 1.6), size=10)
    arrow(ax, (71, 20), (92, 29), "3. 둘 다 비었을 때만", color=NORMAL, offset=(3, -3.6), size=10)

    box(ax, 28, 4, 62, 7, "카운팅 세마포어 s_msg_count = 세 큐의 메시지 총 개수\nmq_send가 Give → 메시지가 없으면 Receiver는 mq_receive에서 잠든다",
        size=9.5, fill=BOX)
    arrow(ax, (90, 7.5), (102, 25), style="--", offset=(8, -1), text="깨움", size=9.5)

    ax.text(116, 34, "매번\n1번부터\n다시", fontsize=10, color=MUTED, va="center", linespacing=1.4)
    save(fig, "diagram2_priority_queues.png")


def read_csv():
    """결과 파일에서 'CSV,'로 시작하는 줄만 읽는다."""
    rows = [l.strip().split(",")[1:] for l in RESULT.read_text(encoding="utf8").splitlines() if l.startswith("CSV,")]
    if not rows:
        raise SystemExit(f"{RESULT} 에 CSV 줄이 없습니다. 먼저 make result 를 실행하세요.")
    return rows


def diagram3(rows):
    retry = next(r for r in rows if r[0] == "S2_RETRY")
    calls, recovered, gave_up, dropped = retry[1:5]

    fig, ax = canvas(130, 62)
    title(ax, 2, 58, "그림 3. 큐가 가득 찼을 때: 실패 콜백 → Retry 담당 연결",
          "Figure 3. When a queue is full: failure callback hands the message to Retry")

    box(ax, 3, 34, 17, 10, "Sender 태스크\nmq_send(msg, 0)", size=10)
    box(ax, 28, 32, 17, 14, "유형별 큐에\n빈자리 있나?\nIs there room?", fill="white", edge=INK, size=10)
    box(ax, 53, 34, 18, 10, "MQ_OK\n큐에 저장", fill=TINT[NORMAL], edge=NORMAL, size=10)
    box(ax, 28, 12, 17, 11, "failed 통계 +1\nMQ_ERR_FULL 반환", fill=TINT[SYSTEM], edge=SYSTEM, size=9.5)
    box(ax, 53, 12, 19, 11, "실패 콜백\nfull_callback(msg)\n기다리면 안 됨", fill=OURS, edge=EMERGENCY, size=9.5, lw=1.4)
    box(ax, 80, 12, 18, 11, "Retry 대기함\nxQueueSend(…, 0)", size=10)
    box(ax, 106, 12, 21, 11, "Retry 태스크\nbackoff 50→100→200\nretry_count + 1", size=9.5)
    box(ax, 106, 34, 21, 10, "3회 넘으면 포기\ngive up", fill=BOX, size=10)

    arrow(ax, (20, 39), (28, 39))
    arrow(ax, (45, 39), (53, 39), "예 Yes", offset=(0, 1.5))
    arrow(ax, (36.5, 32), (36.5, 23), "아니오 No", offset=(5.5, 0))
    arrow(ax, (45, 17.5), (53, 17.5))
    arrow(ax, (72, 17.5), (80, 17.5))
    arrow(ax, (98, 17.5), (106, 17.5))
    arrow(ax, (116.5, 23), (116.5, 34), style="-")

    # 재전송 경로는 박스를 가로지르지 않도록 아래쪽으로 돌려서 Sender로 되돌린다.
    ax.plot([116.5, 116.5, 11, 11], [12, 7, 7, 15], color=LINE, linewidth=1.5, linestyle="--")
    arrow(ax, (11, 15), (11, 34), style="--")
    ax.text(62, 4.6, "재전송 retry: 다시 mq_send()", ha="center", fontsize=9.5, color=MUTED)

    ax.text(65, 51, f"시나리오 2 실측: 실패 콜백 {calls}회 · 재전송 성공 {recovered}회 · 한도 초과 포기 {gave_up}건 · Retry 대기함 넘침 {dropped}건",
            ha="center", fontsize=10.5, color=INK)
    save(fig, "diagram3_full_callback_retry.png")


def diagram4(rows):
    total = {r[1]: r for r in rows if r[0] == "CMP_TOTAL"}
    order = ["FIFO", "FIFO_BOOST", "PRIORITY"]
    labels = ["A. 단일 FIFO\nsingle FIFO", "B. FIFO + Emergency\nSender 태스크 우선순위↑", "C. 우선순위 큐\npriority queues"]
    e_avg = [float(total[k][4]) for k in order]
    e_low = [float(total[k][5]) for k in order]
    e_high = [float(total[k][6]) for k in order]
    n_avg = [float(total[k][8]) for k in order]

    fig, axes = plt.subplots(1, 2, figsize=(13, 5.6), dpi=200, sharey=True)
    fig.patch.set_facecolor(SURFACE)

    panels = [(axes[0], e_avg, EMERGENCY, "Emergency 평균 대기 tick (낮을수록 좋음)"),
              (axes[1], n_avg, NORMAL, "Normal 평균 대기 tick")]
    for ax, values, color, heading in panels:
        ax.set_facecolor(SURFACE)
        bars = ax.bar(range(3), values, width=0.42, color=color, zorder=3)
        ax.set_xticks(range(3), labels, fontsize=10, color=INK)
        ax.set_title(heading, fontsize=12.5, color=INK, loc="left", pad=12)
        ax.yaxis.grid(True, color=GRID, linewidth=0.8, zorder=0)
        ax.set_axisbelow(True)
        ax.tick_params(axis="both", length=0, labelcolor=MUTED)
        for side in ("top", "right", "left"):
            ax.spines[side].set_visible(False)
        ax.spines["bottom"].set_color(LINE)
        for bar, value in zip(bars, values):
            ax.text(bar.get_x() + bar.get_width() / 2, value + 18, f"{value:.1f}", ha="center", fontsize=11.5, color=INK)

    axes[0].errorbar(range(3), e_avg, yerr=[[a - l for a, l in zip(e_avg, e_low)], [h - a for a, h in zip(e_avg, e_high)]],
                     fmt="none", ecolor=INK, elinewidth=1, capsize=4, zorder=4)
    axes[0].set_ylabel("tick (1 tick = 1 ms)", fontsize=10, color=MUTED)
    axes[0].set_ylim(0, max(max(e_avg), max(n_avg)) * 1.14)

    fig.suptitle("그림 4. 비교 실험 실측값 (방식별 5회 합산, 같은 부하, 총 용량 32칸)   Figure 4. Measured comparison",
                 fontsize=13.5, color=INK, x=0.01, ha="left")
    fig.text(0.01, 0.005, "오차 막대: 회차별 평균의 최솟값~최댓값. Emergency 메시지는 방식마다 39개. A와 B는 이번 실험에서 소수점까지 같았다.",
             fontsize=9.5, color=MUTED)
    fig.tight_layout(rect=(0, 0.03, 1, 0.95))
    save(fig, "diagram4_compare_result.png")


if __name__ == "__main__":
    rows = read_csv()
    diagram1()
    diagram2()
    diagram3(rows)
    diagram4(rows)
