#pragma once

#include <string>
#include <functional>
#include <vector>

std::vector<std::string> ParsePlaylist(std::string url, bool force);

std::vector<std::string> ParsePlaylistFile(const TCHAR* fileName);

std::vector<std::string> ParsePlaylistText(const std::string& text);

std::pair<std::string, std::string> getYoutubeUrl(std::string url, bool adaptive);

// start, duration, text
typedef std::function<void(double, double, const std::string&)> AddYoutubeTranscriptCallback;

bool getYoutubeTranscripts(std::string url, AddYoutubeTranscriptCallback cb);
