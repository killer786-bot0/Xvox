AutoVox - automatic vocal processor (VST3, JUCE 8)

Drop it on a vocal track, pick a style, turn Intensity. The plugin listens to the vocal and sets the chain itself.

WHAT THE CHAIN DOES (fixed order)
1. Analysis: measures loudness (RMS), brightness (spectral centroid), sibilance (5-9 kHz share) and a rough pitch spread. Always on, even when bypassed.
2. Auto gain: moves the vocal toward the style's loudness target (max 12 dB either way).
3. High-pass: corner frequency set by the style.
4. 3-band EQ: tilt (dark vs bright), presence peak at 3.5 kHz, air shelf at 11 kHz. If the vocal is brighter than the style wants, presence and air are cut, and the reverse if it is darker.
5. De-esser: 3:1 on the 5-9 kHz band. Threshold drops when the vocal is very sibilant.
6. Compressor: style ratio, attack, release. Auto makeup gain. Ratio is eased for wide-pitch melodic singing.
7. Reverb send: romantic and sad get real space, trap and drill stay almost dry.
8. Limiter at -1 dBFS.

Intensity (0-100%) blends from a neutral chain (no EQ, no comp, no reverb) up to the full style values.

STYLES
Romantic: warm, smooth, spacious. Gentle 2.5:1 compression, slow release, lush room (wet 0.28).
Sad: dark and intimate. Top end rolled off a little, slow attack, long release, biggest dark reverb (wet 0.40).
Hip Hop: punchy and forward. Presence boost, 3.5:1 compression, tiny room (wet 0.08).
Trap: dry, bright, tight. Fast 4.5:1 compression, strong presence and air, almost no reverb (wet 0.03).
Drill: dry, aggressive, controlled. Fastest attack, 5:1 compression, bright top end, minimal space (wet 0.02).

UI
Five style buttons, one Intensity slider, one Bypass toggle, and a live meter (loudness, brightness, sibilance) with a pitch-spread readout.

REQUIREMENTS
- Windows 10 or 11 (64-bit)
- Visual Studio 2022 Community (free): install the "Desktop development with C++" workload
- CMake 3.22 or newer (cmake.org, tick "Add CMake to PATH" in the installer)
- Git for Windows (JUCE is downloaded automatically by CMake)

BUILD (run in "Developer PowerShell for VS 2022" or any PowerShell)
1. Unzip autovox.zip and open a terminal inside the AutoVox folder (the one with CMakeLists.txt).
2. Configure:
   cmake -G "Visual Studio 17 2022" -A x64 -S . -B build
   The first run downloads JUCE 8.0.4 (takes a minute or two).
3. Build the VST3:
   cmake --build build --config Release

WHERE THE PLUGIN ENDS UP
build\AutoVox_artefacts\Release\VST3\AutoVox.vst3
This is a folder (the VST3 bundle). Copy the whole AutoVox.vst3 folder, not just a file inside it.

INSTALL IN FL STUDIO 25
1. Copy AutoVox.vst3 to C:\Program Files\Common Files\VST3\ (you need administrator rights, so run Explorer as admin or accept the UAC prompt).
2. Open FL Studio 25, go to Options > Manage plugins.
3. Click "Find installed plugins" (or Start scanning). Make sure the VST3 folder is included.
4. Open the Mixer, insert AutoVox on the vocal track's insert slot, pick a style, set Intensity to taste (start at 70%).

NOTES
- Mono or stereo tracks both work.
- The pitch-spread readout is a zero-crossing proxy, not a true pitch tracker. It is good enough to tell a flat vocal from a melodic one, not for note accuracy.
- The chain is set to be safe with no tweaking, but it is not a mastering engineer. If a vocal sounds wrong, lower Intensity first.
- To change a style's sound, edit the kProfiles table at the top of PluginProcessor.cpp and rebuild.
