# WhateverXe-RaptorLakeP

[English](README.md)

[WhateverXe](https://github.com/b00t0x/WhateverXe)のforkです。Intel **Raptor Lake-P**内蔵GPUのmacOS対応を目指しています。
対象は第13世代Core H／P／Uシリーズのノート向けCPUに搭載されたUHD／Iris Xe Graphicsと、ノートPCの内蔵ディスプレイ（eDP）です。

> **現状（2026-11-02）：内蔵パネルはネイティブに点灯し、GPUアクセラレータ（MetalとOpenGL）がその上で動作し、輝度調整も動作します。未解決はシステムのスリープと復帰です。**
> - 2026-10-20以降、ネイティブのDisplay 13フレームバッファ経路が検証機のeDPパネルを駆動します（リンクトレーニング、60 Hzモード、カーソル、WindowServerデスクトップ。起動引数`-wex-rplp-gt`）。同日、ネイティブのアクセラレータもその上で起動しました。Metal／OpenGL／ビデオの各プラグインが接続され、WindowServerはGPUで描画し、「このMacについて」はiGPUに1536 MBを表示します。
> - ブランチは`rplp-accel`です。upstream 0.3.0（Big Sur〜Tahoe対応の単一バイナリ。20G1443カーネル向けのMMIOキャッシュ用ブリッジはBARのページ範囲に束縛）にこのforkのパネル経路を加えたものです。`main`は古く、検証済みのラウンドの後に更新します。upstream 0.6.1は取得済みで未マージです。
> - ディスプレイのスリープと復帰は動作します（パイプ有効化時に割り込みを再設定）。
> - OpenGLは2026-10-28以降動作します。アクセラレータのハードウェア性能応答はパッチ済みのOpenGL／ビデオのユーザモードドライバにTiger Lake GT2（9A49）を示し（2026-10-21以降。`-wex-rplp-nocaps`で物理IDのまま）、プラグインは計測された6バンクではなく5つのURBバンクをドライバに公開します（`wex-rplp-urb-banks=5`）。これによりOpenGLドライバの頂点URB割り当ては384 KiBではなく320 KiBで終わり、オフスクリーンの読み戻しプローブはすべて合格し、ChessとWebGLは正しく描画されます。ドライバの正確な割り当て規則は今後の課題です。ハードウェア動画：Safariの再生は動作しますが、画面収録はピクセル転送中にビデオドライバ内でクラッシュします（未解決）。
> - 輝度は2026-10-21以降、組み込みコントローラ（EC）経由で動作します。ディスプレイエンジンのPWMデューティはこのパネルに届かないため、`-wex-rplp-ecbl`がフレームバッファのデューティ書き込みをファームウェアのWMI輝度メソッドへ転送し、AppleBacklightに汎用パネルカーブを与えます。輝度キーはDSDTのキー用ガードへの2つのバイトパッチとacidantheraのBrightnessKeysで動作します（どちらもEFI側で、このプラグインの外です）。
> - システムのスリープと復帰は進行中です（2026-10-29からのテストラウンド）。プラットフォームはスリープして復帰し、ディスプレイエンジンは実際の電源断から戻りますが、パネルはまだ再点灯しません。原因を4つ順に見つけて修正しました。いずれも復帰の一段先です：プラグイン自身の復元で冷えたパワーウェルのfuseを1マイクロ秒しか待たなかったこと（現在は2 ms）、電源断でリセットされるプレーンバッファとMBUSのレジスタをエンジン有効化が拒んだこと（現在は再設定）、2つのコンボPHYが状態を失っていたこと（現在はi915と同じ手順で再初期化）、そして未解決のまま、電源断の後にAUX Aパワーウェルが要求に応答しないこと。現在のビルドは電源要求ごとにPHYのAUXレーンビットとAUXチャネルの状態を記録し、i915がIce Lakeで用いるレーン有効化の書き込みを実験として提供します。復帰の最初の瞬間に、レジスタをまだデコードしていないデバイスへのディスパッチがforcewakeの解放で2回パニックしました。任意有効化のガードがそれらの呼び出しを省きます。約2分のスリープ突入はグラフィックスファミリ自身のスリープ応答のタイムアウトで、ドライバのないdGPUのプレースホルダのフレームバッファを隠す実験を用意しました。任意有効化のフラグ：`-wex-rplp-wakelog`、`-wex-rplp-fwguard`、`-wex-rplp-coldinit`、`-wex-rplp-auxlane`。未検証：デスクトップ以外のMetalワークロード、144 Hz、外部出力。
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
| R4 | 内蔵ディスプレイの本対応：パネル電源シーケンス、バックライト、消灯／点灯、ADL-Pのテーブルによるリンク再トレーニング、144Hz | 輝度調整、スリープ／復帰、ネイティブのリフレッシュレートが動作する | 進行中：輝度は2026-10-21から（EC経由）、ディスプレイのスリープ／復帰は完了。システムのスリープ／復帰は2026-10-29から：原因を4つ順に修正し、実際の電源断後のAUX Aウェルの応答が未解決。144 Hzと再トレーニングは未着手 |
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
| `-wex-rplp-wakelog` | Raptor Lake-Pのみ、診断用。システムのスリープ／復帰メッセージ、それに対するプラグインの submit・ソフトウェア割り込み・forcewake（取得と解放を別に数え、復帰ウィンドウ中は各呼び出し前に forcewake の要求／応答レジスタと参照カウントを読む）、アクセラレータのエンジン開始とその全ての戻りを、レジストリルートの `RPLPWakeLog` と NVRAM の `rplp-wakelog` に記録する。復帰中にパニックしても順序が残る。回数・時刻・レジスタ読み出しのみで、何も遅延・拒否しない。 |
| `-wex-rplp-coldinit` | Raptor Lake-Pのみ、実験的。実際の電源断後のパワーウェル復元の前に、ストラップから PCH の raw クロックを設定し、PCH リセットハンドシェイクを有効にし、Linux の i915 がパワーウェル1を要求する前に行うのと同じ手順で二つのコンボ PHY を初期化する（各 PHY はまず検証し、状態が残っていれば触らない。`N97FBColdPHY`）。その前後に i915 の Alder Lake-P 向けワークアラウンド（ウェル要求前の FLR ソース無効化、MBUS 後の DCPR_2 ビットと致命的エラーマスク）も適用する。raw クロックはディバイダ欄がゼロのときだけ書く。レジスタは前後を `N97FBPowerRestoreWait` と `N97FBColdResume` に記録し、既に一致している場合や all-ones を読む場合は何も書かない。 |
| `-wex-rplp-fwguard` | Raptor Lake-Pのみ、実験的。forcewake の各呼び出しの前にレンダー応答レジスタを読み、all-ones（正常ではあり得ない値）ならネイティブ呼び出しを省く。その値はデバイスがレジスタをデコードしていないことを意味し（復帰直後、コンフィギュレーション空間の復元前）、ネイティブの取得はそれを通過し、解放は決してクリアされないビットを待ってパニックする。参照カウントは触らないので取得と解放の対は保たれる。省いた回数は復帰記録（`-wex-rplp-wakelog`、ワード46とイベントフラグ）に残り、`RPLPForceWakeGuardArmed` がガードの有効を示す。 |
| `-wex-rplp-auxlane` | Raptor Lake-Pのみ、実験的。コンボ AUX のパワーウェル要求の後にコンボ PHY の AUX レーン有効ビット（CL_DW12 ビット0）を立て、解放の前に落とす。Linux の i915 が Ice Lake でのみ用いる手順で、この世代では使われていないため、実際の電源断後の復帰で AUX ウェルの応答が来ない場合の実験であり、確立された要件ではない。`N97FBAuxLane` に最後の操作を記録し、フラグなしではレジスタを `N97FBAcquireWait` に読むだけ。 |
| `-wex-rplp-coldpps` | Raptor Lake-Pのみ、実験的。実際の電源断後の復帰で、ネイティブのフレームバッファが動く前に、パネル電源シーケンサの遅延（PP_ON_DELAYS、PP_OFF_DELAYS、PP_CONTROLの電源サイクル遅延フィールド）を、起動後最初のエンジン有効化時に読んだ値で書き直します。両方の遅延レジスタが0を読むときだけで、Linuxのi915が復帰時に再設定するのと同じ手順です。電源オン、VDD、バックライト、リセットの各ビットはネイティブ経路のままです。`N97FBColdPPS`が結果を、`N97FBPortBoot`と`N97FBPortCold`が起動時と復元後のポートのレジスタを、`N97FBPowerRing`と`N97FBPortRing`が直近の電源要求とその時点のポートの状態を記録します。 |
| `-wex-rplp-coldpll` | Raptor Lake-Pのみ、実験的。実際の電源断後の復帰で、ファームウェアがDDI A用に選んでいたポートPLL（DPLL0またはDPLL1）を、起動後最初のエンジン有効化時に読んだ設定で再び有効にし、ポートのクロックを選択してゲートを外します。Linuxのi915と同じ順序（電源有効化とその状態、設定レジスタ、有効化とロック、クロック選択とクロックオフビットを別々の書き込みで）で、そのPLLが有効でもロック済みでもないときだけ行います。`N97FBColdPLL`。 |
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
