#!/usr/bin/env python3
"""裸 PCM 拼接: 去头，字节合并，单 WAV 播放。"""
import sys, wave, os

OUT = "/tmp/tts_out.wav"

def main():
    files = [f for f in sys.argv[1:] if os.path.exists(f)]
    if not files: sys.exit(0)

    with wave.open(files[0], 'rb') as w0:
        sr, sw = w0.getframerate(), w0.getsampwidth()

    frames = []
    for f in files:
        with wave.open(f, 'rb') as w:
            frames.append(w.readframes(w.getnframes()))

    with wave.open(OUT, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(sw)
        w.setframerate(sr)
        w.writeframes(b''.join(frames))

    os.system("aplay {} 2>/dev/null".format(OUT))

if __name__ == '__main__':
    main()
