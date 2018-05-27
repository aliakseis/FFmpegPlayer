
// PlayerDoc.h : interface of the CPlayerDoc class
//

#pragma once

#pragma warning(disable:4996)

#include <memory>
#include <atomic>
#include <deque>
#include <string>

#include <boost/signals2/signal.hpp>

#include "decoderinterface.h"

enum { UPDATE_HINT_CLOSING = 1 };

class CPlayerDoc : public CDocument, public FrameDecoderListener
{
protected: // create from serialization only
    CPlayerDoc();
    DECLARE_DYNCREATE(CPlayerDoc)

// Attributes
public:
    IFrameDecoder* getFrameDecoder() const { return m_frameDecoder.get(); }

// Operations
public:

// Overrides
public:
    virtual BOOL OnNewDocument();
    virtual void Serialize(CArchive& ar);
    virtual void OnCloseDocument();
#ifdef SHARED_HANDLERS
    virtual void InitializeSearchContent();
    virtual void OnDrawThumbnail(CDC& dc, LPRECT lprcBounds);
#endif // SHARED_HANDLERS

// Implementation
public:
    virtual ~CPlayerDoc();
#ifdef _DEBUG
    virtual void AssertValid() const;
    virtual void Dump(CDumpContext& dc) const;
#endif

protected:
    void changedFramePosition(long long start, long long frame, long long total) override;
    void onEndOfStream() override;

// Generated message map functions
protected:
    afx_msg void OnAudioTrack(UINT id);
    afx_msg void OnUpdateAudioTrack(CCmdUI* pCmdUI);
    afx_msg void OnAutoplay();
    afx_msg void OnUpdateAutoplay(CCmdUI *pCmdUI);
    DECLARE_MESSAGE_MAP()

#ifdef SHARED_HANDLERS
    // Helper function that sets search content for a Search Handler
    void SetSearchContent(const CString& value);
#endif // SHARED_HANDLERS
public:
    BOOL OnOpenDocument(LPCTSTR lpszPathName) override;
    BOOL OnSaveDocument(LPCTSTR) override { return FALSE; }
    void OnIdle() override;

    bool pauseResume();
    bool seekByPercent(double percent);
    void setVolume(double volume);

    bool isPlaying() const;
    bool isPaused() const;
    double soundVolume() const;

    boost::signals2::signal<void(long long, long long)> framePositionChanged;
    boost::signals2::signal<void(double)> totalTimeUpdated;
    boost::signals2::signal<void(double)> currentTimeUpdated;

    std::string getSubtitle() const;
    bool isUnicodeSubtitles() const { return m_unicodeSubtitles; }

private:
    bool OpenSubRipFile(LPCTSTR lpszVideoPathName);
    bool OpenSubStationAlphaFile(LPCTSTR lpszVideoPathName);
    void MoveToNextFile();
    bool openUrl(std::string url);

private:
    std::unique_ptr<IFrameDecoder> m_frameDecoder;

    std::atomic<double> m_currentTime;

    class SubtitlesMap;
    std::unique_ptr<SubtitlesMap> m_subtitles;
    bool m_unicodeSubtitles;
    bool m_onEndOfStream;
    bool m_autoPlay;

    std::deque<std::string> m_playList;
};
