#pragma once

#include <string>
#include <vector>

std::vector<std::string> ParsePlaylist(const std::string& url);

std::vector<std::string> ParsePlaylistFile(const TCHAR* fileName);

std::string getYoutubeUrl(std::string url);
