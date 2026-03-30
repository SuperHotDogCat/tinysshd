# Remote Terminal Server (C++)

TCP/IPソケットと擬似端末 (PTY) を使用したシンプルなリモートターミナルサーバーです。

## 機能

- TCPポート 2222 で待受。
- 接続ごとに `/bin/bash` を起動し、標準入出力をPTYにリダイレクト。
- `posix_openpt`, `grantpt`, `unlockpt`, `ptsname`, `fork`, `setsid`, `dup2`, `execlp`, `select` などの標準POSIX APIを使用。

## コンパイル方法

`g++` がインストールされている環境で `make` を実行してください。

```bash
make
```

これにより、実行ファイル `remote_terminal` が生成されます。

## 実行方法

サーバーを起動します。

```bash
./remote_terminal
```

## 接続方法

別の端末から `nc` (netcat) などを使用して接続できます。

```bash
nc localhost 2222
```

接続に成功すると、リモートでシェル操作が可能になります。

## 注意事項

このプログラムには認証機能が含まれていません。公開されたネットワーク上で実行しないでください。
