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

#include "unzip.h"
#include "http_get.h"

#include "MemoryMappedFile.h"

namespace {

bool getValueFromPropertiesFile(const TCHAR* configPath, const char* key, std::string& value)
{
    std::ifstream istr(configPath);
    std::string buffer;
    while (std::getline(istr, buffer))
    {
        std::istringstream ss(buffer);
        std::getline(ss, buffer, '=');
        boost::algorithm::trim(buffer);
        if (buffer == key)
        {
            std::getline(ss, value);
            boost::algorithm::trim(value);
            return true;
        }
    }
    return false;
}

// http://thejosephturner.com/blog/post/embedding-python-in-c-applications-with-boostpython-part-2/
// Parses the value of the active python exception
// NOTE SHOULD NOT BE CALLED IF NO EXCEPTION
std::string parse_python_exception()
{
    namespace py = boost::python;

    PyObject *type_ptr = NULL, *value_ptr = NULL, *traceback_ptr = NULL;
    // Fetch the exception info from the Python C API
    PyErr_Fetch(&type_ptr, &value_ptr, &traceback_ptr);

    // Fallback error
    std::string ret("Unfetchable Python error");
    // If the fetch got a type pointer, parse the type into the exception string
    if(type_ptr != NULL){
        py::handle<> h_type(type_ptr);
        py::str type_pstr(h_type);
        // Extract the string from the boost::python object
        py::extract<std::string> e_type_pstr(type_pstr);
        // If a valid string extraction is available, use it 
        //  otherwise use fallback
        if(e_type_pstr.check())
            ret = e_type_pstr();
        else
            ret = "Unknown exception type";
    }
    // Do the same for the exception value (the stringification of the exception)
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
    // Parse lines from the traceback using the Python traceback module
    if(traceback_ptr != NULL){
        py::handle<> h_tb(traceback_ptr);
        // Load the traceback module and the format_tb function
        py::object tb(py::import("traceback"));
        py::object fmt_tb(tb.attr("format_tb"));
        // Call format_tb to get a list of traceback strings
        py::object tb_list(fmt_tb(h_tb));
        // Join the traceback strings into a single string
        py::object tb_str(py::str("\n").join(tb_list));
        // Extract the string, check the extraction, and fallback in necessary
        py::extract<std::string> returned(tb_str);
        if(returned.check())
            ret += ": " + returned();
        else
            ret += ": Unparseable Python traceback";
    }
    return ret;
}


boost::python::object LoadScriptAndGetFunction(const char* script, const char* funcName,
    std::initializer_list<std::pair<const char*, boost::python::object>> globals)
{
    using namespace boost::python;
    // Retrieve the main module.
    object main = import("__main__");

    // Retrieve the main module's namespace
    object global(main.attr("__dict__"));

    for (auto& v : globals)
        global[v.first] = v.second;

    // Define function in Python.
    object exec_result = exec(script, global, global);

    return global[funcName];
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


//const char PYTUBE_URL[] = "https://github.com/pytube/pytube/archive/master.zip";
const char PYTUBE_URL[] = "https://github.com/JuanBindez/pytubefix/archive/refs/heads/main.zip";
const char YOUTUBE_TRANSCRIPT_API_URL[] = "https://github.com/jdepoix/youtube-transcript-api/archive/master.zip";

//*

const char SCRIPT_TEMPLATE[] = R"(import sys, socket
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

install_and_import('typing_extensions')
sys.path.append(getPytubePathWithPackage())
from pytubefix import YouTube

try:
    import subprocess
    from pytubefix.botGuard.bot_guard import NODE_PATH, VM_PATH

    def generate_po_token_substitute(visitor_data: str) -> str:
        """
        Run nodejs to generate poToken through botGuard.
        Requires nodejs installed.
        """
        startupinfo = subprocess.STARTUPINFO()
        startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startupinfo.wShowWindow = 6 # SW_MINIMIZE
        result = subprocess.check_output(
            [NODE_PATH, VM_PATH, visitor_data],
            startupinfo=startupinfo
        ).decode()
        return result.replace("\n", "")

    import pytubefix
    pytubefix.botGuard.bot_guard.generate_po_token = generate_po_token_substitute
except BaseException as e:
    print(f"Failed to substitute generate_po_token: {e}")

def getYoutubeUrl(url, adaptive):
    socket.setdefaulttimeout(10)
    s=YouTube(url, 'WEB').streams
    if adaptive:
        return [s.get_audio_only().url] \
            + [x.url for x in s.filter(only_video=True).order_by('resolution').desc() \
            if not x.video_codec.startswith("av01")]
    return s.get_highest_resolution().url)";

//*/

/*

const char SCRIPT_TEMPLATE[] = R"(import sys, socket
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

install_and_import("yt_dlp", "https://github.com/yt-dlp/yt-dlp/archive/refs/heads/master.zip")

def getYoutubeUrl(url, adaptive):
    socket.setdefaulttimeout(10)
    if adaptive:

        ydl_opts = {
            'format': 'bestvideo+bestaudio',
            'noplaylist': True,
            'quiet': True,
        }

        formats = yt_dlp.YoutubeDL(ydl_opts).extract_info(url, download=False)['formats']
        best_video_format = max(formats, key=lambda f: f.get('height', 0) if f['vcodec'] != 'none' and not f['vcodec'].startswith('av01') and f['protocol'] != 'm3u8_native' else 0)
        video_url = best_video_format['url']

        #ydl_opts = {
        #    'format': 'bestaudio',
        #    'noplaylist': True,
        #    'quiet': True,
        #}

        #formats = yt_dlp.YoutubeDL(ydl_opts).extract_info(url, download=False)['formats']
        best_audio = next(f['url'] for f in formats if 'audio_channels' in f and f['audio_channels'] is not None and f['vcodec'] == 'none' and f['protocol'] != 'm3u8_native')

        return [video_url, best_audio]

    ydl_opts = {
        'format': 'best',
        'noplaylist': True,
        'quiet': True,
    }

    formats = yt_dlp.YoutubeDL(ydl_opts).extract_info(url, download=False)['formats']
    best_video_format = max(formats, key=lambda f: f.get('height', 0) if f['vcodec'] != 'none' and not f['vcodec'].startswith('av01') and 'audio_channels' in f and f['audio_channels'] is not None and f['protocol'] != 'm3u8_native' else 0)
    return best_video_format['url'])";

//*/

const char TRANSCRIPT_TEMPLATE[] = R"(import sys
import re
sys.stderr = LoggerStream()

install_and_import('requests')
sys.path.append(getTranscriptPathWithPackage())
from youtube_transcript_api import YouTubeTranscriptApi

_YT_ID_RE = re.compile(
    r'(?:v=|\/v\/|youtu\.be\/|\/embed\/|\/shorts\/|watch\?.*v=)([A-Za-z0-9_-]{11})'
)

def extract_youtube_id(url_or_id: str) -> str:
    """
    Accepts either a full YouTube URL (many common forms) or a raw 11-char id.
    Returns the 11-character video id or raises ValueError if none found.
    """
    if not isinstance(url_or_id, str):
        raise ValueError("video id or url must be a string")
    url_or_id = url_or_id.strip()
    if len(url_or_id) == 11 and re.fullmatch(r'[A-Za-z0-9_-]{11}', url_or_id):
        return url_or_id
    m = _YT_ID_RE.search(url_or_id)
    if m:
        return m.group(1)
    # fallback: try to parse v= query param explicitly
    q = re.search(r'[?&]v=([A-Za-z0-9_-]{11})', url_or_id)
    if q:
        return q.group(1)
    raise ValueError("Could not extract YouTube video id from input")

def getYoutubeTranscript(url_or_id: str):
    video_id = extract_youtube_id(url_or_id)
    return YouTubeTranscriptApi().fetch(video_id).to_raw_data())";


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

// http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/package.html#encodeURIComponent()
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

/*
bool extractYoutubeUrl(std::string& s)
{
    std::regex txt_regex(R"((http(s)?:\/\/)?((w){3}.)?youtu(be|.be)?(\.com)?\/.+)");
    std::string copy = s;
    for (int unescaped = 0; unescaped < 2; ++unescaped)
    {
        std::smatch m;
        if (std::regex_search(copy, m, txt_regex))
        {
            s = copy.substr(m.position());
            return true;
        }
        if (!unescaped)
            copy = UrlUnescapeString(copy);
    }

    return false;
}

bool extractYoutubeId(std::string& s)
{
    std::string copy = s;
    if (!extractYoutubeUrl(copy))
        return false;

    std::regex txt_regex(R"((?:v=|\/)([0-9A-Za-z_-]{11}).*)");
    std::smatch m;
    if (std::regex_search(copy, m, txt_regex) && m.size() == 2)
    {
        s = m[1];
        return true;
    }

    return false;
}
*/

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

bool DownloadAndExtractZip(const char* zipfile, const TCHAR* root, const TCHAR* name)
{
    std::string urlSubst;
    {
        TCHAR configPath[MAX_PATH];
        _tcscpy_s(configPath, root);
        PathAppend(configPath, _T("git-subst.cfg"));
        if (getValueFromPropertiesFile(configPath, zipfile, urlSubst)) {
            zipfile = urlSubst.c_str();
        }
    }

    unzFile uf = unzOpen((voidpf)zipfile);
    if (!uf)
    {
        return false;
    }
    unzGoToFirstFile(uf);
    do {
        TCHAR path[MAX_PATH];
        _tcscpy_s(path, root);

        PathAppend(path, name);

        char filename[MAX_PATH];
        unzGetCurrentFileInfo(uf, 0, filename, sizeof(filename), 0, 0, 0, 0);

        if (auto substr = strchr(filename, '/'))
        {
            PathAppend(path, CA2T(substr, CP_UTF8));
        }
        auto pathlen = _tcslen(path);
        if (pathlen > 0 && (path[pathlen - 1] == _T('/') || path[pathlen - 1] == _T('\\')))
        {
            if (_tmkdir(path) != 0)
                return false;
        }
        else
        {
            unzOpenCurrentFile(uf);

            std::ofstream f(path, std::ofstream::binary);

            char buf[1024 * 64];
            int r;
            do
            {
                r = unzReadCurrentFile(uf, buf, sizeof(buf));
                if (r > 0)
                {
                    f.write(buf, r);
                }
            } while (r > 0);
            unzCloseCurrentFile(uf);
        }
    } while (unzGoToNextFile(uf) == UNZ_OK);

    unzClose(uf);
    return true;
}

std::string getPathWithPackage(const char* url, const TCHAR* name)
{
    // String buffer for holding the path.
    TCHAR strPath[MAX_PATH]{};

    // Get the special folder path.
    SHGetSpecialFolderPath(
        0,       // Hwnd
        strPath, // String buffer
        CSIDL_LOCAL_APPDATA, // CSLID of folder
        TRUE); // Create if doesn't exist?

    CString localAppdataPath = strPath;

    PathAppend(strPath, name);

    if (-1 == _taccess(strPath, 0)
        && (!DownloadAndExtractZip(url, localAppdataPath, name)
            || -1 == _taccess(strPath, 0)))
    {
        return{};
    }

    CT2A const convert(strPath, CP_UTF8);
    LPSTR const pszConvert = convert;
    std::replace(pszConvert, pszConvert + strlen(pszConvert), '\\', '/');

    return pszConvert;
}

std::string loadScriptText(const TCHAR* name)
{
    // String buffer for holding the path.
    TCHAR strPath[MAX_PATH]{};

    // Get the special folder path (e.g., %LOCALAPPDATA%)
    SHGetSpecialFolderPath(
        nullptr,       // Hwnd
        strPath,       // String buffer
        CSIDL_LOCAL_APPDATA, // Folder ID
        FALSE);         // Create if doesn't exist

    // Append the script file name
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
    TCHAR szCmdline[MAX_PATH + 20] = _T("\"");  // Ensure enough space for the path and arguments
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

    // Close process and thread handles.
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return ok;
}

class YouTubeDealer 
{
public:
    YouTubeDealer();
    ~YouTubeDealer();

    bool isValid() const { return !!m_obj; }
    std::vector<std::string> getYoutubeUrl(const std::string& url, bool adaptive);

private:
    boost::python::object m_obj;
};

auto getPytubePathWithPackage()
{
    return getPathWithPackage(PYTUBE_URL, _T("pytube-master"));
}

YouTubeDealer::YouTubeDealer()
{
    if (!isPythonInstalled())
    {
        AfxMessageBox(_T("Matching Python is not installed: ") _T(PY_VERSION)
#ifdef _WIN64
            _T(" (64 bits)")
#else
            _T(" (32 bits)")
#endif
        );
        return;
    }

    using namespace boost::python;

    Py_Initialize();

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

        const auto scriptText = loadScriptText(_T("getYoutubeUrl.py"));
        m_obj = LoadScriptAndGetFunction(scriptText.empty()? SCRIPT_TEMPLATE : scriptText.c_str(),
            "getYoutubeUrl",
            { 
                { "LoggerStream", getLoggerStream() }, 
                { "getPytubePathWithPackage", boost::python::make_function(getPytubePathWithPackage) }
            });

        if (!m_obj)
            Py_Finalize();
    }
    catch (const std::exception& ex)
    {
        BOOST_LOG_TRIVIAL(error) << "getYoutubeUrl() bootstrap exception \"" << ex.what() << "\"";
        Py_Finalize();
        return;
    }
    catch (const error_already_set&)
    {
        BOOST_LOG_TRIVIAL(error) << "getYoutubeUrl() bootstrap error \"" << parse_python_exception() << "\"";
        Py_Finalize();
        return;
    }
}

YouTubeDealer::~YouTubeDealer()
{
    if (isValid())
        Py_Finalize();
}


std::vector<std::string> YouTubeDealer::getYoutubeUrl(const std::string& url, bool adaptive)
{
    BOOST_LOG_TRIVIAL(trace) << "getYoutubeUrl() url = \"" << url << "\"";
    using namespace boost::python;

    try
    {
        object py_result = m_obj(url, adaptive);

        if (py_result.is_none())
            return {};

        // If it's a plain string, return it as a single-element vector
        extract<std::string> as_str(py_result);
        if (as_str.check())
            return { as_str() };

        // Otherwise try to iterate it as a sequence and convert each element to string.
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
                // Fallback: call Python's str() on the element and extract C++ string
                result.push_back(extract<std::string>(str(*it))());
            }
        }
        return result;
    }
    catch (const error_already_set&)
    {
        BOOST_LOG_TRIVIAL(error) << "getYoutubeUrl() python error \"" << parse_python_exception() << "\"";
        PyErr_Clear();
    }
    catch (const std::exception& ex)
    {
        BOOST_LOG_TRIVIAL(error) << "getYoutubeUrl() exception \"" << ex.what() << "\"";
    }

    return {};
}


class YouTubeTranscriptDealer
{
public:
    YouTubeTranscriptDealer();
    ~YouTubeTranscriptDealer();

    bool isValid() const { return !!m_obj; }
    bool getYoutubeTranscripts(const std::string& id, AddYoutubeTranscriptCallback cb);

private:
    boost::python::object m_obj;
};

auto getTranscriptPathWithPackage()
{
    return getPathWithPackage(YOUTUBE_TRANSCRIPT_API_URL, _T("youtube-transcript-api-master"));
}

YouTubeTranscriptDealer::YouTubeTranscriptDealer()
{
    if (!Py_IsInitialized())
    {
        return;
    }

    using namespace boost::python;

    //Py_Initialize();
    try {
        const auto scriptText = loadScriptText(_T("getYoutubeTranscript.py"));
        m_obj = LoadScriptAndGetFunction(scriptText.empty() ? TRANSCRIPT_TEMPLATE : scriptText.c_str(),
            "getYoutubeTranscript",
            {
                { "getTranscriptPathWithPackage", boost::python::make_function(getTranscriptPathWithPackage) }
            });
        //if (!m_obj)
        //    Py_Finalize();
    }
    catch (const std::exception& ex)
    {
        BOOST_LOG_TRIVIAL(error) << "YouTubeTranscriptDealer() bootstrap exception \"" << ex.what() << "\"";
        //Py_Finalize();
        return;
    }
    catch (const error_already_set&)
    {
        BOOST_LOG_TRIVIAL(error) << "YouTubeTranscriptDealer() bootstrap error \"" << parse_python_exception() << "\"";
        //Py_Finalize();
        return;
    }
}

YouTubeTranscriptDealer::~YouTubeTranscriptDealer()
{
    //if (isValid())
    //    Py_Finalize();
}


bool YouTubeTranscriptDealer::getYoutubeTranscripts(const std::string& id, AddYoutubeTranscriptCallback cb)
{
    BOOST_LOG_TRIVIAL(trace) << "getYoutubeTranscripts() id = \"" << id << "\"";
    using namespace boost::python;
    try
    {
        const auto v = m_obj(id);
        const auto length = len(v);
        for (int i = 0; i < length; ++i)
        {
            const auto& el = v[i];
            std::string text = extract<std::string>(el["text"]);
            boost::algorithm::trim_right(text);
            if (!text.empty())
            {
                cb(extract<double>(el["start"]),
                    extract<double>(el["duration"]),
                    text);
            }
        }
        return true;
    }
    catch (const std::exception& ex)
    {
        BOOST_LOG_TRIVIAL(error) << "getYoutubeTranscripts() exception \"" << ex.what() << "\"";
    }
    catch (const error_already_set&)
    {
        BOOST_LOG_TRIVIAL(error) << "getYoutubeTranscripts() error \"" << parse_python_exception() << "\"";
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

std::pair<std::string, std::string> getYoutubeUrl(std::string url, bool adaptive, bool useSAN)
{
    enum { ATTEMPTS_NUMBER = 2 };

    if (extractHttpOrHostUrl(url))
    {
        static std::map<std::string, std::pair<std::string, std::string>> mapToDownloadLinks[2];
        auto it = mapToDownloadLinks[adaptive].find(url);
        if (it != mapToDownloadLinks[adaptive].end())
        {
            if (isHttpStatusOk(HttpGetStatus(it->second.first, useSAN))
                    && (it->second.second.empty() || isHttpStatusOk(HttpGetStatus(it->second.second, useSAN))))
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
                    const auto status = HttpGetStatus(urls[0], useSAN);
                    BOOST_LOG_TRIVIAL(trace) << "Resource status: " << status;
                    if (isHttpStatusOk(status))
                    {
                        if (urls.size() > 1)
                        {
                            for (int i = 1; i < urls.size(); ++i)
                            {
                                const auto status = HttpGetStatus(urls[i], useSAN);
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

std::pair<std::string, std::string> getYoutubeUrl(std::string url, bool /*adaptive*/, bool /*useSAN*/)
{
    return{ url, {} };
}

bool getYoutubeTranscripts(std::string, AddYoutubeTranscriptCallback)
{
    return false;
}

#endif // YOUTUBE_EXPERIMENT
