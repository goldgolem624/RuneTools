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
bool UploadAuto();    void SetUploadAuto(bool on);      // combat_upload.txt "1 <since ms>", default off
long long UploadAutoSince();                            // logs that ended from then on upload automatically
bool UploadLive();    void SetUploadLive(bool on);      // combat_live.txt, default off
bool KeepNames();     void SetKeepNames(bool on);       // combat_names.txt, default off
// combat_visibility.txt: private, unlisted or public, default private. A log takes the value it had when it
// started recording and asks the site for it on upload.
std::string UploadVisibility(); void SetUploadVisibility(const std::string& v);
bool ValidVisibility(const std::string& v);

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
// The log on runetools.io: set only from an accepted answer that passed the checks (empty id = none).
struct UploadInfo { std::string id, url, visibility; long long at = 0; };
// Where an upload stands: "" (nothing to do), queued, sending, failed, unlinked or skipped. `error` is the
// launcher's own text for `code`, never the site's.
struct UploadState { std::string state, error, code; int tries = 0; long long next = 0; bool manual = false, final = false; };
struct IndexRow {
    std::string id, character, file;                    // file: "<character>/<name>.json.gz" under Root()
    long long startedAt = 0, endedAt = 0, bytes = 0, events = 0;
    std::vector<IndexFight> fights;
    UploadInfo upload; UploadState state;
    std::string uploadAs;                               // the visibility asked for on upload; "" = the setting at upload time
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
bool Gunzip(const std::string& in, std::string& out, std::size_t cap, bool* capped = nullptr);   // capped: the output passed `cap`

// ---- uploads to the RuneTools account this PC is linked to (opt-in; nothing leaves the PC otherwise) ----
void StartUploads();                                    // the upload thread; idempotent
void SetHeadless();                                     // this process records without the user's settings: no auto or live uploads
std::string UploadRequestJson(const std::string& logId);    // fightUpload: queue one log by hand
std::string UploadStatusJson(const std::string& logId);     // fightUploadStatus; "" = can this PC upload at all
void AfterCompact(const IndexRow& row);                 // a log just saved: the auto rule, or the final upload after live
// Live upload: the lines of an open log as the recorder writes them, sent in chunks while it is open.
bool LiveWanted();                                      // live on, linked, not headless (cached)
void LiveFeed(std::uint32_t pid, const std::string& logId, const std::vector<std::string>& lines);
void LiveClose(std::uint32_t pid, const std::string& logId);
void LiveGap(const std::string& logId);                 // lines of a live log were written while live was not wanted
std::string LiveStateJson();

// Headless test paths against a local server (base url http://127.0.0.1:<port> or http://localhost:<port>);
// they never touch the combat folder, the settings or the account link. Return the exit code.
int CliUpload(const std::wstring& in, const std::wstring& base, bool keep, std::string& report);
int CliPrep(const std::wstring& in, const std::wstring& out, bool keep, std::string& report);
int CliLiveReplay(const std::wstring& in, const std::wstring& base, int chunkEvents, bool final, std::string& report);

}  // namespace rtx::launcher::fights
