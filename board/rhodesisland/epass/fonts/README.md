# epass 共享字体 - 产品字表

`epass-fonts` 包在 host 侧子集化时读取这里的字表,决定思源黑/宋保留哪些字、
FontAwesome 保留哪些图标码点。产物装到设备 `/usr/share/fonts/epass/`。

```
fonts/
├─ charset/          # 思源黑/宋要保留的字 (逐字符; 由 Config.in 列出的文件取并集)
│  ├─ common.txt     #   常用字 + 标点
│  ├─ operators.txt  #   方舟干员名用字
│  ├─ literals.txt   #   drm_app_neo 源码里出现的中日文/全角字面量
│  └─ anime_games.txt#   二次元游戏角色名用字 (多游戏合辑)
└─ icons.txt         # FontAwesome/LV_SYMBOL 图标码点 (十六进制, 每行一个)
```

假名 / 各类标点 / 全角等 CJK 排版基础区间由子集脚本固定内建,不用列在这里。

## 更新字表

`common` / `operators` / `literals` / `icons` 由
`drm_app_neo/tools/font_generate/export_board_charset.py` 从各程序源码
(+ 可选 `character_table.json` 干员表) 重新导出到本目录。换字体、加新 UI 文案 /
新图标后重跑一次并提交,再重建 `epass-fonts` 包即可。

`anime_games.txt` 另从角色库抽取中日文名用字,不覆盖上述导出;主要来源:

- [deepghs/game_characters](https://huggingface.co/datasets/deepghs/game_characters)
  (方舟 / 碧蓝航线 / 蔚蓝档案 / FGO / 原神 / 少前 / 云图 / NIKKE / 无期迷途 / 星穹铁道)
- 绝区零短名: [fcitx5-pinyin-mihoyo](https://github.com/Yukari0201/fcitx5-pinyin-mihoyo) `zenlesszonezero.dict.yaml`
- 蔚蓝档案补充: [BlueArchive-PinyinDictionary](https://github.com/ylhcqN/BlueArchive-PinyinDictionary)

方舟完整干员表仍以 `operators.txt` (`character_table.json`) 为准; `anime_games.txt` 侧重跨游戏补字。
