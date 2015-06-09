
// PlayerDoc.h : interface of the CPlayerDoc class
//


#pragma once

#include <memory>
#include <atomic>

#include "decoderinterface.h"

[event_source(native)]
class CPlayerDoc : public CDocument, public FrameDecoderListener
{
protected: // create from serialization only
    CPlayerDoc();
    DECLARE_DYNCREATE(CPlayerDoc)

// Attributes
public:
    IFrameDecoder* getFrameDecoder() { return m_frameDecoder.get(); }

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
    void changedFramePosition(long long frame, long long total) override;

// Generated message map functions
protected:
    DECLARE_MESSAGE_MAP()

#ifdef SHARED_HANDLERS
    // Helper function that sets search content for a Search Handler
    void SetSearchContent(const CString& value);
#endif // SHARED_HANDLERS
public:
    virtual BOOL OnOpenDocument(LPCTSTR lpszPathName);

    bool pauseResume();
    bool seekByPercent(double percent, int64_t totalDuration = -1);
    void setVolume(double volume);

    bool isPlaying() const;
    bool isPaused() const;
    double soundVolume() const;

    __event void framePositionChanged(long long frame, long long total);
    __event void totalTimeUpdated(double secs);
    __event void currentTimeUpdated(double secs);

    std::string getSubtitle();

private:
    void OpenSubRipFile(LPCTSTR lpszVideoPathName);

private:
    std::unique_ptr<IFrameDecoder> m_frameDecoder;

    std::atomic<double> m_currentTime;

    class SubtitlesMap;
    std::unique_ptr<SubtitlesMap> m_subtitles;
};
