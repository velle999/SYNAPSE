"""Market data — velle.ai's quant engine, ported to the standard library.

⛔ THE CHART ENDPOINT, NOT THE QUOTE ENDPOINT. velle.ai asks
`v7/finance/quote` first, which since 2023 needs a crumb fetched from one host
and replayed with the cookie from another — three requests, two hosts, and a
handshake that breaks whenever Yahoo changes it. Its own code already falls
back to `v8/finance/chart`, which needs none of that and carries the same last
price, previous close and the candles the indicators need. So the fallback is
the path here, and the handshake is not ported at all: an authentication dance
that exists to be worked around is not a feature to reimplement.

⛔ THIS IS THE ONE PART OF THE ASSISTANT THAT LEAVES THE MACHINE, and the model
must ask before using it. Every other tool reads or writes locally; this one
tells a company in California which ticker somebody asked about. That is why
`market_quote` is on the confirmation list even though it writes nothing — the
line for a network call is not "does it write" but "does it leave".

⚠ Indicators are computed on CLOSES from the same response, so a number and the
chart it came from can never disagree.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request

CHART = "https://query2.finance.yahoo.com/v8/finance/chart/{sym}"
UA = "Mozilla/5.0 (X11; Linux x86_64) vibe/quant"
TIMEOUT = 12


class QuantError(RuntimeError):
    pass


def _fetch(symbol: str, rng: str = "6mo", interval: str = "1d") -> dict:
    url = (f"{CHART.format(sym=urllib.parse.quote(symbol))}"
           f"?range={rng}&interval={interval}&includePrePost=false")
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
            body = json.loads(r.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        # ⚠ A BAD TICKER IS A 404, and it is the ordinary mistake here — it
        # gets a sentence rather than a stack trace, because the model reads
        # this string and a traceback teaches it nothing.
        if e.code == 404:
            raise QuantError(f"No such symbol: {symbol}") from None
        raise QuantError(f"Yahoo returned HTTP {e.code}") from None
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        raise QuantError(f"Could not reach Yahoo: {e}") from None
    except json.JSONDecodeError:
        raise QuantError("Yahoo sent something that is not JSON") from None

    chart = (body or {}).get("chart") or {}
    if chart.get("error"):
        raise QuantError(str(chart["error"].get("description") or chart["error"]))
    results = chart.get("result") or []
    if not results:
        raise QuantError(f"No data for {symbol}")
    return results[0]


def _series(result: dict) -> dict:
    q = ((result.get("indicators") or {}).get("quote") or [{}])[0]
    def col(name):
        return [x for x in (q.get(name) or []) if x is not None]
    return {"close": col("close"), "high": col("high"), "low": col("low"),
            "volume": col("volume")}


# ── Indicators ──────────────────────────────────────────────────────────────

def sma(values: list[float], period: int) -> float | None:
    if len(values) < period:
        return None
    return sum(values[-period:]) / period


def ema(values: list[float], period: int) -> float | None:
    """⚠ Seeded on the first `period` values as a simple average, then wound
    forward — the same seeding velle.ai uses, so the numbers match."""
    if len(values) < period:
        return None
    k = 2 / (period + 1)
    out = sum(values[:period]) / period
    for v in values[period:]:
        out = v * k + out * (1 - k)
    return out


def rsi(closes: list[float], period: int = 14) -> float | None:
    if len(closes) < period + 1:
        return None
    gains = losses = 0.0
    for a, b in zip(closes[1:period + 1], closes[:period]):
        d = a - b
        gains += max(d, 0.0)
        losses += max(-d, 0.0)
    avg_g, avg_l = gains / period, losses / period
    for a, b in zip(closes[period + 1:], closes[period:]):
        d = a - b
        avg_g = (avg_g * (period - 1) + max(d, 0.0)) / period
        avg_l = (avg_l * (period - 1) + max(-d, 0.0)) / period
    if avg_l == 0:
        return 100.0
    rs = avg_g / avg_l
    return round(100 - 100 / (1 + rs), 1)


def macd(closes: list[float], fast: int = 12, slow: int = 26,
         signal: int = 9) -> dict | None:
    if len(closes) < slow + signal:
        return None
    line = []
    for i in range(slow, len(closes) + 1):
        window = closes[:i]
        f, s = ema(window, fast), ema(window, slow)
        if f is None or s is None:
            continue
        line.append(f - s)
    if len(line) < signal:
        return None
    sig = ema(line, signal)
    return {"macd": round(line[-1], 3), "signal": round(sig, 3),
            "histogram": round(line[-1] - sig, 3)}


def bollinger(closes: list[float], period: int = 20, sd: float = 2.0) -> dict | None:
    if len(closes) < period:
        return None
    window = closes[-period:]
    mid = sum(window) / period
    var = sum((x - mid) ** 2 for x in window) / period
    dev = var ** 0.5
    return {"upper": round(mid + sd * dev, 2), "middle": round(mid, 2),
            "lower": round(mid - sd * dev, 2)}


def atr(highs: list[float], lows: list[float], closes: list[float],
        period: int = 14) -> float | None:
    if min(len(highs), len(lows), len(closes)) < period + 1:
        return None
    trs = []
    for i in range(1, len(closes)):
        trs.append(max(highs[i] - lows[i],
                       abs(highs[i] - closes[i - 1]),
                       abs(lows[i] - closes[i - 1])))
    return round(sum(trs[-period:]) / period, 3)


# ── The answer ──────────────────────────────────────────────────────────────

def quote(symbol: str) -> dict:
    """Last price, the day's move, and the indicators, in one request."""
    return build(_fetch(symbol), symbol)


def build(result: dict, symbol: str = "") -> dict:
    """The numbers, from one chart response.

    ⚠ SPLIT FROM THE FETCH so the arithmetic can be tested without a network —
    the previous-close rule below was wrong once and no offline test could have
    seen it while the parse and the request were one function."""
    meta = result.get("meta") or {}
    s = _series(result)
    closes = s["close"]
    if not closes:
        raise QuantError(f"No prices for {symbol}")

    price = meta.get("regularMarketPrice") or closes[-1]

    # ⛔ NOT `chartPreviousClose`, WHICH IS THE CLOSE BEFORE THE RANGE. On the
    # six months this asks for, that is the price half a year ago — and using
    # it reported AAPL as +21% on the day, measured, because a number that
    # looks like a previous close and is one for a 1-day chart is a different
    # number entirely for a 6-month one. The day's move comes from the last two
    # daily closes, which are in this same response.
    # ⚠ `previousClose` (no `chart` prefix) IS yesterday's when Yahoo sends it,
    # and it is often absent from this endpoint — so it is preferred and not
    # relied on.
    prev = meta.get("previousClose")
    if prev is None:
        prev = closes[-2] if len(closes) > 1 else price
    change = price - prev
    return {
        "symbol": meta.get("symbol", symbol.upper()),
        "currency": meta.get("currency", ""),
        "price": round(price, 2),
        "previous_close": round(prev, 2),
        "change": round(change, 2),
        "change_pct": round(change / prev * 100, 2) if prev else 0.0,
        "sma50": round(sma(closes, 50), 2) if sma(closes, 50) else None,
        "sma200": round(sma(closes, 200), 2) if sma(closes, 200) else None,
        "rsi14": rsi(closes),
        "macd": macd(closes),
        "bollinger": bollinger(closes),
        "atr14": atr(s["high"], s["low"], closes),
        "points": len(closes),
    }


def report(q: dict) -> str:
    """The quote as lines. ⚠ Plain text and no colour: this string is read by a
    terminal, by the chat window's tool pane, and by the model itself."""
    arrow = "+" if q["change"] >= 0 else ""
    cur = f" {q['currency']}" if q["currency"] else ""
    out = [f"{q['symbol']}  {q['price']}{cur}  "
           f"{arrow}{q['change']} ({arrow}{q['change_pct']}%)"]
    trend = []
    if q["sma50"]:
        trend.append(f"50d {q['sma50']}")
    if q["sma200"]:
        trend.append(f"200d {q['sma200']}")
    if q["rsi14"] is not None:
        trend.append(f"RSI {q['rsi14']}")
    if q["atr14"] is not None:
        trend.append(f"ATR {q['atr14']}")
    if trend:
        out.append("  " + "   ".join(trend))
    if q["macd"]:
        m = q["macd"]
        out.append(f"  MACD {m['macd']} signal {m['signal']} hist {m['histogram']}")
    if q["bollinger"]:
        b = q["bollinger"]
        out.append(f"  Bollinger {b['lower']} · {b['middle']} · {b['upper']}")
    return "\n".join(out)
