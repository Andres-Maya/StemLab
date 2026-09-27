#!/usr/bin/env python3
"""StemLab: codifica a MP3 la mezcla exportada.

JUCE solo sabe leer MP3, no escribirlo. StemLab (C++) renderiza la mezcla a un
WAV temporal de 16 bits y ejecuta este script:

    python -u -X utf8 stemlab_encode_mp3.py --input mezcla.wav --output mezcla.mp3 --bitrate 320

Usa LAME a través del paquete lameenc (lo instala Demucs; si falta:
pip install lameenc). Solo lee el WAV con el módulo estándar "wave", así que
no necesita numpy ni soundfile. Se comunica por stdout con el mismo protocolo
que stemlab_separate.py:

    @@STATUS <texto>
    @@PROGRESS <0..1>
    @@DONE
    @@ERROR <mensaje>         (y código de salida != 0)
"""

from __future__ import annotations

import argparse
import sys
import wave
from pathlib import Path

# Frecuencias que admite MPEG-1/2 Layer III.
SUPPORTED_RATES = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000}
BITRATES = {96, 128, 160, 192, 224, 256, 320}


def emit(tag: str, message: str = "") -> None:
    print(f"@@{tag} {message}".rstrip(), flush=True)


def fail(message: str) -> int:
    emit("ERROR", message.replace("\n", "\\n"))
    return 1


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Codificación MP3 para StemLab (LAME).")
    parser.add_argument("--input", type=Path, required=True, help="WAV PCM de 16 bits")
    parser.add_argument("--output", type=Path, required=True, help="archivo MP3 de salida")
    parser.add_argument("--bitrate", type=int, default=320, help="kbps (CBR)")
    parser.add_argument("--quality", type=int, default=2, help="2 = mejor calidad, 7 = más rápido")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)

    try:
        import lameenc
    except ImportError:
        return fail("Falta el paquete de Python 'lameenc' (codificador MP3).\n"
                    "Instálalo con: python\\.venv\\Scripts\\python -m pip install lameenc")

    if args.bitrate not in BITRATES:
        return fail(f"Tasa de bits no válida: {args.bitrate} kbps.")

    try:
        source = wave.open(str(args.input), "rb")
    except (OSError, wave.Error) as error:
        return fail(f"No se pudo leer el WAV temporal: {error}")

    with source:
        channels = source.getnchannels()
        rate = source.getframerate()
        width = source.getsampwidth()
        total = source.getnframes()

        if width != 2:
            return fail("El WAV temporal debe ser PCM de 16 bits.")
        if channels not in (1, 2):
            return fail("MP3 solo admite audio mono o estéreo.")
        if rate not in SUPPORTED_RATES:
            return fail(f"MP3 no admite {rate} Hz.")

        encoder = lameenc.Encoder()
        encoder.set_bit_rate(args.bitrate)
        encoder.set_in_sample_rate(rate)
        encoder.set_channels(channels)
        encoder.set_quality(max(2, min(7, args.quality)))

        emit("STATUS", f"Codificando MP3 a {args.bitrate} kbps...")
        args.output.parent.mkdir(parents=True, exist_ok=True)

        chunk = rate  # un segundo por paso: el progreso avanza con fluidez
        done = 0

        try:
            with open(args.output, "wb") as output:
                while True:
                    frames = source.readframes(chunk)
                    if not frames:
                        break
                    output.write(encoder.encode(frames))
                    done += len(frames) // (channels * width)
                    emit("PROGRESS", f"{done / max(1, total):.4f}")

                output.write(encoder.flush())
        except OSError as error:
            return fail(f"No se pudo escribir el MP3: {error}")

    emit("PROGRESS", "1.0")
    emit("DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
