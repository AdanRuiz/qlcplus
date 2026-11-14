# Changelog

All notable changes to VertexAV Director are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Versions are numbered `X.Y.Z`:

- **X** (major): changes that break compatibility, such as the project file
  format or the way plugins and scripts talk to the application.
- **Y** (feature): new functionality that keeps existing projects working.
- **Z** (fix): bug fixes only.

Until 1.0.0 the product is not considered stable, and a `0.Y.0` release may
still break compatibility. A release is a git tag named `vX.Y.Z`; pushing
one also runs the unit tests of the engine.

This file covers what VertexAV Director adds or changes on top of the
QLC+ engine it is built on. Changes that come from QLC+ itself are in
[debian/changelog-v5](debian/changelog-v5).

## [0.1.1] - Unreleased

Based on the QLC+ 5.3.0 engine.

### Added

- The Director: runs a show on its own following the music, choosing looks
  by song section (silence, calm, verse, chorus, build, drop) from the
  functions the Stage Wizard generates. See [docs/director.md](docs/director.md).
- Demonstration mode, to watch the Director work in the 3D view without
  music, fixtures or a sender.
- Tilt sweep for motorised bars.
- Functions made by hand can go along with a layer of the Director, for what
  only one kind of fixture can do (a spinning head, a laser, a second light
  source). There is no interface for it yet.
- Optional trace file of the Director's decisions.
- VertexAV Link input plugin, which brings the musical information the
  Director reads into the application.
- A fixture model can have a 3D item of its own, for when it does not look
  or behave like the rest of its type. The SteelPro Nova has one: a spinning
  octagonal head with four beams and four yellow strobe LEDs on its sides,
  and a laser fan out of its front.
- A button inside the Simple Desk moves it to a window of its own, and
  another one in that window brings it back.
- Director Overrides panel, opened from the Director panel: section,
  intensity, colour and live controls, each in a box of its own.
- Colour override: the operator paints the show in one colour, whatever the
  music sends, until it is released.
- Rock stage as the default stage of the 3D view.
- SteelPro Nova fixture definition.
- Own name, icon and logo. The QLC+ engine is credited in the About box,
  `--version`, the Windows installer, the README and the NOTICE file.

### Changed

- The first time, the application opens in a window that fits a Full HD
  laptop instead of taking the whole screen. After that it remembers the
  size it was closed with.
- 3D view: a beam fades out towards the rim of its cone where it lands,
  instead of ending in a hard edged ellipse, and the stage is darker by
  default (ambient light 25 % instead of 60 %) so the lights stand out.
- Settings, cache, Windows installer and file association identifiers are
  separate from those of QLC+, so both can be installed side by side.
  Project files (`.qxw`), fixture definitions and the user data folder are
  still shared and compatible.
- The About box is wider, so the text is not squeezed on a small window.
