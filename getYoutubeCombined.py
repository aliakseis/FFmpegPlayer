import sys, platform, socket, re
import os, json, requests, subprocess, importlib

sys.stdout = LoggerStream()
sys.stderr = LoggerStream()

py_version = f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}"

# 64bit / 32bit
arch = platform.architecture()[0]

STATE_FILE = f"YoutubeCombined_packages-{py_version}-{arch}.json"

def _load_state():
    return json.load(open(STATE_FILE)) if os.path.exists(STATE_FILE) else {}

def _save_state(state):
    json.dump(state, open(STATE_FILE, "w"))

def install_and_import(package, url=None):
    importlib.invalidate_caches()
    state = _load_state()
    if url is None:
        url = package

    prev = state.get(url, {})
    headers = {}
    if prev.get("etag"):
        headers["If-None-Match"] = prev["etag"]
    if prev.get("last_modified"):
        headers["If-Modified-Since"] = prev["last_modified"]

    # HEAD
    try:
        r = requests.head(url, allow_redirects=True, timeout=10, headers=headers)
    except Exception as e:
        print(f"[WARN] HEAD failed for {url}: {e}")
        r = None

    need_install = False
    reason = "initial install"

    if r:
        if r.status_code == 304:
            need_install = False
            reason = "304 Not Modified"
        else:
            etag = r.headers.get("ETag")
            last_modified = r.headers.get("Last-Modified")
            size = r.headers.get("Content-Length")

            if etag and etag == prev.get("etag"):
                need_install = False
                reason = "ETag match"
            elif last_modified and last_modified == prev.get("last_modified"):
                need_install = False
                reason = "Last-Modified match"
            elif size and size == prev.get("size"):
                need_install = False
                reason = "Content-Length match"
            else:
                need_install = True
                reason = "metadata changed"

    try:
        importlib.import_module(package)
        if need_install:
            raise ImportError("force reinstall")
        print(f"[INFO] {package} already up-to-date ({reason})")
    except ImportError:
        print(f"[INFO] Installing {package} from {url} ({reason})...")
        library_dir = os.path.dirname(os.path.abspath(socket.__file__))
        pip_path = os.path.join(library_dir, "../scripts/pip3")

        cmd = [pip_path, "install", "--force-reinstall", url]

        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        print("[INFO] pip output:\n", result.stdout)

        for line in result.stdout.splitlines():
            if "Installing collected packages" in line or "Successfully installed" in line:
                print("[INFO] Dependencies updated:", line)

        if r:
            state[url] = {
                "etag": r.headers.get("ETag"),
                "last_modified": r.headers.get("Last-Modified"),
                "size": r.headers.get("Content-Length"),
            }
            _save_state(state)

    globals()[package] = importlib.import_module(package)

# ---- yt-dlp based getYoutubeUrl ----
install_and_import("yt_dlp", "https://github.com/yt-dlp/yt-dlp/archive/refs/heads/master.zip")

from urllib.parse import urlencode, urlparse, parse_qs

def parsePlaylist(url: str, force: bool):
    # 1. Detect search query (no dots or slashes)
    if all(ch not in url for ch in "./"):
        parts = url.split()
        query = "+".join(urlencode({"q": p})[2:] for p in parts)
        search_url = f"ytsearch50:{query}"
        return _extract_flat_urls(search_url)

    # 2. URL case
    parsed = urlparse(url)
    qs = parse_qs(parsed.query)

    is_list = "list" in qs
    is_channel = _is_channel_url(url)

    # Playlist or channel -> treat as list
    if is_list or is_channel:
        return _extract_flat_urls(url)

    # 3. Single video page
    if force:
        return _extract_from_video_page(url)

    return []


def _is_channel_url(url: str):
    """Detects YouTube channel URLs but does NOT treat them as results."""
    return any(part in url for part in [
        "/channel/",
        "/user/",
        "/c/",
        "/@"
    ])


def _extract_flat_urls(yt_url: str):
    ydl_opts = {
        "quiet": True,
        "skip_download": True,
        "extract_flat": True,
    }

    with yt_dlp.YoutubeDL(ydl_opts) as ydl:
        info = ydl.extract_info(yt_url, download=False)

    entries = info.get("entries", [])
    urls = []

    for e in entries:
        if "url" in e:
            # Filter out channel URLs
            if not _is_channel_url(e["url"]):
                urls.append(e["url"])

    return urls


def _extract_from_video_page(video_url: str):
    ydl_opts = {
        "quiet": True,
        "skip_download": True,
        "extract_flat": False,
    }

    with yt_dlp.YoutubeDL(ydl_opts) as ydl:
        info = ydl.extract_info(video_url, download=False)

    urls = []

    # The video itself
    if "webpage_url" in info:
        urls.append(info["webpage_url"])

    # Playlist URLs (if video is inside a playlist)
    for pl in info.get("playlists", []):
        if "id" in pl:
            urls.append(f"https://www.youtube.com/playlist?list={pl['id']}")

    # Try to approximate "related videos" using search
    title = info.get("title")
    if title:
        search_query = f"ytsearch10:{title}"
        with yt_dlp.YoutubeDL({"quiet": True, "extract_flat": True}) as ydl:
            s = ydl.extract_info(search_query, download=False)
            for e in s.get("entries", []):
                if "url" in e and not _is_channel_url(e["url"]):
                    urls.append(e["url"])

    # Deduplicate
    urls = list(dict.fromkeys(urls))

    return urls


def _iter_formats(info):
    if not info:
        return []
    if isinstance(info, dict) and 'entries' in info and info['entries']:
        for e in info['entries']:
            if e:
                info = e
                break
    if isinstance(info, dict) and 'requested_formats' in info and info['requested_formats']:
        return info['requested_formats']
    if isinstance(info, dict) and 'formats' in info and info['formats']:
        return info['formats']
    if isinstance(info, dict) and 'url' in info:
        return [info]
    return []

def _is_usable_video_format(f):
    vcodec = f.get('vcodec')
    proto = f.get('protocol', '')
    if not vcodec or vcodec == 'none':
        return False
    if vcodec.startswith('av01'):
        return False
    if proto == 'm3u8_native' or proto == 'm3u8':
        return False
    return True

def _is_usable_audio_format(f):
    vcodec = f.get('vcodec', 'none')
    proto = f.get('protocol', '')
    if vcodec != 'none':
        return False
    if proto == 'm3u8_native' or proto == 'm3u8':
        return False
    return 'audio_channels' in f and f.get('audio_channels') is not None or 'abr' in f

def getYoutubeUrl(url, adaptive):
    socket.setdefaulttimeout(10)
    base_opts = {
        'noplaylist': True,
        'quiet': True,
        'skip_download': True,
    }
    ydl_opts = base_opts.copy()
    ydl_opts['format'] = 'bestvideo+bestaudio' if adaptive else 'best'
    with yt_dlp.YoutubeDL(ydl_opts) as ydl:
        info = ydl.extract_info(url, download=False)
    formats = _iter_formats(info)
    if adaptive:
        video_candidates = [f for f in formats if _is_usable_video_format(f)]
        if video_candidates:
            best_video = max(video_candidates, key=lambda f: (f.get('height') or 0, f.get('tbr') or 0))
            video_url = best_video.get('url')
        else:
            video_url = info.get('url')
        audio_candidates = [f for f in formats if _is_usable_audio_format(f)]
        if audio_candidates:
            best_audio = max(audio_candidates, key=lambda f: (f.get('audio_channels') or 0, f.get('abr') or 0))
            audio_url = best_audio.get('url')
        else:
            any_audio = next((f for f in formats if f.get('vcodec') == 'none' and f.get('url')), None)
            audio_url = any_audio.get('url') if any_audio else None
        return (video_url, audio_url)
    combined_candidates = [f for f in formats if f.get('vcodec') != 'none' and f.get('audio_channels') is not None and not f.get('vcodec','').startswith('av01') and f.get('protocol') not in ('m3u8_native', 'm3u8')]
    if not combined_candidates:
        combined_candidates = [f for f in formats if 'url' in f]
    best_combined = max(combined_candidates, key=lambda f: (f.get('height') or 0, f.get('tbr') or 0, f.get('abr') or 0))
    return best_combined.get('url')

# ---- youtube_transcript_api based getYoutubeTranscript ----
install_and_import("youtube_transcript_api", "https://github.com/jdepoix/youtube-transcript-api/archive/master.zip")

_YT_ID_RE = re.compile(
    r'(?:v=|\/v\/|youtu\.be\/|\/embed\/|\/shorts\/|watch\?.*v=)([A-Za-z0-9_-]{11})'
)

def extract_youtube_id(url_or_id: str) -> str:
    if not isinstance(url_or_id, str):
        raise ValueError("video id or url must be a string")
    url_or_id = url_or_id.strip()
    if len(url_or_id) == 11 and re.fullmatch(r'[A-Za-z0-9_-]{11}', url_or_id):
        return url_or_id
    m = _YT_ID_RE.search(url_or_id)
    if m:
        return m.group(1)
    q = re.search(r'[?&]v=([A-Za-z0-9_-]{11})', url_or_id)
    if q:
        return q.group(1)
    raise ValueError("Could not extract YouTube video id from input")

def getYoutubeTranscript(url_or_id: str):
    video_id = extract_youtube_id(url_or_id)
    return youtube_transcript_api.YouTubeTranscriptApi().fetch(video_id).to_raw_data()
