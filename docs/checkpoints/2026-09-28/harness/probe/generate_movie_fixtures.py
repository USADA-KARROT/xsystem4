#!/usr/bin/env python3
"""Generate small, entirely synthetic movies outside the source tree."""
import argparse
import ctypes
import math
import os
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(parent):
    ffmpeg = shutil.which('ffmpeg')
    ffprobe = shutil.which('ffprobe')
    if not ffmpeg or not ffprobe:
        raise RuntimeError('movie fixtures require ffmpeg and ffprobe')
    # Each build owns a new directory; never overwrite another test's data.
    out = Path(tempfile.mkdtemp(prefix='movie-fixtures-', dir=parent))
    commands = []

    def run(argv):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=30)
        commands.append(argv)
        if result.returncode:
            raise RuntimeError(f'fixture command failed: {argv!r}\n{result.stderr}')
        return result.stdout

    version = run([ffmpeg, '-version']).splitlines()[0]
    # libsndfile is already an engine dependency. Homebrew FFmpeg can lack
    # libvorbis, while its experimental native encoder rejects mono input.
    libdir = run(['pkg-config', '--variable=libdir', 'sndfile']).strip()
    if not libdir:
        raise RuntimeError('pkg-config did not locate libsndfile')
    snd = ctypes.CDLL(str(Path(libdir) / 'libsndfile.dylib'))

    class Info(ctypes.Structure):
        _fields_ = [('frames', ctypes.c_int64), ('samplerate', ctypes.c_int),
                    ('channels', ctypes.c_int), ('format', ctypes.c_int),
                    ('sections', ctypes.c_int), ('seekable', ctypes.c_int)]

    snd.sf_version_string.restype = ctypes.c_char_p
    snd.sf_format_check.argtypes = [ctypes.POINTER(Info)]
    snd.sf_format_check.restype = ctypes.c_int
    snd.sf_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Info)]
    snd.sf_open.restype = ctypes.c_void_p
    snd.sf_writef_float.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_int64]
    snd.sf_writef_float.restype = ctypes.c_int64
    snd.sf_close.argtypes = [ctypes.c_void_p]
    snd.sf_close.restype = ctypes.c_int
    snd.sf_strerror.argtypes = [ctypes.c_void_p]
    snd.sf_strerror.restype = ctypes.c_char_p
    snd_version = snd.sf_version_string().decode('ascii')
    audio_inputs = []

    def write_vorbis(path, duration, channels):
        # Constants and SF_INFO layout are from the public sndfile.h API.
        info = Info(0, 44100, channels, 0x200000 | 0x0060, 0, 0)
        if not snd.sf_format_check(ctypes.byref(info)):
            raise RuntimeError(f'libsndfile cannot encode {channels}-channel Ogg Vorbis')
        count = round(float(duration) * info.samplerate)
        samples = (ctypes.c_float * (count * channels))()
        for frame in range(count):
            for channel in range(channels):
                samples[frame * channels + channel] = 0.25 * math.sin(
                    2 * math.pi * 440 * (channel + 1) * frame / info.samplerate)
        handle = snd.sf_open(os.fsencode(path), 0x20, ctypes.byref(info))
        if not handle:
            raise RuntimeError(f'cannot create {path.name}: {snd.sf_strerror(None).decode()}')
        try:
            written = snd.sf_writef_float(handle, samples, count)
            error = snd.sf_strerror(handle).decode()
        finally:
            closed = snd.sf_close(handle)
        if written != count or closed:
            raise RuntimeError(f'{path.name}: wrote {written}/{count} frames, close={closed}: {error}')
        audio_inputs.append({'file': path.name, 'sample_rate': info.samplerate,
                             'frames': count, 'channels': channels, 'amplitude': 0.25,
                             'frequencies_hz': [440 * (i + 1) for i in range(channels)]})
    prefix = [ffmpeg, '-hide_banner', '-loglevel', 'error', '-nostdin', '-n']
    for name, duration, channels in [('short-mono.ogg', '0.08', 1),
                                     ('long-mono.ogg', '1.2', 1),
                                     ('long-stereo.ogg', '1.2', 2)]:
        write_vorbis(out / name, duration, channels)

    width, height, frames = 32, 24, 12
    raw = out / 'red-blue.rgb'
    raw.write_bytes(bytes([255, 0, 0]) * (width * height * 6)
                    + bytes([0, 0, 255]) * (width * height * 6))
    video_input = ['-f', 'rawvideo', '-pixel_format', 'rgb24',
                   '-video_size', f'{width}x{height}', '-framerate', '10', '-i', str(raw)]
    run(prefix + video_input + ['-i', str(out / 'long-stereo.ogg'),
        '-map', '0:v:0', '-map', '1:a:0', '-c:v', 'ffv1', '-pix_fmt', 'bgr0',
        '-c:a', 'copy', str(out / 'red-blue-av.mkv')])
    run(prefix + video_input + ['-c:v', 'ffv1', '-pix_fmt', 'bgr0',
                               str(out / 'video-only.mkv')])
    # The current extractor skips eight bytes after data_off before chunks.
    ogg = (out / 'long-stereo.ogg').read_bytes()
    header = b'APEG' + struct.pack('<IIHH', 16, 0, width, height)
    (out / 'synthetic.apeg').write_bytes(header + bytes(8) + b'SOND'
                                        + struct.pack('<I', len(ogg)) + ogg)
    (out / 'empty.bin').write_bytes(b'')
    (out / 'invalid.bin').write_bytes(b'synthetic invalid movie\n')

    files = {}
    for path in sorted(out.iterdir()):
        item = {'sha256': sha(path), 'size': path.stat().st_size}
        if path.suffix in ('.ogg', '.mkv'):
            data = json.loads(run([ffprobe, '-v', 'error', '-count_packets',
                '-show_streams', '-show_format', '-of', 'json', str(path)]))
            data.get('format', {}).pop('filename', None)
            item['media'] = data
        files[path.name] = item
    for name in ['short-mono.ogg', 'long-mono.ogg', 'long-stereo.ogg']:
        streams = files[name]['media']['streams']
        if len(streams) != 1 or streams[0]['codec_name'] != 'vorbis' or streams[0]['sample_fmt'] != 'fltp':
            raise RuntimeError(f'{name}: expected one planar-float Vorbis stream')
    if int(files['short-mono.ogg']['media']['streams'][0]['nb_read_packets']) >= 10:
        raise RuntimeError('short fixture must reach EOF during preload (< 10 packets)')
    if int(files['long-stereo.ogg']['media']['streams'][0]['nb_read_packets']) <= 10:
        raise RuntimeError('long fixture must exceed the preload threshold')
    av = files['red-blue-av.mkv']['media']['streams']
    if len(av) != 2 or av[0]['codec_name'] != 'ffv1' or av[1]['codec_name'] != 'vorbis':
        raise RuntimeError('AV fixture must contain FFV1 video and Vorbis audio')
    if av[0]['width'] != width or av[0]['height'] != height or int(av[0]['nb_read_packets']) != frames:
        raise RuntimeError('AV fixture video dimensions or frame count changed')
    if av[1]['sample_fmt'] != 'fltp':
        raise RuntimeError('AV fixture audio must be planar float')
    manifest = {'generator_sha256': sha(Path(__file__)), 'ffmpeg_version': version,
                'libsndfile_version': snd_version, 'audio_inputs': audio_inputs,
                'scope': 'Synthetic waveforms and red/blue RGB frames only; no game assets',
                'expected': {'width': width, 'height': height, 'fps': 10,
                             'video_frames': frames, 'colors': [[255, 0, 0], [0, 0, 255]]},
                'files': files, 'commands': commands}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return out


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('parent', type=Path)
    args = parser.parse_args()
    print(generate(args.parent.resolve()))
