<!-- Template for the GitHub release (.github/workflows/build.yml passes this file as the notes).
     Replace the text in brackets before tagging; delete the comments. -->

Pilotwings 64 (US) for the original Xbox. Your own ROM is needed; see the README.

## Install

Unzip `Wings64-X-<tag>.zip`, copy the `Wings64-X` folder to `E:\Games\` on the console, put your ROM (`.z64`, `.n64`, `.v64`, `.rom` or `.bin`, 8 MB, US `NPWE`) in it next to `default.xbe`, and launch from the dashboard. For a disc, `tools/make-xiso` packs the folder and your ROM into an ISO.

## Changes

- [what changed since the last release]

## Known issues

- [what is not working or not checked on hardware]

Updating from an earlier release: copy the new `default.xbe` and `default.tbn` over the old ones. Saves and settings stay in `E:\UDATA\57580001\`.
