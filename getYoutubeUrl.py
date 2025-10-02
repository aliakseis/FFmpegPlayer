import sys, socket
sys.stderr = LoggerStream()

def install_and_import(package, url=None):
    import importlib
    if url is None:
        url = package
    try:
        importlib.import_module(package)
    except ImportError:
        import subprocess
        import os
        library_dir = os.path.dirname(os.path.abspath(socket.__file__))
        subprocess.run([library_dir + "/../scripts/pip3", "install", url])
    finally:
        globals()[package] = importlib.import_module(package)

install_and_import("yt_dlp", "https://github.com/yt-dlp/yt-dlp/archive/refs/heads/master.zip")



def _iter_formats(info):
    # If extract_info returned an entry with 'entries' (playlist), pick first entry.
    if not info:
        return []
    if isinstance(info, dict) and 'entries' in info and info['entries']:
        # prefer first non-None entry
        for e in info['entries']:
            if e:
                info = e
                break
    # requested_formats (when format selected like bestvideo+bestaudio) holds chosen components
    if isinstance(info, dict) and 'requested_formats' in info and info['requested_formats']:
        return info['requested_formats']
    # formats key (typical multi-format)
    if isinstance(info, dict) and 'formats' in info and info['formats']:
        return info['formats']
    # single-format item: emulate a single format entry
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
    # prefer formats that expose audio_channels or abr
    return 'audio_channels' in f and f.get('audio_channels') is not None or 'abr' in f

def getYoutubeUrl(url, adaptive):
    """
    Returns:
      - if adaptive is True: tuple(video_url, audio_url)
      - if adaptive is False: single best combined stream url (string)
    Works with any provider supported by yt-dlp.
    """
    socket.setdefaulttimeout(10)

    # Base options: don't download, be quiet
    base_opts = {
        'noplaylist': True,
        'quiet': True,
        'skip_download': True,
    }

    # For adaptive we will request both video and audio selections.
    # For non-adaptive, ask yt-dlp for all formats and pick the best combined.
    ydl_opts = base_opts.copy()
    ydl_opts['format'] = 'bestvideo+bestaudio' if adaptive else 'best'

    with yt_dlp.YoutubeDL(ydl_opts) as ydl:
        info = ydl.extract_info(url, download=False)

    formats = _iter_formats(info)

    if adaptive:
        # Find best video
        video_candidates = [f for f in formats if _is_usable_video_format(f)]
        if video_candidates:
            # choose by height primarily, then by bitrate
            best_video = max(video_candidates, key=lambda f: (f.get('height') or 0, f.get('tbr') or 0))
            video_url = best_video.get('url')
        else:
            # fallback: if the top-level info has a direct url with video, use it
            video_url = info.get('url')

        # Find best audio
        audio_candidates = [f for f in formats if _is_usable_audio_format(f)]
        if audio_candidates:
            # choose by audio channels then abr
            best_audio = max(audio_candidates, key=lambda f: (f.get('audio_channels') or 0, f.get('abr') or 0))
            audio_url = best_audio.get('url')
        else:
            # fallback: sometimes requested_formats contains an audio entry, or info['url'] is audio-only
            # try to find any format with vcodec == 'none'
            any_audio = next((f for f in formats if f.get('vcodec') == 'none' and f.get('url')), None)
            audio_url = any_audio.get('url') if any_audio else None

        return (video_url, audio_url)

    # non-adaptive: pick best combined stream (must have audio_channels)
    combined_candidates = [f for f in formats if f.get('vcodec') != 'none' and f.get('audio_channels') is not None and not f.get('vcodec','').startswith('av01') and f.get('protocol') not in ('m3u8_native', 'm3u8')]
    if not combined_candidates:
        # fallback to any format with url
        combined_candidates = [f for f in formats if 'url' in f]
    best_combined = max(combined_candidates, key=lambda f: (f.get('height') or 0, f.get('tbr') or 0, f.get('abr') or 0))
    return best_combined.get('url')
