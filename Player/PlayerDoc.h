
// PlayerDoc.h : interface of the CPlayerDoc class
//

#pragma once

#pragma warning(disable:4996)

#include <memory>
#include <atomic>
#include <deque>
#include <string>
#include <functional>

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
    BOOL OnNewDocument() override;
    void Serialize(CArchive& ar) override;
    void OnCloseDocument() override;
#ifdef SHARED_HANDLERS
    virtual void InitializeSearchContent();
    virtual void OnDrawThumbnail(CDC& dc, LPRECT lprcBounds);
#endif // SHARED_HANDLERS

// Implementation
public:
    ~CPlayerDoc() override;
#ifdef _DEBUG
    void AssertValid() const override;
    void Dump(CDumpContext& dc) const override;
#endif

protected:
    void changedFramePosition(long long start, long long frame, long long total) override;
    void fileLoaded(long long start, long long total) override;
    void onEndOfStream(bool error) override;

// Generated message map functions
protected:
    afx_msg void OnAudioTrack(UINT id);
    afx_msg void OnUpdateAudioTrack(CCmdUI* pCmdUI);
    afx_msg void OnVideoSpeed(UINT id);
    afx_msg void OnUpdateVideoSpeed(CCmdUI* pCmdUI);
    afx_msg void OnAutoplay();
    afx_msg void OnUpdateAutoplay(CCmdUI *pCmdUI);
    afx_msg void OnLooping();
    afx_msg void OnUpdateLooping(CCmdUI *pCmdUI);
    afx_msg void OnOpensubtitlesfile();
    afx_msg void OnUpdateOpensubtitlesfile(CCmdUI *pCmdUI);
    afx_msg void OnCopyUrlToClipboard();
    afx_msg void OnGetSubtitles(UINT id);
    DECLARE_MESSAGE_MAP()

#ifdef SHARED_HANDLERS
    // Helper function that sets search content for a Search Handler
    void SetSearchContent(const CString& value);
#endif // SHARED_HANDLERS
public:
    BOOL OnOpenDocument(LPCTSTR lpszPathName) override;
	BOOL DoFileSave() override { return FALSE; }
    BOOL OnSaveDocument(LPCTSTR) override;
    void OnIdle() override;
    void OnFileSaveCopyAs();

    bool pauseResume();
    bool nextFrame();
    bool seekByPercent(double percent);
    void setVolume(double volume);

    bool isPlaying() const;
    bool isPaused() const;
    double soundVolume() const;

    boost::signals2::signal<void(long long, long long)> framePositionChanged;
    boost::signals2::signal<void(double)> startTimeUpdated;
    boost::signals2::signal<void(double)> totalTimeUpdated;
    boost::signals2::signal<void(double)> currentTimeUpdated;

    boost::signals2::signal<void(long long, long long)> rangeStartTimeChanged;
    boost::signals2::signal<void(long long, long long)> rangeEndTimeChanged;

    boost::signals2::signal<void(bool)> onPauseResume;

    boost::signals2::signal<void()> onDestructing;

    std::wstring getSubtitle() const;

    void OnDropFiles(HDROP hDropInfo);
    void OnEditPaste(const std::string& text);

    double getCurrentTime() const { return m_currentTime; }
	double getStartTime() const { return m_startTime; }
	double getEndTime() const { return m_endTime; }

    void setRangeStartTime(double time);
    void setRangeEndTime(double time);

	bool isFullFrameRange() const;

    void OnAsyncUrl(const CString& url);

    bool isOrientationMirrorx() const { return m_bOrientationMirrorx; }
    bool isOrientationMirrory() const { return m_bOrientationMirrory; }
    bool isOrientationUpend() const { return m_bOrientationUpend; }

private:
    void MoveToNextFile();

    bool openDocument(LPCTSTR lpszPathName, bool openSeparateFile = false);
    bool openTopLevelUrl(const CString& url, bool force, const CString& pathName = {});
    bool openUrl(const std::string& url, const std::string& inputFormat = {});
    bool openUrlFromList();
    bool openUrlFromList(const std::vector<std::string>& playList, const CString& pathName = {});

    void reset();

    float getVideoSpeed() const;

private:
    std::unique_ptr<IFrameDecoder> m_frameDecoder;

    std::atomic<double> m_currentTime;
    double m_startTime;
    double m_endTime;

    double m_rangeStartTime{};
    double m_rangeEndTime{};

    class SubtitlesMap;
    std::shared_ptr<SubtitlesMap> m_subtitles;
    bool m_onEndOfStream = false;
    bool m_autoPlay = false;
    bool m_looping = false;

    std::string m_originalUrl;
    std::string m_url;

    class StringDifference;
    std::unique_ptr<StringDifference> m_separateFileDiff;
    std::unique_ptr<StringDifference> m_subtitlesFileDiff;

    std::deque<std::string> m_playList;
    std::function<void()> m_reopenFunc;

    bool m_nightcore = false;

    unsigned int m_documentGeneration = 0;

    bool m_maximalResolution = false;
    bool m_superResolution = false;

    bool m_bOrientationMirrorx = false;
    bool m_bOrientationMirrory = false;
    bool m_bOrientationUpend = false;

public:
    afx_msg void OnMaximalresolution();
    afx_msg void OnUpdateMaximalresolution(CCmdUI *pCmdUI);
    afx_msg void OnHwAcceleration();
    afx_msg void OnUpdateHwAcceleration(CCmdUI *pCmdUI);
    afx_msg void OnSuperResolution();
    afx_msg void OnUpdateSuperResolution(CCmdUI *pCmdUI);
    afx_msg void OnOrientationMirrorx();
    afx_msg void OnUpdateOrientationMirrorx(CCmdUI *pCmdUI);
    afx_msg void OnOrientationMirrory();
    afx_msg void OnUpdateOrientationMirrory(CCmdUI *pCmdUI);
    afx_msg void OnOrientationUpend();
    afx_msg void OnUpdateOrientationUpend(CCmdUI *pCmdUI);
    afx_msg void OnOrientationDoNothing();
    afx_msg void OnOrientationRotate90();
    afx_msg void OnOrientationRotate180();
    afx_msg void OnOrientationRotate270();
};
