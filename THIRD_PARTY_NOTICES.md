# Note su software di terze parti

## Airwindows Tube2, Logical4, Coils2 (MIT)
`Source/DSP/marley/Tube2Stage.h` (da Marley, Meteora Plugins) e' derivato da Tube2 di Airwindows.
`Source/DSP/fet1179/` (da meteoraFET1179, Meteora Plugins) contiene i port di Logical4 (modificato: attack e release
separati, stadio C con divisore coerente), Coils2 e Tube2 di Airwindows
(https://github.com/airwindows/airwindows), con le modifiche elencate nel commento del file.
Il DSP degli altri moduli si ispira ad altri algoritmi Airwindows (Channel9, Desk4, DeEss,
Baxandall2, Capacitor2, ButterComp2, ClipOnly2): dove copiato, lo dichiara nei commenti.
Le curve dell'EQ vengono da Meteora Winter Dreams; l'EQ Pultec e' il DSP di Placebo EQ (entrambi Meteora Plugins).

    MIT License

    Copyright (c) airwindows, Airwindows uses the MIT license (Tube2: Copyright (c) 2016 airwindows)

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

## JUCE
Il plugin usa JUCE (https://juce.com), con licenza AGPLv3 oppure commerciale.
Se distribuisci i binari, devi rispettare una delle due: https://juce.com/legal/juce-8-licence/
Il codice di questo repository e' MIT (compatibile con AGPLv3); il binario che include JUCE
e' soggetto ai termini JUCE scelti.

## VST
VST e' un marchio registrato di Steinberg Media Technologies GmbH.


## Font DejaVu Sans Condensed
`Assets/font_bold.ttf` e `Assets/font_regular.ttf` (scritte DRY/WET, INPUT TRANSFORMER e finestra di scelta dell'EQ) sono DejaVu Sans Condensed (Bold e Book), derivati da Bitstream Vera.
Licenza: Bitstream Vera Fonts Copyright / DejaVu changes are in public domain (https://dejavu-fonts.github.io/License.html).
Si possono incorporare liberamente nel software; i file non vanno venduti da soli.

## Meteora DirtyDESK (Meteora Plugins)
`Source/DSP/dirtydesk/` contiene lo stadio del banco (ChannelX32, profilo LLS) e le utility DSP di Meteora DirtyDESK, stesso autore.
