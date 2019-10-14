#pragma once

#include <string>
#include <functional>
#include <vector>

std::vector<std::string> ParsePlaylist(std::string url, bool force);

std::vector<std::string> ParsePlaylistFile(const TCHAR* fileName);

std::string getYoutubeUrl(std::string url);

// start, duration, text
typedef std::function<void(double, double, const std::string&)> AddYoutubeTranscriptCallback;

bool getYoutubeTranscripts(std::string url, AddYoutubeTranscriptCallback cb);
