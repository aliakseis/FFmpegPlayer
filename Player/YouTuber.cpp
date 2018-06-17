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

#include "unzip.h"

namespace {

const char PYTUBE_URL[] = "https://github.com/nficano/pytube/archive/master.zip";

const char SCRIPT_TEMPLATE[] = R"(import sys
sys.path.append("%s")
from pytube import YouTube
def getYoutubeUrl(url):
	return YouTube(url).streams.filter(progressive=True).order_by('resolution').desc().first().url)";


char* replace_char(char* str, char find, char replace)
{
    char *current_pos = strchr(str,find);
    while (current_pos) {
        *current_pos = replace;
        current_pos = strchr(++current_pos, find);
    }
    return str;
}

bool isUrlYoutube(const std::string& url)
{
    std::regex txt_regex(R"(^(http(s)?:\/\/)?((w){3}.)?youtu(be|.be)?(\.com)?\/.+)");
    return std::regex_match(url, txt_regex);
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
        strPath, // String buffer.
        CSIDL_LOCAL_APPDATA, // CSLID of folder
        TRUE); // Create if doesn't exists?}

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


        CT2A convert(strPath, CP_UTF8);

        replace_char(convert, '\\', '/');

        char script[4096];
        sprintf_s(script, SCRIPT_TEMPLATE, static_cast<LPSTR>(convert));

        // Define greet function in Python.
        object exec_result = exec(script, global, global);

        // Create a reference to it.
        m_obj = global["getYoutubeUrl"];
        if (!m_obj)
            Py_Finalize();
    }
    catch (const std::exception&)
    {
        Py_Finalize();
        return;
    }
    catch (const error_already_set&)
    {
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
    catch (const std::exception&)
    {
    }
    catch (const error_already_set&)
    {
    }

    BOOST_LOG_TRIVIAL(trace) << "getYoutubeUrl() returning \"" << result << "\"";

    return result;
}


} // namespace


std::string getYoutubeUrl(const std::string& url)
{
    if (isUrlYoutube(url))
    {
        static YouTubeDealer buddy;
        if (buddy.isValid())
            return buddy.getYoutubeUrl(url);
    }

    return url;
}


#else // YOUTUBE_EXPERIMENT

std::string getYoutubeUrl(const std::string& url)
{
    return url;
}

#endif // YOUTUBE_EXPERIMENT
