#!/usr/bin/env python3
"""Generate original sine-wave MP3 fixtures; ffmpeg is needed only to regenerate them."""
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parent / 'audio'
root.mkdir(exist_ok=True)
cases = [
    ('short_mono_cbr.mp3', 16000, 1, '0.25', ['-b:a', '32k']),
    ('short_stereo_cbr.mp3', 48000, 2, '0.6', ['-b:a', '128k']),
    ('long_stereo_vbr.mp3', 44100, 2, '4.25', ['-q:a', '4']),
]
for name, rate, channels, duration, quality in cases:
    subprocess.run([
        'ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi',
        '-i', f'sine=frequency=440:sample_rate={rate}:duration={duration}',
        '-ac', str(channels), '-c:a', 'libmp3lame', *quality,
        '-write_xing', '1', '-id3v2_version', '3',
        '-metadata', 'title=AudioPlay generated test tone', str(root / name)
    ], check=True)
    print(name, (root / name).stat().st_size, 'bytes')
