# ============================================================
# Atom 跳绳小挑战 · 后端参考实现（FastAPI + Redis，单文件可跑）
#
# 与 code/backend-spec.md 一一对应（2026-08-17 评审会及会后决议口径）：
#   · 三榜：今日单轮最高(日榜, ZADD GT + TTL) / 1'/2' 历史最佳 / 连胜
#   · 上榜口径 = 单轮最高（当日累计不上榜，防刷）
#   · 全员默认参与，无退出项；同分按显示名首字母（读侧二次排序）
#   · 显示名脱敏：昵称 ≤10 截断；未填 → 邮箱/手机前缀 + ***
#   · 一次 session = 一条 mini-workout 记录（对齐 oneset）；按天聚合展示
#   · 校验：空轮拒收 / free ms ≤ 15min、timed ms ≤ testMin / >8 跳每秒剔除 /
#           单日封顶 3000（超出照常入档但不再上榜/给星）
#   · 账号铁律：所有 key 带 {edition} 分区（cn/intl 永不合并）
#
# 运行：
#   pip install -r requirements.txt
#   redis-server &                       # 或 REDIS_URL 指到现有实例
#   uvicorn app:app --reload
# 冒烟：
#   python app.py smoke                  # 内置 fakeredis 冒烟自测（无需真 Redis）
# ============================================================
from __future__ import annotations

import hashlib
import json
import os
import time
from datetime import datetime, timedelta, timezone
from typing import Literal, Optional

from fastapi import Depends, FastAPI, Header, HTTPException
from pydantic import BaseModel, Field

import redis

# ---------------- 配置 ----------------
REDIS_URL = os.environ.get("REDIS_URL", "redis://localhost:6379/0")
FREE_CAP_MS = 15 * 60_000          # 自由跳单轮上限（端上执行，服务端复验）
DAILY_CAP = 3000                   # 单日封顶（防刷）：超出照常入档，不再上榜/给星
MAX_JPS = 8.0                      # 频率异常剔除阈值（识别标称 ≤3 跳/秒，>8 即可疑）
BOARD_TZ = timezone(timedelta(hours=8))   # 日榜滚动时区（按部署区配置）

app = FastAPI(title="jump-rope-backend-ref", version="0.9")
_r: Optional[redis.Redis] = None


def rdb() -> redis.Redis:
    global _r
    if _r is None:
        _r = redis.Redis.from_url(REDIS_URL, decode_responses=True)
    return _r


# ---------------- Key 布局（{edition} 分区，跨区永不合并） ----------------
def k_profile(ed: str, uid: str) -> str: return f"{ed}:jump:profile:{uid}"
def k_log(ed: str, uid: str) -> str: return f"{ed}:jump:log:{uid}"            # session 明细(List)
def k_round(ed: str, date: str) -> str: return f"{ed}:jump:round:{date}"      # 今日单轮最高(日榜)
def k_timed(ed: str, minute: int) -> str: return f"{ed}:jump:timed{minute}:best"
def k_streak(ed: str) -> str: return f"{ed}:jump:streak"
def k_name(ed: str) -> str: return f"{ed}:jump:names"                          # uid -> display_name


def board_key(ed: str, kind: str, date: str) -> str:
    if kind == "round":
        return k_round(ed, date)
    if kind == "timed1":
        return k_timed(ed, 1)
    if kind == "timed2":
        return k_timed(ed, 2)
    if kind == "streak":
        return k_streak(ed)
    raise HTTPException(400, "unknown board kind")


def today(ed_now: Optional[datetime] = None) -> str:
    return (ed_now or datetime.now(BOARD_TZ)).strftime("%Y-%m-%d")


# ---------------- 显示名（脱敏，评审会定稿） ----------------
def display_name(nickname: str | None, email: str | None, phone: str | None) -> str:
    if nickname:
        return nickname[:10]                       # ≤10 字符，超出截断
    src = email or phone or "star"
    return src[:3].lower() + "***"                 # 街机三字母风；不露域名/手机后段


# ---------------- 请求模型 ----------------
class SessionEnd(BaseModel):
    user_id: str
    count: int = Field(gt=0, description="空轮(0)端上已拦截，服务端同样拒收")
    ms: int = Field(gt=0)
    mode: Literal["free", "timed"]
    test_min: int = 1                       # timed 时 1/2
    goal: int = 0
    kcal: float = 0
    new_best: bool = False
    started_at: Optional[int] = None        # epoch ms；跨零点按 session 开始时间归天
    stars_earned: int = 0


class FormHint(BaseModel):
    user_id: str
    kind: Literal["alternating", "single_foot", "low_jump", "low_light"]
    session_ts: Optional[int] = None


class DailyCard(BaseModel):
    user_id: str
    date: str                               # YYYY-MM-DD
    png_b64: str                            # 端上渲染的日卡 PNG（自动同步 App 云相册）


class NameSet(BaseModel):
    user_id: str
    nickname: Optional[str] = None
    email: Optional[str] = None
    phone: Optional[str] = None


def edition(x_account_edition: str = Header(default="cn")) -> str:
    if x_account_edition not in ("cn", "intl"):
        raise HTTPException(400, "edition must be cn|intl")   # 双区隔离，按邮箱查询必带 edition
    return x_account_edition


# ---------------- 结算（收到 session_end；= backend-spec §7.1） ----------------
@app.post("/v1/jump/session_end")
def session_end(ev: SessionEnd, ed: str = Depends(edition)):
    r = rdb()
    # 复验（端上先行，服务端兜底）
    if ev.mode == "free" and ev.ms > FREE_CAP_MS:
        raise HTTPException(422, "free session exceeds 15-min cap")
    if ev.mode == "timed" and ev.ms > ev.test_min * 60_000 + 2_000:
        raise HTTPException(422, "timed session exceeds duration")
    if ev.count / (ev.ms / 1000) > MAX_JPS:
        raise HTTPException(422, "jump rate anomaly")          # 影子封禁另行处理

    started = ev.started_at or int(time.time() * 1000)
    date = today(datetime.fromtimestamp(started / 1000, BOARD_TZ))   # 跨零点归开始日

    pk = k_profile(ed, ev.user_id)
    p = json.loads(r.get(pk) or "{}")
    day = p.setdefault("history", {}).setdefault(
        date, {"count": 0, "best": 0, "n": 0, "ms": 0, "done": False})

    capped = day["count"] >= DAILY_CAP                          # 封顶后：入档，不上榜/不给星
    day["count"] += ev.count
    day["n"] += 1                                               # 轮数：仅数据，不上屏
    day["best"] = max(day["best"], ev.count)                    # 今日单轮最高（榜单口径）
    day["ms"] += ev.ms
    p["total"] = p.get("total", 0) + ev.count
    p["best_session"] = max(p.get("best_session", 0), ev.count)
    if ev.mode == "timed":
        bk = f"best_{ev.test_min}min"
        p[bk] = max(p.get(bk, 0), ev.count)
    if not capped:
        p["stars"] = p.get("stars", 0) + ev.stars_earned

    # 打卡与连胜（服务端时间为准；设备改时间无效）
    goal_num = p.get("goal_num", 100)
    if not day["done"] and (day["count"] >= goal_num or day["count"] >= 50):
        day["done"] = True
        yesterday = (datetime.now(BOARD_TZ) - timedelta(days=1)).strftime("%Y-%m-%d")
        p["streak"] = p.get("streak", 0) + 1 if p.get("last_done") == yesterday else 1
        p["last_done"] = date
        p["max_streak"] = max(p.get("max_streak", 0), p["streak"])
        r.zadd(k_streak(ed), {ev.user_id: p["streak"]})

    r.set(pk, json.dumps(p))
    # session 明细：一条 mini-workout 记录（对齐 oneset；命名 profile_log_jump_rope_*）
    r.rpush(k_log(ed, ev.user_id), json.dumps({
        "ts": started, "date": date, "mode": ev.mode, "test_min": ev.test_min,
        "count": ev.count, "ms": ev.ms, "kcal": round(ev.kcal, 1), "goal": ev.goal}))

    if not capped:
        # 三榜写入：ZADD GT = 只升不降（天然"取单轮最高"）
        rk = k_round(ed, date)
        r.zadd(rk, {ev.user_id: ev.count}, gt=True)
        r.expire(rk, 48 * 3600)                                # TTL 覆盖时区跨日 = 每日 0 点重置
        if ev.mode == "timed":
            r.zadd(k_timed(ed, ev.test_min), {ev.user_id: ev.count}, gt=True)

    return {"date": date, "day": day, "streak": p.get("streak", 0),
            "capped": capped, "total": p["total"]}


# ---------------- 榜单读（Top3 + 我 + 上一名；= backend-spec §7.2） ----------------
@app.get("/v1/jump/board/{kind}")
def board_view(kind: Literal["round", "timed1", "timed2", "streak"],
               user_id: str, ed: str = Depends(edition)):
    r = rdb()
    key = board_key(ed, kind, today())
    names = r.hgetall(k_name(ed))

    def disp(uid: str) -> str:
        return names.get(uid) or display_name(None, uid, None)

    raw = r.zrevrange(key, 0, -1, withscores=True)
    # 同分并列：按显示名首字母升序（读侧二次排序）
    rows = sorted(raw, key=lambda t: (-t[1], disp(t[0])))
    idx = next((i for i, (uid, _) in enumerate(rows) if uid == user_id), None)
    me_score = int(r.zscore(key, user_id) or 0)
    top3 = [{"rank": i + 1, "name": disp(uid), "score": int(s)}
            for i, (uid, s) in enumerate(rows[:3])]
    above = rows[idx - 1] if idx else None
    return {
        "kind": kind, "top3": top3,
        "me": {"rank": (idx + 1) if idx is not None else len(rows) + 1, "score": me_score},
        "chase": ({"name": disp(above[0]), "gap": int(above[1]) - me_score + 1}
                  if above else None),                 # 供"再跳 N 下超过 XX"
    }


# ---------------- 显示名登记（账号服务同步昵称/邮箱时调用） ----------------
@app.post("/v1/jump/display_name")
def set_name(req: NameSet, ed: str = Depends(edition)):
    dn = display_name(req.nickname, req.email, req.phone)
    rdb().hset(k_name(ed), req.user_id, dn)
    return {"display_name": dn}


# ---------------- 纠错事件（规则调优数据；≥20s 频控在端上） ----------------
@app.post("/v1/jump/form_hint")
def form_hint(ev: FormHint, ed: str = Depends(edition)):
    rdb().rpush(f"{ed}:jump:form_hints", json.dumps(
        {"uid": ev.user_id, "kind": ev.kind, "ts": ev.session_ts or int(time.time() * 1000)}))
    return {"ok": True}


# ---------------- 日卡上传（自动同步手机 App 云相册；同日按内容 hash 去重） ----------------
@app.post("/v1/jump/daily_card")
def daily_card(card: DailyCard, ed: str = Depends(edition)):
    h = hashlib.sha256(card.png_b64.encode()).hexdigest()[:16]
    dedup_key = f"{ed}:jump:card:{card.user_id}:{card.date}"
    if rdb().get(dedup_key) == h:
        return {"ok": True, "dedup": True}
    rdb().set(dedup_key, h, ex=7 * 24 * 3600)
    # TODO 工程接力：png 落对象存储 → 调 App 云相册写入接口（App 侧提供）
    return {"ok": True, "dedup": False, "asset": f"jump_card_{card.date}_{h}.png"}


# ---------------- 连胜提醒（每分钟扫描到点用户；= backend-spec §7.3，通道待定） ----------------
def push_streak_reminders(now_hhmm: str):
    r = rdb()
    for ed in ("cn", "intl"):
        for pk in r.scan_iter(f"{ed}:jump:profile:*"):
            p = json.loads(r.get(pk) or "{}")
            if p.get("remind") != now_hhmm:
                continue
            if p.get("last_done") != today():
                uid = pk.rsplit(":", 1)[1]
                print(f"[push] {ed}/{uid} 连胜 {p.get('streak', 0)} 天保卫战")  # TODO 接推送通道


# ---------------- 冒烟自测（fakeredis，无需真实例）----------------
def smoke():
    import fakeredis
    global _r
    _r = fakeredis.FakeRedis(decode_responses=True)
    ed = "cn"
    set_name(NameSet(user_id="u1", nickname="小明同学一二三四五六七"), ed)   # 截断
    set_name(NameSet(user_id="u2", email="alexlim2022@example.com"), ed)      # 脱敏
    for uid, cnt in (("u1", 96), ("u1", 130), ("u2", 130)):                   # 单轮口径 + 同分
        session_end(SessionEnd(user_id=uid, count=cnt, ms=cnt * 500,
                               mode="free", stars_earned=cnt // 20 * 2), ed)
    session_end(SessionEnd(user_id="u2", count=118, ms=60_000, mode="timed",
                           test_min=1, new_best=True), ed)
    b = board_view("round", "u1", ed)
    assert b["top3"][0]["score"] == 130 and b["me"]["score"] == 130
    assert b["top3"][0]["name"].endswith("***") or len(b["top3"][0]["name"]) <= 10
    t = board_view("timed1", "u2", ed)
    assert t["me"]["score"] == 118
    try:
        session_end(SessionEnd(user_id="u1", count=500, ms=20_000, mode="free"), ed)
        raise AssertionError("rate anomaly not caught")
    except HTTPException as e:
        assert e.status_code == 422
    print("smoke OK:", json.dumps(b, ensure_ascii=False))


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1 and sys.argv[1] == "smoke":
        smoke()
    else:
        import uvicorn
        uvicorn.run(app, host="0.0.0.0", port=8000)
