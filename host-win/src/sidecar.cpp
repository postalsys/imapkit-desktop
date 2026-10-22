// Runs the Node.js sidecar (sidecar/src/index.js) and exchanges JSON lines with it over its stdin and stdout
#include "app.h"

#include <shlwapi.h>

namespace {

// the sidecar script: next to the exe in a release, or in the source tree above the build directory in development
std::wstring FindScript() {
    std::wstring dir = ExeDirectory();
    for (int level = 0; level < 5; level++) {
        std::wstring candidate = dir + L"\\sidecar\\src\\index.js";
        if (PathFileExistsW(candidate.c_str())) {
            return candidate;
        }
        size_t slash = dir.find_last_of(L'\\');
        if (slash == std::wstring::npos) {
            break;
        }
        dir.resize(slash);
    }
    return {};
}

// a bundled node.exe next to the exe, or the one on PATH
std::wstring FindNode() {
    std::wstring bundled = ExeDirectory() + L"\\node\\node.exe";
    if (PathFileExistsW(bundled.c_str())) {
        return bundled;
    }
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, found, nullptr)) {
        return found;
    }
    return {};
}

}  // namespace

Sidecar& TheSidecar() {
    static Sidecar sidecar;
    return sidecar;
}

bool Sidecar::Start(HWND notify, std::wstring& error) {
    notify_ = notify;
    std::wstring script = FindScript();
    std::wstring node = FindNode();
    if (script.empty()) {
        error = L"The sidecar script (sidecar\\src\\index.js) was not found next to the program.";
        return false;
    }
    if (node.empty()) {
        error = L"Node.js was not found. Install Node.js 20 or newer, or put node.exe in the node folder next to the program.";
        return false;
    }

    SECURITY_ATTRIBUTES inherit = {sizeof(inherit), nullptr, TRUE};
    HANDLE stdinRead, stdoutRead, stdoutWrite, stderrRead, stderrWrite;
    CreatePipe(&stdinRead, &stdinWrite_, &inherit, 0);
    CreatePipe(&stdoutRead, &stdoutWrite, &inherit, 0);
    CreatePipe(&stderrRead, &stderrWrite, &inherit, 0);
    // only the child ends are inherited
    SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup = {sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdinRead;
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = stderrWrite;

    std::wstring command = L"\"" + node + L"\" \"" + script + L"\"";
    PROCESS_INFORMATION info = {};
    BOOL created = CreateProcessW(node.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                  nullptr, &startup, &info);
    CloseHandle(stdinRead);
    CloseHandle(stdoutWrite);
    CloseHandle(stderrWrite);
    if (!created) {
        error = L"Starting Node.js failed (" + node + L").";
        CloseHandle(stdoutRead);
        CloseHandle(stderrRead);
        return false;
    }

    // the sidecar goes away with this process, even when it crashes
    job_ = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    AssignProcessToJobObject(job_, info.hProcess);
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    process_ = info.hProcess;

    stdoutThread_ = std::thread([this, stdoutRead] { ReadLoop(stdoutRead, WM_SIDECAR_LINES); });
    stderrThread_ = std::thread([this, stderrRead] { ReadLoop(stderrRead, WM_SIDECAR_STDERR); });
    waitThread_ = std::thread([this] {
        WaitForSingleObject(process_, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(process_, &code);
        PostMessageW(notify_, WM_SIDECAR_EXIT, code, 0);
    });
    return true;
}

void Sidecar::ReadLoop(HANDLE pipe, UINT message) {
    std::string buffer;
    char chunk[65536];
    DWORD read = 0;
    while (ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr) && read > 0) {
        buffer.append(chunk, read);
        // the complete lines of this read go to the UI thread in one message, a burst of trace events would
        // otherwise fill its message queue
        auto lines = std::make_unique<std::vector<std::string>>();
        size_t start = 0;
        size_t end;
        while ((end = buffer.find('\n', start)) != std::string::npos) {
            lines->emplace_back(buffer, start, end - start);
            start = end + 1;
        }
        buffer.erase(0, start);
        if (!lines->empty() && PostMessageW(notify_, message, 0, reinterpret_cast<LPARAM>(lines.get()))) {
            lines.release();
        }
    }
    CloseHandle(pipe);
}

void Sidecar::Stop() {
    if (stdinWrite_) {
        // closing stdin tells the sidecar to save and exit
        CloseHandle(stdinWrite_);
        stdinWrite_ = nullptr;
    }
    if (process_ && WaitForSingleObject(process_, 3000) == WAIT_TIMEOUT) {
        TerminateProcess(process_, 1);
    }
    for (std::thread* thread : {&stdoutThread_, &stderrThread_, &waitThread_}) {
        if (thread->joinable()) {
            thread->join();
        }
    }
    if (process_) {
        CloseHandle(process_);
        process_ = nullptr;
    }
    if (job_) {
        CloseHandle(job_);
        job_ = nullptr;
    }
}

void Sidecar::Call(const std::string& method, const json& params, Callback callback) {
    int id = ++nextId_;
    std::string line = json{{"id", id}, {"method", method}, {"params", params}}.dump() + "\n";
    DWORD written = 0;
    if (!stdinWrite_ || !WriteFile(stdinWrite_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)) {
        if (callback) {
            callback(nullptr, "The sidecar is not running");
        }
        return;
    }
    if (callback) {
        pending_[id] = std::move(callback);
    }
}

void Sidecar::HandleLine(const std::string& line) {
    json message = json::parse(line, nullptr, false);
    if (message.is_discarded()) {
        return;
    }
    if (message.contains("event")) {
        static const json empty = json::object();
        if (onEvent) {
            auto data = message.find("data");
            onEvent(JsonString(message, "event"), data != message.end() ? *data : empty);
        }
        return;
    }
    int id = message.value("id", 0);
    auto found = pending_.find(id);
    if (found == pending_.end()) {
        return;
    }
    Callback callback = std::move(found->second);
    pending_.erase(found);
    if (message.contains("error")) {
        callback(nullptr, message["error"].value("message", std::string("Unknown error")));
    } else {
        callback(message.value("result", json()), "");
    }
}
