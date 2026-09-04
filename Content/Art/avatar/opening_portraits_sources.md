# 有限开场头像来源清单

生成日期：2026-09-02（Asia/Shanghai）
生成方式：内置 `image_gen`，五次独立生成；未使用 CLI、API、脚本或占位图。
统一规格：1:1 方形、肩部以上、居中正视、脸部清晰、无文字/水印/UI 边框。运行时由 `FAscendArt::GetTexture` 按 `Content/Art` 相对路径加载。

| 稳定 ID | 开场说话人 | 最终资源 | 简短生成提示与质量检查 |
| --- | --- | --- | --- |
| `opening_a_yao` | 阿杳 | `Art/avatar/opening_yao_v1.png` | 明确十二岁的青禾渡农家孩子；圆脸、黑色短发、旧蓝灰棉衣、雨日灶屋；确认儿童比例、眼睛清楚、头发和下巴未裁切。 |
| `opening_sang_wan` | 桑晚 | `Art/avatar/opening_sangwan_v1.png` | 栖鹤观旧药圃的杂灵根少女；窄长脸、双辫、褪色青绿杂役衣、药圃晨雾；确认年龄与身份可辨、脸部清楚、未裁切。 |
| `opening_lu_qinghe` | 陆青禾 | `Art/avatar/opening_luqinghe_v1.png` | 临溪陆氏年轻旁支族姐；柔方脸、半束黑发、炭灰与青绿色族衣、祖堂油灯；确认与其他头像脸型/发式有差异、未裁切。 |
| `opening_wen_yanhui` | 闻雁回 | `Art/avatar/opening_wenyanhui_v1.png` | 玄微门外院巡夜弟子；棱角心形脸、高束发、靛蓝与锈红实用衣、夜间药炉；确认眼神与身份清楚、未裁切。 |
| `opening_blind_grandmother` | 瞎婆婆 | `Art/avatar/opening_blind_grandmother_v1.png` | 白石坊修伞摊真实长者；七十岁上下、白灰发、深皱纹、云翳眼和失焦目光、旧靛蓝头巾；确认明显年长与失明特征、未裁切。 |

## 注册表约定

`Content/Data/rp_characters.json` 中每个 ID 只有对应中文说话人名作为别名，避免英文或泛化别名误撞。`art` 和开场实际使用的表情键都指向该 ID 自己的版本化 PNG；未知人物继续使用注册表既有的中性回退，不复用这五张特定头像。
