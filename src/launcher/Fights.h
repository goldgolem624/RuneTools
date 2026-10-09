#pragma once
// Combat log store: the open JSONL log per client, compaction to .json.gz, index.json, the settings
// flags and the recovery of logs a crash left open. Files under %USERPROFILE%\RuneToolsX\combat\
// (DESIGN.md 2.3). The compaction and the gzip are pure and testable; the folder is overridable.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace rtx::launcher::fights {

std::filesystem::path Root();                           // the combat folder, created on first use
void SetRoot(const std::filesystem::path& p);           // the headless recorder and the tests

bool RecordEnabled(); void SetRecordEnabled(bool on);   // combat_record.txt, default off
bool UploadAuto();    void SetUploadAuto(bool on);      // combat_upload.txt, default off
bool KeepNames();     void SetKeepNames(bool on);       // combat_names.txt, default off

std::string NewLogId();                                 // 16 random bytes, base64url

// The open log of one client: <root>\<character>\current_<pid>.jsonl, appended line by line.
class OpenLog {
public:
    ~OpenLog() { Close(); }
    bool Open(const std::string& character, std::uint32_t pid);
    bool Append(const std::vector<std::string>& lines);
    void Close();
    bool IsOpen() const { return f_ != nullptr; }
    const std::filesystem::path& Path() const { return path_; }
private:
    std::FILE* f_ = nullptr; std::filesystem::path path_;
};

struct Summary { long long durMs = 0, dealt = 0, taken = 0, healed = 0, hits = 0, crits = 0, maxHit = 0, blocked = 0, deaths = 0, kills = 0; double dps = 0, dpm = 0; };
struct IndexFight { int n = 0; long long start = 0, end = 0; std::string kind, boss; int kills = 0, deaths = 0; Summary summary; };
struct IndexRow {
    std::string id, character, file;                    // file: "<character>/<name>.json.gz" under Root()
    long long startedAt = 0, endedAt = 0, bytes = 0, events = 0;
    std::vector<IndexFight> fights;
    int version = 1;
};
std::string RowJson(const IndexRow& r);

// The JSONL text as the log object (DESIGN.md 1.2); `endBy` fills in a missing end line. False when the
// text has no header. `row` gets everything but the file name and the byte count.
bool CompactText(const std::string& jsonl, const char* endBy, std::string& outJson, IndexRow& row);
// Compacts a JSONL file into <its folder>\<yyyy-mm-dd_hh-mm-ss>_<logId>.json.gz, records it in index.json
// and deletes the JSONL. A log without a fight is deleted and not recorded. False when nothing was written.
bool Compact(const std::filesystem::path& jsonl, const char* endBy, IndexRow* rowOut = nullptr);
void Recover();                                         // current_*.jsonl left behind -> compacted as "recovered"
void Retention();                                       // 90 days or 2 GB, oldest first

std::string ListJson();                                 // index.json rows, newest first
std::string LoadJson(const std::string& logId);         // the log object, "" when absent or over 8 MB
bool Delete(const std::string& logId);
// A copy of the .json.gz: a Save As dialog when `pick`, else <root>\export\. Returns the copy's path, "" when cancelled.
std::string Export(const std::string& logId, bool pick, void* ownerHwnd);
void OpenFolder();

bool Gzip(const std::string& in, std::string& out);
bool Gunzip(const std::string& in, std::string& out, std::size_t cap);

}  // namespace rtx::launcher::fights
