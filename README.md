# CompassNavigationOverhaul-SleepFix

Removes the unnecessary 6-second startup delay in alexsylex's **Compass Navigation Overhaul (v2.2.0)**.

## Overview

In version 2.2.0 of Compass Navigation Overhaul, a redundant 6-second `sleep` call artificially delays Skyrim's startup sequence. This patch safely eliminates that wait to restore faster game load times.

## Target & Compatibility

This patch is strictly matched to the following binary build:

- **File:** `CompassNavigationOverhaul.dll`
- **Version:** `2.2.0`
- **Expected SHA-256:** `4449723b381fa111fa8f21bfde4e899dce53001308f0cc1920c55033bf0f51ba`


> **Safety Check:** If the target DLL cannot be located or the SHA-256 hash does not match, the patch will not take effect to prevent unexpected behavior.

## License

Licensed under the **GPL-3.0-or-later**. See [`LICENSE`](https://www.google.com/search?q=LICENSE) for details.
