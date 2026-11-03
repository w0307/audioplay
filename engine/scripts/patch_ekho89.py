#!/usr/bin/env python3
"""Ekho 8.9 AudioPlay patch v2: PCM flush, speed, cooperative cancel and final ACK.
Accepts official v8.9 or this package's v1 patch. Makes .before-audioplay-v2 backups.
"""
import pathlib
import re
import sys

if len(sys.argv) != 2:
    raise SystemExit('Usage: python3 scripts/patch_ekho89.py EKHO_SOURCE_DIR')
root = pathlib.Path(sys.argv[1]).resolve()
if not re.search(r'AC_INIT\(\[ekho\],\[8\.9\],', (root / 'configure.ac').read_text()):
    raise SystemExit('This patch is only for official Ekho v8.9.')
path = root / 'src/libekho_impl.cpp'
header = root / 'src/ekho_impl.h'
source = path.read_text()
header_source = header.read_text()
marker = 'extern "C" int audioplay_ekho89_patch_level() { return %d; }'
if marker % 2 in source:
    if 'std::atomic<bool> isStopped;' not in header_source or 'AudioPlay cancellation probe' not in source:
        raise SystemExit('Incomplete v2 patch; inspect source before rebuilding.')
    print('AudioPlay patch v2 already present. Rebuild if libekho.a is stale.')
    raise SystemExit(0)
if 'audioplay_ekho89_patch_level' in source and marker % 1 not in source:
    raise SystemExit('Unknown AudioPlay patch; no files changed.')

def once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit('Source differs from expected v8.9 at: ' + old[:90] + '; no files changed.')
    return text.replace(old, new)

patched = source
if marker % 1 not in source:
    old = 'int EkhoImpl::writeToSonicStream(short* pcm, int frames, OverlapType type) {\n'
    patched = once(patched, old, old + '''  // AudioPlay v8.9 compatibility: finish this utterance completely.
  // This also avoids reading mPendingPcm[-1] for an empty flush.
  if (frames == 0) {
    if (mPendingFrames > 0) {
      this->audio->writeShortFrames(mPendingPcm, mPendingFrames);
      mPendingFrames = 0;
    }
    this->audio->flushFrames();
    return 1; // writePcm must read Sonic's remaining output, even with no pending PCM.
  }
''')
    patched = once(patched, '    pEkho->setSpeed(pEkho->tempoDelta);',
                   '    pEkho->audio->setTempo(pEkho->tempoDelta); // Already normalized by setSpeed.')
    patched += '\n' + marker % 2 + '\n'
else:
    patched = once(patched, marker % 1, marker % 2)

patched = once(patched, '''              EkhoImpl::speechdSynthCallback(buffer, frames, 16,
                  pEkho->audio->channels, pEkho->audio->sampleRate, 0);''',
'''              if (EkhoImpl::speechdSynthCallback(buffer, frames, 16,
                  pEkho->audio->channels, pEkho->audio->sampleRate, 0) != 0) {
                pEkho->isStopped = true; // AudioPlay cooperative cancellation.
              }''')
patched = once(patched, '''      if (EkhoImpl::speechdSynthCallback) {
        EkhoImpl::speechdSynthCallback(0, 0, 0, 0, 0, 1);
      }
''', '')
patched = once(patched, '''      while (not pEkho->mSpeechQueue.empty()) {
        pEkho->mSpeechQueue.pop();
      }
''', '''      pthread_mutex_lock(&pEkho->mSpeechQueueMutex);
      while (not pEkho->mSpeechQueue.empty()) {
        pEkho->mSpeechQueue.pop();
      }
      pthread_mutex_unlock(&pEkho->mSpeechQueueMutex);
      // Discard pending overlap and Sonic output before another utterance.
      pEkho->mPendingFrames = 0;
      pEkho->audio->flushFrames();
      short discarded[BUFFER_SIZE];
      while (pEkho->audio->readShortFrames(discarded, BUFFER_SIZE) > 0) {}
''')
patched = once(patched, '''      #ifdef ENABLE_ESPEAK
        espeak_Cancel();
      #endif
    }
  }
''', '''      #ifdef ENABLE_ESPEAK
        espeak_Cancel();
      #endif
    }
    // Terminal ACK only after all queue/cache cleanup, on success AND cancel.
    // The wrapper may submit the next utterance as soon as this returns.
    if (EkhoImpl::speechdSynthCallback) {
      EkhoImpl::speechdSynthCallback(0, 0, 0, 0, 0, 1);
    }
  }
''')
patched = once(patched, '''    if (this->isStopped) {
      break;
    }
''', '''    // AudioPlay cancellation probe: event=2 is not the terminal event=1.
    if (this->isStopped || (EkhoImpl::speechdSynthCallback &&
        EkhoImpl::speechdSynthCallback(0, 0, 0, 0, 0, 2) != 0)) {
      this->isStopped = true;
      break;
    }
''')
patched_header = once(header_source, '  bool isStopped;', '  std::atomic<bool> isStopped;')
# Header guard remains unchanged; atomic is required by the existing C++11 build.
patched_header = '#include <atomic>\n' + patched_header
for file in (path, header):
    backup = file.with_name(file.name + '.before-audioplay-v2')
    if backup.exists():
        raise SystemExit('Backup already exists without a complete v2 patch: ' + str(backup))
for file, new_text in ((path, patched), (header, patched_header)):
    file.with_name(file.name + '.before-audioplay-v2').write_bytes(file.read_bytes())
    file.write_text(new_text)
    print('Patched:', file)
print('Rebuild ALL Ekho objects with -fPIC (internal header layout changed), then rebuild AudioPlay.')
