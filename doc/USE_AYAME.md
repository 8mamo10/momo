# Ayame を使って Momo を動かしてみる

Ayame は時雨堂が開発し OSS として公開している、 WebRTC シグナリングサーバです。

[OpenAyame プロジェクト](https://gist.github.com/voluntas/90cc9686a11de2f1acca845c6278a824)

Momo を利用する場合 Ayame を利用したモードをおすすめします。

## Ayame を利用したサービス Ayame Lite を利用する

Ayame を利用してシグナリングサーバを立てるのが面倒な人向けに Ayame Lite を提供しています。

Ayame Lite は時雨堂が提供している Ayame を利用したサービスです。無料で利用可能です。

https://ayame-lite.shiguredo.jp/beta

### サインアップしない場合

Ayame Lite はサインアップせずにそのままシグナリングサーバだけでも利用可能です。

```shell
$ ./momo --no-audio ayame wss://ayame-lite.shiguredo.jp/signaling open-momo
```

Ayame SDK のオンラインサンプルを利用します。 URL の引数にルーム ID を指定してアクセスします。

```
https://openayame.github.io/ayame-web-sdk-samples/recvonly.html?roomId=open-momo
```

### サインアップする場合

Ayame Lite にサインアップした場合はルーム ID に GitHub ユーザ名 を先頭に指定する必要があります。

- ルーム ID は GitHub ユーザ名を先頭に指定する必要があります
- シグナリングキーを --signaling-key にて指定する必要があります

```shell
$ ./momo --no-audio ayame \
    wss://ayame-lite.shiguredo.jp/signaling \
    shiguredo@open-momo \
    --signaling-key fW7LX2hv4k9AnB-S69L1HpaApcXYSp-mrxBhJgqulEHAr7BK
```

Ayame SDK のオンラインサンプルを利用します。 URL の引数にルーム ID とシグナリングキーを指定してアクセスします。

```
https://openayame.github.io/ayame-web-sdk-samples/recvonly.html?roomId=shiguredo@open-momo&signalingKey=fW7LX2hv4k9AnB-S69L1HpaApcXYSp-mrxBhJgqulEHAr7BK
```

## 詳細版

[Raspberry Pi 4 と Momo と Ayame Lite でお手軽 WebRTC 配信](https://gist.github.com/voluntas/35b8c9d4b2edf11493632e22d483d4a4)
