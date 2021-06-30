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


const char PYTUBE_URL[] = "https://github.com/pytube/pytube/archive/master.zip";
const char YOUTUBE_TRANSCRIPT_API_URL[] = "https://github.com/jdepoix/youtube-transcript-api/archive/master.zip";

const char SCRIPT_TEMPLATE[] = R"(import sys, socket
sys.stderr = LoggerStream()

def install_and_import(package):
    import importlib
    try:
        importlib.import_module(package)
    except ImportError:
        import subprocess
        subprocess.call(["pip", "install", package])
    finally:
        globals()[package] = importlib.import_module(package)

install_and_import('typing_extensions')
sys.path.append("%s")
from pytube import YouTube
def getYoutubeUrl(url, adaptive):
    socket.setdefaulttimeout(10)
    s=YouTube(url).streams
    if(adaptive):
        return [s.get_audio_only().url] \
            + [x.url for x in s.filter(only_video=True).order_by('resolution').desc() \
            if not x.video_codec.startswith("av01")] 
    else:
        return s.get_highest_resolution().url)";


const char TRANSCRIPT_TEMPLATE[] = R"(import sys
sys.stderr = LoggerStream()

install_and_import('requests')
sys.path.append("%s")
from youtube_transcript_api import YouTubeTranscriptApi
def getYoutubeTranscript(video_id):
	return YouTubeTranscriptApi.get_transcript(video_id))";


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


auto getLoggerStream()
{
    return boost::python::class_<LoggerStream>("LoggerStream")
        .def("write", &LoggerStream::write)
        .def("flush", &LoggerStream::flush);
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


YouTubeDealer::YouTubeDealer()
{
    const auto packagePath = getPathWithPackage(PYTUBE_URL, _T("pytube-master"));
    if (packagePath.empty())
    {
        return;
    }

    using namespace boost::python;

    Py_Initialize();
    try {
        char script[4096];
        sprintf_s(script, SCRIPT_TEMPLATE, packagePath.c_str());

        m_obj = LoadScriptAndGetFunction(script, "getYoutubeUrl",
            { { "LoggerStream", getLoggerStream() } });

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
        if (adaptive)
        {
            std::vector<std::string> result;
            const auto v = m_obj(url, true);
            const auto length = len(v);
            for (int i = 0; i < length; ++i)
            {
                result.push_back(extract<std::string>(v[i]));
            }
            return result;
        }
        else
            return{ extract<std::string>(m_obj(url, false)) };
    }
    catch (const std::exception& ex)
    {
        BOOST_LOG_TRIVIAL(error) << "getYoutubeUrl() exception \"" << ex.what() << "\"";
    }
    catch (const error_already_set&)
    {
        BOOST_LOG_TRIVIAL(error) << "getYoutubeUrl() error \"" << parse_python_exception() << "\"";
    }

    return{ };
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


YouTubeTranscriptDealer::YouTubeTranscriptDealer()
{
    const auto packagePath = getPathWithPackage(
        YOUTUBE_TRANSCRIPT_API_URL, _T("youtube-transcript-api-master"));
    if (packagePath.empty())
    {
        return;
    }

    using namespace boost::python;

    //Py_Initialize();
    try {
        char script[4096];
        sprintf_s(script, TRANSCRIPT_TEMPLATE, packagePath.c_str());

        m_obj = LoadScriptAndGetFunction(script, "getYoutubeTranscript", {});
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
            cb(extract<double>(el["start"]),
               extract<double>(el["duration"]),
               extract<std::string>(el["text"]));
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

    auto doSearch = [pDataBegin, pDataEnd, &result](const auto& watch) {
        auto pData = pDataBegin;
        while ((pData = std::search(pData, pDataEnd, std::begin(watch), std::prev(std::end(watch)))) != pDataEnd)
        {
            const auto localEnd = std::find_if(pData, pDataEnd, [](char ch) {
                return ch == '&' || ch == '"' || ch == '\'' || ch == '\\' || std::isspace(static_cast<unsigned char>(ch));
            });
            auto el = "https://www.youtube.com" + std::string(pData, localEnd);
            if (std::find(result.begin(), result.end(), el) == result.end())
                result.push_back(std::move(el));
            pData += sizeof(watch) / sizeof(watch[0]) - 1;
        }
    };

    doSearch("/watch?v=");
    if (includeLists)
    {
        doSearch("/playlist?list=");
    }

    return result;
}


} // namespace


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
    if (!_tcsstr(fileName, _T("playlist")) && !_tcsstr(fileName, _T("watch"))
		&& (_tcslen(fileName) <= 5 || _tcsicmp(fileName + _tcslen(fileName) - 5, _T(".html")) != 0))
        return{};

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

std::pair<std::string, std::string> getYoutubeUrl(std::string url, bool adaptive)
{
    enum { ATTEMPTS_NUMBER = 2 };

    if (extractYoutubeUrl(url))
    {
        static std::map<std::string, std::pair<std::string, std::string>> mapToDownloadLinks[2];
        auto it = mapToDownloadLinks[adaptive].find(url);
        if (it != mapToDownloadLinks[adaptive].end())
        {
            if (HttpGetStatus(it->second.first.c_str()) == 200
                    && (it->second.second.empty() || HttpGetStatus(it->second.second.c_str()) == 200))
                return it->second;
            else
                mapToDownloadLinks[adaptive].erase(it);
        }

        CWaitCursor wait;
        static YouTubeDealer buddy;
        if (buddy.isValid())
        {
            for (int i = 0; i < ATTEMPTS_NUMBER; ++i)
            {
                auto urls = buddy.getYoutubeUrl(url, adaptive);
                if (!urls.empty())
                {
                    const auto status = HttpGetStatus(urls[0].c_str());
                    BOOST_LOG_TRIVIAL(trace) << "Resource status: " << status;
                    if (status == 200)
                    {
                        if (adaptive)
                        {
                            for (int i = 1; i < urls.size(); ++i)
                            {
                                const auto status = HttpGetStatus(urls[i].c_str());
                                BOOST_LOG_TRIVIAL(trace) << "Resource status: " << status;
                                if (status == 200)
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
    if (extractYoutubeId(url))
    {
        CWaitCursor wait;
        static YouTubeTranscriptDealer buddy;
        if (buddy.isValid())
            return buddy.getYoutubeTranscripts(url, cb);
    }

    return{};
}

#else // YOUTUBE_EXPERIMENT

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

std::pair<std::string, std::string> getYoutubeUrl(std::string url, bool /*adaptive*/)
{
    return{ url, {} };
}

bool getYoutubeTranscripts(std::string, AddYoutubeTranscriptCallback)
{
    return false;
}

#endif // YOUTUBE_EXPERIMENT
