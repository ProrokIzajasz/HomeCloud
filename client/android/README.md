# HomeCloud for Android

Native Android shell for the self-hosted HomeCloud service. The responsive UI is
served by HomeCloud, while connection configuration comes from a C++20/NDK
library linked with the shared client core.

Build with `gradlew.bat assembleDebug`. Public access must be enabled separately
on the host before the APK works outside the owner's Tailscale network.
