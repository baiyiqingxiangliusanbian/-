# 万象图鉴 · AscendSpire 内容工作台

这是一个只在本机运行的图鉴与 JSON 内容编辑器。它直接读取或写回工程的 `Content/Data`，不会上传数据。

## 启动

首次使用：

```bash
cd /Users/audezest/AscendSpire/Tools/ContentWorkbench
npm install
npm run dev
```

然后打开 `http://localhost:3000`，点击“选择工程目录”，选择 `/Users/audezest/AscendSpire`。浏览器会要求授予该目录的读写权限。

## LLM JSON 协议

工作台右侧“生成协议”可以复制当前内容类型的完整提示词，也可以直接粘贴模型返回值。标准信封为：

```json
{
  "kind": "card",
  "data": {
    "id": "blood_edge",
    "name": "血刃",
    "type": "sword",
    "rarity": "uncommon",
    "cost": 1,
    "description": "失去3点气血，对一个敌人造成 {effect1} 点伤害。每损失5点气血，伤害+2。",
    "effects": [
      { "action": "self_damage", "target": "self", "value": 3 },
      { "action": "damage", "target": "enemy", "value": 8, "scale_by": "missing_hp", "scale_factor": 2, "scale_divisor": 5 }
    ]
  }
}
```

字段全集与枚举位于 `Content/Data/content_schema.json`。同 ID 导入会替换原条目，新 ID 会追加；保存前仍建议查看工作台的校验提示。

卡牌还可以声明表现层：`visual.animation` 支持 `slash`（剑光）、`fireball`（投射并爆炸）、`impact`、`block`、`heal`、`draw`，`visual.sound` 支持对应的程序化短音效。表现层只影响 UI，不改变卡牌结算。

## 数据同步提醒

Mac 游戏包不会自动取得新 JSON。游戏编译后仍需把 `Content/Data/*.json` 同步到：

`Binaries/Mac/AscendSpire.app/Contents/UE/AscendSpire/Content/Data/`
