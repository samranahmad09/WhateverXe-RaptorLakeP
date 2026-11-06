# WhateverXe-RaptorLakeP

[English](README.md)

[WhateverXe](https://github.com/b00t0x/WhateverXe)のforkです。Intel **Raptor Lake-P**内蔵GPUのmacOS対応を目指しています。
対象は第13世代Core H／P／Uシリーズのノート向けCPUに搭載されたUHD／Iris Xe Graphicsと、ノートPCの内蔵ディスプレイ（eDP）です。

> **現状（2026-11-04）：内蔵パネルはネイティブに点灯し、GPUアクセラレータ（MetalとOpenGL）がその上で動作し、輝度調整も動作します。未解決はシステムのスリープと復帰です。**
> - 2026-10-20以降、ネイティブのDisplay 13フレームバッファ経路が検証機のeDPパネルを駆動します（リンクトレーニング、60 Hzモード、カーソル、WindowServerデスクトップ。起動引数`-wex-rplp-gt`）。同日、ネイティブのアクセラレータもその上で起動しました。Metal／OpenGL／ビデオの各プラグインが接続され、WindowServerはGPUで描画し、「このMacについて」はiGPUに1536 MBを表示します。
> - ブランチは`rplp-accel`です。upstream 0.3.0（Big Sur〜Tahoe対応の単一バイナリ。20G1443カーネル向けのMMIOキャッシュ用ブリッジはBARのページ範囲に束縛）にこのforkのパネル経路を加えたものです。`main`は古く、検証済みのラウンドの後に更新します。upstream 0.6.1は取得済みで未マージです。
> - ディスプレイのスリープと復帰は動作します（パイプ有効化時に割り込みを再設定）。
> - OpenGLは2026-10-28以降動作します。アクセラレータのハードウェア性能応答はパッチ済みのOpenGL／ビデオのユーザモードドライバにTiger Lake GT2（9A49）を示し（2026-10-21以降。`-wex-rplp-nocaps`で物理IDのまま）、プラグインは計測された6バンクではなく5つのURBバンクをドライバに公開します（`wex-rplp-urb-banks=5`）。これによりOpenGLドライバの頂点URB割り当ては384 KiBではなく320 KiBで終わり、オフスクリーンの読み戻しプローブはすべて合格し、ChessとWebGLは正しく描画されます。ドライバの正確な割り当て規則は今後の課題です。ハードウェア動画：Safariの再生は動作しますが、画面収録はピクセル転送中にビデオドライバ内でクラッシュします（未解決）。
> - 輝度は2026-10-21以降、組み込みコントローラ（EC）経由で動作します。ディスプレイエンジンのPWMデューティはこのパネルに届かないため、`-wex-rplp-ecbl`がフレームバッファのデューティ書き込みをファームウェアのWMI輝度メソッドへ転送し、AppleBacklightに汎用パネルカーブを与えます。輝度キーはDSDTのキー用ガードへの2つのバイトパッチとacidantheraのBrightnessKeysで動作します（どちらもEFI側で、このプラグインの外です）。
> - システムのスリープと復帰は進行中です（2026-10-29からのテストラウンド）。プラットフォームはスリープして復帰し、ディスプレイエンジンは実際の電源断から戻り、ラウンド51（2026-11-04）からはパネルも実際の電源断の後に再点灯します（リンクがトレーニングされ、バックライトとカーソルが戻ります）。原因を5つ順に見つけて修正しました。いずれも復帰の一段先です：プラグイン自身の復元で冷えたパワーウェルのfuseを1マイクロ秒しか待たなかったこと（現在は2 ms）、電源断でリセットされるプレーンバッファとMBUSのレジスタをエンジン有効化が拒んだこと（現在は再設定）、2つのコンボPHYが状態を失っていたこと（現在はi915と同じ手順で再初期化）、電源断の後のAUX Aパワーウェルの要求がブリッジを停止させていたこと（現在は停止しません）、そしてリンク立ち上げ中にネイティブドライバのトランザクションごとの要求がAUX A IOパワーウェルを一度落とすと二度と戻らなかったこと。最後のものは、プラグインが借用していただけのファームウェアの常設要求が、復元で再現されない唯一の起動時要求だったためです（現在は常設参照 `-wex-rplp-auxstanding`：起動時の引き継ぎでファームウェアの要求を引き受け、電源断後の復元で再現します。Linuxのi915がDisplayPortエンコーダのAUXパワードメインについて取る方針です）。現在の未解決は一段上です：復帰後にGPUが何も実行しません（アクセラレータの待機中の作業が完了せず、ハードウェア割り込みが届かず、エンジン開始が二度と走らず、ソフトウェア割り込みの経路が空回りします）。そのためウィンドウサーバがアサートし、デスクトップが現れません。復帰時のアクセラレータの最初の作業は、システムの電源投入通知の4 ms後に、まだレジスタをデコードしていないデバイス（すべての読み出しが全ビット1。プラグイン自身の復元がデコード中のデバイスを見つける20 ms前）に届きます。そのforcewakeでデバイスを待つこと（`-wex-rplp-fwwait`、ラウンド52）は何も変えませんでした。アクセラレータの復帰作業はその前に始まっており、復帰4秒後もGTの割り込み有効化はゼロのままでした。現在のビルドは、電源投入通知の時点で（プラグイン自身のスリープ／復帰ハンドラが最初に呼ばれる場所で）デバイスのコンフィギュレーション空間を自ら復元できます（`-wex-rplp-pciwake`：電源状態をD0へ戻し、スリープ時のコピーと異なるBARとコマンドレジスタを書き戻す。ファミリ自身の復元より約20 ms早い）。またforcewakeドメインを保持した状態でGTの割り込み・forcewake・コマンドストリーマ・電源レジスタを、アクセラレータのエンジン開始が残した状態・スリープ直前・コールド復元後・復帰4秒後に記録し（`N97GTEngineStart`、`N97GTSleep`、`N97GTRestore`、`N97GTWake`）、復帰の最初の送信とforcewake呼び出し（`RPLPWakeSubmits`、`RPLPWakeForceWakeFirst`）、ソフトウェア割り込み（`RPLPWakeSWIRQ`）、復帰前後のコンフィギュレーション空間（`RPLPWakePCI`）、アクセラレータ自身のスリープ／復帰／電源状態の出入り（キャッシュされた状態・forcewakeカウンタ・割り込みブリッジのフラグ付き、`RPLPAccelLifecycle`）、ディスプレイIRQブリッジのライブカウンタ（`N97FBIRQLive`）を記録します。また各電源要求の前後の状態（`N97FBPowerRing`）、ファームウェアが残したPHYのワード（`N97FBPHYFirmware`、`-wex-rplp-coldphyboot`でコールドレジューム時にオプトインで再現）、復帰の立ち上げ中は25 msごとのポートのレジスタを記録し、コールドレジュームでMBUSクレジットのワードを復元します。約2分のスリープ突入はグラフィックスファミリ自身のスリープ応答のタイムアウトです。任意有効化のフラグ：`-wex-rplp-wakelog`、`-wex-rplp-fwguard`、`-wex-rplp-coldinit`、`-wex-rplp-auxlane`、`-wex-rplp-coldpps`、`-wex-rplp-coldpll`、`-wex-rplp-coldphyboot`、`-wex-rplp-auxstanding`、`-wex-rplp-fwwait`、`-wex-rplp-pciwake`、`-wex-rplp-coldcore`。ラウンド53〜54（2026-11-05）では電源投入通知の時点でプラグインがGPUのコンフィグ空間を復元し（`-wex-rplp-pciwake`）、アクセラレータの復帰後最初の処理がデコード中のデバイスに届くようになりましたが、GPU側は変わりませんでした。アクセラレータ自身のスリープ／復帰／電源状態のエントリはこの機種では一度も呼ばれず、ラウンド54のWindowServerクラッシュレポートはパネルのディスプレイパイプに新しいdid-wakeスタンプと起動時のままのwill-sleepスタンプを示しています。グラフィックスファミリのスリープ通知がパイプに届いていないため、復帰通知はスリープしていないパイプに届いています。現在のビルドはその理由を記録します：ファミリのスリープ処理の状態をスリープ移行中と復帰後1分間、3秒ごとに（`RPLPWakeIOG`）、ディスプレイパイプの通知先とタイムスタンプを定点で（`RPLPWakePipes`）、フレームバッファ通知の入口と出口を（`RPLPWakeFBEvents`）、forcewake参照カウントが最初に負になった呼び出しとその呼び出し元を（`RPLPForceWakeUnderflow`）。ラウンド55向けのビルドからは、実際の電源断でリセットされるディスプレイコアのレジスタ（MBUSのABOXワード、帯域バディレジスタ、DCPRチキンレジスタ、致命的エラーマスク、CHICKEN_PAR1_1とCHICKEN_MISC_2、アービタとクロックゲーティングのレジスタ、各パイプのDBOXとチキンレジスタ）を、起動時、スリープオーバーライド時、コールド復元の直後（プラグイン自身の書き込み前）、その書き込み後、復帰4秒後に記録し（`N97FBCoreBoot`、`N97FBCoreSleep`、`N97FBCoreCold`、`N97FBCoreResume`、`N97FBCoreWake`）、その一部を起動時の値へ戻せます（`-wex-rplp-coldcore`：帯域バディレジスタ、ビット30を立てたCHICKEN_MISC_2（Linuxのi915が `CHICKEN_MISC_DISABLE_DPT` と呼び、この世代ではパイプ有効化のたびに書くビット）、電源の入ったパイプのチキンレジスタ。`N97FBColdCore`）。このビットが落ちていると、ディスプレイエンジンはタイル化されたプレーンを、macOSのどのドライバも作らないディスプレイページテーブル経由で読むため、タイル化されたプライマリプレーンは黒のまま、リニアなカーソルだけが映ります。ラウンド51以降のすべての復帰がそう見えています。別のDisplay 13ノート向けフォーク（[Thatisjigen/WhateverXe](https://github.com/Thatisjigen/WhateverXe)、ブランチ `pr/system-sleep-0.7.0`）がシステムスリープ後にこのレジスタがゼロになることを見つけました。ラウンド55で読みました。電源断後、CHICKEN_MISC_2とパイプのチキンレジスタはゼロで、復元され、パネルはウィンドウサーバの未描画フレームの前に一瞬ベースメモリの内容を映しました。ラウンド55はスリープ通知の問題も決着させました。ファミリ自身のスリープ処理の状態（`RPLPWakeIOG`）は、dGPUのプレースホルダフレームバッファのコントローラがサイクル全体を通してウィンドウサーバ待ちビットを立てたままであることを示し、公開されているIOGraphicsのソース（585.1、`IOFramebuffer::systemWork`）は、どのコントローラもそのビットを立てていない間だけ、ウィンドウサーバへの電源断通知と、その応答の後のアクセラレータへの早期WillSleepを送ります。ウィンドウサーバはプレースホルダを決して開かないのでビットは消えず、サーバはスリープを知らされず、すべてのフレームバッファが「サーバ応答済み電源オン」の状態のままとなり、アクセラレータはWillSleepを受け取らず、その復帰には再開すべきものがありません。現在のビルドは、will-sleep時に、フレームバッファがすべて`IONDRVFramebuffer`プレースホルダであるコントローラのそのビットを消せます（`-wex-rplp-wsstart`、記録 `RPLPWSStart`）。ウィンドウサーバがそれらを開いていれば残したはずの状態です。ラウンド56ではバイトは消えたのに（記録あり）何も変わりませんでした。ファミリの`computeState`は、コントローラに未処理の作業がない限りキャッシュされた状態ワードを返し、プレースホルダのコントローラには作業が決して来ないため、その古いワード（待ちビットと「サーバ応答済み電源オン」）がサイクル全体を通して集約状態に残り（フレームバッファごとの記録が今回から埋まり、それを示しています）、ウィンドウサーバへのスリープ通知は送られませんでした。ラウンド57向けのビルドはそのキャッシュされたワードもゼロにしました（will-sleep時と、スリープ突入中および復帰中のサンプラの各ティックで）。そして**ラウンド57（2026-11-06）は成功しました。スリープ突入は13秒、ウィンドウサーバは3秒以内に通知を受けて応答し、WillSleepはアクセラレータに届き、アクセラレータ自身のスリープと復帰のエントリがこの機種で初めて実行され、復帰でエンジン開始が再び走り、デスクトップが戻ってソーク中も動作し続けました。この機種のシステムスリープと復帰はラウンド57から動作しています。**同じ起動での2回目のスリープ（ChessとSafariを開いた状態）は、システムスリープの18秒前のディスプレイ電源オフで失敗しました。ネイティブの経路がパネルのDDIパワーウェルを、パイプA・トランスコーダ・バッファがすべてオフの状態で解放したのに、プラグインの解放ゲートは自身の記録がまだパイプをアクティブと数え、割り込みマスタがオンだったために拒否し、その拒否が復帰の引き継ぐ障害をラッチしました（復元NotReady、暗いパネル、スクリプトによる正常な再起動）。現在のビルドは、ハードウェアが影響するすべてのパイプの停止を示していればポートのみの解放を記録にかかわらず通し、スリープオーバーライド時にハンドラが動いていないのにオンのままのディスプレイ割り込みマスタを消します（`-wex-rplp-entryirq`、記録 `N97FBEntryIRQ`）。ラウンド58ではラダー（システムスリープ2回、リッド、長時間スリープ）を試します。ラウンド56はディスプレイ側も決着させました。CHICKEN_MISC_2はネイティブの立ち上げ（復帰4秒後）を通して復元されたままで、そのモードセットはパイプのチキンレジスタのビット7だけを立てます。未検証：デスクトップ以外のMetalワークロード、144 Hz、外部出力。
> - 読み取り専用の`-wex-rplp-observe`と単発の`-wex-rplp-gpu`セルフテストは引き続き利用できます。
> - Raptor Lake-P対応は実験段階で、1台の機種でのみ検証しています。
> - upstreamが対応する機種（Tiger Lake、Alder Lake-N / Twin Lake）の動作はupstreamと変わりません。
> - これまでのハードウェア調査結果：[docs/raptor-lake-p-findings.md](docs/raptor-lake-p-findings.md)（英語）。

本forkは個人が趣味で運営する独立したforkであり、upstreamの作者とは関係ありません。実機で検証できた成果はupstreamへ提案する予定です。`main`は検証済みのラウンドの後に最新の`rplp-*`ブランチから更新します。

## 対象ハードウェア

| 項目 | 内容 |
| --- | --- |
| GPUファミリー | Raptor Lake-P（Linux i915の`RAPTORLAKE_P`／`RAPTORLAKE_U`）。device IDは`A7A0`、`A7A1`、`A7A8`、`A7A9`、`A7AA`–`A7AD` |
| CPU | 第13世代Intel Core **H**／**P**／**U**シリーズのノート向けCPU（例：i7-13620H、i7-1360P、i5-1335U） |
| GPUコア | Xe-LP（Gen12）。Tiger LakeのIris Xeと同じアーキテクチャ。EU数はSKUにより最大96 |
| ディスプレイエンジン | Display version 13（XE_LPD）。upstreamが既に駆動しているAlder Lake-Nと同世代 |
| 対象外 | Raptor Lake-SデスクトップおよびノートPC向け**HX**シリーズ（`A780`–`A78B`）。Display version 12のため別の経路が必要 |
| 検証機 | ASUS TUF Gaming F15 FX507VI：i7-13620H、UHD Graphics `A7A8` rev 04（64 EU）、1920×1080・144HzのeDPパネル。MUX経由のNVIDIA RTX 4070は本forkでは使用しない |

## `device-id`の上書きだけでは足りない理由

- **GTプロファイル。** upstreamのAlder Lake-N用プロファイルは最大2 DSS／32 EUまでです。検証機のGPUは4 DSS／64 EUのため、専用のL3／URB値が必要です。
- **ステッピング。** revision 04（Linuxの表ではGT C0／display E0）は、ワークアラウンドを個別に確認する必要があります。
- **内蔵ディスプレイ。** このブランチには実験的なRaptor Lake-P eDP起動引き継ぎ処理があります。ネイティブのデスクトップ動作、パネル電源シーケンス、バックライト、リンクの再トレーニングは引き続き実機検証が必要です。
- **出力。** 検証機を含む多くのRaptor Lake-Hノートでは、Intel GPUの出力はeDPパネルとUSB-CのDisplayPortのみで、HDMIはNVIDIA GPUに接続されています。upstreamはDisplay version 13でどちらもまだ駆動しないため、最初に表示できる可能性があるのは内蔵パネル（R3）です。
- **プラットフォーム。** 検証機では、OpenCoreのCPUID偽装と`ProvideCurrentCpuInfo`に加えて、Booterの`SetupVirtualMap`を有効にするとBig Surが起動します。`SetupVirtualMap`がないと、この機種のファームウェアはmacOSへの引き渡し中（`EXITBS:START`）に停止します。

## 計画

各段階はupstreamと同じく「観測 → 検証 → 有効化」の順で進め、ハードウェアに関するすべての規則にCPUテストを用意します。

| 段階 | 目標 | 完了条件 | 状況 |
| --- | --- | --- | --- |
| R1 | 読み取り専用のRaptor Lake-Pプロファイル：fuse、EU構成、ファームウェアの画面状態。forcewakeの取得／解放以外の書き込みなし | macOSでの読み出し結果が、同じ機種のLinux i915キャプチャと一致する | 完了：検証機でのmacOSの読み出し結果がLinuxキャプチャと一致（fuse 5つすべて、64 EU、4 DSS） |
| R2 | GTバックエンド：既存のBCSセルフテスト、次にこのGPUのURB／L3値でアクセラレータ修正を適用 | セルフテストが通り、ファームウェアのframebuffer上でアクセラレータが起動する | 検証機で完了：BCSセルフテストが成功し、2026-10-20からネイティブのアクセラレータが内蔵パネル上で起動する（ブランチ`rplp-accel`） |
| R3 | 内蔵ディスプレイの初表示：donorの内蔵パネル用コネクタを残し、ファームウェアが確立したリンクを引き継ぐ | ファームウェアのモードで内蔵パネルにデスクトップが表示される | 検証機で完了（2026-10-20）：内蔵パネルで1920×1080・60 Hzのネイティブデスクトップ。電源シーケンス、バックライト、再トレーニングはR4 |
| R4 | 内蔵ディスプレイの本対応：パネル電源シーケンス、バックライト、消灯／点灯、ADL-Pのテーブルによるリンク再トレーニング、144Hz | 輝度調整、スリープ／復帰、ネイティブのリフレッシュレートが動作する | 進行中：輝度は2026-10-21から（EC経由）、ディスプレイのスリープ／復帰は完了。システムのスリープ／復帰は2026-10-29から：原因を4つ順に修正し、実際の電源断後のAUX IOパワーウェルは2026-11-04に修正（AUXウェルへの常設参照）、復帰後のGPUが未解決（電源投入通知時のコンフィギュレーション空間復元を試験中）。144 Hzと再トレーニングは未着手 |
| R5 | 内蔵ディスプレイでのMetalと安定化。その後USB-C出力 | 内蔵パネルでWindowServerがアクセラレーションされる | 進行中：2026-10-20から内蔵パネルでWindowServerがアクセラレーションされ、2026-10-28からOpenGLが正しく描画される。システムのスリープ／復帰は進行中（R4）。デスクトップ以外のワークロードとUSB-C出力は未検証 |

R2〜R4に必要なハードウェア情報は検証機で測定済みで、[docs/raptor-lake-p-findings.md](docs/raptor-lake-p-findings.md)にまとめています。
- GTプロファイルの値（64 EU、L3 6バンク、URB 384 KB、Alder Lake-Nと同じmediaのfuse）
- ファームウェアが動作させたままにするeDPリンク（1920×1080・144Hz、HBR×4、PSRオフ）
- 出力構成
- バックライト：エンベデッドコントローラが制御しているが、ファームウェアはIntelのPWMを動作させたままにする

## R1診断の試し方

**動作。** 診断は`-wex-rplp-observe`を指定した場合に限り、物理的にRaptor Lake-PのGPUであるときだけ実行されます。
- GPUのfuse（slice、DSS、EU、L3、media）、Display 13の起動時状態、eDP関連レジスタ（パネル電源、バックライト、DPリンク、PSR）を読み取ります。
- 読み取った値をIORegistryに公開します。
- レジスタへの書き込みは、upstreamがAlder Lake-Nで使うものと同じforcewakeの取得／解放のみです。既に他がforcewakeを保持している場合は実行しません。
- GPUは非対応のままです。

**必要なもの。** Liluと、このforkのDEBUG版WhateverXe。この段階ではOCLPのroot patchとAMFIPassは不要です。

**macOS。** WhateverXeはBig Surを対象としているため、R1はBig Surのどのビルドでも動作します。後のアクセラレーションには11.7.11（20G1443）が必要です。Big Sur以外のmacOSでは、この読み取り専用の段階に限り`-wexbeta`を追加し、終わったら削除してください。OpenCoreでLiluとWhateverXeを注入すれば、Big Surのリカバリーからも実行できます。リカバリーのターミナルには`ioreg`と`dmesg`があり、`sudo`なしで実行します。

**ビルド。**
- [Actions](https://github.com/samranahmad09/WhateverXe-RaptorLakeP/actions/workflows/ci.yml)で、`main`で成功した最新の実行を開き、**Artifacts**をダウンロードしてください。`WhateverXe-…-DEBUG.zip`が含まれています。
- これらのビルドはupstreamと同じバージョン番号のため、upstreamのリリースとは分けて保管してください。

**収集。**

```sh
ioreg -lw0 -r -k RPLPObserveVersion > ~/Desktop/rplp-ioreg.txt
sudo dmesg | grep -E 'RaptorLakeP|WhateverXe' > ~/Desktop/rplp-dmesg.txt
```

**解析。**
- `python3 tools/rplp-decode.py rplp-ioreg.txt`で読み出し結果を名前付きの値に変換できます。
- `--linux <キャプチャ>`を付けると、同じ機種のLinux参照キャプチャと比較します。キャプチャはUbuntuのライブ環境で、読み取り専用の[`tools/rplp-capture.sh`](tools/rplp-capture.sh)を使って作成します。

**公開されるproperty。**
- `RPLPGTReadout`、`RPLPDisplay13Boot`、`RPLPEDPBoot`
- `RPLPPhysicalMemoryLayout`、`RPLPStolenReservation`
- 物理ID、revision、GTとディスプレイのステッピング名

## R2 GPUセルフテストの試し方

**動作。** `-wex-rplp-observe`に加えて`-wex-rplp-gpu`を指定すると、ブリッター（BCS）のコマンドを1回だけexeclistキュー経由で投入し、GPUが既知の2つの値をメモリに書き込んだことを確認します。upstreamのAlder Lake-N用セルフテストを、Raptor Lake-P用のリースで実行するものです。
- 実行されるのは、revision 4かつ検証機で測定したfuse構成（4 DSS、64 EU、L3 `0x88`、media `0x000E00FE`）で、DMAが物理アドレスで、ファームウェアがstolen memoryを1:1でGGTTにマップしている場合のみです。
- stolen memory内の32 MiBの位置にGGTTの16ページをリースします。ファームウェアのプレーンとは重ならず、元のエントリは保存します。
- それらのPTEとBCSエンジンのレジスタに書き込み、PCIのバスマスタを有効にすることがあります。エンジンへ一度でも書き込んだ後は、そのページを再起動まで固定したままにします。
- Appleのドライバを必要としないため、Big Surのリカバリーからも実行できます。

**読み方。** `tools/rplp-decode.py`がリースと結果を表示し、最後に`R2 BCS self-test verdict`を出力します。propertyは`RPLPGPUAdmission`、`RPLPGPULease`、`RPLPGPUCommandTest`です。

## 関連する取り組み（このリポジトリ外）

このforkは、検証機でグラフィックスを動かす趣味の取り組みの主な経路です。ほかに2つの経路があり、どちらもこのリポジトリには含まれません。

- **NVIDIA dGPU（任意の実験）。**
  - 検証機のMUXを「dGPUのみ」モードにすると、RTX 4070がパネルを直接駆動します。
  - [null-mothのNVIDIAドライバ](https://github.com/nullmoth/nvidia-macos-driver)（macOS Sequoia、PolyForm Noncommercial）はこのGPUを対象に含みますが、バックライト制御などノートPC固有の部分はまだ未実装です。
- **Appleのバイナリを使わないIntel向けスタック（長期的な研究）。**
  - MesaのANV Vulkanドライバ、新規のIntel用カーネルドライバ、既存のMetal-on-Vulkanレイヤーを組み合わせる構想です。
  - AMD向けの[Navi48-MacOS](https://github.com/Almosst-DEV/Navi48-MacOS)やnull-mothのMetalプラグインと同じ方式に倣います。
  - 調査段階で、コードはまだありません。

---

## upstream WhateverXeのドキュメント

WhateverXeは、一部のIntel Xe-LP内蔵GPU向けにAppleのグラフィックスドライバを適応させるLiluプラグインです。WhateverGreenを基に開発されています。

本プロジェクトは実験段階であり、[WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics)の署名済みグラフィックスpkgが必要です。資材を`/Library`へ配置してAuxiliary Kernel Collection（AuxKC）を更新し、Systemファイルやsnapshotにはパッチを適用しません。WEXとグラフィックスpkgは、それぞれ独立したバージョンでリリースします。

以下はupstreamのドキュメントで、このforkと異なる箇所のみ修正しています。Appleのドライバを読み込む段階（R2以降）に適用されます。

### 対応範囲

単一のWEXバイナリと1つのグラフィックスpkgで、以下のmacOSに対応します。

| macOS | 必要バージョン |
| --- | --- |
| Big Sur | 11.7.11 |
| Monterey | 12.7.6 |
| Ventura | 13.7.8 |
| Sonoma | 14.8.9 |
| Sequoia | 15.8.x |
| Tahoe | 26.7以上 |

Big Sur〜Sonomaは検証済みカーネルの識別を必要とします。SequoiaとTahoeでは、実行時の命令列・ABI検査を維持した上で、同じOS系列内の更新を受け付けます。将来の更新が検証済みという意味ではありません。

WEXはOSのABIとGPUバックエンドを独立して選択します。OS別のグラフィックス資材と共通のAppleIntelTGLCompatはWhateverXe Graphicsが導入し、GPU世代・コネクタ配線への対応はWEXが担います。

| GPUファミリー | バックエンド実装があるdevice ID |
| --- | --- |
| Tiger Lake | `9A40`、`9A49`、`9A78` |
| Alder Lake-N / Twin Lake | `46D0`–`46D4` |

Raptor Lake-P（`A7A8`のrevision 4）は`-wex-rplp-gt`を指定した場合にのみ組み込まれます。上記の[計画](#計画)を参照してください。

対応状況は機種やコネクタ構成によって異なります。この表はバックエンドの実装範囲を示すもので、すべての機種での動作を保証するものではありません。詳細な選択条件と上書き設定は[デバイス選択](docs/device-selection.md)を参照してください。

### 必須構成

[Releases](https://github.com/b00t0x/WhateverXe/releases)から **RELEASE** ZIPをダウンロードしてください。DEBUG ZIPは問題調査用です。各ZIPには`WhateverXe.kext`と対応するdSYMのみを収録し、両ZIPのチェックサムは`SHA256SUMS`で配布します。各版の変更点は[CHANGELOG](CHANGELOG.md)を参照してください。

このforkは上記のとおりCIビルドのみを公開しています。upstreamのリリースは[b00t0x/WhateverXeのReleases](https://github.com/b00t0x/WhateverXe/releases)にあります。署名済みグラフィックスpkgは検証機ではまだ試していません。検証機は従来のOCLP forkによるXe root patchのままです。

| コンポーネント | 役割 |
| --- | --- |
| Lilu | WhateverXeの動作に必要なランタイム |
| WhateverXe.kext | OpenCoreでLiluより後に読み込む |
| [WhateverXe Graphics pkg](https://github.com/b00t0x/WhateverXe-Graphics/releases) | 署名済み資材を`/Library`へ導入しAuxKCを更新する |
| [AMFIPass v1.4.1 Mod](https://github.com/b00t0x/WhateverXe-Graphics/blob/main/extras/AMFIPass-v1.4.1-Mod-RELEASE.zip) | 改変されたグラフィックス構成に必要。OpenCoreで導入する |

SIPは`csr-active-config=03080000`（値`0x803`）に設定してください。SIPの全面有効化には対応していません。署名済みグラフィックスpkgとリンク先のMod AMFIPassを使用する場合、`amfi=0x80`と`ipc_control_port_options=0`は不要です。

Tahoeでは`CoreMedia / AllowOnePassMetalScaling`の無効化が必要です。WhateverXe Graphicsのインストーラーが設定します。

グラフィックスpkgはWEX **0.3.0以上**を読み込んだ対象OSから実行してください。旧Xe System patchがある場合は、先に元の復元手順で戻してください。pkgはEFI・Systemファイル・snapshot・SIP設定を変更しません。pkgの使用方法と単独のアンインストールスクリプトは[導入と削除](https://github.com/b00t0x/WhateverXe-Graphics/blob/main/README_ja.md#インストール)を参照してください。

リンク先の **Mod版** アーカイブから`AMFIPass.kext`を展開し、`EFI/OC/Kexts`へ配置して、`Kernel > Add`で有効にしてください。既存のAMFIPassがある場合はこのMod版へ置き換え、両方を同時に読み込まないでください。upstreamのAMFIPassはMod版の署名チェーンに対応していません。

他のroot patchにOCLPを併用する場合は、同じMod版の署名チェーンを使用する[独自ビルド版OCLP](https://github.com/b00t0x/OpenCore-Legacy-Patcher)を使用してください。Xeグラフィックスの導入自体はOCLPに依存せず、WhateverXe Graphicsを使用します。

複数GPUの構成を含め、WhateverGreenとWhateverXeは同時に有効にしないでください。

### 設定

GPUはPCI device IDから自動選択します。対応範囲内ではファームウェアのVBTから配線を読み取るため、固定HDMI／固定DisplayPort構成は通常、配線propertyの手動指定が不要です。高度な設定としての`device-id`上書きは[デバイス選択](docs/device-selection.md)で説明しています。

対応GPUでは、WhateverGreenと同様に、検出されたIntel Management Engineのデバイス名も`IMEI`へ統一します。`HECI`や`MEI`から`IMEI`へのACPI renameを別途追加する必要はありません。

#### DVMT

BIOS/UEFIで変更できる場合は、**DVMT Pre-Allocatedを96MBに設定することを推奨します**。

**60MBから変更できない場合**は、`boot-args`に`-igfxdvmt`を追加し、OpenCoreの`DeviceProperties > Add`にあるiGPUの項目（通常は`PciRoot(0x0)/Pci(0x2,0x0)`）へ以下の2つを設定してください。

| Property | 型 | 値（16進数） |
| --- | --- | --- |
| `framebuffer-patch-enable` | Data | `01000000` |
| `framebuffer-stolenmem` | Data | `0000C003` |

Data値はリトルエンディアンです。`-igfxdvmt`は容量計算を補正し、DevicePropertiesは表示用メモリ領域を60MB内に収めます。ファームウェアが確保したメモリを増やす設定ではありません。96MBの場合、これらのDVMT用設定は不要です。

#### 必須ブート引数

OpenCoreの`boot-args`に以下を追加してください。

```text
-allow3d -disablegfxfirmware
```

| 引数 | 用途 |
| --- | --- |
| `-allow3d` | Appleドライバの3D処理を有効にする。 |
| `-disablegfxfirmware` | 現在のバックエンドで、Appleドライバのグラフィックスファームウェア読み込みを無効にする。 |
| `-wex-nopanel` | 内蔵画面のないTiger Lake機で追加する。現在のAlder Lake-N / Twin Lakeは外部画面用の構成を自動生成するため、この引数を使用しない。 |

Alder Lake-N / Twin Lakeで世代選択や個別機能の有効化引数を追加する必要はありません。

#### 任意のブート引数

| 引数 | 用途 |
| --- | --- |
| `gfxrst=` | 起動時framebufferの引継ぎ方式。0：自動（既定）、1：reset、2：copy、3：clear、4：無効。 |
| `-wexdbg` | debug buildでログを有効にする。 |
| `-wexoff` | WhateverXeを無効にする。 |
| `-wexbeta` | プラグインのOSバージョンチェックを回避する。kernel／driverの一致条件は維持する。 |
| `-wex-tgl-off` | Tiger Lakeの処理を無効にする。 |
| `-wex-noaccel` | Tiger LakeのFramebuffer単独モード。Acceleratorのprobeを停止します。 |
| `-wex-rplp-observe` | Raptor Lake-Pのみ。上記の読み取り専用R1診断を公開する。GPUは非対応のまま。 |
| `-wex-rplp-gpu` | Raptor Lake-Pのみ。`-wex-rplp-observe`と併用し、上記のR2 BCSセルフテストを1回実行する。GPUは非対応のまま。 |
| `-wex-rplp-gt` | Raptor Lake-PのA7A8（revision 4）のみ。開発中のバックエンドへの組み込みを許可する（ネイティブの内蔵パネル経路とアクセラレータ）。 |
| `-wex-rplp-pwm` | Raptor Lake-Pのみ。`-wex-rplp-gt`と併用し、デスクトップ表示後にディスプレイエンジンのバックライトPWMデューティを70 %に1度だけ書き込み（ファームウェアは約35 %）、記録する。PWMピンがこのパネルを駆動するかを調べる。 |
| `-wex-rplp-nocaps` | Raptor Lake-Pのみ。アクセラレータのハードウェア性能応答に物理device IDをそのまま残す。既定ではこの応答にTiger Lake GT2（9A49）を示し、device IDリストにA7A8がないパッチ済みOpenGL／ビデオのユーザモードドライバにGPUを認めさせる。Metalはどちらでも影響を受けない。 |
| `-wex-rplp-capsadln` | Raptor Lake-Pのみ。その応答でTiger Lake GT2の代わりにAlder Lake-N（46D0）を示す。これらのユーザモードドライバが受け入れるもう一方のIDで、OpenGL描画の乱れを調べる実験用。`-wex-rplp-nocaps`と併用した場合はそちらが優先。 |
| `-wex-rplp-ecbl` | Raptor Lake-Pのみ。輝度を組み込みコントローラ（EC）経由で制御する。フレームバッファのバックライトPWMデューティ書き込みを0〜100の値に換算し、ファームウェアのWMI輝度メソッドへ転送する（このパネルはPWMピンではなくECが駆動）。AppleBacklight用のPNLFデバイスが必要で、そのパネル検索にはWhateverGreenの`applbkl`と同様に汎用パネルカーブを与え、内蔵ディスプレイに輝度スライダーを出す（`N97RPLPPanelInject`）。デスクトップ表示後に1.5秒間明るくする自己診断を行い、現在値・最大値・制御元を`N97RPLPECBacklight`に記録する。 |
| `-wex-rplp-wakelog` | Raptor Lake-Pのみ、診断用。システムのスリープ／復帰メッセージ、それに対するプラグインの submit・ソフトウェア割り込み・forcewake（取得と解放を別に数え、復帰ウィンドウ中は各呼び出し前に forcewake の要求／応答レジスタと参照カウントを読む）、アクセラレータのエンジン開始とその全ての戻りを、レジストリルートの `RPLPWakeLog` と NVRAM の `rplp-wakelog` に記録する。復帰中にパニックしても順序が残る。回数・時刻・レジスタ読み出しのみで、何も遅延・拒否しない。 記録を有効にすると、パワーウェルの復元のたびに、パネルのポートのレジスタ（パネル電源シーケンサ、DPLL0とクロック選択、DDIバッファ、トレーニングパターンと状態、PHYのレーンワード、AUXチャネル、DDIウェル）を25 ms間隔で4秒間記録するトレースも始まり（`N97FBPortTrace`、読み取りのみ）、復帰で最初にDDIが解放される前の直近の電源要求も保持されます（`N97FBPowerRingAtPutDDI`、`N97FBPortRingAtPutDDI`）。 トレースにはAUXとドライバのパワーウェルのレジスタも含まれます。 r341以降、このフラグはIOGraphicsFamilyも記録のために結び付けます：ファミリのスリープ処理の状態をスリープ移行中と復帰後1分間、3秒ごとに（`RPLPWakeIOG`）、アクセラレータのディスプレイパイプの通知先とタイムスタンプを定点で（`RPLPWakePipes`）、ファミリのフレームバッファ通知の入口と出口を（`RPLPWakeFBEvents`）、forcewake参照カウントが最初に負になった呼び出しとその呼び出し元を（`RPLPForceWakeUnderflow`）記録します。オブジェクトのオフセットはインストール済みのIOGraphicsFamily 585.2とIOAcceleratorFamily2 442.10のもので、ファミリの関数アンカーが一致しない限り何も読まず、ポインタは辿る前に検査します。 |
| `-wex-rplp-coldinit` | Raptor Lake-Pのみ、実験的。実際の電源断後のパワーウェル復元の前に、ストラップから PCH の raw クロックを設定し、PCH リセットハンドシェイクを有効にし、Linux の i915 がパワーウェル1を要求する前に行うのと同じ手順で二つのコンボ PHY を初期化する（各 PHY はまず検証し、状態が残っていれば触らない。`N97FBColdPHY`）。その前後に i915 の Alder Lake-P 向けワークアラウンド（ウェル要求前の FLR ソース無効化、MBUS 後の DCPR_2 ビットと致命的エラーマスク）も適用する。raw クロックはディバイダ欄がゼロのときだけ書く。レジスタは前後を `N97FBPowerRestoreWait` と `N97FBColdResume` に記録し、既に一致している場合や all-ones を読む場合は何も書かない。 コールドレジュームでは、電源断でリセットされたMBUSクレジットのワード（ABOX 0と1、各パイプのDBOX）も、起動後最初のエンジン有効化時に読んだ値に書き戻します（`N97FBCreditBoot`、`N97FBColdMBUSCredits`）。Linuxのi915はDBOXのワードをモードセットのたびに書きます。 |
| `-wex-rplp-fwguard` | Raptor Lake-Pのみ、実験的。forcewake の各呼び出しの前にレンダー応答レジスタを読み、all-ones（正常ではあり得ない値）ならネイティブ呼び出しを省く。その値はデバイスがレジスタをデコードしていないことを意味し（復帰直後、コンフィギュレーション空間の復元前）、ネイティブの取得はそれを通過し、解放は決してクリアされないビットを待ってパニックする。参照カウントは触らないので取得と解放の対は保たれる。省いた回数は復帰記録（`-wex-rplp-wakelog`、ワード46とイベントフラグ）に残り、`RPLPForceWakeGuardArmed` がガードの有効を示す。 |
| `-wex-rplp-auxlane` | Raptor Lake-Pのみ、実験的。コンボ AUX のパワーウェル要求の後にコンボ PHY の AUX レーン有効ビット（CL_DW12 ビット0）を立て、解放が完了してから落とす。Linux の i915 が Ice Lake でのみ用いる手順で、この世代では使われていないため、実際の電源断後の復帰で AUX ウェルの応答が来ない場合の実験であり、確立された要件ではない。`N97FBAuxLane` に最後の操作を記録し、フラグなしではレジスタを `N97FBAcquireWait` に読むだけ。 |
| `-wex-rplp-coldpps` | Raptor Lake-Pのみ、実験的。実際の電源断後の復帰で、ネイティブのフレームバッファが動く前に、パネル電源シーケンサの遅延（PP_ON_DELAYS、PP_OFF_DELAYS、PP_CONTROLの電源サイクル遅延フィールド）を、起動後最初のエンジン有効化時に読んだ値で書き直します。両方の遅延レジスタが0を読むときだけで、Linuxのi915が復帰時に再設定するのと同じ手順です。電源オン、VDD、バックライト、リセットの各ビットはネイティブ経路のままです。`N97FBColdPPS`が結果を、`N97FBPortBoot`と`N97FBPortCold`が起動時と復元後のポートのレジスタを、`N97FBPowerRing`と`N97FBPortRing`が直近の電源要求とその時点のポートの状態を記録します。 |
| `-wex-rplp-coldpll` | Raptor Lake-Pのみ、実験的。実際の電源断後の復帰で、ファームウェアがDDI A用に選んでいたポートPLL（DPLL0またはDPLL1）を、起動後最初のエンジン有効化時に読んだ設定で再び有効にし、ポートのクロックを選択してゲートを外します。Linuxのi915と同じ順序（電源有効化とその状態、設定レジスタ、有効化とロック、クロック選択とクロックオフビットを別々の書き込みで）で、そのPLLが有効でもロック済みでもないときだけ行います。`N97FBColdPLL`。 |
| `-wex-rplp-coldphyboot` | Raptor Lake-Pのみ、実験的。`-wex-rplp-coldinit`と併用し、状態を失ったコンボPHYを、Linuxの表だけでなく起動後最初のエンジン有効化時に読んだファームウェア自身の値（`N97FBPHYFirmware`：procmon参照値と、電源断で消える2つの共通レーンのビットフィールド、CL_DW5のクロック設定とCL_DW10の下位ビット。後者はLinuxのi915がDP有効化で書くもの。CL_DW12の上位ビットはステータスとして読めるため記録のみ）で初期化します。`N97FBColdPHY`に何を書き、何が読み戻せたかを記録します。この機種ではファームウェアの値は表と同じなので、このフラグが変えるのは2つのビットフィールドだけです。 |
| `-wex-rplp-auxstanding` | Raptor Lake-Pのみ、実験的。起動ポートのAUX IOパワーウェルへの常設参照を起動時の引き継ぎから保持します。ファームウェアが残した要求を引き受け、実際の電源断後のパワーウェル復元でその要求を再現します（`N97FBAUXStanding`）。これがないと、電源断の後はネイティブドライバのトランザクションごとの要求がウェルを物理的にオンオフし、この機種では復帰のリンク立ち上げ中に一度電源が落ちたウェルが二度と応答しませんでした。起動時には同じトランザクションの間もファームウェアの常設要求がウェルを上げたままでした（ラウンド50）。Linuxのi915はDisplayPortエンコーダのAUXパワードメインをリンクが上がっている間保持します。ラウンド51：これにより実際の電源断の後もリンクがトレーニングされ、パネルが点灯しました。 |
| `-wex-rplp-fwwait` | Raptor Lake-Pのみ、実験的。通常のforcewake呼び出しが、レジスタをまだデコードしていないデバイス（レンダ応答レジスタが全ビット1。復帰の最初の瞬間、デバイスのコンフィギュレーション空間が復元される前の状態）に出会ったとき、100マイクロ秒刻みで最大0.5秒待ってから呼び出しを続行します。上限を過ぎた場合はガード（`-wex-rplp-fwguard`）が判断します。`RPLPForceWakeWait`に待ちを記録します。ラウンド51：復帰時のアクセラレータの最初の作業が電源投入通知の4 ms後にそのようなデバイスへ向かい、その後GPUは何も実行しませんでした。ラウンド52：待ちは起きて（8 ms、省略なし）何も変わりませんでした。GTのレジスタを、アクセラレータ自身のカウント付きエントリでforcewakeドメインを保持したまま、エンジン開始が残した状態・スリープ直前・コールド復元後・復帰4秒後に記録します（`N97GTEngineStart`、`N97GTSleep`、`N97GTRestore`、`N97GTWake`、読み出しのみ。最初のエンジン有効化時の`N97GTBoot`はアクセラレータより前の状態です）。復帰のソフトウェア割り込みは`RPLPWakeSWIRQ`、最初の16回の送信は`RPLPWakeSubmits`、forcewake呼び出しは`RPLPWakeForceWakeFirst`に記録します。 |
| `-wex-rplp-pciwake` | Raptor Lake-Pのみ、実験的。システムの電源投入通知の時点（プラグイン自身のスリープ／復帰ハンドラが最初に呼ばれる場所）でGPUのコンフィギュレーション空間を読み、スリープ直前のコピーと異なる場合はデバイスをD0へ戻し、BARとコマンドレジスタをそのコピーから書き戻します。これによりアクセラレータ自身の復帰作業（ラウンド51・52では通知の4 ms後）がレジスタをデコードするデバイスに届きます。フラグなしではファミリ自身の復元が約20 ms後、その作業が走った後に同じことを行い、記録だけが残ります（`RPLPWakePCI`：スリープ直前、通知時の前後のコンフィギュレーション）。 |
| `-wex-rplp-coldcore` | Raptor Lake-Pのみ、実験的。実際の電源断後の復帰で、プラグインのMBUSクレジット復元の後、ネイティブのフレームバッファが動く前に、起動後最初のエンジン有効化時に読んだ値（`N97FBCoreBoot`）へ、帯域バディレジスタ、ビット30を立てたCHICKEN_MISC_2（Linuxのi915の `CHICKEN_MISC_DISABLE_DPT`。ディスプレイ世代13ではパイプ有効化のたびに書かれる。これが落ちているとディスプレイエンジンはタイル化プレーンを、macOSのどのドライバも作らないディスプレイページテーブル経由で読み、タイル化されたプライマリは黒、リニアなカーソルだけが映る）、電源の入ったパイプのチキンレジスタを、現在値が異なる場合にだけ書き戻します。先に検証し、書いた後に読み戻します。`N97FBColdCore` が結果を、`N97FBCoreSleep`、`N97FBCoreCold`、`N97FBCoreResume`、`N97FBCoreWake` がスリープオーバーライド時、コールド復元後（プラグインの書き込み前）、書き込み後、復帰4秒後の20レジスタを記録します。システムスリープがこの世代のこれらのレジスタをゼロにするという観察は [Thatisjigen/WhateverXe](https://github.com/Thatisjigen/WhateverXe)（ブランチ `pr/system-sleep-0.7.0`）によるものです。 |
| `-wex-rplp-wsstart` | Raptor Lake-Pのみ、実験的。システムのwill-sleep通知の時点と、スリープ突入中および復帰中のサンプラの各ティックで、フレームバッファがすべて`IONDRVFramebuffer`（ファミリが未駆動のディスクリートGPUに付けるプレースホルダで、ウィンドウサーバが決して開かないもの）であるフレームバッファコントローラの、グラフィックスファミリのウィンドウサーバ待ちバイトとキャッシュされた計算済み状態ワードを消します。その待ちビットが集約状態にある間、ファミリはウィンドウサーバへのスリープ通知もアクセラレータへのWillSleepも送らないため（公開IOGraphicsソース、`IOFramebuffer::systemWork`）、アクセラレータの復帰には再開すべきものがなく、スリープ突入はファミリの90秒の応答タイムアウトを待つことになります。またファミリの`computeState`は、コントローラに未処理の作業がない限りキャッシュされたワードを返し、プレースホルダには作業が来ないので、バイトだけでは何も変わりません（ラウンド56）。書き込みはファミリのコントローラオブジェクトへの1バイトと1ワードで、ファミリ自身の経路が使うオフセットに対して、コントローラのフレームバッファをクラスで確認した後にだけ行います。ゼロの状態ワードが透過的な値です。`RPLPWSStart` がフラグの有無にかかわらず各コントローラのバイトとワードを前後で記録し、再度消す必要のあったティック数も記録します。 |
| `-wex-rplp-entryirq` | Raptor Lake-Pのみ、実験的。スリープ突入時の割り込み記録に関する2つの変更です。解放ゲートが「ブリッジがまだ影響するパイプをアクティブと数えている」という理由だけで拒否するポートのみの電源解放（PW1でもパイプウェルでもないもの）を、ハードウェアが影響するすべてのパイプの停止（パイプ、カーソル、プレーン、トランスコーダ、DSBがオフ）を示していれば通します。ラウンド57の2回目のスリープでは、ネイティブの経路がディスプレイ電源オフ時にすべてオフの状態でパネルのDDIウェルを解放したのに記録が古く、拒否が復帰の引き継ぐ障害をラッチしました。また、スリープオーバーライド時にハンドラが動いていないのにオンのままのディスプレイ割り込みマスタを、プラグイン自身の無効化経路で消し、サスペンドの静止証明が最初のスリープと同じ状態に出会うようにします。`N97FBEntryIRQ` が緩和規則による通過と拒否、オーバーライド時の無効化を記録し、`N97FBFirstPowerCheck` のワード24が緩和規則を試したか、通ったかを示します。 |
| `-wex-no-va-renderer` | 実験的、対応GPU共通。アクセラレータの `IOVARendererID` を公開しません。[Thatisjigen/WhateverXe](https://github.com/Thatisjigen/WhateverXe)（ブランチ `pr/video-optin-0.7.0`）からの移植で、そこではこのキーを隠すだけでピクセル転送中のVAドライバのクラッシュ（画面収録、カメラ、メディア解析）が止まります。このフォークのRaptor Lake-Pノートが画面収録クラッシュとして挙げているものと同じ症状です。Appleのデコーダサービスがハードウェアレンダラを見つけられず、動画をソフトウェアでデコードする可能性があります。この機種では未検証です。 |
| `-wex-rplp-warev3` | Raptor Lake-Pのみ、診断用。カーネルアクセラレータのワークアラウンド表の初期化にだけ、物理revision 4の代わりに3を渡す（revision 4未満のポリシー、パックされた13ビットが異なる）。OpenGL読み戻しプローブを既定と比較するための実験で、描画・メディア・性能・安定性に影響しうる。修正ではなく常用もしない。`N97RPLPWARev3`。 |
| `wex-rplp-urb-banks=` | Raptor Lake-Pのみ。ドライバへ公開するURB（unified return buffer）のバンク数を、実測の6ではなく1〜8の指定値にする。ユーザーモードのOpenGLドライバは頂点シェーダのURB割り当てをこの数から決める。5にすると割り当ては384 KiBではなく320 KiBで終わり、Raptor Lake-Pの文書にある描画欠落は消える（オフスクリーンの全プローブが合格し、ChessとCubeが正しく描画される）。正確な割り当て規則をユーザーモードで持つまでは、このパーツでは`=5`が動作する設定。すべての利用者のURB容量が少し減る。プラグインの admission ガードは公開した値を基準にし、適用された上書きは`RPLPURBBanksOverride`として公開される。先頭にダッシュは付けない。ダッシュ付きの名前はカーネルにフラグとして扱われ、値が無視される。 |
| `-wex-rplp-gl-urb` | Big Sur 20G1443上の認定済みRaptor Lake-P A7A8 revision 04向けの実験的なOpenGL URB割り当て補正。ソースで指定したドライバイメージが必要。物理的な6バンクの能力値を維持し、検証済みのマップページ内で一致する割り当てパケットのみを補正する。ディスク上のバンドルは変更しない。既定では無効。`wex-rplp-urb-banks=`とは併用できない。OS、ドライバ、ファイル、検証状態が一致しなければ補正は有効にならない。`RPLPGLURBReady`と`RPLPGLURBApplied`でフックの準備完了とページへの適用を区別する。実機での検証は未完了。 |

### 動作確認済みの機能と既知の問題

Tiger LakeとAlder Lake-Nで、全6 OSにわたり、外部2画面の1920×1080・60Hz出力、Metal/MPS、OpenGL、OpenCL、ハードウェアH.264エンコード／デコード、ハードウェアHEVCデコード、画面の消灯／復帰、Safariのハードウェア動画デコード、Widevine再生を確認しています。共通TGLCompatビルドも、Alder Lake-Nの全6 OSで導入と機能を確認しています。すべてのcodec・アプリケーション・表内のGPUでの互換性を確認したものではありません。

#### 既知の問題と制限

- VA画像変換でnull参照によるクラッシュが発生することがあります。
- Sequoia・Tahoeなどで、DisplayPortの再接続により黒画面やシステムハングが発生することがあります。
- Safariでは少数のフレーム落ちを確認しています。
- 3画面同時出力、システムスリープ、より高い解像度や他のコネクタ構成は追加検証が必要です。USB-Cと内蔵ディスプレイへの対応は下記ロードマップに含まれます。

### upstreamのロードマップ

#### Planned

- Alder Lake／Raptor LakeのS/H/P/U対応
- USB-C画面出力対応
- 内蔵ディスプレイ対応

#### Not Planned

- Xe-LPG対応
- Xe2以降のアーキテクチャへの対応

### ビルドとテスト

macOS、Xcode command-line tools、Python 3.9以降、MacKernelSDK、Liluの開発用ヘッダ／ライブラリを使用します。CIではTahoeのkernel定義を含むLiluの[1.7.2リリースタグ](https://github.com/acidanthera/Lilu/tree/1.7.2)を使用します。`MacKernelSDK/`と`Lilu.kext/`をリポジトリ直下に配置するか、`--sdk`と`--lilu`でパスを指定してください。

```sh
python3 tools/build.py --release
python3 tools/test.py
```

WhateverXeとAppleIntelTGLCompatのkext、およびソースのmanifestは`build/`に出力されます。TGLCompatのソースと共有ヘッダーはこのリポジトリで管理し、署名済みバイナリはWhateverXe Graphicsで配布します。WEXのRelease ZIPに含まれるkextはOpenCore用プラグインのみで、グラフィックスpkgは含みません。任意のdonor再生試験には`--donor /path/to/AppleIntelTGLGraphicsFramebuffer`と`--accelerator /path/to/AppleIntelTGLGraphics`を指定します。各試験が受け付ける正確なバイナリが必要です。CPUテストだけで実GPUの互換性が確認できるわけではありません。ビルドにWhateverGreenのソースは不要です。
このforkのCIは、`main`へのpushごと、および`rplp-*`ブランチでの手動実行で同じコマンドを実行し、パッケージ化したビルドをアップロードします。

### ライセンスとクレジット

WhateverXeはWhateverGreenの著作権表示とBSD-3-Clauseライセンスを継承しています。[LICENSE.txt](LICENSE.txt)を参照してください。このforkで追加した部分も同じライセンスです。LiluとMacKernelSDKは別の依存コンポーネントです。
