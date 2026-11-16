//
// IntervalList.h - edge(弧長 s)沿いの区間分割プリミティブ。
//
// TrackEdge(src/world/alignment/TrackGraph.h)の内部を弧長方向に複数区間へ分割し、
// 区間ごとに値(プラグインセット/速度制限/閉塞名など)を持たせるための共通データ構造。
//
// このクラスは edge の全長(終端 s)を保持しない。呼び出し側(TrackEdge)が長さを持つ
// ため、各区間の終端は「次の区間の開始 s」(最後の区間だけは呼び出し側が持つ終端)
// で暗黙に決まる。IntervalList 自身は終端に依存せずに動作する。
//
// T は operator== を要求する(隣接区間の値が等しいかどうかの判定に使う)。
//
// 境界はユーザーが置いたものとして保持し、前後の値が同じでも自動では消さない
// (「境界を先に置いてから片側を編集する」という編集手順を成立させるため)。
// したがって境界は必ずしも値の変化点ではない。値の変化点が欲しい場合は
// NextChangeAfter を使う。冗長な境界をまとめたい場合だけ Compact を明示的に呼ぶ。
//
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

namespace rs::world {

// 区間幅の最小値[m]。これ未満の幅の区間は作らない、または前の区間へ吸収する。
inline constexpr double kMinSpanLen = 1e-3;

template <class T>
class IntervalList {
public:
    struct Span {
        double sStart = 0.0;  // この区間の開始弧長[m]。終端は「次の Span の sStart」
        T value{};
        bool operator==(const Span&) const = default;
    };

    bool operator==(const IntervalList& other) const { return spans_ == other.spans_; }

    IntervalList() = default;

    explicit IntervalList(T initial) {
        spans_ = { Span{ 0.0, std::move(initial) } };
    }

    static IntervalList<T> FromSpans(std::vector<Span> spans) {
        if (spans.empty() || spans.front().sStart != 0.0) {
            return IntervalList<T>();
        }
        for (std::size_t i = 0; i < spans.size(); ++i) {
            if (!std::isfinite(spans[i].sStart) || spans[i].sStart < 0.0) {
                return IntervalList<T>();
            }
            if (i > 0 && spans[i].sStart <= spans[i - 1].sStart) {
                return IntervalList<T>();
            }
        }
        IntervalList<T> result;
        result.spans_ = std::move(spans);
        return result;
    }

    const std::vector<Span>& Spans() const {
        return spans_;
    }

    std::size_t SpanCount() const {
        return spans_.size();
    }

    // sStart <= s を満たす最後の Span の value を返す(始点包含)。
    // s が先頭より前ならば先頭の value を返す。
    const T& ValueAt(double s) const {
        // sStart > s となる最初の要素の1つ手前が求める Span。
        typename std::vector<Span>::const_iterator it = std::upper_bound(
            spans_.begin(), spans_.end(), s,
            [](double value, const Span& span) { return value < span.sStart; });
        if (it == spans_.begin()) {
            return spans_.front().value;
        }
        --it;
        return it->value;
    }

    // s に境界を1本挿入する。新規に境界を作った場合は true。
    // 前後の値は同じままなので、この直後に片側を SetRange で書き換える使い方を想定。
    bool SplitAt(double s) {
        if (s <= 0.0) {
            return false;
        }
        // 既存境界との差が kMinSpanLen 未満ならスナップ扱いで何もしない。
        for (const Span& span : spans_) {
            if (std::abs(span.sStart - s) < kMinSpanLen) {
                return false;
            }
        }
        T valueAtS = ValueAt(s);
        typename std::vector<Span>::iterator insertPos = std::upper_bound(
            spans_.begin(), spans_.end(), s,
            [](double value, const Span& span) { return value < span.sStart; });
        spans_.insert(insertPos, Span{ s, std::move(valueAtS) });
        Normalize();
        return true;
    }

    // 隣接する同値の区間をマージして冗長な境界をまとめる(明示呼び出し専用)。
    // 通常の編集操作では呼ばない(ユーザーが置いた境界を勝手に消さないため)。
    // 呼び出し元は .rs2 書き出し(3層の境界の和集合で RailWay へ展開する
    // 際に冗長な RailWay を作らないため)。
    void Compact() {
        std::vector<Span> merged;
        merged.reserve(spans_.size());
        merged.push_back(spans_.front());
        for (std::size_t i = 1; i < spans_.size(); ++i) {
            if (spans_[i].value == merged.back().value) {
                continue;
            }
            merged.push_back(spans_[i]);
        }
        spans_ = std::move(merged);
    }

    // [s0, s1) の範囲を value で上書きする。
    void SetRange(double s0, double s1, const T& value) {
        if (s0 < 0.0) {
            s0 = 0.0;
        }
        if (s1 - s0 < kMinSpanLen) {
            return;
        }

        // (a) s1 より後ろに続く値を先に保存しておく。
        const T tail = ValueAt(s1);

        // (b) sStart が [s0, s1) の範囲にある既存 Span を全削除。
        typename std::vector<Span>::iterator removeBegin = std::lower_bound(
            spans_.begin(), spans_.end(), s0,
            [](const Span& span, double value) { return span.sStart < value; });
        typename std::vector<Span>::iterator removeEnd = std::lower_bound(
            spans_.begin(), spans_.end(), s1,
            [](const Span& span, double value) { return span.sStart < value; });
        spans_.erase(removeBegin, removeEnd);

        // (c) {s0, value} を挿入。
        typename std::vector<Span>::iterator insertPos = std::upper_bound(
            spans_.begin(), spans_.end(), s0,
            [](double v, const Span& span) { return v < span.sStart; });
        spans_.insert(insertPos, Span{ s0, value });

        // (d) {s1, tail} を挿入(既存境界と kMinSpanLen 未満で一致するなら挿入しない)。
        bool s1CoincidesWithBoundary = false;
        for (const Span& span : spans_) {
            if (std::abs(span.sStart - s1) < kMinSpanLen) {
                s1CoincidesWithBoundary = true;
                break;
            }
        }
        if (!s1CoincidesWithBoundary) {
            typename std::vector<Span>::iterator tailPos = std::upper_bound(
                spans_.begin(), spans_.end(), s1,
                [](double v, const Span& span) { return v < span.sStart; });
            spans_.insert(tailPos, Span{ s1, tail });
        }

        // (e) 不変条件を強制。
        Normalize();
    }

    // index番目の区間の値だけを差し替える(境界は一切動かさない)。
    // 「各区間の値を個別に書き換えたい」場合に使う。SetRangeで代用しようとすると、
    // 最後の区間だけは終端sが境界として登録されていない(IntervalListはedge全長を
    // 持っていない)ため、s1に全長を渡した時点で余分な境界が生えてしまう。
    void SetSpanValue(std::size_t index, const T& value) {
        if (index >= spans_.size()) {
            return;
        }
        spans_[index].value = value;
    }

    // index番目の境界(=spans_[index].sStart)を newS へ動かす(値は動かさない、
    // SetSpanValueと対になる操作)。index==0は先頭(常に0.0で境界ではない)なので
    // 動かせず false。両隣の境界との間に kMinSpanLen 以上の余裕を残すよう
    // クランプし、クランプ後の位置が元と kMinSpanLen 未満しか違わなければ
    // 何もせず false を返す(no-op)。index が範囲外なら false。
    bool SetSpanStart(std::size_t index, double newS) {
        if (index == 0 || index >= spans_.size()) {
            return false;
        }
        const double lowerBound = spans_[index - 1].sStart + kMinSpanLen;
        double upperBound = std::numeric_limits<double>::infinity();
        if (index + 1 < spans_.size()) {
            upperBound = spans_[index + 1].sStart - kMinSpanLen;
        }
        if (upperBound < lowerBound) {
            // 両隣の間隔が既にkMinSpanLen未満(通常起こり得ないが安全側で拒否)。
            return false;
        }
        const double clamped = std::clamp(newS, lowerBound, upperBound);
        if (std::abs(clamped - spans_[index].sStart) < kMinSpanLen) {
            return false;
        }
        spans_[index].sStart = clamped;
        return true;
    }

    // 全区間を value 単一区間に置き換える。
    void SetAll(const T& value) {
        spans_ = { Span{ 0.0, value } };
    }

    // s より後ろにある最小の境界(sStart)を返す。無ければ nullopt。
    // SplitAt で明示的に入れた境界は前後で値が同じことがあるため、「次に値が
    // 変わる点」が欲しい場合は NextChangeAfter を使うこと。
    std::optional<double> NextBoundaryAfter(double s) const {
        typename std::vector<Span>::const_iterator it = std::upper_bound(
            spans_.begin(), spans_.end(), s,
            [](double value, const Span& span) { return value < span.sStart; });
        if (it == spans_.end()) {
            return std::nullopt;
        }
        return it->sStart;
    }

    // s より後ろで、実際に値が変わる最初の境界を返す。無ければ nullopt。
    // 走行側の先読み(速度制限の低下点までの距離、閉塞への進入前チェック)は
    // これを使う。SplitAt が作った
    // 値の変わらない境界は読み飛ばす。
    std::optional<double> NextChangeAfter(double s) const {
        typename std::vector<Span>::const_iterator it = std::upper_bound(
            spans_.begin(), spans_.end(), s,
            [](double value, const Span& span) { return value < span.sStart; });
        for (; it != spans_.end(); ++it) {
            // 直前の区間(必ず存在する: it != begin)と値が違えばそこが変化点。
            typename std::vector<Span>::const_iterator prev = it - 1;
            if (!(it->value == prev->value)) {
                return it->sStart;
            }
        }
        return std::nullopt;
    }

    // 全 sStart を newLen / oldLen 倍にスケールする(edge長の変更に追従する用途)。
    void Rescale(double oldLen, double newLen) {
        if (oldLen <= 0.0 || newLen <= 0.0) {
            return;
        }
        const double ratio = newLen / oldLen;
        for (Span& span : spans_) {
            span.sStart *= ratio;
        }
        spans_.front().sStart = 0.0;
        Normalize();
    }

    // s で分割し、[0,s)を*thisに残したまま[s,末尾)を新しいIntervalListとして
    // 切り出す(全境界からsを減算し、先頭を0にする)。edge分割(SplitEdge)専用。境界はSplitAtと同じ
    // 規則でsへスナップする。sが全区間の外(末尾を超える)なら、後半は最後の値
    // 1区間として返す。
    IntervalList<T> SplitOff(double s) {
        SplitAt(s);
        std::size_t splitIndex = spans_.size();
        for (std::size_t i = 0; i < spans_.size(); ++i) {
            if (spans_[i].sStart >= s - kMinSpanLen) {
                splitIndex = i;
                break;
            }
        }
        IntervalList<T> back;
        if (splitIndex >= spans_.size()) {
            back.spans_ = { Span{ 0.0, spans_.back().value } };
            return back;
        }
        std::vector<Span> backSpans(spans_.begin() + static_cast<std::ptrdiff_t>(splitIndex),
                                    spans_.end());
        spans_.erase(spans_.begin() + static_cast<std::ptrdiff_t>(splitIndex), spans_.end());
        if (spans_.empty()) {
            spans_ = { Span{ 0.0, backSpans.front().value } };
        }
        Normalize();
        for (Span& span : backSpans) span.sStart -= s;
        back.spans_ = std::move(backSpans);
        back.Normalize();
        return back;
    }

    // s' = totalLen - s で全区間を反転したコピーを返す(edge結合(MergeEdge)で
    // 反転側のedgeを連結する前に使う専用関数)。
    IntervalList<T> ReversedCopy(double totalLen) const {
        IntervalList<T> result;
        std::vector<Span> reversedSpans;
        reversedSpans.reserve(spans_.size());
        for (std::size_t i = spans_.size(); i-- > 0;) {
            const double origEnd =
                (i + 1 < spans_.size()) ? spans_[i + 1].sStart : totalLen;
            reversedSpans.push_back(Span{ totalLen - origEnd, spans_[i].value });
        }
        if (!reversedSpans.empty()) reversedSpans.front().sStart = 0.0;
        result.spans_ = std::move(reversedSpans);
        result.Normalize();
        return result;
    }

    // s=lenOfThis の位置で tail(結合される後半)を連結する(MergeEdge専用)。tail の全境界に lenOfThis を
    // 加算して自分の末尾へ追加する。連結点の値が同じなら境界を作らず自動マージする
    // (tailの先頭Spanの値がthisの最終Spanの値と等しい場合、その境界だけ挿入しない)。
    void AppendFrom(const IntervalList<T>& tail, double lenOfThis) {
        if (tail.spans_.empty()) return;
        const bool skipFirst =
            !spans_.empty() && tail.spans_.front().value == spans_.back().value;
        const std::size_t startIdx = skipFirst ? 1 : 0;
        for (std::size_t i = startIdx; i < tail.spans_.size(); ++i) {
            Span appended = tail.spans_[i];
            appended.sStart += lenOfThis;
            spans_.push_back(appended);
        }
        Normalize();
    }

private:
    // 不変条件を守らせる:
    //   - spans_ は非空
    //   - spans_[0].sStart == 0.0
    //   - sStart は狭義単調増加、隣接差は kMinSpanLen 以上
    // 値に関する正規化(隣接同値マージ)はここでは行わない(Compact を参照)。
    void Normalize() {
        if (spans_.empty()) {
            spans_ = { Span{} };
            return;
        }
        spans_.front().sStart = 0.0;

        // 幅が kMinSpanLen 未満に潰れた区間を削除し、前の区間へ吸収する。
        std::vector<Span> widthFiltered;
        widthFiltered.reserve(spans_.size());
        widthFiltered.push_back(spans_.front());
        for (std::size_t i = 1; i < spans_.size(); ++i) {
            if (spans_[i].sStart - widthFiltered.back().sStart < kMinSpanLen) {
                // 前の区間へ吸収(前の値を維持し、この境界は捨てる)。
                continue;
            }
            widthFiltered.push_back(spans_[i]);
        }
        spans_ = std::move(widthFiltered);
    }

    std::vector<Span> spans_{ Span{} };
};

}  // namespace rs::world
