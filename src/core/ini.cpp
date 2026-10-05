#include "ini.h"

#include "fileutil.h"
#include "strutil.h"

namespace omm {

bool IniDocument::Load(const std::string& path) {
    std::string bytes;
    if (!fs::ReadAll(path, bytes)) return false;
    std::string text;
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        encoding_ = Encoding::Utf16LE;
        std::u16string u;
        u.reserve((bytes.size() - 2) / 2);
        for (size_t i = 2; i + 1 < bytes.size(); i += 2)
            u.push_back(static_cast<char16_t>(static_cast<unsigned char>(bytes[i]) |
                                              (static_cast<unsigned char>(bytes[i + 1]) << 8)));
        text = str::Utf16ToUtf8(u);
    } else if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
               static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        encoding_ = Encoding::Utf8Bom;
        text = bytes.substr(3);
    } else {
        encoding_ = Encoding::Ansi;
        text = bytes;
    }
    Parse(text);
    dirty_ = false;
    return true;
}

void IniDocument::Parse(const std::string& text) {
    lines_.clear();
    eol_ = (text.find("\r\n") != std::string::npos || text.find('\n') == std::string::npos) ? "\r\n" : "\n";
    trailingNewline_ = text.empty() || text.back() == '\n';
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string raw = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        lines_.push_back(ParseLine(raw));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
}

IniDocument::Line IniDocument::ParseLine(const std::string& raw) {
    Line l;
    l.raw = raw;
    std::string t = str::Trim(raw);
    if (t.empty()) {
        l.kind = Line::Blank;
    } else if (t[0] == ';' || t[0] == '#' || (t.size() > 1 && t[0] == '/' && t[1] == '/')) {
        l.kind = Line::Comment;
    } else if (t.front() == '[' && t.back() == ']') {
        l.kind = Line::Section;
        l.name = str::Trim(t.substr(1, t.size() - 2));
    } else {
        size_t eq = t.find('=');
        if (eq == std::string::npos) {
            l.kind = Line::Other;
        } else {
            l.kind = Line::KeyValue;
            l.name = str::Trim(t.substr(0, eq));
            l.value = t.substr(eq + 1);  // keep value verbatim (UE3 is whitespace sensitive in structs)
        }
    }
    return l;
}

std::string IniDocument::ToString() const {
    std::string out;
    for (size_t i = 0; i < lines_.size(); ++i) {
        out += lines_[i].raw;
        if (i + 1 < lines_.size() || trailingNewline_) out += eol_;
    }
    return out;
}

bool IniDocument::Save(const std::string& path) const {
    std::string text = ToString();
    std::string bytes;
    switch (encoding_) {
        case Encoding::Utf16LE: {
            std::u16string u = str::Utf8ToUtf16(text);
            bytes.reserve(2 + u.size() * 2);
            bytes.push_back(static_cast<char>(0xFF));
            bytes.push_back(static_cast<char>(0xFE));
            for (char16_t c : u) {
                bytes.push_back(static_cast<char>(c & 0xFF));
                bytes.push_back(static_cast<char>((c >> 8) & 0xFF));
            }
            break;
        }
        case Encoding::Utf8Bom:
            bytes = "\xEF\xBB\xBF" + text;
            break;
        case Encoding::Ansi:
            bytes = text;
            break;
    }
    return fs::WriteAllAtomic(path, bytes);
}

int IniDocument::FindSection(const std::string& section) const {
    for (size_t i = 0; i < lines_.size(); ++i)
        if (lines_[i].kind == Line::Section && str::IEquals(lines_[i].name, section)) return static_cast<int>(i);
    return -1;
}

std::pair<int, int> IniDocument::SectionBody(int headerIndex) const {
    int start = headerIndex + 1;
    int end = start;
    while (end < static_cast<int>(lines_.size()) && lines_[end].kind != Line::Section) ++end;
    return {start, end};
}

int IniDocument::InsertionPoint(int headerIndex) const {
    auto [start, end] = SectionBody(headerIndex);
    // Insert after the last non-blank line of the section so the blank
    // separator before the next section is preserved.
    int ins = end;
    while (ins > start && lines_[ins - 1].kind == Line::Blank) --ins;
    return ins;
}

bool IniDocument::HasSection(const std::string& section) const { return FindSection(section) >= 0; }

std::vector<std::string> IniDocument::Sections() const {
    std::vector<std::string> out;
    for (const Line& l : lines_)
        if (l.kind == Line::Section) out.push_back(l.name);
    return out;
}

std::optional<std::string> IniDocument::Get(const std::string& section, const std::string& key) const {
    // UE3 merges duplicate section headers, so search all of them.
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].kind != Line::Section || !str::IEquals(lines_[i].name, section)) continue;
        auto [start, end] = SectionBody(static_cast<int>(i));
        for (int k = start; k < end; ++k)
            if (lines_[k].kind == Line::KeyValue && str::IEquals(lines_[k].name, key)) return lines_[k].value;
    }
    return std::nullopt;
}

std::vector<std::string> IniDocument::GetAll(const std::string& section, const std::string& key) const {
    std::vector<std::string> out;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].kind != Line::Section || !str::IEquals(lines_[i].name, section)) continue;
        auto [start, end] = SectionBody(static_cast<int>(i));
        for (int k = start; k < end; ++k)
            if (lines_[k].kind == Line::KeyValue && str::IEquals(lines_[k].name, key)) out.push_back(lines_[k].value);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> IniDocument::Entries(const std::string& section) const {
    std::vector<std::pair<std::string, std::string>> out;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].kind != Line::Section || !str::IEquals(lines_[i].name, section)) continue;
        auto [start, end] = SectionBody(static_cast<int>(i));
        for (int k = start; k < end; ++k)
            if (lines_[k].kind == Line::KeyValue) out.emplace_back(lines_[k].name, lines_[k].value);
    }
    return out;
}

bool IniDocument::Set(const std::string& section, const std::string& key, const std::string& value) {
    bool found = false;
    bool changed = false;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].kind != Line::Section || !str::IEquals(lines_[i].name, section)) continue;
        auto [start, end] = SectionBody(static_cast<int>(i));
        for (int k = start; k < end; ++k) {
            Line& l = lines_[k];
            if (l.kind != Line::KeyValue || !str::IEquals(l.name, key)) continue;
            found = true;
            if (l.value != value) {
                l.value = value;
                l.raw = l.name + "=" + value;
                changed = true;
            }
        }
    }
    if (!found) {
        int header = FindSection(section);
        Line nl;
        nl.kind = Line::KeyValue;
        nl.name = key;
        nl.value = value;
        nl.raw = key + "=" + value;
        if (header < 0) {
            if (!lines_.empty() && lines_.back().kind != Line::Blank) lines_.push_back(ParseLine(""));
            lines_.push_back(ParseLine("[" + section + "]"));
            lines_.push_back(nl);
            lines_.push_back(ParseLine(""));
        } else {
            lines_.insert(lines_.begin() + InsertionPoint(header), nl);
        }
        changed = true;
    }
    if (changed) dirty_ = true;
    return changed;
}

bool IniDocument::AddUnique(const std::string& section, const std::string& key, const std::string& value) {
    for (const std::string& v : GetAll(section, key))
        if (v == value) return false;
    int header = FindSection(section);
    Line nl;
    nl.kind = Line::KeyValue;
    nl.name = key;
    nl.value = value;
    nl.raw = key + "=" + value;
    if (header < 0) {
        if (!lines_.empty() && lines_.back().kind != Line::Blank) lines_.push_back(ParseLine(""));
        lines_.push_back(ParseLine("[" + section + "]"));
        lines_.push_back(nl);
        lines_.push_back(ParseLine(""));
    } else {
        lines_.insert(lines_.begin() + InsertionPoint(header), nl);
    }
    dirty_ = true;
    return true;
}

int IniDocument::Remove(const std::string& section, const std::string& key, const std::optional<std::string>& value) {
    int removed = 0;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].kind != Line::Section || !str::IEquals(lines_[i].name, section)) continue;
        auto [start, end] = SectionBody(static_cast<int>(i));
        for (int k = end - 1; k >= start; --k) {
            const Line& l = lines_[k];
            if (l.kind == Line::KeyValue && str::IEquals(l.name, key) && (!value || l.value == *value)) {
                lines_.erase(lines_.begin() + k);
                ++removed;
            }
        }
    }
    if (removed) dirty_ = true;
    return removed;
}

}  // namespace omm
