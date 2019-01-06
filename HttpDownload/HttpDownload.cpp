// HttpDownload.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "http_download.h"

#include <stdlib.h>
#include <exception>
#include <iostream>

int _tmain(int argc, TCHAR *argv[])
{
    if (argc != 3)
        return EXIT_FAILURE;
    try
    {
        const bool ok = HttpDownload(argv[1], argv[2]);
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& ex)
    { 
        std::cerr << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
