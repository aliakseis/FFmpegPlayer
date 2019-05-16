#pragma once

#include <string>
#include <vector>

std::vector<std::string> ParsePlaylist(const std::string& url, bool force);

std::vector<std::string> ParsePlaylistFile(const TCHAR* fileName);

std::string getYoutubeUrl(std::string url);

struct TranscriptRecord
{
    std::string text;
    double start, duration;
};

std::vector<TranscriptRecord> getYoutubeTranscripts(std::string url);