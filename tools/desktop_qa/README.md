# Native desktop evidence

Build `NebulensDesktopWalkthrough` with `NEBULENS_BUILD_DESKTOP_QA=ON` on
macOS using the macOS 15 or later SDK. It links the same `NebulensDesktop`
objects and `DESKTOP_RESOURCES` as the product. The QA executable is local-only;
do not register it with CTest or run it headless. It requires an interactive
macOS session. If the host denies GUI or capture access, use its standard
permission flow; never switch to display capture.

Run with one fresh evidence directory per invocation:

```zsh
qa=/path/to/build/NebulensDesktopWalkthrough
"$qa" --evidence-dir /private/tmp/nebulens-walkthrough
"$qa" --still-only --language=en --size=920x540 --evidence-dir /private/tmp/nebulens-en-small
"$qa" --still-only --size=1280x740 --evidence-dir /private/tmp/nebulens-zh-large
```

Sizes are Qt client sizes in logical points, not PNG dimensions. PNGs include
the native window frame at the display's pixel scale. Only the external
compositor shadow is excluded. The recorder uses the same single, current
process, desktop-independent window ID with no display, child windows,
microphone or audio. Live ScreenCaptureKit video buffers go straight into
one realtime AVAssetWriter session with monotonic timestamps. There are no
file-based screenshots in the video path and no post-processing.

The default walkthrough starts in Chinese with an empty order, clicks the
three products, clicks every discovered timelapse sample frame, returns to
Still, opens Advanced from Assets, changes exposure with the keyboard, and
clicks the toolbar's visible return button. The runner checks that the same
Assets step and shared masthead remain, and compares pixels inside the proof
area (not the checked radio buttons). `run.json` lists evidence and the actual
controls used, with frame count and maximum sampling gap. Inspect the
unedited recording and PNGs as well as the log.

With `--raw <alias>`, use a fresh directory under the private
`star-photograph-tool-review` directory. After the empty-order examples the
runner types a temporary local copy's path into the product file chooser,
waits for the source preview, opens Advanced from Save, returns to Save,
and checks the exposure value in Quick Adjustments. It waits for the
on-screen EV label and rendered image at both exposures, then measures
brightness in the same photo region of both unaltered window PNGs. The
separate chooser is not captured and the original fixture path is not
logged. Only pre-import empty-order evidence is suitable for public use.

The inputs are synthetic AppKit mouse/key events posted to this process's
real NSWindow, not physical human input or a global event tap. Standard
AppKit close, minimize and zoom buttons are checked for presence, visibility
and matching captured colors. Their actions are not invoked.

The launcher gives the GUI process a fresh HOME before Cocoa starts,
redirects application settings, and seeds the automatic update timestamp
before creating MainWindow. The runner verifies that timestamp is unchanged
and detects Qt network replies. Keep all captured media outside the repo.
`--probe` only tests the capture/input path against a pre-unification window;
it is not product acceptance.
