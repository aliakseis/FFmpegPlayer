#include "stdafx.h"

#include "YouTuber.h"

//#define YOUTUBE_EXPERIMENT

#ifdef YOUTUBE_EXPERIMENT

#include <Shlobj.h>

#include <boost/python/exec.hpp>
#include <boost/python/import.hpp>
#include <boost/python/extract.hpp>

#include <boost/log/trivial.hpp>

#include <regex>
#include <fstream>
#include <iterator>
#include <streambuf>
#include <algorithm>
#include <utility>

#include "unzip.h"
#include "http_get.h"

namespace {

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
        if(returned.check())
            ret +=  ": " + returned();
        else
            ret += std::string(": Unparseable Python error: ");
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
            ret += std::string(": Unparseable Python traceback");
    }
    return ret;
}

const char PYTUBE_URL[] = "https://github.com/nficano/pytube/archive/master.zip";

const char SCRIPT_TEMPLATE[] = R"(import sys
sys.path.append("%s")
from pytube import YouTube
def getYoutubeUrl(url):
	return YouTube(url).streams.filter(progressive=True).order_by('resolution').desc().first().url)";


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

bool DownloadAndExtractZip(const char* zipfile, const TCHAR* root)
{
    unzFile uf = unzOpen((voidpf)zipfile);
    if (!uf)
    {
        return false;
    }
    unzGoToFirstFile(uf);
    do {
        char filename[MAX_PATH];
        unzGetCurrentFileInfo(uf, 0, filename, sizeof(filename), 0, 0, 0, 0);

        TCHAR path[MAX_PATH];
        _tcscpy_s(path, root);
        PathAppend(path, CA2T(filename, CP_UTF8));

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

class YouTubeDealer 
{
public:
    YouTubeDealer();
    ~YouTubeDealer();

    bool isValid() const { return !!m_obj; }
    std::string getYoutubeUrl(const std::string& url);

private:
    boost::python::object m_obj;
};


YouTubeDealer::YouTubeDealer()
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

    PathAppend(strPath, _T("pytube-master"));

    if (-1 == _taccess(strPath, 0)
        && (!DownloadAndExtractZip(PYTUBE_URL, localAppdataPath)
            || -1 == _taccess(strPath, 0)))
    {
        return;
    }
    using namespace boost::python;

    Py_Initialize();
    try {
        // Retrieve the main module.
        object main = import("__main__");

        // Retrieve the main module's namespace
        object global(main.attr("__dict__"));

        CT2A const convert(strPath, CP_UTF8);
        LPSTR const pszConvert = convert;
        std::replace(pszConvert, pszConvert + strlen(pszConvert), '\\', '/');

        char script[4096];
        sprintf_s(script, SCRIPT_TEMPLATE, pszConvert);

        // Define function in Python.
        object exec_result = exec(script, global, global);

        // Create a reference to it.
        m_obj = global["getYoutubeUrl"];
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


std::string YouTubeDealer::getYoutubeUrl(const std::string& url)
{
    using namespace boost::python;
    std::string result;
    try
    {
        result = extract<std::string>(m_obj(url));
    }
    catch (const std::exception& ex)
    {
        BOOST_LOG_TRIVIAL(error) << "getYoutubeUrl() exception \"" << ex.what() << "\"";
    }
    catch (const error_already_set&)
    {
        BOOST_LOG_TRIVIAL(error) << "getYoutubeUrl() error \"" << parse_python_exception() << "\"";
    }

    BOOST_LOG_TRIVIAL(trace) << "getYoutubeUrl() returning \"" << result << "\"";

    return result;
}


} // namespace


std::vector<std::string> ParsePlaylist(const std::string& url)
{
    if (url.find("/playlist?list=") == std::string::npos)
        return{};

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

    char* pData = nullptr;

    hr = SafeArrayAccessData(psa, (void**)&pData);
    if (FAILED(hr))
        return{};

    const char watch[] = "/watch?v=";


    std::vector<std::string> result;

    char* const pDataEnd = pData + iUBound - iLBound + 1;

    while ((pData = std::search(pData, pDataEnd, std::begin(watch), std::prev(std::end(watch)))) != pDataEnd)
    {
        const auto localEnd = std::find(pData, pDataEnd, '&');
        auto el = "https://www.youtube.com" + std::string(pData, localEnd);
        if (std::find(result.begin(), result.end(), el) == result.end())
            result.push_back(std::move(el));
        pData += sizeof(watch) / sizeof(watch[0]) - 1;
    }

    SafeArrayUnaccessData(psa);

    return result;
}

std::string getYoutubeUrl(std::string url)
{
    if (extractYoutubeUrl(url))
    {
        CWaitCursor wait;
        static YouTubeDealer buddy;
        if (buddy.isValid())
            return buddy.getYoutubeUrl(url);
    }

    return url;
}


#else // YOUTUBE_EXPERIMENT

std::vector<std::string> ParsePlaylist(const std::string& url)
{
    return{};
}

std::string getYoutubeUrl(std::string url)
{
    return url;
}

#endif // YOUTUBE_EXPERIMENT
