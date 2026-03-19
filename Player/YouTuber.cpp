#include "stdafx.h"

#include "YouTuber.h"

#define YOUTUBE_EXPERIMENT

#ifdef YOUTUBE_EXPERIMENT

#include <Shlobj.h>

#include <boost/python/exec.hpp>
#include <boost/python/import.hpp>
#include <boost/python/extract.hpp>
#include <boost/python.hpp>

#include <boost/log/trivial.hpp>
#include <boost/log/sources/channel_logger.hpp>

#include <boost/algorithm/string.hpp>

#include <regex>
#include <fstream>
#include <iterator>
#include <streambuf>
#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <utility>

#include <tchar.h>

#include "http_get.h"

#include "MemoryMappedFile.h"

namespace {

// Parses the value of the active python exception
std::string parse_python_exception()
{
    namespace py = boost::python;

    PyObject *type_ptr = NULL, *value_ptr = NULL, *traceback_ptr = NULL;
    PyErr_Fetch(&type_ptr, &value_ptr, &traceback_ptr);

    std::string ret("Unfetchable Python error");
    if(type_ptr != NULL){
        py::handle<> h_type(type_ptr);
        py::str type_pstr(h_type);
        py::extract<std::string> e_type_pstr(type_pstr);
        if(e_type_pstr.check())
            ret = e_type_pstr();
        else
            ret = "Unknown exception type";
    }
    if(value_ptr != NULL){
        py::handle<> h_val(value_ptr);
        py::str a(h_val);
        py::extract<std::string> returned(a);
        try {
            if (returned.check())
                ret += ": " + returned();
            else
                ret += ": Unparseable Python error: ";
        }
        catch (const py::error_already_set&) {
            ret += ": Unparseable Python error: ";
        }
    }
    if(traceback_ptr != NULL){
        py::handle<> h_tb(traceback_ptr);
        py::object tb(py::import("traceback"));
        py::object fmt_tb(tb.attr("format_tb"));
        py::object tb_list(fmt_tb(h_tb));
        py::object tb_str(py::str("\n").join(tb_list));
        py::extract<std::string> returned(tb_str);
        if(returned.check())
            ret += ": " + returned();
        else
            ret += ": Unparseable Python traceback";
    }
    return ret;
}


class LoggerStream
{
public:
    void write(const std::string& what)
    {
        using namespace boost::log;
        sources::channel_logger_mt<> logger(keywords::channel = "python");
        BOOST_LOG(logger) << what;
    }
    void flush() {}
};


const char COMBINED_TEMPLATE[] = R"(import sys, socket, re
sys.stderr = LoggerStream()

def install_and_import(package, url=None):
    import importlib
    if url is None:
        url = package
    try:
        importlib.import_module(package)
    except ImportError:
        import subprocess
        import os
        library_dir = os.path.dirname(os.path.abspath(socket.__file__))
        subprocess.run([library_dir + "/../scripts/pip3", "install", url])
    finally:
        globals()[package] = importlib.import_module(package)

# ---- yt-dlp based getYoutubeUrl ----
install_and_import("yt_dlp", "https://github.com/yt-dlp/yt-dlp/archive/refs/heads/master.zip")

def _iter_formats(info):
    if not info:
        return []
    if isinstance(info, dict) and 'entries' in info and info['entries']:
        for e in info['entries']:
            if e:
                info = e
                break
    if isinstance(info, dict) and 'requested_formats' in info and info['requested_formats']:
        return info['requested_formats']
    if isinstance(info, dict) and 'formats' in info and info['formats']:
        return info['formats']
    if isinstance(info, dict) and 'url' in info:
        return [info]
    return []

def _is_usable_video_format(f):
    vcodec = f.get('vcodec')
    proto = f.get('protocol', '')
    if not vcodec or vcodec == 'none':
        return False
    if vcodec.startswith('av01'):
        return False
    if proto == 'm3u8_native' or proto == 'm3u8':
        return False
    return True

def _is_usable_audio_format(f):
    vcodec = f.get('vcodec', 'none')
    proto = f.get('protocol', '')
    if vcodec != 'none':
        return False
    if proto == 'm3u8_native' or proto == 'm3u8':
        return False
    return 'audio_channels' in f and f.get('audio_channels') is not None or 'abr' in f

def getYoutubeUrl(url, adaptive):
    socket.setdefaulttimeout(10)
    base_opts = {
        'noplaylist': True,
        'quiet': True,
        'skip_download': True,
    }
    ydl_opts = base_opts.copy()
    ydl_opts['format'] = 'bestvideo+bestaudio' if adaptive else 'best'
    with yt_dlp.YoutubeDL(ydl_opts) as ydl:
        info = ydl.extract_info(url, download=False)
    formats = _iter_formats(info)
    if adaptive:
        video_candidates = [f for f in formats if _is_usable_video_format(f)]
        if video_candidates:
            best_video = max(video_candidates, key=lambda f: (f.get('height') or 0, f.get('tbr') or 0))
            video_url = best_video.get('url')
        else:
            video_url = info.get('url')
        audio_candidates = [f for f in formats if _is_usable_audio_format(f)]
        if audio_candidates:
            best_audio = max(audio_candidates, key=lambda f: (f.get('audio_channels') or 0, f.get('abr') or 0))
            audio_url = best_audio.get('url')
        else:
            any_audio = next((f for f in formats if f.get('vcodec') == 'none' and f.get('url')), None)
            audio_url = any_audio.get('url') if any_audio else None
        return (video_url, audio_url)
    combined_candidates = [f for f in formats if f.get('vcodec') != 'none' and f.get('audio_channels') is not None and not f.get('vcodec','').startswith('av01') and f.get('protocol') not in ('m3u8_native', 'm3u8')]
    if not combined_candidates:
        combined_candidates = [f for f in formats if 'url' in f]
    best_combined = max(combined_candidates, key=lambda f: (f.get('height') or 0, f.get('tbr') or 0, f.get('abr') or 0))
    return best_combined.get('url')

# ---- youtube_transcript_api based getYoutubeTranscript ----
install_and_import("youtube_transcript_api", "https://github.com/jdepoix/youtube-transcript-api/archive/master.zip")

_YT_ID_RE = re.compile(
    r'(?:v=|\/v\/|youtu\.be\/|\/embed\/|\/shorts\/|watch\?.*v=)([A-Za-z0-9_-]{11})'
)

def extract_youtube_id(url_or_id: str) -> str:
    if not isinstance(url_or_id, str):
        raise ValueError("video id or url must be a string")
    url_or_id = url_or_id.strip()
    if len(url_or_id) == 11 and re.fullmatch(r'[A-Za-z0-9_-]{11}', url_or_id):
        return url_or_id
    m = _YT_ID_RE.search(url_or_id)
    if m:
        return m.group(1)
    q = re.search(r'[?&]v=([A-Za-z0-9_-]{11})', url_or_id)
    if q:
        return q.group(1)
    raise ValueError("Could not extract YouTube video id from input")

def getYoutubeTranscript(url_or_id: str):
    video_id = extract_youtube_id(url_or_id)
    return youtube_transcript_api.YouTubeTranscriptApi().fetch(video_id).to_raw_data()
)";


int from_hex(char ch)
{
    return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

std::string UrlUnescapeString(const std::string& s)
{
    std::istringstream ss(s);
    std::string result;
    std::getline(ss, result, '%');
    std::string buffer;
    while (std::getline(ss, buffer, '%'))
    {
        if (buffer.size() >= 2)
        {
            result += char((from_hex(buffer[0]) << 4) | from_hex(buffer[1])) + buffer.substr(2);
        }
    }
    return result;
}

void hexchar(unsigned char c, unsigned char &hex1, unsigned char &hex2)
{
    hex1 = c / 16;
    hex2 = c % 16;
    hex1 += hex1 <= 9 ? '0' : 'a' - 10;
    hex2 += hex2 <= 9 ? '0' : 'a' - 10;
}

std::string urlencode(const std::string& s)
{
    std::string v;
    for (char c : s)
    {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_' || c == '.' || c == '!' || c == '~' ||
            c == '*' || c == '\'' || c == '(' || c == ')')
        {
            v.push_back(c);
        }
        else if (c == ' ')
        {
            v.push_back('+');
        }
        else
        {
            v.push_back('%');
            unsigned char d1, d2;
            hexchar(c, d1, d2);
            v.push_back(d1);
            v.push_back(d2);
        }
    }

    return v;
}

// Extracts a single URL from s. Accepts URLs with http or https schemes,
// or scheme-less URLs that start with a valid host (e.g., "example.com" or "www.example.com").
// Rejects inputs that include a non-http(s) scheme (e.g., "ftp://", "htp://").
// On success sets s to the matched URL (including path, query and fragment) and returns true.
// The match is conservative: it stops at whitespace and common delimiters < > " ' ( ).
// If the input may be percent-escaped, keep UrlUnescapeString available; we try both raw and unescaped.
bool extractHttpOrHostUrl(std::string& s)
{
    // scheme: optional, but if present must be http or https
    // host: domain (labels + tld), localhost or IPv4
    // port: optional
    // path+query+fragment: optional, but must preserve ? and # if present
    static const std::regex url_regex(
        R"((?:https?:\/\/)?(?:(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\.)+[A-Za-z]{2,}|localhost|\d{1,3}(?:\.\d{1,3}){3})(?::\d{1,5})?(?:\/[^\s\"\'<>()]*)?)",
        std::regex::icase
    );

    std::string copy = s;
    for (int unescaped = 0; unescaped < 2; ++unescaped)
    {
        std::smatch m;
        if (std::regex_search(copy, m, url_regex))
        {
            // keep the full matched length so query params and fragments remain
            s = copy.substr(m.position(), m.length());
            // If caller expects the URL to start at the match position and include the rest
            // of the original string, change to: s = copy.substr(m.position());
            return true;
        }
        if (!unescaped)
        {
            // If you have a UrlUnescapeString implementation, use it here to try unescaped input.
            // If not needed, remove this block or leave it as a no-op.
            copy = UrlUnescapeString(copy);
        }
    }

    return false;
}

std::string loadScriptText(const TCHAR* name)
{
    TCHAR strPath[MAX_PATH]{};
    SHGetSpecialFolderPath(
        nullptr,
        strPath,
        CSIDL_LOCAL_APPDATA,
        FALSE);

    PathAppend(strPath, name);

    std::ifstream file(strPath, std::ios::in | std::ios::binary);

    if (!file)
        return {};

    std::ostringstream contents;
    contents << file.rdbuf();

    BOOST_LOG_TRIVIAL(trace) << "loadScriptText() script loaded: \"" << name << "\"";

    return contents.str();
}

auto getLoggerStream()
{
    return boost::python::class_<LoggerStream>("LoggerStream")
        .def("write", &LoggerStream::write)
        .def("flush", &LoggerStream::flush);
}

// Execute second application instance with -check_python flag
// and return true if exit code is 0
bool isPythonInstalled()
{
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    TCHAR szCmdline[MAX_PATH + 20] = _T("\"");
    auto len = GetModuleFileName(NULL, szCmdline + 1, ARRAYSIZE(szCmdline) - 1);
    _tcscpy_s(szCmdline + len + 1, ARRAYSIZE(szCmdline) - len - 1, _T("\" -check_python"));
    if (!CreateProcess(NULL, szCmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        return false;
    }

    DWORD exitCode;

    bool ok = WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0
        && GetExitCodeProcess(pi.hProcess, &exitCode)
        && exitCode == 0;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return ok;
}

// Shared Python namespace loader/holder
bool EnsureSharedPythonNamespaceLoaded(boost::python::object& outNamespace)
{
    using namespace boost::python;

    static bool s_loaded = false;
    static object s_namespace;

    if (s_loaded)
    {
        outNamespace = s_namespace;
        return true;
    }

    if (!isPythonInstalled())
    {
        BOOST_LOG_TRIVIAL(error) << "Python not installed or wrong bitness.";
        return false;
    }

    Py_Initialize();
    if (!Py_IsInitialized())
    {
        BOOST_LOG_TRIVIAL(error) << "Py_Initialize failed";
        return false;
    }

    // Add atexit finalize guard (optional)
    atexit(Py_Finalize);

    try {
        PyObject* sysPath = PySys_GetObject("path");
        object path(borrowed(sysPath));
        const auto length = len(path);
        std::vector<std::string> sysPathList;
        for (int i = 0; i < length; ++i)
        {
            std::string v{extract<std::string>(path[i])};
            v += "/site-packages";
            if (_taccess(CA2T(v.c_str(), CP_UTF8), 0) == 0)
            {
                sysPathList.push_back(std::move(v));
            }
        }

        for (const auto& v : sysPathList)
        {
            PyList_Insert(sysPath, 0, PyUnicode_FromString(v.c_str()));
        }

        // Load script text from local override if present; otherwise use combined template
        const auto localScript = loadScriptText(_T("getYoutubeCombined.py"));
        const char* scriptToExec = localScript.empty() ? COMBINED_TEMPLATE : localScript.c_str();

        object main = import("__main__");
        object global(main.attr("__dict__"));

        // inject LoggerStream class to python
        global["LoggerStream"] = getLoggerStream();

        exec(scriptToExec, global, global);

        s_namespace = global;
        s_loaded = true;
        outNamespace = s_namespace;
        return true;
    }
    catch (const error_already_set&)
    {
        BOOST_LOG_TRIVIAL(error) << "Shared python bootstrap error \"" << parse_python_exception() << "\"";
        PyErr_Clear();
        return false;
    }
    catch (const std::exception& ex)
    {
        BOOST_LOG_TRIVIAL(error) << "Shared python bootstrap exception \"" << ex.what() << "\"";
        return false;
    }
}

class YouTubeDealer
{
public:
    YouTubeDealer();
    ~YouTubeDealer() = default;

    bool isValid() const { return !!m_func; }
    std::vector<std::string> getYoutubeUrl(const std::string& url, bool adaptive);

private:
    boost::python::object m_func;
};

YouTubeDealer::YouTubeDealer()
{
    using namespace boost::python;
    try
    {
        object ns;
        if (!EnsureSharedPythonNamespaceLoaded(ns))
            return;

        // Attempt to get callable 'getYoutubeUrl' from shared namespace
        try {
            m_func = ns["getYoutubeUrl"];
        }
        catch (const error_already_set&) {
            BOOST_LOG_TRIVIAL(error) << "YouTubeDealer: getYoutubeUrl not found in shared python namespace: " << parse_python_exception();
            PyErr_Clear();
        }
    }
    catch (const std::exception& ex)
    {
        BOOST_LOG_TRIVIAL(error) << "YouTubeDealer bootstrap exception \"" << ex.what() << "\"";
    }
    catch (const boost::python::error_already_set&)
    {
        BOOST_LOG_TRIVIAL(error) << "YouTubeDealer bootstrap python error \"" << parse_python_exception() << "\"";
        PyErr_Clear();
    }
}

std::vector<std::string> YouTubeDealer::getYoutubeUrl(const std::string& url, bool adaptive)
{
    BOOST_LOG_TRIVIAL(trace) << "YouTubeDealer::getYoutubeUrl() url = \"" << url << "\"";
    using namespace boost::python;

    if (!isValid())
        return {};

    try
    {
        object py_result = m_func(url, adaptive);

        if (py_result.is_none())
            return {};

        extract<std::string> as_str(py_result);
        if (as_str.check())
            return { as_str() };

        std::vector<std::string> result;
        for (stl_input_iterator<object> it(py_result), end; it != end; ++it)
        {
            extract<std::string> elem_str(*it);
            if (elem_str.check())
            {
                result.push_back(elem_str());
            }
            else
            {
                result.push_back(extract<std::string>(str(*it))());
            }
        }
        return result;
    }
    catch (const error_already_set&)
    {
        BOOST_LOG_TRIVIAL(error) << "YouTubeDealer python error \"" << parse_python_exception() << "\"";
        PyErr_Clear();
    }
    catch (const std::exception& ex)
    {
        BOOST_LOG_TRIVIAL(error) << "YouTubeDealer exception \"" << ex.what() << "\"";
    }

    return {};
}


class YouTubeTranscriptDealer
{
public:
    YouTubeTranscriptDealer();
    ~YouTubeTranscriptDealer() = default;

    bool isValid() const { return !!m_func; }
    bool getYoutubeTranscripts(const std::string& id, AddYoutubeTranscriptCallback cb);

private:
    boost::python::object m_func;
};

YouTubeTranscriptDealer::YouTubeTranscriptDealer()
{
    using namespace boost::python;
    try
    {
        object ns;
        if (!EnsureSharedPythonNamespaceLoaded(ns))
            return;

        try {
            m_func = ns["getYoutubeTranscript"];
        }
        catch (const error_already_set&) {
            BOOST_LOG_TRIVIAL(error) << "YouTubeTranscriptDealer: getYoutubeTranscript not found in shared python namespace: " << parse_python_exception();
            PyErr_Clear();
        }
    }
    catch (const std::exception& ex)
    {
        BOOST_LOG_TRIVIAL(error) << "YouTubeTranscriptDealer bootstrap exception \"" << ex.what() << "\"";
    }
    catch (const boost::python::error_already_set&)
    {
        BOOST_LOG_TRIVIAL(error) << "YouTubeTranscriptDealer bootstrap python error \"" << parse_python_exception() << "\"";
        PyErr_Clear();
    }
}

bool YouTubeTranscriptDealer::getYoutubeTranscripts(const std::string& id, AddYoutubeTranscriptCallback cb)
{
    BOOST_LOG_TRIVIAL(trace) << "YouTubeTranscriptDealer::getYoutubeTranscripts() id = \"" << id << "\"";
    using namespace boost::python;
    if (!isValid())
        return false;

    try
    {
        object v = m_func(id);
        if (v.is_none())
            return false;

        const auto length = len(v);
        for (int i = 0; i < length; ++i)
        {
            object el = v[i];
            std::string text = extract<std::string>(el["text"]);
            boost::algorithm::trim_right(text);
            if (!text.empty())
            {
                double start = extract<double>(el["start"]);
                double duration = extract<double>(el["duration"]);
                cb(start, duration, text);
            }
        }
        return true;
    }
    catch (const error_already_set&)
    {
        BOOST_LOG_TRIVIAL(error) << "YouTubeTranscriptDealer python error \"" << parse_python_exception() << "\"";
        PyErr_Clear();
    }
    catch (const std::exception& ex)
    {
        BOOST_LOG_TRIVIAL(error) << "YouTubeTranscriptDealer exception \"" << ex.what() << "\"";
    }

    return false;
}

std::vector<std::string> DoParsePlaylist(
    const char* const pDataBegin, const char* const pDataEnd, bool includeLists = true)
{
    std::vector<std::string> result;

    auto doSearch = [pDataBegin, pDataEnd, &result](const auto& watch, const char* prefix) {
        enum { WATCH_SIZE = sizeof(watch) / sizeof(watch[0]) - 1 };
        auto pData = pDataBegin;
        while ((pData = std::search(pData, pDataEnd, std::begin(watch), std::prev(std::end(watch)))) != pDataEnd)
        {
            const auto localEnd = std::find_if(pData + WATCH_SIZE, pDataEnd, [](char ch) {
                return ch == '&' || ch == '"' || ch == '\'' || ch == '\\' || std::isspace(static_cast<unsigned char>(ch));
                });
            auto el = prefix + std::string(pData, localEnd);
            if (std::find(result.begin(), result.end(), el) == result.end())
                result.push_back(std::move(el));
            pData += WATCH_SIZE;
        }
        };

    doSearch("/watch?v=", "https://www.youtube.com");
    doSearch("youtu.be/", "https://");
    if (includeLists)
    {
        doSearch("/playlist?list=", "https://www.youtube.com");
    }

    return result;
}

} // namespace

void CheckPython()
{
    Py_Initialize();

    if (!Py_IsInitialized())
    {
        exit(1);
    }

    atexit(Py_Finalize);

    if (!_PyThreadState_UncheckedGet())
    {
        exit(1);
    }

    PyObject* sysPath = PySys_GetObject("path");
    if (!sysPath)
    {
        exit(1);
    }

    try {
        boost::python::object path(boost::python::borrowed(sysPath));
        const auto length = len(path);
        if (length < 1)
        {
            exit(1);
        }

        std::string v{ boost::python::extract<std::string>(path[0]) };
        if (v.empty())
        {
            exit(1);
        }
    }
    catch (...) {
        exit(1);
    }
}

std::vector<std::string> ParsePlaylist(std::string url, bool force)
{
    bool isList = false;
    if (url.find_first_of("./") == std::string::npos)
    {
        std::istringstream ss(url);
        std::string result;
        ss >> result;
        result = urlencode(result);
        std::string buffer;
        while (ss >> buffer)
        {
            if (!buffer.empty())
                result += '+' + urlencode(buffer);
        }
        url = "https://www.youtube.com/results?search_query=" + result;
    }
    else
    {
        isList = url.find("/playlist?list=") != std::string::npos;
        if (!force && !isList)
            return{};
    }

    CWaitCursor wait;
    CComVariant varBody = HttpGet(url.c_str());
    if ((VT_ARRAY | VT_UI1) != V_VT(&varBody))
        return{};

    auto psa = V_ARRAY(&varBody);
    LONG iLBound, iUBound;
    HRESULT hr = SafeArrayGetLBound(psa, 1, &iLBound);
    if (FAILED(hr))
        return{};
    hr = SafeArrayGetUBound(psa, 1, &iUBound);
    if (FAILED(hr))
        return{};

    const char* pData = nullptr;

    hr = SafeArrayAccessData(psa, (void**)&pData);
    if (FAILED(hr) || !pData)
        return{};

    const char* const pDataEnd = pData + iUBound - iLBound + 1;

    std::unique_ptr<SAFEARRAY, decltype(&SafeArrayUnaccessData)> guard(
        psa, SafeArrayUnaccessData);
    return DoParsePlaylist(pData, pDataEnd, !isList);
}

std::vector<std::string> ParsePlaylistFile(const TCHAR* fileName)
{
    MemoryMappedFile memoryMappedFile;
    if (!memoryMappedFile.MapFlie(fileName))
        return{};
    auto* const pData = static_cast<const char*>(memoryMappedFile.data());
    return DoParsePlaylist(pData, pData + memoryMappedFile.size());
}

std::vector<std::string> ParsePlaylistText(const std::string& text)
{
    if (text.empty())
    {
        return{};
    }
    auto* const pData = text.data();
    return DoParsePlaylist(pData, pData + text.size());
}

inline bool isHttpStatusOk(int status)
{
    const bool result = status == 200 || status == 302;
    if (!result)
        BOOST_LOG_TRIVIAL(trace) << "HTTP error status: " << status;
    return result;
}

std::pair<std::string, std::string> getYoutubeUrl(std::string url, bool adaptive, bool useHHO)
{
    enum { ATTEMPTS_NUMBER = 2 };

    if (extractHttpOrHostUrl(url))
    {
        static std::map<std::string, std::pair<std::string, std::string>> mapToDownloadLinks[2];
        auto it = mapToDownloadLinks[adaptive].find(url);
        if (it != mapToDownloadLinks[adaptive].end())
        {
            if (isHttpStatusOk(HttpGetStatus(it->second.first, useHHO))
                    && (it->second.second.empty() || isHttpStatusOk(HttpGetStatus(it->second.second, useHHO))))
                return it->second;
            else
                mapToDownloadLinks[adaptive].erase(it);
        }

        CWaitCursor wait;
        static YouTubeDealer buddy;
        if (buddy.isValid())
        {
            for (int j = 0; j < ATTEMPTS_NUMBER; ++j)
            {
                auto urls = buddy.getYoutubeUrl(url, adaptive);
                if (!urls.empty())
                {
                    const auto status = HttpGetStatus(urls[0], useHHO);
                    BOOST_LOG_TRIVIAL(trace) << "Resource status: " << status;
                    if (isHttpStatusOk(status))
                    {
                        if (urls.size() > 1)
                        {
                            for (int i = 1; i < urls.size(); ++i)
                            {
                                const auto status = HttpGetStatus(urls[i], useHHO);
                                BOOST_LOG_TRIVIAL(trace) << "Resource status: " << status;
                                if (isHttpStatusOk(status))
                                {
                                    return mapToDownloadLinks[adaptive][url] = { urls[i], urls[0] };
                                }
                            }
                        }
                        else
                        {
                            return mapToDownloadLinks[adaptive][url] = { urls[0],{} };
                        }
                    }
                    Sleep(50);
                }
            }
        }

        return{};
    }

    return{ url, {} };
}

bool getYoutubeTranscripts(std::string url, AddYoutubeTranscriptCallback cb)
{
    if (extractHttpOrHostUrl(url))
    {
        CWaitCursor wait;
        static YouTubeTranscriptDealer buddy;
        if (buddy.isValid())
            return buddy.getYoutubeTranscripts(url, cb);
    }

    return false;
}

#else // YOUTUBE_EXPERIMENT

void CheckPython() {}

std::vector<std::string> ParsePlaylist(std::string, bool)
{
    return{};
}

std::vector<std::string> ParsePlaylistFile(const TCHAR*)
{
    return{};
}

std::vector<std::string> ParsePlaylistText(const std::string&)
{
    return{};
}

std::pair<std::string, std::string> getYoutubeUrl(std::string url, bool /*adaptive*/, bool /*useHHO*/)
{
    return{ url, {} };
}

bool getYoutubeTranscripts(std::string, AddYoutubeTranscriptCallback)
{
    return false;
}

#endif // YOUTUBE_EXPERIMENT
