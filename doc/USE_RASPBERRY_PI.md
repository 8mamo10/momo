# Raspberry Pi で Momo を使ってみる

## Raspberry Pi 向けのバイナリは以下にて提供しています

https://github.com/shiguredo/momo/releases にて最新版のバイナリをダウンロードしてください。

## ダウンロードしたパッケージ、解凍後の構成

```
$ tree
.
├── html
│   ├── test.html
│   └── webrtc.js
├── LICENSE
├── momo
└── NOTICE
```

## 準備

### パッケージのインストール

下記を実行してください。

```
$ sudo apt-get install libnspr4 libnss3
```

### Raspbian で Raspberry Pi の Raspberry Pi 用カメラを利用する場合

これは USB カメラを利用する場合は不要なオプションです。

raspi-config で Camera を Enable にしてください。

さらに、以下のコマンドか

```
$ sudo modprobe bcm2835-v4l2 max_video_width=2592 max_video_height=1944
```

/etc/modules の末尾に

```
bcm2835-v4l2 max_video_width=2592 max_video_height=1944
```

を追加して再起動してください。

## 動かしてみる

動かし方については [USE_TEST.md](USE_TEST.md) をご確認ください。

## Raspberry Pi 向けの追加のオプション

```
$ ./momo --help
Momo - WebRTC ネイティブクライアント
Usage: ./momo [OPTIONS] [SUBCOMMAND]
Options:
  -h,--help                   Print this help message and exit
  --no-video                  ビデオを表示しない
  --no-audio                  オーディオを出さない
  --force-i420                強制的にI420にする
  --use-native                MJPEGのデコードとビデオのリサイズをハードウェアで行う
  --video-device              ビデオデバイス指定
  --resolution STR in [QVGA,VGA,HD,FHD,4K]
                              解像度
  --framerate INT in [1 - 60] フレームレート
  --fixed-resolution          固定解像度
  --priority STR in [BALANCE,FRAMERATE,RESOLUTION]
                              優先設定 (Experimental)
  --port INT in [0 - 65535]   ポート番号
  --daemon                    デーモン化する
  --version                   バージョン情報の表示
  --log-level INT in [0 - 5]  ログレベル
Subcommands:
  test                        開発向け
  ayame                       WebRTC Signaling Server Ayame
  sora                        WebRTC SFU Sora
```


### --use-native

`--use-native` は ハードウェアによるビデオのリサイズ と USB カメラ用の場合 MJPEG をハードウェアデコードします。

```shell
$ ./momo --use-native --no-audio --port 8080 test
```

### --force-i420

`--force-i420` は Raspberry Pi 専用カメラ用では MJPEG を使うとパフォーマンスが落ちるため HD 以上の解像度でも MJPEG にせず強制的に I420 でキャプチャーします。
USBカメラでは逆にフレームレートが落ちるため使わないでください。


```shell
$ ./momo --force-i420 --no-audio --port 8080 test
```

## Raspberry Pi 専用カメラでパフォーマンスが出ない

[Raspbian で Raspberry Pi の Raspberry Pi 用カメラを利用する場合](#raspbian-で-raspberry-pi-の-raspberry-pi-用カメラを利用する場合)通りに設定されているか確認してください。特に `max_video_width=2592 max_video_height=1944` が記載されていなければ高解像度時にフレームレートが出ません。

Raspberry Pi 専用カメラ利用時には `--use-native --force-i420` オプションを併用するとCPU使用率が下がりフレームレートが上がります。例えば、 RaspberryPi Zero の場合には

```shell
$ ./momo --resolution=HD --framerate=20 --force-i420 --use-native test
```

がリアルタイムでの最高解像度設定となります。パフォーマンスが限られた Zero でリアルタイムにするには framerate を制限することも重要になります。

## Raspberry Pi で USB カメラ利用時に use-native を使ってもフレームレートが出ない

USB カメラ利用時には `--use-native` を使わない方がフレームレートは出ます。しかし `--use-native` を使ってCPU使用率を下げた状態で利用したい場合は /boot/config.txt の末尾に下記を追記してください

```
gpu_mem=128
force_turbo=1
avoid_warnings=2
```

この設定であれば HD は 30fps, FHD では 15fps 程度の性能を発揮します。


