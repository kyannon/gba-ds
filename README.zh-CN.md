# GBA DS（中文分支）

[GBA DS](https://github.com/kyannon/gba-ds) 是给**双屏掌机**做的 Game Boy Advance 模拟器（基于 libmgba），最初为 Anbernic RG DS Plus（ROCKNIX / Android）而写。游戏在一块屏幕上跑，另一块屏幕显示游戏自己的地图（从隐藏的第二份游戏里截取）或游戏徽标，就像 NDS 上的《恶魔城》那样。

**这个仓库是它的中文分支（`zh-cn`）**：界面做成简体中文和繁体中文，其余功能与原项目一致。

## 中文界面

- 游戏列表、设置、暂停菜单、快捷栏、金手指、补丁、覆盖层、热键、按键映射、更新、文件夹选择，以及游戏中的各种提示，都是中文。
- 语言在**设置 > 游戏 > 语言**里选：自动（跟随系统，系统不是中文就用英文，默认）、English、中文（简体）、中文（繁體）。
- 切换立即生效，不用重启；选 English 就和原版一样。
- 中文用 Noto Sans SC / Noto Sans TC（SIL Open Font License 1.1），按界面实际用字裁剪，所以体积不大；中文段落按字断行。
- 成就名称和说明来自 RetroAchievements 服务器，游戏名和路径是你自己的数据，这些保持原样。

文本和字体是怎么做的、怎么加词条，见 [docs/I18N.md](docs/I18N.md)（英文）。

## 安装

发行包请用[原项目的 Releases](https://github.com/kyannon/gba-ds/releases)；本分支要自己编译，或者用你信任的人编译的包。原项目的安装指南（ROCKNIX、BananaOS、Stock Linux、Android、浏览器）在 [wiki](https://github.com/kyannon/gba-ds/wiki) 里。

## 编译

和原项目一样（需要 x86_64 Linux 主机、git、cmake、make、zip）：

```sh
scripts/build-rocknix.sh            # -> dist/gba-ds-v<VERSION>-linux-aarch64.zip
```

桌面版（开发和测试用，另需 libpng-dev）：

```sh
scripts/setup-host.sh   # 第一次：把 mGBA 和 SDL2 取到 deps/
make && make tools      # 之后：build/host/gba-ds、build/host/harness
```

Android 的 APK 由 GitHub Actions 构建（`.github/workflows/android.yml`）。

界面文本在 `assets/i18n/*.tsv`，改动后跑：

```sh
tools/make_i18n.py        # 生成 src/i18n_zh.c、src/i18n_zt.c
tools/make_zh_font.py     # 有新的汉字时才需要（需要 fontTools 和两个源字体）
tools/embed_fonts.py      # 生成 src/fonts_data.c
```

## 许可

GBA DS 是自由软件，按 [GNU 通用公共许可证](LICENSE) 第 3 版发布。内置的 Jost、Noto Sans SC/TC 字体按 SIL Open Font License 1.1 使用（`assets/fonts/` 里有许可证原文）。本项目与任天堂无关。

其余说明（支持的游戏、第二屏的原理、哲学、参与方式）以原项目为准：<https://github.com/kyannon/gba-ds>。
