# CycVAE-MWDLP VC
Fast non-parallel VC.

## Original paper
Patrick Lumban Tobing & Tomoki Toda. (2021) `Low-latency real-time non-parallel voice conversion based on cyclic variational autoencoder and multiband WaveRNN with data-driven linear prediction`. Arxiv.

## Requirements:
- UNIX
- 3.6 >= python <= 3.9
- CUDA 11.1
- virtualenv
- jq
- make
- gcc


## Installation
```
$ cd tools
$ make
$ cd ..
```


## Samples and real-time compilable demo with CPU
* [Samples](https://drive.google.com/drive/folders/14pJSpYsoPpLR6Ah-EbENSsN6ABcSvB0w?usp=sharing)
* [Real-time compilable demo with CPU](https://drive.google.com/file/d/1j7ddvltaWwie0wEp79W6VL2EV-SSAW-g/view?usp=sharing)


## Steps to build the models:
1. Data preparation and preprocessing
2. VC and neural vocoder models training [~ 2.5 and 4 days each, respectively]
3. VC fine-tuning with fixed neural vocoder [~ 2.5 days]
4. VC decoder fine-tuning with fixed encoder and neural vocoder [~ 1.5 days]


## Steps for real-time low-latency decoding with CPU:
1. Dump and compile models
2. Decode

Real-time implementation is based on [LPCNet](https://github.com/mozilla/LPCNet/).


## Details

Please see **egs/cycvae_mwdlp_vcc20/README.md** for more details on VC + neural vocoder

or

**egs/mwdlp_vcc20/README.md** for more details on neural vocoder only.


## Contact
Please check original repository.  