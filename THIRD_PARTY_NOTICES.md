# Third-party notices

These notices cover the third-party software and font that Emerald Rogue Assistant 1.0
includes or uses during its build.

Release packages include the full license text for SFML, ENet, FreeType,
HarfBuzz, and SheenBidi in the `licenses` folder. The license for the included
strutil header appears below. The full license text controls if a summary is
different.

## Included components

| Component | Version | Use | License |
| --- | --- | --- | --- |
| SFML | 3.1.0 | Statically linked graphics, window, and system modules | zlib/libpng |
| ENet | 1.3.18 | Statically linked multiplayer transport | MIT |
| FreeType | 2.14.3 | Statically linked through SFML | FreeType License |
| HarfBuzz | 14.1.0 | Statically linked through SFML | Old MIT |
| SheenBidi | 3.0.0 | Statically linked through SFML | Apache-2.0 |
| cpp-unicodelib | SFML 3.1.0 vendored snapshot | Compiled into SFML System | MIT |
| GLAD | 2.0.8 generated loaders | Compiled into SFML Window/Graphics | `(WTFPL OR CC0-1.0) AND Apache-2.0` |
| QOI | SFML 3.1.0 vendored snapshot | Compiled into SFML Graphics | MIT |
| stb_image / stb_image_write | 2.30 / 1.16 | Compiled into SFML Graphics | MIT alternative selected |
| Vulkan headers | 1.1.83 | Compiled into SFML Window | Apache-2.0 |
| strutil | 1.0.2-derived vendored header | Trims multiplayer host addresses | MIT |
| Pokemon Emerald Pro | 1.0 | Bundled application font | CC BY-SA 3.0 |

Portions of this software are copyright (C) 2026 The FreeType Project
(https://freetype.org). All rights reserved. This software is based in part on
the work of the FreeType Team.

mGBA, the Pokémon Emerald ROM, and Pokémon Emerald Rogue are not bundled.
Platform system libraries remain subject to their own operating-system terms.

## SFML 3.1.0

Copyright (C) 2007-2026 Laurent Gomila - laurent@sfml-dev.org

This software is provided 'as-is', without any express or implied warranty. In
no event will the authors be held liable for any damages arising from the use
of this software.

Permission is granted to anyone to use this software for any purpose, including
commercial applications, and to alter it and redistribute it freely, subject
to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim
   that you wrote the original software. If you use this software in a product,
   an acknowledgment in the product documentation would be appreciated but is
   not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

## ENet 1.3.18

Copyright (c) 2002-2024 Lee Salzman

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## strutil 1.0.2-derived vendored header

MIT License

Copyright (c) 2020 Tomasz Gałaj

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## HarfBuzz 14.1.0

HarfBuzz is licensed under the so-called "Old MIT" license. For parts licensed
under different licenses, consult the individual source files in the pinned
HarfBuzz archive.

Copyright (C) 2010-2022 Google, Inc.
Copyright (C) 2015-2020 Ebrahim Byagowi
Copyright (C) 2019,2020 Facebook, Inc.
Copyright (C) 2012,2015 Mozilla Foundation
Copyright (C) 2011 Codethink Limited
Copyright (C) 2008,2010 Nokia Corporation and/or its subsidiary(-ies)
Copyright (C) 2009 Keith Stribley
Copyright (C) 2011 Martin Hosken and SIL International
Copyright (C) 2007 Chris Wilson
Copyright (C) 2005,2006,2020,2021,2022,2023 Behdad Esfahbod
Copyright (C) 2004,2007,2008,2009,2010,2013,2021,2022,2023 Red Hat, Inc.
Copyright (C) 1998-2005 David Turner and Werner Lemberg
Copyright (C) 2016 Igalia S.L.
Copyright (C) 2022 Matthias Clasen
Copyright (C) 2018,2021 Khaled Hosny
Copyright (C) 2018,2019,2020 Adobe, Inc.
Copyright (C) 2013-2015 Alexei Podtelezhnikov

Permission is hereby granted, without written agreement and without license or
royalty fees, to use, copy, modify, and distribute this software and its
documentation for any purpose, provided that the above copyright notice and
the following two paragraphs appear in all copies of this software.

IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR DIRECT,
INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE
OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE COPYRIGHT HOLDER HAS BEEN
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND
THE COPYRIGHT HOLDER HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT,
UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

## SFML embedded helper libraries

SFML's pinned source archive compiles several header libraries into the three
modules used by Emerald Rogue Assistant. Their upstream notices identify:

- cpp-unicodelib: Copyright (c) 2025 Yuji Hirose, MIT License;
- GLAD 2.0.8 generated OpenGL/EGL/GLX/WGL loaders: SPDX expression
  `(WTFPL OR CC0-1.0) AND Apache-2.0`, including Khronos API declarations;
- QOI: Copyright (c) 2021 Dominic Szablewski, MIT License;
- stb_image and stb_image_write: Copyright (c) 2017 Sean Barrett, with the MIT
  alternative selected; and
- Vulkan 1.1.83 headers: Copyright (c) 2014-2018 The Khronos Group Inc., with
  additional notices for Valve Corporation and LunarG, Inc., Apache-2.0.

The applicable MIT permission and warranty text appears in the ENet section
above. The complete Apache-2.0 text is included as `licenses/SheenBidi.txt`.
The generated GLAD and source-header notices remain intact in the
checksum-pinned SFML source archive.

## Pokemon Emerald Pro font

Emerald Rogue Assistant packages the inherited font file without modifying it. The file
identifies itself as “Pokemon Emerald Pro” version 1.0, copyright
`crystalwalrein` 2013. Its embedded metadata links to
<https://fontstruct.com/fontstructions/show/832818/pok_mon_emerald_pro>; that
source identifies the same author and Creative Commons Attribution-ShareAlike
3.0 Unported license. The canonical license URI is
<https://creativecommons.org/licenses/by-sa/3.0/>. No endorsement by the font
author is implied.

## Build and packaging tools

Catch2 3.15.3 is used only for tests under the Boost Software License 1.0. The
Linux release uses linuxdeploy `1-alpha-20251107-1` only as a packaging tool;
linuxdeploy is licensed GPL-3.0-or-later and is not included in Emerald Rogue Assistant
artifacts. Their authoritative license texts remain in their checksum-pinned
source/tool distributions.
