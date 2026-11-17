"""ヘッダごとに「変えたら何個の .cpp が再コンパイルになるか」を数える(差分ビルドを重くしているヘッダを探すため)。

#include "..." をたどって、.cpp ごとに読むプロジェクト内のヘッダを集める。<...> の標準・外部ヘッダは数えない。
マクロで切り替わる #include も、全部読むものとして数える(多めに見積もる)。

例: python tools/IncludeFanout.py                         読む .cpp の数が多いヘッダの上位40件
    python tools/IncludeFanout.py --churn 2026-09-17      変更回数 × 読む .cpp の数(その日以降の git log)で並べる
    python tools/IncludeFanout.py --via TrackGraph.h      そのヘッダを直接読んでいるヘッダを、読む .cpp の数で並べる
    python tools/IncludeFanout.py --hubs                  読む .cpp の数 × 中で読むヘッダの数 が大きい中継ヘッダ
"""
import argparse
import collections
import os
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN_DIRS = ("src", "tests")
INCLUDE_RX = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


def Norm(path: str) -> str:
    return os.path.normpath(path).replace("\\", "/")


def Rel(path: str) -> str:
    return os.path.relpath(path, ROOT).replace("\\", "/")


def CollectFiles() -> list:
    files = []
    for d in SCAN_DIRS:
        for dirPath, _, names in os.walk(os.path.join(ROOT, d)):
            for name in names:
                if name.endswith((".h", ".hpp", ".inl", ".cpp")):
                    files.append(Norm(os.path.join(dirPath, name)))
    return files


def BuildIncludeMap(files: list) -> dict:
    byName = collections.defaultdict(list)
    for f in files:
        byName[os.path.basename(f)].append(f)
    includes = {}
    for f in files:
        with open(f, encoding="utf-8", errors="replace") as fp:
            text = fp.read()
        found = []
        for spec in INCLUDE_RX.findall(text):
            # 自分の場所からの相対 → src/ からの相対 → 名前が一意ならその名前、の順に解決する
            candidates = [Norm(os.path.join(os.path.dirname(f), spec)), Norm(os.path.join(ROOT, "src", spec))]
            hit = next((c for c in candidates if os.path.exists(c)), None)
            if hit is None and len(byName.get(os.path.basename(spec), [])) == 1:
                hit = byName[os.path.basename(spec)][0]
            if hit is not None:
                found.append(hit)
        includes[f] = found
    return includes


class Fanout:
    def __init__(self, includes: dict):
        self.includes = includes
        self.closures = {}
        self.users = collections.Counter()
        cpps = [f for f in includes if f.endswith(".cpp")]
        self.cppCount = len(cpps)
        total = 0
        for cpp in cpps:
            closure = self.Closure(cpp)
            total += len(closure)
            for h in closure:
                self.users[h] += 1
        self.avgHeaders = total / max(1, len(cpps))

    def Closure(self, f: str) -> set:
        # 再帰の代わりに明示スタックで集める(循環 include でも止まる)
        if f in self.closures:
            return self.closures[f]
        seen = set()
        stack = list(self.includes.get(f, []))
        while stack:
            g = stack.pop()
            if g in seen:
                continue
            seen.add(g)
            stack.extend(self.includes.get(g, []))
        self.closures[f] = seen
        return seen


def Churn(since: str) -> collections.Counter:
    out = subprocess.run(["git", "log", f"--since={since}", "--name-only", "--format=", "--", "src"],
                         cwd=ROOT, capture_output=True, text=True, encoding="utf-8").stdout
    return collections.Counter(ln.strip() for ln in out.splitlines() if ln.strip())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--churn", metavar="SINCE", default="")
    ap.add_argument("--via", nargs="+", default=[])
    ap.add_argument("--hubs", action="store_true")
    a = ap.parse_args()

    fan = Fanout(BuildIncludeMap(CollectFiles()))
    print(f".cpp {fan.cppCount} 個 / .cpp 1個あたりのプロジェクト内ヘッダ 平均 {fan.avgHeaders:.1f} 個")
    headers = [h for h in fan.users if not h.endswith(".cpp")]

    if a.via:
        for name in a.via:
            targets = [h for h in headers if h.endswith("/" + name)]
            if not targets:
                print(f"{name}: 見つからない")
                continue
            target = targets[0]
            print(f"== {Rel(target)}: {fan.users[target]} 個の .cpp が読む。直接読んでいるヘッダ:")
            readers = [h for h in headers if target in fan.includes.get(h, [])]
            for h in sorted(readers, key=lambda h: -fan.users[h])[:a.top]:
                print(f"  {fan.users[h]:5d}  {Rel(h)}")
        return 0

    if a.churn:
        churn = Churn(a.churn)
        rows = sorted(((churn[Rel(h)] * fan.users[h], churn[Rel(h)], fan.users[h], h) for h in headers
                       if churn[Rel(h)]), reverse=True)
        print(f"再コンパイルの延べ数(変更回数 × 読む .cpp の数、{a.churn} 以降): {sum(r[0] for r in rows)}")
        print("  延べ数  変更  読む.cpp  ヘッダ")
        for cost, n, users, h in rows[:a.top]:
            print(f"  {cost:6d}  {n:4d}  {users:7d}  {Rel(h)}")
        return 0

    if a.hubs:
        rows = sorted(((fan.users[h] * len(fan.Closure(h)), h) for h in headers), reverse=True)
        print("  読む.cpp  中で読むヘッダ  ヘッダ")
        for _, h in rows[:a.top]:
            print(f"  {fan.users[h]:7d}  {len(fan.Closure(h)):12d}  {Rel(h)}")
        return 0

    print("  読む.cpp  ヘッダ")
    for h in sorted(headers, key=lambda h: -fan.users[h])[:a.top]:
        print(f"  {fan.users[h]:7d}  {Rel(h)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
