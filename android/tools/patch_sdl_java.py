#!/usr/bin/env python3
"""Adapts SDL's Java sources (a copy) for this build:
 - no HIDAPI: libSDL2.so is built without it (C++), so its Java manager must not start;
 - no lambdas / method references: the dexer (dx) does not desugar them, so they become plain loops.
usage: patch_sdl_java.py <copied org/libsdl/app dir>
"""
import os
import re
import sys

d = sys.argv[1]


def patch(name, pairs):
    path = os.path.join(d, name)
    s = open(path).read()
    for old, new in pairs:
        n = len(re.findall(old, s, flags=re.S))
        if n != 1:
            sys.exit("%s: expected one match of %r, got %d" % (name, old, n))
        s = re.sub(old, lambda m: new, s, flags=re.S)
    open(path, "w").write(s)


patch("SDLActivity.java", [
    (r"mHIDDeviceManager = HIDDeviceManager\.acquire\(this\);",
     "mHIDDeviceManager = null; // d2port: HIDAPI is not built"),
])


def find_device(kind):
    return (r"return Arrays\.stream\(audioManager\.getDevices\(AudioManager\.GET_DEVICES_%s\)\)\s*"
            r"\.filter\(deviceInfo -> deviceInfo\.getId\(\) == deviceId\)\s*\.findFirst\(\)\s*\.orElse\(null\);" % kind,
            "for (AudioDeviceInfo deviceInfo : audioManager.getDevices(AudioManager.GET_DEVICES_%s)) {\n"
            "                if (deviceInfo.getId() == deviceId) return deviceInfo;\n"
            "            }\n"
            "            return null;" % kind)


def device_ids(kind):
    return (r"return Arrays\.stream\(audioManager\.getDevices\(AudioManager\.GET_DEVICES_%s\)\)"
            r"\.mapToInt\(AudioDeviceInfo::getId\)\.toArray\(\);" % kind,
            "AudioDeviceInfo[] devs = audioManager.getDevices(AudioManager.GET_DEVICES_%s);\n"
            "            int[] ids = new int[devs.length];\n"
            "            for (int i = 0; i < devs.length; i++) ids[i] = devs[i].getId();\n"
            "            return ids;" % kind)


patch("SDLAudioManager.java", [
    (r"Arrays\.stream\(addedDevices\)\.forEach\(deviceInfo -> addAudioDevice\(deviceInfo\.isSink\(\), deviceInfo\.getId\(\)\)\);",
     "for (AudioDeviceInfo deviceInfo : addedDevices) addAudioDevice(deviceInfo.isSink(), deviceInfo.getId());"),
    (r"Arrays\.stream\(removedDevices\)\.forEach\(deviceInfo -> removeAudioDevice\(deviceInfo\.isSink\(\), deviceInfo\.getId\(\)\)\);",
     "for (AudioDeviceInfo deviceInfo : removedDevices) removeAudioDevice(deviceInfo.isSink(), deviceInfo.getId());"),
    find_device("INPUTS"),
    find_device("OUTPUTS"),
    device_ids("OUTPUTS"),
    device_ids("INPUTS"),
])

# nothing left that would need desugaring
for name in os.listdir(d):
    for i, line in enumerate(open(os.path.join(d, name)), 1):
        code = line.split("//")[0]
        if re.search(r"\w\s*->|\)\s*->|::", code) and not code.strip().startswith("*"):
            sys.exit("%s:%d still has a lambda: %s" % (name, i, line.strip()))
print("SDL Java patched")
