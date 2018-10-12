# Momo のビルドに挑戦する

ビルドにはマシンパワーにもよりますが、少なくとも 30 分以上かかり、さらに 20GB 以上のダウンロードが必要です。
そのため、覚悟を持って make コマンドを叩いてください。

まずは momo のリポジトリをダウンロードします。git submodule を利用しているため --recursive を忘れないでください。

```shell
$ git clone --recursive git@github.com:shiguredo/momo.git
```

## 注意

- Windows 版の Docker は未検証のため非対応としています。Linux 版、または macOS 版の Docker をご利用ください。

## Raspbian June 2018 (armv7) 向けバイナリを作成する

**Docker 18.03 以降がインストールされている必要があります**

Raspberry Pi 3 B/B+ は実際は armv8 ですが 64 ビット機能が Raspbian では利用できないため、実質 armv7 相当のビルドになります。

build ディレクトリ以下で make armv7 と打つことで Momo のバイナリが生成されます。

```shell
$ make armv7
```

うまくいかない場合は `make armv7.rebuild` を試してみてください。それでもだめな場合は issues にお願いします。

## Raspbian June 2018 (armv6) 向けバイナリを作成する

**Docker 18.03 以降がインストールされている必要があります**

build ディレクトリ以下で make armv6 と打つことで Momo のバイナリが生成されます。

```shell
$ make armv6
```

うまくいかない場合は `make armv6.rebuild` を試してみてください。それでもだめな場合は issues にお願いします。

## Ubuntu 16.04 (armv8) 向けバイナリを作成する

**Docker 18.03 以降がインストールされている必要があります**

build ディレクトリ以下で make armv8 と打つことで Momo のバイナリが生成されます。

```shell
$ make armv8
```

うまくいかない場合は `make armv8.rebuild` を試してみてください。それでもだめな場合は issues にお願いします。

## Ubuntu 18.04 (x86_64) 向けバイナリを作成する

**Docker 18.03 以降がインストールされている必要があります**

build ディレクトリ以下で make x86_64 と打つことで Momo の Ubuntu 18.04 x86_64 向けバイナリが生成されます。

```shell
$ make x86_64
```

## macOS 10.14 または macOS 10.13

**現在準備中です**

## Windows 10

**現在準備中です**

## ビルド時に Raspberry Pi の HWA を無効にする方法

- armv6, armv7 の場合、デフォルトで HWA を利用する。もし利用したくない場合は `make USE_IL_ENCODER=0 armv7` のように `USE_IL_ENCODER=0` を指定すること。
