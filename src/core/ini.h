// Format-preserving INI document.
//
// Unreal Engine 3 config files contain duplicate keys (arrays such as
// "Bindings=(...)"), comments, odd section names ("[DebugLookZone
// MobileInputZone]") and CRLF line endings. Edits made by the mod must leave
// everything it does not touch byte-for-byte identical, so this class keeps
// the original lines and only rewrites the ones it changes.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace omm {

class IniDocument {
public:
    enum class Encoding { Ansi, Utf8Bom, Utf16LE };

    bool Load(const std::string& path);
    void Parse(const std::string& utf8Text);
    bool Save(const std::string& path) const;
    std::string ToString() const;  // UTF-8, original line endings

    bool HasSection(const std::string& section) const;
    std::vector<std::string> Sections() const;

    // First value for the key (case-insensitive section and key).
    std::optional<std::string> Get(const std::string& section, const std::string& key) const;
    std::vector<std::string> GetAll(const std::string& section, const std::string& key) const;
    std::vector<std::pair<std::string, std::string>> Entries(const std::string& section) const;

    // Sets every occurrence of the key; inserts it (creating the section if
    // needed) when absent. Returns true when the document changed.
    bool Set(const std::string& section, const std::string& key, const std::string& value);
    // Appends "key=value" unless an identical line already exists.
    bool AddUnique(const std::string& section, const std::string& key, const std::string& value);
    // Removes lines for the key; when value is given only exact matches go.
    int Remove(const std::string& section, const std::string& key, const std::optional<std::string>& value = {});

    bool Dirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }
    Encoding GetEncoding() const { return encoding_; }

private:
    struct Line {
        enum Kind { Blank, Comment, Section, KeyValue, Other } kind = Other;
        std::string raw;
        std::string name;   // section name (for Section) or key (for KeyValue)
        std::string value;  // for KeyValue
    };

    static Line ParseLine(const std::string& raw);
    // Index of the section header line, or -1.
    int FindSection(const std::string& section) const;
    // [start, end) line range of the section body.
    std::pair<int, int> SectionBody(int headerIndex) const;
    int InsertionPoint(int headerIndex) const;

    std::vector<Line> lines_;
    std::string eol_ = "\r\n";
    bool trailingNewline_ = true;
    Encoding encoding_ = Encoding::Ansi;
    bool dirty_ = false;
};

}  // namespace omm
