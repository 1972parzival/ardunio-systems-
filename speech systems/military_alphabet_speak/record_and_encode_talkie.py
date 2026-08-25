#!/usr/bin/env python3
"""
Record a short word → encode to Talkie-compatible LPC data
"""

import os
import sys
import tempfile
import subprocess
import sounddevice as sd
import soundfile as sf
import numpy as np

# ------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------
DURATION   = 2          # seconds to record
SAMPLE_RATE = 8000        # Talkie requires 8 kHz
CHANNELS   = 1
WORD_NAME  = "MYWORD"     # change this → becomes spMYWORD
# ------------------------------------------------------------------

def record_word():
    print(f"\nSpeak the word now... (recording {DURATION} s)")
    print(">>> GO! <<<")
    audio = sd.rec(int(DURATION * SAMPLE_RATE),
                   samplerate=SAMPLE_RATE,
                   channels=CHANNELS,
                   dtype='int16')
    sd.wait()
    print("Recording finished.\n")
    return audio

def save_wav(audio, path):
    sf.write(path, audio, SAMPLE_RATE, subtype='PCM_16')
    print(f"Saved temporary WAV → {path}")

def encode_with_python_wizard(wav_path, varname):
    """
    Calls python_wizard with the recommended flags for Talkie:
      -S  = explicit stop frame (required by Talkie)
      -T tms5220
      -f arduino
      -p  = pre-emphasis (usually improves quality)
    """
    # Adjust this path if you cloned python_wizard somewhere else
    wizard = os.path.join(os.path.dirname(__file__),
                          "python_wizard", "python_wizard")
    if not os.path.isfile(wizard):
        wizard = "python_wizard"          # hope it’s on PATH
        if not shutil.which(wizard):
            print("ERROR: cannot find python_wizard.")
            print("Clone it first:  git clone https://github.com/ptwz/python_wizard.git")
            sys.exit(1)

    cmd = [
        sys.executable, wizard,
        "-S",                          # explicit stop frame
        "-T", "tms5220",
        "-f", "arduino",
        "-p",                          # pre-emphasis
        wav_path
    ]

    print("Running LPC encoder…")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print("Encoder failed:")
        print(result.stderr)
        sys.exit(1)

    # python_wizard prints the whole “const uint8_t FILENAME[] …” block
    # We just replace the placeholder name
    code = result.stdout
    code = code.replace("FILENAME", varname)
    code = code.replace(f"sp{varname}", f"sp{varname}")   # already correct
    return code

# ------------------------------------------------------------------
if __name__ == "__main__":
    import shutil

    # 1. Record
    audio = record_word()

    # 2. Save temporary WAV
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        wav_path = tmp.name
    save_wav(audio, wav_path)

    # 3. Encode
    varname = WORD_NAME.upper()
    arduino_code = encode_with_python_wizard(wav_path, varname)

    # 4. Show the result
    print("\n" + "="*60)
    print("Copy-paste this into your Arduino sketch:\n")
    print(arduino_code)
    print("="*60)
    print(f'\nThen call:  voice.say(sp{varname}, getSpeechRate());')

    # Cleanup
    os.unlink(wav_path)