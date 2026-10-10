# WhateverXe-RaptorLakeP

[English](README.md)

[WhateverXe](https://github.com/b00t0x/WhateverXe)のforkです。Intel **Raptor Lake-P**内蔵GPUのmacOS対応を目指しています。
対象は第13世代Core H／P／Uシリーズのノート向けCPUに搭載されたUHD／Iris Xe Graphicsと、ノートPCの内蔵ディスプレイ（eDP）です。

> **現状：まだ動作するドライバではありません。**
> - このforkが現在追加しているのは、任意かつ読み取り専用の診断機能（`-wex-rplp-observe`）のみです。GPUの構成とファームウェアの画面状態を報告します。
> - Raptor Lake-PのGPUは引き続き非対応です。
> - upstreamが対応する機種（Tiger Lake、Alder Lake-N / Twin Lake）の動作はupstreamと変わりません。

本forkは個人が趣味で運営する独立したforkであり、upstreamの作者とは関係ありません。実機で検証できた成果はupstreamへ提案する予定です。そのため`rplp-*`ブランチにはfork固有の変更を含めず、`main`ではこのREADMEを追加しています。

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
- **内蔵ディスプレイ。** WhateverXeが駆動するのは固定HDMI／DisplayPort出力です。eDP（パネル電源シーケンス、バックライト、リンクトレーニング）はどのプラットフォームでも未実装です。
- **プラットフォーム。** まず第13世代のハイブリッドCPUでBig Sur 11.7.11が起動する必要があります。検証機ではまだ確認できていません。

## 計画

各段階はupstreamと同じく「観測 → 検証 → 有効化」の順で進め、ハードウェアに関するすべての規則にCPUテストを用意します。

| 段階 | 目標 | 完了条件 | 状況 |
| --- | --- | --- | --- |
| R1 | 読み取り専用のRaptor Lake-Pプロファイル：fuse、EU構成、ファームウェアの画面状態。forcewakeの取得／解放以外の書き込みなし | macOSでの読み出し結果が、同じ機種のLinux i915キャプチャと一致する | コード完成・CIテスト済み。実機での実行待ち |
| R2 | GTバックエンド：既存のBCSセルフテスト、次にこのGPUのURB／L3値でアクセラレータ修正を適用 | セルフテストが通り、ファームウェアのframebuffer上でアクセラレータが起動する | 未着手 |
| R3 | 内蔵ディスプレイの初表示：donorの内蔵パネル用コネクタを残し、ファームウェアが確立したリンクを引き継ぐ | ファームウェアのモードで内蔵パネルにデスクトップが表示される | 未着手 |
| R4 | 内蔵ディスプレイの本対応：パネル電源シーケンス、バックライト、消灯／点灯、ADL-Pのテーブルによるリンク再トレーニング、144Hz | 輝度調整、スリープ／復帰、ネイティブのリフレッシュレートが動作する | 未着手 |
| R5 | 内蔵ディスプレイでのMetalと安定化。その後USB-C出力 | 内蔵パネルでWindowServerがアクセラレーションされる | 未着手 |

## R1診断の試し方

**動作。** 診断は`-wex-rplp-observe`を指定した場合に限り、物理的にRaptor Lake-PのGPUであるときだけ実行されます。
- GPUのfuse（slice、DSS、EU、L3、media）、Display 13の起動時状態、eDP関連レジスタ（パネル電源、バックライト、DPリンク、PSR）を読み取ります。
- 読み取った値をIORegistryに公開します。
- レジスタへの書き込みは、upstreamがAlder Lake-Nで使うものと同じforcewakeの取得／解放のみです。既に他がforcewakeを保持している場合は実行しません。
- GPUは非対応のままです。

**必要なもの。** Liluと、このforkのDEBUG版WhateverXe。この段階ではOCLPのroot patchとAMFIPassは不要です。

**macOS。** WhateverXeが対象とするのはBig Sur 11.7.11（20G1443）のみです。他のバージョンでは、この読み取り専用の段階に限り`-wexbeta`を追加し、終わったら削除してください。

**ビルド。**
- [Actions](https://github.com/samranahmad09/WhateverXe-RaptorLakeP/actions/workflows/ci.yml)で、`main`で成功した最新の実行を開き、**Artifacts**をダウンロードしてください。`WhateverXe-…-DEBUG.zip`が含まれています。
- これらのビルドはupstreamと同じバージョン番号のため、upstreamのリリースとは分けて保管してください。

**収集。**

```sh
ioreg -lw0 -r -k RPLPObserveVersion > ~/Desktop/rplp-ioreg.txt
sudo dmesg | grep -E 'RaptorLakeP|WhateverXe' > ~/Desktop/rplp-dmesg.txt
```

**公開されるproperty。**
- `RPLPGTReadout`、`RPLPDisplay13Boot`、`RPLPEDPBoot`
- `RPLPPhysicalMemoryLayout`、`RPLPStolenReservation`
- 物理ID、revision、GTとディスプレイのステッピング名

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

実験段階のプロジェクトであり、[OpenCore Legacy Patcherのfork](https://github.com/b00t0x/OpenCore-Legacy-Patcher)が提供するXe root patchを必要とします。

以下はupstreamのドキュメントで、このforkと異なる箇所のみ修正しています。Appleのドライバを読み込む段階（R2以降）に適用されます。

### 対応範囲

現在の対象は **macOS Big Sur build 20G1443** です。

| GPUファミリー | バックエンド実装があるdevice ID |
| --- | --- |
| Tiger Lake | `9A40`、`9A49`、`9A78` |
| Alder Lake-N / Twin Lake | `46D0`–`46D4` |

Raptor Lake-Pには、このforkでもまだバックエンドがありません。上記の[計画](#計画)を参照してください。

対応状況は機種やコネクタ構成によって異なります。この表はバックエンドの実装範囲を示すもので、すべての機種での動作を保証するものではありません。詳細な選択条件と上書き設定は[デバイス選択](docs/device-selection.md)を参照してください。

**upstreamで確認済みの内容。**
- Tiger LakeとAlder Lake-N：外部2画面の1920×1080・60Hz出力、Metal/OpenGL、ハードウェアHEVC、画面の消灯／復帰。
- Tiger Lake：ハードウェアH.264のエンコード／デコードも確認済み。
- 追加検証が必要なもの：3画面同時出力、システムスリープ、より高い解像度、他のコネクタ構成。
- USB-Cと内蔵ディスプレイへの対応は、upstreamのロードマップに含まれています。

upstreamのリリースは[b00t0x/WhateverXeのReleases](https://github.com/b00t0x/WhateverXe/releases)にあります。このforkは上記のとおりCIビルドのみを公開しています。

### 必須構成

| コンポーネント | 役割 |
| --- | --- |
| Lilu | WhateverXeの動作に必要なランタイム |
| WhateverXe.kext | OpenCoreでLiluより後に読み込む |
| [OpenCore Legacy Patcherのfork](https://github.com/b00t0x/OpenCore-Legacy-Patcher) | Xe root patchを適用してグラフィックス構成を導入する |
| [AMFIPass v1.4.1 Mod](https://github.com/b00t0x/OpenCore-Legacy-Patcher/blob/mod/payloads/Kexts/Acidanthera/AMFIPass-v1.4.1-Mod-RELEASE.zip) | 改変されたroot patchのグラフィックス構成に必要。OpenCoreで導入する |

OCLP forkは[Releasesページ](https://github.com/b00t0x/OpenCore-Legacy-Patcher/releases)からダウンロードしてください。rootの構成要素の導入とkernel collectionの更新はOCLPが行います。

リンク先の **Mod版** アーカイブから`AMFIPass.kext`を展開し、`EFI/OC/Kexts`へ配置して、`Kernel > Add`で有効にしてください。既存のAMFIPassがある場合はこのMod版へ置き換え、両方を同時に読み込まないでください。

複数GPUの構成を含め、WhateverGreenとWhateverXeは同時に有効にしないでください。

### 設定

GPUはPCI device IDから自動選択します。
- 対応範囲内では、ファームウェアのVBTから配線を読み取ります。
- 固定HDMI／固定DisplayPort構成では、通常、配線propertyの手動指定は不要です。
- 高度な設定としての`device-id`上書きは[デバイス選択](docs/device-selection.md)で説明しています。

#### DVMT

BIOS/UEFIで変更できる場合は、**DVMT Pre-Allocatedを96MBに設定することを推奨します**。

**60MBから変更できない場合**は、`boot-args`に`-igfxdvmt`を追加してください。あわせて、OpenCoreの`DeviceProperties > Add`にあるiGPUの項目（通常は`PciRoot(0x0)/Pci(0x2,0x0)`）へ以下の2つを設定してください。

| Property | 型 | 値（16進数） |
| --- | --- | --- |
| `framebuffer-patch-enable` | Data | `01000000` |
| `framebuffer-stolenmem` | Data | `0000C003` |

- Data値はリトルエンディアンです。
- `-igfxdvmt`は容量計算を補正し、DevicePropertiesは表示用メモリ領域を60MB内に収めます。
- どちらもファームウェアが確保したメモリを増やす設定ではありません。
- 96MBの場合、これらのDVMT用設定は不要です。

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
| `-wex-rplp-observe` | Raptor Lake-Pのみ。上記の読み取り専用R1診断を公開する。GPUは非対応のまま。 |

### upstreamのロードマップ

#### Planned

- OCLPの機能を利用したmacOS Monterey〜Tahoe対応
- Alder Lake／Raptor LakeのS/H/P/U対応
- USB-C画面出力対応
- 内蔵ディスプレイ対応

#### Not Planned

- Xe-LPG対応
- Xe2以降のアーキテクチャへの対応

### ビルドとテスト

macOS、Xcode command-line tools、Python 3.9以降、MacKernelSDK、Liluの開発用ヘッダ／ライブラリを使用します。`MacKernelSDK/`と`Lilu.kext/`をリポジトリ直下に配置するか、`--sdk`と`--lilu`でパスを指定してください。

```sh
python3 tools/build.py --release
python3 tools/test.py
```

- kextとソースのmanifestは`build/`に出力されます。
- 任意のdonor再生試験には`--donor /path/to/AppleIntelTGLGraphicsFramebuffer`と`--accelerator /path/to/AppleIntelTGLGraphics`を指定します。各試験が受け付ける正確なバイナリが必要です。
- CPUテストだけで実GPUの互換性が確認できるわけではありません。
- ビルドにWhateverGreenのソースは不要です。
- このforkのCIは、`main`へのpushごとに同じコマンドを実行し、パッケージ化したビルドをアップロードします。

### ライセンスとクレジット

WhateverXeはWhateverGreenの著作権表示とBSD-3-Clauseライセンスを継承しています。[LICENSE.txt](LICENSE.txt)を参照してください。このforkで追加した部分も同じライセンスです。LiluとMacKernelSDKは別の依存コンポーネントです。
