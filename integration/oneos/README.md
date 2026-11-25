# iOS 27 add-on for OneOS

The add-on is a separate Android package, `org.ios27.oneosaddon`. Open it from
Android's app drawer or from the OneOS carousel's menu after applying
`launcher.patch` to the OneOS source. The patch adds an optional menu entry
when Android can resolve the add-on. Android Back returns to OneOS when the
menu opened it.

The patch was checked against OneOS commit
[`f04cc2e`](https://github.com/fahadalijaved/OneOS/commit/f04cc2e23e6e5ab0640dd974aad3b36337a71b5d).
Apply it to a OneOS checkout with `git apply --check <patch>`, then
`git apply <patch>`. A stock OneOS APK can coexist with the add-on and open
independently. An installed OneOS release needs an upstream-signed build to
receive the menu entry through an in-place update.

## Build and download

```sh
./android/gradlew -p android :oneos-addon:assembleDebug :oneos-addon:lintDebug
```

The build emits `oneos-addon-debug.apk` and a separate
`ios27-runtime-compat.apk` under the module's `build/outputs`. The add-on's
catalog pins the compatibility APK's exact size and SHA-256 and uses the
verified Apple CDN URL, length and digest from the repository's simulator
inventory. The 8.05 GB Apple archive downloads on an unmetered connection.
The add-on verifies both downloads before reporting them ready.

For the local kit URL, serve the kit directory on the PC and connect the
phone over USB with Android debugging enabled.

```sh
adb reverse tcp:8277 tcp:8277
python -m http.server 8277 --bind 127.0.0.1 --directory android/oneos-addon/build/outputs/kit
```

Tap **Download compatibility kit**, wait for **SHA-256 verified**, then use
Android's installer if you want to install that APK. Stop the local server
and remove the reverse mapping with `adb reverse --remove tcp:8277` when done.
For a hosted kit, build with `-Pios27KitUrl=https://…/ios27-runtime-compat.apk`
and serve that exact build output at the configured URL.

The Apple download is a Simulator archive. Archive extraction, SpringBoard
initialization and iOS graphics remain separate from the add-on downloader.
