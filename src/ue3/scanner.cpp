#include "scanner.h"

#include "../core/strutil.h"
#include "types.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>

namespace omm::ue3 {

namespace {
constexpr size_t P = sizeof(uintptr_t);

uintptr_t AlignUp(uintptr_t v, uintptr_t a) { return (v + a - 1) & ~(a - 1); }

bool Printable(const char* s) {
    if (!s[0]) return false;
    for (int i = 0; s[i] && i < 64; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}
}  // namespace

std::string Layout::Describe() const {
    return str::Format(
        "FNameEntry{str=0x%X idx=%d shifted=%d} UObject{index=0x%X outer=0x%X name=0x%X class=0x%X size=0x%X} "
        "UStruct{next=0x%X super=0x%X children=0x%X} UProperty{dim=0x%X elem=0x%X flags=0x%X offset=0x%X size=0x%X} "
        "UFunction{flags=0x%X native=0x%X parms=0x%X ret=0x%X func=0x%X}",
        nameString, nameIndexField, nameIndexShifted ? 1 : 0, objIndex, objOuter, objName, objClass, objSize, fieldNext,
        structSuper, structChildren, propArrayDim, propElementSize, propFlags, propOffset, propSize, funcFlags, funcNative,
        funcParmsSize, funcRetOffset, funcFunc);
}

Scanner::Scanner(const mem::Oracle& oracle, std::vector<mem::Range> dataRanges, std::function<bool(uintptr_t)> isCode)
    : oracle_(oracle), ranges_(std::move(dataRanges)), isCode_(std::move(isCode)) {}

void Scanner::Note(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    r_.notes.emplace_back(buf);
}

// ---------------------------------------------------------------------------
// Raw reads (always validated through the oracle).

bool Scanner::ReadPtr(uintptr_t addr, uintptr_t& out) const {
    if (!oracle_.Readable(addr, P)) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(addr), P);
    return true;
}
bool Scanner::ReadI32(uintptr_t addr, int32_t& out) const {
    if (!oracle_.Readable(addr, 4)) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(addr), 4);
    return true;
}
bool Scanner::ReadU16(uintptr_t addr, uint16_t& out) const {
    if (!oracle_.Readable(addr, 2)) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(addr), 2);
    return true;
}
bool Scanner::ReadU32(uintptr_t addr, uint32_t& out) const {
    if (!oracle_.Readable(addr, 4)) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(addr), 4);
    return true;
}
bool Scanner::ReadU64(uintptr_t addr, uint64_t& out) const {
    if (!oracle_.Readable(addr, 8)) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(addr), 8);
    return true;
}

bool Scanner::MatchStr(uintptr_t addr, const char* s) const {
    size_t n = std::strlen(s) + 1;
    if (!oracle_.Readable(addr, n)) return false;
    return std::memcmp(reinterpret_cast<const void*>(addr), s, n) == 0;
}

// ---------------------------------------------------------------------------
// Name table.

int32_t Scanner::NamesNum() const {
    int32_t n = 0;
    ReadI32(r_.gnames + P, n);
    return n;
}

uintptr_t Scanner::NameEntry(int32_t idx) const {
    if (idx < 0 || idx >= NamesNum()) return 0;
    uintptr_t data = 0, e = 0;
    if (!ReadPtr(r_.gnames, data) || !ReadPtr(data + static_cast<uintptr_t>(idx) * P, e)) return 0;
    return e;
}

std::string Scanner::NameAt(int32_t idx) const {
    uintptr_t e = NameEntry(idx);
    if (!e) return std::string();
    uintptr_t s = e + static_cast<uintptr_t>(r_.layout.nameString);
    bool wide = false;
    if (r_.layout.nameIndexField >= 0 && r_.layout.nameIndexShifted) {
        int32_t v = 0;
        if (ReadI32(e + static_cast<uintptr_t>(r_.layout.nameIndexField), v)) wide = (v & 1) != 0;
    }
    std::string out;
    if (wide) {
        for (int i = 0; i < 1024; ++i) {
            uint16_t c = 0;
            if (!ReadU16(s + static_cast<uintptr_t>(i) * 2, c) || c == 0) break;
            out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    } else {
        for (int i = 0; i < 1024; ++i) {
            if (!oracle_.Readable(s + static_cast<uintptr_t>(i), 1)) break;
            char c = *reinterpret_cast<const char*>(s + static_cast<uintptr_t>(i));
            if (!c) break;
            out.push_back(c);
        }
    }
    return out;
}

int32_t Scanner::FindNameIndex(const char* s) const {
    int32_t n = NamesNum();
    uintptr_t data = 0;
    if (!ReadPtr(r_.gnames, data)) return -1;
    size_t len = std::strlen(s) + 1;
    for (int32_t i = 0; i < n; ++i) {
        uintptr_t e = 0;
        if (!ReadPtr(data + static_cast<uintptr_t>(i) * P, e) || !e) continue;
        uintptr_t str = e + static_cast<uintptr_t>(r_.layout.nameString);
        if (!oracle_.Readable(str, len)) continue;
        if (std::memcmp(reinterpret_cast<const void*>(str), s, len) == 0) return i;
    }
    return -1;
}

bool Scanner::FindNames() {
    for (const mem::Range& rg : ranges_) {
        for (uintptr_t p = AlignUp(rg.begin, P); p + P + 8 <= rg.end; p += P) {
            if (!oracle_.Readable(p, P + 8)) {
                p = AlignUp(p + 1, 0x1000) - P;  // skip the unreadable page
                continue;
            }
            uintptr_t data;
            int32_t num, max;
            std::memcpy(&data, reinterpret_cast<const void*>(p), P);
            std::memcpy(&num, reinterpret_cast<const void*>(p + P), 4);
            std::memcpy(&max, reinterpret_cast<const void*>(p + P + 4), 4);
            if (num < 1000 || num > 8000000 || max < num || max > 64000000) continue;
            if (!data || (data % P) != 0) continue;
            if (!oracle_.Readable(data, 3 * P)) continue;
            uintptr_t e[3];
            std::memcpy(e, reinterpret_cast<const void*>(data), 3 * P);
            if (!e[0] || !e[1] || !e[2]) continue;
            for (int off = 0; off <= 0x40; off += 2) {
                if (MatchStr(e[0] + off, "None") && MatchStr(e[1] + off, "ByteProperty") &&
                    MatchStr(e[2] + off, "IntProperty")) {
                    r_.gnames = p;
                    r_.layout.nameString = off;
                    Note("GNames at %p (%d names), string offset 0x%X", reinterpret_cast<void*>(p), num, off);
                    // FNameEntry::Index lets us spot UTF-16 names.
                    for (int f = 0; f + 4 <= off; f += 4) {
                        int plain = 0, shifted = 0, tested = 0;
                        for (int32_t i = 1; i < 64 && i < num; ++i) {
                            uintptr_t ei = 0;
                            int32_t v = 0;
                            if (!ReadPtr(data + static_cast<uintptr_t>(i) * P, ei) || !ei || !ReadI32(ei + f, v)) continue;
                            ++tested;
                            if (v == i) ++plain;
                            if (v == (i << 1)) ++shifted;
                        }
                        if (tested >= 20 && shifted == tested) {
                            r_.layout.nameIndexField = f;
                            r_.layout.nameIndexShifted = true;
                            break;
                        }
                        if (tested >= 20 && plain == tested) {
                            r_.layout.nameIndexField = f;
                            r_.layout.nameIndexShifted = false;
                            break;
                        }
                    }
                    return true;
                }
            }
        }
    }
    Note("GNames not found");
    return false;
}

// ---------------------------------------------------------------------------
// Object table.

int32_t Scanner::ObjectsNum() const {
    int32_t n = 0;
    ReadI32(r_.gobjects + P, n);
    return n;
}

uintptr_t Scanner::ObjectAt(int32_t i) const {
    uintptr_t data = 0, o = 0;
    if (!ReadPtr(r_.gobjects, data)) return 0;
    if (!ReadPtr(data + static_cast<uintptr_t>(i) * P, o)) return 0;
    return o;
}

bool Scanner::IsObject(uintptr_t p) const {
    if (!p || (p % P) != 0) return false;
    int32_t idx = -1;
    if (!ReadI32(p + static_cast<uintptr_t>(r_.layout.objIndex), idx)) return false;
    if (idx < 0 || idx >= ObjectsNum()) return false;
    return ObjectAt(idx) == p;
}

bool Scanner::FindObjects() {
    const int preferred = P == 8 ? 0x38 : 0x20;
    for (const mem::Range& rg : ranges_) {
        for (uintptr_t p = AlignUp(rg.begin, P); p + P + 8 <= rg.end; p += P) {
            if (p == r_.gnames) continue;
            if (!oracle_.Readable(p, P + 8)) {
                p = AlignUp(p + 1, 0x1000) - P;
                continue;
            }
            uintptr_t data;
            int32_t num, max;
            std::memcpy(&data, reinterpret_cast<const void*>(p), P);
            std::memcpy(&num, reinterpret_cast<const void*>(p + P), 4);
            std::memcpy(&max, reinterpret_cast<const void*>(p + P + 4), 4);
            if (num < 5000 || num > 16000000 || max < num || max > 64000000) continue;
            if (!data || (data % P) != 0) continue;
            int32_t probe = std::min<int32_t>(num, 4000);
            if (!oracle_.Readable(data, static_cast<size_t>(probe) * P)) continue;

            std::vector<int> offsets{preferred};
            for (int off = static_cast<int>(P); off < 0x80; off += 4)
                if (off != preferred) offsets.push_back(off);
            for (int off : offsets) {
                int tested = 0, ok = 0;
                for (int32_t i = 0; i < probe && tested < 400; ++i) {
                    uintptr_t o;
                    std::memcpy(&o, reinterpret_cast<const void*>(data + static_cast<uintptr_t>(i) * P), P);
                    if (!o) continue;
                    ++tested;
                    int32_t v = -1;
                    if (ReadI32(o + off, v) && v == i) ++ok;
                }
                if (tested >= 200 && ok * 100 >= tested * 95) {
                    r_.gobjects = p;
                    r_.layout.objIndex = off;
                    Note("GObjects at %p (%d objects), UObject::Index at 0x%X", reinterpret_cast<void*>(p), num, off);
                    return true;
                }
                if (tested < 200) break;  // not an object table at all
            }
        }
    }
    Note("GObjects not found");
    return false;
}

std::string Scanner::ObjName(uintptr_t obj) const { return NameAt(ObjNameIndex(obj)); }

int32_t Scanner::ObjNameIndex(uintptr_t obj) const {
    int32_t idx = -1;
    ReadI32(obj + static_cast<uintptr_t>(r_.layout.objName), idx);
    return idx;
}

uintptr_t Scanner::ClassOf(uintptr_t obj) const {
    uintptr_t c = 0;
    ReadPtr(obj + static_cast<uintptr_t>(r_.layout.objClass), c);
    return c;
}

uintptr_t Scanner::OuterOf(uintptr_t obj) const {
    uintptr_t o = 0;
    ReadPtr(obj + static_cast<uintptr_t>(r_.layout.objOuter), o);
    return o;
}

uintptr_t Scanner::FindObject(const char* name, const char* className, const char* outerName) const {
    int32_t nameIdx = FindNameIndex(name);
    if (nameIdx < 0) return 0;
    int32_t n = ObjectsNum();
    for (int32_t i = 0; i < n; ++i) {
        uintptr_t o = ObjectAt(i);
        if (!o || ObjNameIndex(o) != nameIdx) continue;
        if (className && ObjName(ClassOf(o)) != className) continue;
        if (outerName) {
            uintptr_t outer = OuterOf(o);
            if (!outer || ObjName(outer) != outerName) continue;
        }
        return o;
    }
    return 0;
}

bool Scanner::DetectObjectLayout() {
    // Sample objects spread over the whole table.
    sample_.clear();
    int32_t n = ObjectsNum();
    int32_t stride = std::max<int32_t>(1, n / 6000);
    for (int32_t i = 0; i < n && sample_.size() < 6000; i += stride) {
        uintptr_t o = ObjectAt(i);
        if (o && oracle_.Readable(o, 0x80)) sample_.push_back(o);
    }
    if (sample_.size() < 100) {
        Note("Too few objects to analyse (%zu)", sample_.size());
        return false;
    }
    const int32_t namesNum = NamesNum();
    const double total = static_cast<double>(sample_.size());

    // --- Name ---------------------------------------------------------------
    auto nameScore = [&](int off) {
        int good = 0;
        for (uintptr_t o : sample_) {
            int32_t idx = -1, num = -1, objIdx = -2;
            if (!ReadI32(o + off, idx) || !ReadI32(o + off + 4, num)) continue;
            ReadI32(o + r_.layout.objIndex, objIdx);
            if (idx < 0 || idx >= namesNum || num < 0 || num > 1000000 || idx == objIdx) continue;
            uintptr_t e = NameEntry(idx);
            char buf[8] = {};
            if (!e || !oracle_.Readable(e + r_.layout.nameString, 1)) continue;
            std::memcpy(buf, reinterpret_cast<const void*>(e + r_.layout.nameString), 1);
            if (Printable(buf)) ++good;
        }
        return good / total;
    };
    {
        const int preferred = P == 8 ? 0x48 : 0x2C;
        int best = -1;
        double bestScore = 0;
        if (nameScore(preferred) >= 0.97) {
            best = preferred;
        } else {
            for (int off = static_cast<int>(P); off < 0x80; off += 4) {
                if (off == r_.layout.objIndex) continue;
                double s = nameScore(off);
                if (s > bestScore) {
                    bestScore = s;
                    best = off;
                }
            }
            if (bestScore < 0.97) best = -1;
        }
        if (best < 0) {
            Note("UObject::Name not identified");
            return false;
        }
        r_.layout.objName = best;
    }

    // --- Class: following it twice must reach the self-referencing "Class" --
    auto classScore = [&](int off) {
        int good = 0;
        for (uintptr_t o : sample_) {
            uintptr_t c = 0, cc = 0, ccc = 0;
            if (!ReadPtr(o + off, c) || !IsObject(c)) continue;
            if (!ReadPtr(c + off, cc) || !IsObject(cc)) continue;
            if (!ReadPtr(cc + off, ccc) || ccc != cc) continue;
            ++good;
        }
        return good / total;
    };
    {
        const int preferred = P == 8 ? 0x50 : 0x34;
        int best = -1;
        double bestScore = 0;
        if (classScore(preferred) >= 0.97) {
            best = preferred;
        } else {
            for (int off = static_cast<int>(P); off < 0x80; off += static_cast<int>(P)) {
                double s = classScore(off);
                if (s > bestScore) {
                    bestScore = s;
                    best = off;
                }
            }
            if (bestScore < 0.97) best = -1;
        }
        if (best < 0) {
            Note("UObject::Class not identified");
            return false;
        }
        r_.layout.objClass = best;
    }

    // --- Outer: the outermost object of everything is a Package -------------
    const int32_t packageNameIdx = FindNameIndex("Package");
    auto outerScore = [&](int off, double& nonNullFrac) {
        int good = 0, nonNull = 0;
        for (uintptr_t o : sample_) {
            uintptr_t cur = o, next = 0;
            bool bad = false;
            int steps = 0;
            if (ReadPtr(o + off, next) && next) ++nonNull;
            while (true) {
                if (!ReadPtr(cur + off, next)) {
                    bad = true;
                    break;
                }
                if (!next) break;
                if (!IsObject(next) || ++steps > 32) {
                    bad = true;
                    break;
                }
                cur = next;
            }
            if (bad) continue;
            if (ObjNameIndex(ClassOf(cur)) == packageNameIdx) ++good;
        }
        nonNullFrac = nonNull / total;
        return good / total;
    };
    {
        const int preferred = P == 8 ? 0x40 : 0x28;
        double nn = 0;
        int best = -1;
        double bestScore = 0;
        if (outerScore(preferred, nn) >= 0.95 && nn >= 0.5) {
            best = preferred;
        } else {
            for (int off = static_cast<int>(P); off < 0x80; off += static_cast<int>(P)) {
                if (off == r_.layout.objClass) continue;
                double s = outerScore(off, nn);
                if (s > bestScore && nn >= 0.5) {
                    bestScore = s;
                    best = off;
                }
            }
            if (bestScore < 0.95) best = -1;
        }
        if (best < 0) {
            Note("UObject::Outer not identified");
            return false;
        }
        r_.layout.objOuter = best;
    }

    objectClass_ = FindObject("Object", "Class", "Core");
    if (!objectClass_) {
        Note("Class Core.Object not found - layout rejected");
        return false;
    }
    classClass_ = ClassOf(objectClass_);
    if (ClassOf(classClass_) != classClass_) {
        Note("Class Core.Class is not self-referencing - layout rejected");
        return false;
    }
    Note("UObject layout: Index 0x%X, Outer 0x%X, Name 0x%X, Class 0x%X", r_.layout.objIndex, r_.layout.objOuter,
         r_.layout.objName, r_.layout.objClass);
    return true;
}

// ---------------------------------------------------------------------------
// UField / UStruct.

std::vector<uintptr_t> Scanner::ChildrenOf(uintptr_t structObj) const {
    std::vector<uintptr_t> out;
    uintptr_t cur = 0;
    if (!ReadPtr(structObj + r_.layout.structChildren, cur)) return out;
    while (cur && out.size() < 20000) {
        if (!IsObject(cur)) break;
        out.push_back(cur);
        if (!ReadPtr(cur + r_.layout.fieldNext, cur)) break;
    }
    return out;
}

uintptr_t Scanner::FindChild(uintptr_t structObj, const char* name) const {
    for (uintptr_t c : ChildrenOf(structObj))
        if (ObjName(c) == name) return c;
    return 0;
}

bool Scanner::DetectStructLayout() {
    actorClass_ = FindObject("Actor", "Class", "Engine");
    pawnClass_ = FindObject("Pawn", "Class", "Engine");
    controllerClass_ = FindObject("Controller", "Class", "Engine");
    if (!actorClass_ || !pawnClass_ || !controllerClass_) {
        Note("Engine.Actor/Pawn/Controller classes not loaded yet");
        return false;
    }
    int base = std::max({r_.layout.objIndex + 4, r_.layout.objOuter + static_cast<int>(P), r_.layout.objName + 8,
                         r_.layout.objClass + static_cast<int>(P)});
    base = static_cast<int>(AlignUp(static_cast<uintptr_t>(base), P));

    for (int off = base; off < base + 0x100; off += static_cast<int>(P)) {
        uintptr_t a = 0, b = 0, c = 0;
        if (ReadPtr(actorClass_ + off, a) && ReadPtr(pawnClass_ + off, b) && ReadPtr(controllerClass_ + off, c) &&
            a == objectClass_ && b == actorClass_ && c == actorClass_) {
            r_.layout.structSuper = off;
            break;
        }
    }
    if (r_.layout.structSuper < 0) {
        Note("UStruct::SuperField not identified");
        return false;
    }
    for (int off = r_.layout.structSuper + static_cast<int>(P); off <= r_.layout.structSuper + 0x40;
         off += static_cast<int>(P)) {
        uintptr_t a = 0, b = 0;
        if (ReadPtr(actorClass_ + off, a) && ReadPtr(pawnClass_ + off, b) && IsObject(a) && IsObject(b) &&
            OuterOf(a) == actorClass_ && OuterOf(b) == pawnClass_) {
            r_.layout.structChildren = off;
            break;
        }
    }
    if (r_.layout.structChildren < 0) {
        Note("UStruct::Children not identified");
        return false;
    }
    uintptr_t first = 0;
    ReadPtr(actorClass_ + r_.layout.structChildren, first);
    int best = -1;
    size_t bestLen = 0;
    for (int off = base; off < r_.layout.structSuper; off += static_cast<int>(P)) {
        size_t len = 0;
        uintptr_t cur = first;
        bool ok = true;
        while (cur && len < 5000) {
            if (!IsObject(cur) || OuterOf(cur) != actorClass_) {
                ok = false;
                break;
            }
            if (!ReadPtr(cur + off, cur)) {
                ok = false;
                break;
            }
            ++len;
        }
        if (ok && cur == 0 && len > bestLen) {
            bestLen = len;
            best = off;
        }
    }
    if (best < 0 || bestLen < 10) {
        Note("UField::Next not identified");
        return false;
    }
    r_.layout.fieldNext = best;
    r_.layout.objSize = best;
    Note("UStruct layout: Next 0x%X, SuperField 0x%X, Children 0x%X (Actor has %zu fields)", r_.layout.fieldNext,
         r_.layout.structSuper, r_.layout.structChildren, bestLen);
    return true;
}

// ---------------------------------------------------------------------------
// UProperty.

bool Scanner::DetectPropertyLayout() {
    uintptr_t loc = FindChild(actorClass_, "Location");
    uintptr_t rot = FindChild(actorClass_, "Rotation");
    uintptr_t ds = FindChild(actorClass_, "DrawScale");
    uintptr_t ds3 = FindChild(actorClass_, "DrawScale3D");
    uintptr_t pp = FindChild(actorClass_, "PrePivot");
    if (!loc || !rot || !ds || !ds3 || !pp) {
        Note("Actor transform properties not found");
        return false;
    }
    const int lo = r_.layout.fieldNext + static_cast<int>(P);
    for (int off = lo; off < lo + 0x100; off += 4) {
        int32_t vl, vr, vd, v3, vp;
        if (!ReadI32(loc + off, vl) || !ReadI32(rot + off, vr) || !ReadI32(ds + off, vd) || !ReadI32(ds3 + off, v3) ||
            !ReadI32(pp + off, vp))
            continue;
        if (vl > 0 && vl < 0x10000 && vr == vl + 12 && vd == vr + 12 && v3 == vd + 4 && vp == v3 + 12) {
            r_.layout.propOffset = off;
            break;
        }
    }
    if (r_.layout.propOffset < 0) {
        Note("UProperty::Offset not identified");
        return false;
    }
    for (int off = lo - static_cast<int>(P) + 4; off < r_.layout.propOffset; off += 4) {
        int32_t a, b, c, d;
        if (ReadI32(loc + off, a) && ReadI32(rot + off, b) && ReadI32(ds + off, c) && ReadI32(ds3 + off, d) && a == 12 &&
            b == 12 && c == 4 && d == 12) {
            r_.layout.propElementSize = off;
            break;
        }
    }
    if (r_.layout.propElementSize < 0) {
        Note("UProperty::ElementSize not identified");
        return false;
    }
    auto dimOk = [&](int off) {
        int32_t a, b, c, d;
        return ReadI32(loc + off, a) && ReadI32(rot + off, b) && ReadI32(ds + off, c) && ReadI32(ds3 + off, d) && a == 1 &&
               b == 1 && c == 1 && d == 1;
    };
    if (dimOk(r_.layout.propElementSize - 4)) {
        r_.layout.propArrayDim = r_.layout.propElementSize - 4;
    } else {
        for (int off = lo - static_cast<int>(P) + 4; off < r_.layout.propElementSize; off += 4)
            if (dimOk(off)) {
                r_.layout.propArrayDim = off;
                break;
            }
    }
    if (r_.layout.propArrayDim < 0) {
        Note("UProperty::ArrayDim not identified");
        return false;
    }

    // Property flags, using Actor.SetLocation(NewLocation) -> bool.
    uintptr_t setLoc = FindChild(actorClass_, "SetLocation");
    uintptr_t newLoc = setLoc ? FindChild(setLoc, "NewLocation") : 0;
    uintptr_t retVal = setLoc ? FindChild(setLoc, "ReturnValue") : 0;
    if (newLoc && retVal) {
        for (int off = r_.layout.propElementSize + 4; off < r_.layout.propOffset; off += 4) {
            uint64_t fn = 0, fr = 0, fl = 0;
            if (!ReadU64(newLoc + off, fn) || !ReadU64(retVal + off, fr) || !ReadU64(loc + off, fl)) continue;
            if ((fn & CPF_Parm) && !(fn & CPF_ReturnParm) && (fr & (CPF_Parm | CPF_ReturnParm)) == (CPF_Parm | CPF_ReturnParm) &&
                !(fl & CPF_Parm)) {
                r_.layout.propFlags = off;
                break;
            }
        }
    }
    if (r_.layout.propFlags < 0) Note("UProperty::PropertyFlags not identified (non-fatal)");

    // sizeof(UProperty): the PropertyClass of object properties follows it.
    uintptr_t owner = FindChild(actorClass_, "Owner");
    uintptr_t pawnCtrl = FindChild(pawnClass_, "Controller");
    uintptr_t ctrlPawn = FindChild(controllerClass_, "Pawn");
    if (!owner || !pawnCtrl || !ctrlPawn) {
        Note("Object properties for UProperty size detection not found");
        return false;
    }
    int start = static_cast<int>(AlignUp(static_cast<uintptr_t>(r_.layout.propOffset + 4), P));
    for (int off = start; off < start + 0x100; off += static_cast<int>(P)) {
        uintptr_t a = 0, b = 0, c = 0;
        if (ReadPtr(owner + off, a) && ReadPtr(pawnCtrl + off, b) && ReadPtr(ctrlPawn + off, c) && a == actorClass_ &&
            b == controllerClass_ && c == pawnClass_) {
            r_.layout.propSize = off;
            break;
        }
    }
    if (r_.layout.propSize < 0) {
        Note("sizeof(UProperty) not identified");
        return false;
    }
    r_.layout.objPropClass = r_.layout.propSize;
    r_.layout.structPropStruct = r_.layout.propSize;
    r_.layout.arrayPropInner = r_.layout.propSize;
    r_.layout.bytePropEnum = r_.layout.propSize;
    r_.layout.boolBitMask = r_.layout.propSize;
    r_.layout.classPropMeta = r_.layout.propSize + static_cast<int>(P);

    // Cross-checks (informational).
    uintptr_t vecStruct = 0;
    ReadPtr(loc + r_.layout.structPropStruct, vecStruct);
    if (!IsObject(vecStruct) || ObjName(vecStruct) != "Vector") Note("Warning: StructProperty::Struct check failed");
    int bools = 0, boolsOk = 0;
    for (uintptr_t c : ChildrenOf(actorClass_)) {
        if (ObjName(ClassOf(c)) != "BoolProperty") continue;
        ++bools;
        uint32_t m = 0;
        if (ReadU32(c + r_.layout.boolBitMask, m) && m && (m & (m - 1)) == 0) ++boolsOk;
    }
    if (bools && boolsOk != bools) Note("Warning: BoolProperty::BitMask check %d/%d", boolsOk, bools);
    Note("UProperty layout: ArrayDim 0x%X, ElementSize 0x%X, Flags 0x%X, Offset 0x%X, size 0x%X", r_.layout.propArrayDim,
         r_.layout.propElementSize, r_.layout.propFlags, r_.layout.propOffset, r_.layout.propSize);
    return true;
}

// ---------------------------------------------------------------------------
// UFunction.

bool Scanner::DetectFunctionLayout() {
    r_.functionsPending = false;
    uintptr_t functionClass = FindObject("Function", "Class", "Core");
    if (!functionClass) {
        Note("Class Core.Function not found");
        return false;
    }
    std::vector<uintptr_t> funcs;
    int32_t n = ObjectsNum();
    for (int32_t i = 0; i < n && funcs.size() < 60000; ++i) {
        uintptr_t o = ObjectAt(i);
        if (o && ClassOf(o) == functionClass) funcs.push_back(o);
    }
    if (funcs.size() < 50) {
        Note("Too few UFunctions (%zu)", funcs.size());
        r_.functionsPending = true;
        return false;
    }

    uintptr_t setLoc = FindChild(actorClass_, "SetLocation");
    uintptr_t destroy = FindChild(actorClass_, "Destroy");
    uintptr_t spawn = FindChild(actorClass_, "Spawn");
    uintptr_t postBegin = FindChild(actorClass_, "PostBeginPlay");
    const int ptr = static_cast<int>(P);
    // UFunction's own members follow the (large) UStruct part.
    const int lo = r_.layout.structChildren + ptr;
    const int hi = r_.layout.structChildren + 0x300;

    // 1. iNative: SetLocation is native(267), Destroy native(279), Spawn has
    //    none. Two exact 16-bit values make this match unambiguous.
    auto nativeOk = [&](int off) {
        uint16_t a = 0, b = 0, c = 1;
        return setLoc && destroy && spawn && ReadU16(setLoc + off, a) && ReadU16(destroy + off, b) &&
               ReadU16(spawn + off, c) && a == 267 && b == 279 && c == 0;
    };
    int nativeOff = -1;
    for (int off = lo; off < hi && nativeOff < 0; off += 2)
        if (nativeOk(off)) nativeOff = off;
    r_.layout.funcNative = nativeOff;

    // 2. FunctionFlags - serialized with the function, so valid as soon as it
    //    is loaded. SetLocation/Destroy/Spawn are native, PostBeginPlay is a
    //    script event; every function with a native index must be FUNC_Native.
    auto flagsOk = [&](int off) {
        uint32_t a = 0, b = 0, c = 0, d = 0;
        return setLoc && spawn && postBegin && destroy && ReadU32(setLoc + off, a) && ReadU32(spawn + off, b) &&
               ReadU32(postBegin + off, c) && ReadU32(destroy + off, d) &&
               (a & (FUNC_Final | FUNC_Native)) == (FUNC_Final | FUNC_Native) && (b & FUNC_Native) &&
               (d & FUNC_Native) && !(c & FUNC_Native) && (c & FUNC_Event);
    };
    auto consistent = [&](int off) {
        if (nativeOff < 0) return true;
        size_t withIndex = 0, nativeFlag = 0;
        for (uintptr_t f : funcs) {
            uint16_t idx = 0;
            uint32_t fl = 0;
            if (!ReadU16(f + nativeOff, idx) || !idx || !ReadU32(f + off, fl)) continue;
            ++withIndex;
            if (fl & FUNC_Native) ++nativeFlag;
        }
        return withIndex == 0 || nativeFlag * 100 >= withIndex * 95;
    };
    int flagsOff = -1;
    if (nativeOff >= 4 && flagsOk(nativeOff - 4) && consistent(nativeOff - 4)) flagsOff = nativeOff - 4;
    for (int off = lo; off < hi && flagsOff < 0; off += 4)
        if (flagsOk(off) && consistent(off)) flagsOff = off;
    r_.layout.funcFlags = flagsOff;

    // 3. ParmsSize / ReturnValueOffset from SetLocation: (vector NewLocation) -> bool.
    uintptr_t retVal = setLoc ? FindChild(setLoc, "ReturnValue") : 0;
    int32_t retOff = -1;
    if (retVal) ReadI32(retVal + r_.layout.propOffset, retOff);
    if (retOff > 0) {
        auto pr = [&](int off) {
            uint16_t ps = 0, ro = 0;
            return ReadU16(setLoc + off, ps) && ReadU16(setLoc + off + 2, ro) && ro == retOff && ps == retOff + 4;
        };
        int from = flagsOff >= 0 ? flagsOff + 4 : lo;
        for (int off = from; off < from + 0x40 && r_.layout.funcParmsSize < 0; off += 2)
            if (pr(off)) {
                r_.layout.funcParmsSize = off;
                r_.layout.funcRetOffset = off + 2;
            }
    }

    // 4. Func. Every script (non-native) function points it at the same
    //    UObject::ProcessInternal; natives point at their own exec thunks.
    //    Natives are the majority in some games (Core alone has hundreds of
    //    native operators), so only non-native functions vote when the flags
    //    are known.
    std::vector<uintptr_t> script, natives;
    for (uintptr_t f : funcs) {
        uint32_t fl = 0;
        if (flagsOff >= 0 && ReadU32(f + flagsOff, fl)) (fl & FUNC_Native ? natives : script).push_back(f);
    }
    const std::vector<uintptr_t>& voters = (flagsOff >= 0 && script.size() >= 20) ? script : funcs;
    struct Candidate {
        int off = -1;
        uintptr_t value = 0;
        size_t count = 0, nonNull = 0;
        bool code = false;
    };
    std::vector<Candidate> cands;
    int start = flagsOff >= 0 ? flagsOff + 4 : lo;
    start = (start + ptr - 1) & ~(ptr - 1);
    for (int off = start; off < hi; off += ptr) {
        std::unordered_map<uintptr_t, size_t> freq;
        Candidate c;
        c.off = off;
        for (uintptr_t f : voters) {
            uintptr_t v = 0;
            if (ReadPtr(f + off, v) && v) {
                ++freq[v];
                ++c.nonNull;
            }
        }
        for (const auto& kv : freq)
            if (kv.second > c.count) {
                c.count = kv.second;
                c.value = kv.first;
            }
        if (c.count) {
            c.code = isCode_(c.value);
            cands.push_back(c);
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) { return a.count > b.count; });
    const Candidate* best = nullptr;
    for (const Candidate& c : cands) {
        if (!c.code) continue;
        bool enough = &voters == &script ? c.count * 10 >= voters.size() * 6 && c.count >= 20
                                         : c.count * 100 >= voters.size() * 15 && c.count >= 50;
        if (!enough) continue;
        // Natives must point somewhere else (each at its own thunk).
        if (!natives.empty()) {
            size_t own = 0;
            for (uintptr_t f : natives) {
                uintptr_t v = 0;
                if (ReadPtr(f + c.off, v) && v && v != c.value) ++own;
            }
            if (own * 2 < natives.size()) continue;
        }
        best = &c;
        break;
    }
    if (!best) {
        Note("UFunction::Func not identified (%zu functions, %zu script / %zu native, flags at 0x%X)", funcs.size(),
             script.size(), natives.size(), flagsOff);
        for (size_t i = 0; i < cands.size() && i < 4; ++i)
            Note("  candidate 0x%X: value %p shared by %zu of %zu (%zu set), %s", cands[i].off,
                 reinterpret_cast<void*>(cands[i].value), cands[i].count, voters.size(), cands[i].nonNull,
                 cands[i].code ? "code" : "not code");
        // Script functions get their code pointer when the class is linked,
        // which can be after the scan; ask the caller to retry.
        r_.functionsPending = true;
        return false;
    }
    r_.layout.funcFunc = best->off;
    r_.processInternal = best->value;
    Note("UFunction::Func at 0x%X, ProcessInternal=%p (%zu/%zu script functions)", best->off,
         reinterpret_cast<void*>(best->value), best->count, voters.size());
    if (r_.layout.funcNative < 0) Note("UFunction::iNative not identified (non-fatal)");
    if (r_.layout.funcFlags < 0) Note("UFunction::FunctionFlags not identified (non-fatal)");
    Note("UFunction layout: Flags 0x%X, iNative 0x%X, ParmsSize 0x%X, ReturnValueOffset 0x%X", r_.layout.funcFlags,
         r_.layout.funcNative, r_.layout.funcParmsSize, r_.layout.funcRetOffset);
    return true;
}

bool Scanner::RunAll() {
    if (!FindNames()) return false;
    if (!FindObjects()) return false;
    if (!DetectObjectLayout()) return false;
    if (!DetectStructLayout()) return false;
    if (!DetectPropertyLayout()) return false;
    DetectFunctionLayout();  // optional
    return true;
}

}  // namespace omm::ue3
