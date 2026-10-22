# Credits

## The platform

LUOGU is built on [fm1-base](https://github.com/charlesvestal/fm1-base) by Charles Vestal (MPL-2.0), and on
[felucca-bsp](https://github.com/hugelton/felucca-bsp) by Leo Kuroshita, Hügelton Instruments (MPL-2.0), from
Felucca. fm1-base's own credits (its Latin fonts, the screen reader, the tools) are in `base/CREDITS.md`. `app/app.c`
and `app/app.h` started from fm1-base's template.

## The Chinese fonts

The screen's Chinese characters are bitmaps rendered by `tools/zh_font.py` from two fonts, both under the SIL Open
Font License 1.1. Only the characters the screens use are included, as 4-bit glyphs in `app/dsp/zh_font.h`; that
file is under the same licence.

- **Noto Sans SC** (the body text), Copyright 2014-2021 Adobe (http://www.adobe.com/), with Reserved Font Name
  'Source'. <https://github.com/google/fonts/tree/main/ofl/notosanssc>. Licence: `LICENSES/OFL-1.1-NotoSansSC.txt`.
- **LXGW WenKai** (霞鹜文楷, the names in brush style), Copyright 2021-2026 LXGW
  (https://github.com/lxgw/LxgwWenKai), with Reserved Font Names '霞鹜', '霞鶩', '落霞孤鹜', '落霞孤鶩' and 'LXGW'.
  Licence: `LICENSES/OFL-1.1-LXGWWenKai.txt`.

The logo's 锣鼓 (`app/logo.h`, drawn by `tools/logo.py`) is from **Fusion Pixel Font**'s 12 px, Copyright (c) 2022,
TakWolf (<https://github.com/TakWolf/fusion-pixel-font>), whose Chinese comes from TakWolf's **Ark Pixel Font**
(<https://github.com/TakWolf/ark-pixel-font>); both SIL OFL 1.1 (`LICENSES/OFL-1.1-FusionPixel.txt`,
`LICENSES/OFL-1.1-ArkPixel.txt`). The rest of the logo is LUOGU's own.

## The 唱段 (not in this repository)

A build made with `tools/tq_data.py` carries a few arias' 京胡 parts and lyrics from the **Jingju Music Scores
Collection**: Rafael Caro Repetto and Xavier Serra, CompMusic, Music Technology Group, Universitat Pompeu Fabra
(2017; <https://doi.org/10.5281/zenodo.1285613>; the scores at <https://github.com/Rafael-Caro/Jingju-Score-Analysis>),
CC BY-NC-ND 4.0, each score as set from its printed source: 《京剧曲谱集成》 (上海文艺出版社) and 曹宝荣 (编著),
《京剧唱腔板式解读》 (人民音乐出版社, 2010). Their tempos were measured on the **Jingju a Cappella Recordings
Collection**'s 京胡 tracks of the same scores (CC BY 4.0) where it has them. The lyrics are drawn from Fusion Pixel
Font's 12 px (above). Not for sharing: the header it writes stays out of LUOGU's sources.

## The sounds

The 单皮鼓, the 板, the 小锣 and the 铙钹 are recordings: eleven strokes from the Beijing Opera Percussion Instrument
dataset (below), cut, levelled and coded by `tools/samples.py` into `app/dsp/samp_tab.h`, which is under CC BY 4.0
like its source (`LICENSES/CC-BY-4.0.txt`). The 大锣 and the 堂鼓 are synthesized, from numbers measured in the
recordings below. Nothing else of any recording is included.

**Beijing Opera Percussion Instrument dataset**: Mi Tian, Ajay Srinivasamurthy, Mark Sandler and Xavier Serra,
"A Study of Instrument-wise Onset Detection in Beijing Opera Percussion Ensembles", ICASSP 2014.
<https://zenodo.org/records/1285212> (also Freesound pack 14056 by ajaysm), licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Played by Ying Wan of the London Jing Kun Opera
Association. LUOGU plays eleven of its strokes, changed as `tools/samples.py` says (each from its onset, cut where it
has fallen 50 dB or at a set length with a fade, resampled to 32 kHz, levelled, coded as 8-bit mu-law): bangu-35,
bangu-38 and bangu-44 (the 单皮鼓), bangu-04 and bangu-11 (the 板), xiaoluo-57, xiaoluo-58, xiaoluo-10 and xiaoluo-13
(the 小锣), naobo-10 and naobo-19 (the 铙钹), Freesound pack 14056 by ajaysm. `tools/perc_fit.py` measured its strokes
too, for the synthesis LUOGU had before (still in `app/dsp/perc_tab.h`). (Its 大锣 isn't a 虎音锣, the 大锣 of 京剧:
the 大锣 is measured from CTIS, below.)

**Jingju a Cappella Recordings Collection (JaCRC)**: Rong Gong, Rafael Caro Repetto, Yile Yang and Xavier Serra,
Music Technology Group, Universitat Pompeu Fabra, <https://doi.org/10.5281/zenodo.6536490>; its 京胡 accompaniment
recordings are licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). `tools/hu_fit.py` measured the harmonics of its 京胡
accompaniment tracks to fit the 京胡's body filter in `app/dsp/hu_tab.h`; `tools/hu_transcribe.py` was checked
against them.

**CTIS, Chinese Traditional Instrument Sound database**: Liang, Li, Liu, Li, Zhu and Han, "Constructing a
Multimedia Chinese Musical Instrument Database", CSMT 2019; Central Conservatory of Music.
<https://huggingface.co/datasets/ccmusic-database/CTIS>, licensed under CC BY-NC-ND 4.0. Only numbers were taken
from it: the 虎音锣's partials, their levels, decays and how far each glides relative to the main tone (the 大锣:
three natural strokes and a damped one, D0049), the 二胡's spectral envelope (for the 京二胡's body filter) and how long
the 月琴's strings ring by pitch.

**The 大锣's sinking pitch** is set from two measurements: CTIS's 虎音锣 struck hard (269 Hz down to 186 Hz, half of it
in about a second) and the 大锣 of the CompMusic recording of 慢长锤 (242 Hz down to 147 Hz), both 6 to 8 semitones;
the 大锣 of a hard stroke in LUOGU starts 58 % above its rest, a 仓's 50 %.

## The music: where it was collected from

The 锣鼓经 and the 过门 are traditional. LUOGU's versions, in `data/luogu.txt` and `data/guomen.txt`, were collected
from these sources and cross-checked; each entry names its sources and a confidence grade.

- 穆文义, 《京剧打击乐技巧与练习：演奏教程》 (人民音乐出版社, 2007): its multi-instrument scores of 导板头, 慢长锤,
  夺头, 小锣夺头 and 闪锤 as transcribed by CompMusic (MTG, Universitat Pompeu Fabra,
  <https://compmusic.upf.edu/bo-perc-patterns>), whose recordings of them gave the five measured tempos; the scores'
  drum parts are the model for the 鼓师's strokes that a 锣鼓经's text leaves out (`tools/lg_data.py` drum_part: a stroke
  with the gongs, eighths through a held one at the 收, none in the closing bar but with the big gong), and they give
  匡 the cymbals. Also the patterns quoted from the book with page numbers in 赵文怡's article on the 锣鼓 of 《野猪林》
  (华音网, 2022).
- "京剧常用锣鼓经用途及打法", a widely reposted text in the 中国戏曲研究院's 打击乐简谱 (percussion notation), with the
  patterns' uses; probably from the 研究院's 《京剧打击乐汇编》 (音乐出版社, 1958), which wasn't seen.
- The 中国京剧锣鼓经大全 teaching series (刘越, 方瑞山) as listed on wiki66.com; collected notations reposted on Sina
  blogs and Baidu Zhidao; the preset data of `LiShuoxue/DrumPlayer2.1` on GitHub (after 范石人 《京剧锣鼓》); 王军 and
  王以东 on the 锣鼓's core vocabulary (华音网, 2025); the 锣鼓 entry of 《中国大百科全书·戏曲曲艺》.
- 《京剧胡琴入门》 (1955, signed "编者 1955.12.10"): its catalogue of 胡琴过门, including the 过门 by 落音.
- 刘碧清, 《京剧常用过门》 (hand-copied, 1998).
- The Jingju Music Scores Collection (Rafael Caro Repetto and Xavier Serra, 2017,
  <https://doi.org/10.5281/zenodo.1285613>), as a guide to the arias' printed sources: 曹宝荣 《京剧唱腔板式解读》
  (2010), 刘吉典 《京剧音乐概论》 (1981), 《京剧曲谱集成》.
- Modern typeset 京胡 scores of 《霸王别姬》 (南梆子, 二六) and 《贵妃醉酒》 (四平调) shared on jianpu.cn.
- Srinivasamurthy, Caro Repetto, Sundar and Serra, "Transcription and Recognition of Syllable based Percussion
  Patterns: The Case of Beijing Opera", ISMIR 2014, for the syllables' instruments.
- Rossing et al. (2004) and Jossic et al., JASA 141 (2017), on the gongs' pitch glides.
- Lectures and articles on the 板式 (their 板眼, where lines start and end, the lead-in patterns), noted in the data
  files where used.

Five tempos (导板头, 慢长锤, 夺头, 小锣夺头, 闪锤) were measured from recordings; the others are estimates.
