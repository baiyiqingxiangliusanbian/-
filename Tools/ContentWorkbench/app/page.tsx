"use client";

import { useEffect, useMemo, useRef, useState } from "react";

type Entity = Record<string, any>;
type CategoryKey = "cards" | "enemies" | "relics" | "pills" | "events" | "story";
type DataDocument = { raw: Entity; items: Entity[] };

const categories: Record<CategoryKey, { label: string; singular: string; file: string; root: string }> = {
  cards: { label: "卡牌", singular: "卡牌", file: "cards.json", root: "cards" },
  enemies: { label: "敌人", singular: "敌人", file: "enemies.json", root: "enemies" },
  relics: { label: "法器", singular: "法器", file: "relics.json", root: "relics" },
  pills: { label: "丹药 / 道具", singular: "丹药", file: "pills.json", root: "pills" },
  events: { label: "奇遇事件", singular: "事件", file: "events.json", root: "events" },
  story: { label: "剧情章节", singular: "剧情章节", file: "story.json", root: "acts" },
};

const actionCatalog = [
  ["damage", "对目标敌人造成伤害"], ["damage_all", "对所有敌人造成伤害"],
  ["damage_random", "随机敌人多段伤害"], ["self_damage", "自己失去气血（默认不致死）"],
  ["block", "获得护体罡气"], ["heal", "恢复气血"], ["draw", "抽牌"],
  ["discover_draw", "从抽牌堆随机展示N张，选择1张加入手牌"],
  ["discard_random", "随机弃牌"], ["discard_hand", "弃掉全部非保留手牌"],
  ["gain_spirit", "获得灵力"], ["lose_spirit", "失去灵力"],
  ["spirit_next_turn", "下回合获得灵力"], ["apply_status", "施加状态"],
  ["remove_status", "移除状态层数"], ["set_status", "把状态设为指定层数"],
  ["apply_temp_strength", "获得本回合力量"], ["gain_gold", "获得灵石"],
  ["gain_max_hp", "提高最大气血"], ["cleanse_toxicity", "清除丹毒"],
  ["create_card", "生成指定卡牌到手牌"], ["add_card_to_draw", "生成卡牌到抽牌堆"],
  ["add_card_to_discard", "生成卡牌到弃牌堆"], ["power", "注册整场战斗功法"],
  ["damage_per_block", "伤害随自身罡气提高"], ["damage_per_status", "伤害随状态层数提高"],
  ["damage_all_per_status", "群体伤害随状态提高"], ["block_per_status", "罡气随状态提高"],
  ["amplify_status", "倍增状态"], ["enhance_one_sword", "强化一剑"],
  ["one_sword_strike", "结算一剑"], ["one_sword_multiply", "一剑额外触发"],
  ["one_sword_free", "一剑费用归零"], ["next_zhaoshi_cost_reduce", "下一张招式减费"],
  ["wan_jian_damage", "万剑归宗动态伤害（兼容旧内容）"],
  ["damage_all_per_repeat", "按计数器重复群伤"],
  ["damage_all_per_basic_gongfa", "按本场基础/功法牌次数群伤"],
  ["cost_free_basic_gongfa", "本回合基础/功法牌免费"],
  ["defense_to_strength", "按本回合罡气获得情况获取力量"], ["none", "无主动效果"],
] as const;

const targets = ["enemy", "self", "all_enemies", "random_enemy"];
const statuses = ["strength", "dexterity", "weak", "vulnerable", "burn", "poison", "nightmare", "temp_strength"];
const triggers = ["none", "combat_start", "turn_start", "on_card_played", "on_player_turn_end", "on_damage_dealt", "on_hp_changed", "on_cards_discarded", "on_kill", "on_reshuffle", "on_victory", "on_loot", "pill_modifier"];
const conditions = ["", "always", "unused_hand", "first_damage_halve", "per_discarded_card", "card_type_filter", "hp_missing_5pct", "heal_on_damage", "status_boost_poison", "status_boost_burn", "self_hp_below:50", "self_hp_above:50", "self_has_status:strength", "self_missing_status:poison", "target_has_status:poison", "target_missing_status:weak", "counter_at_least:3", "hand_size_at_least:5", "draw_pile_at_most:3", "discard_pile_at_least:5"];
const scaleSources = ["", "counter", "self_block", "missing_hp", "hand_size", "draw_pile", "discard_pile", "cards_played_this_turn", "basic_gongfa_played", "self_status:strength", "target_status:poison"];
const counterConditions = ["", "on_basic_play", "on_basic_gongfa_play", "on_any_card_play", "on_same_type_play", "on_sword_play", "on_spell_play", "on_damage_dealt", "on_draw", "on_turn_start"];
const statusActions = ["apply_status", "remove_status", "set_status", "apply_temp_strength", "damage_per_status", "damage_all_per_status", "block_per_status", "amplify_status", "power"];
const paramActions = ["create_card", "add_card_to_draw", "add_card_to_discard"];
const visualAnimations = [["none", "无专用表现"], ["slash", "剑光斩击"], ["fireball", "火球投射 + 爆炸"], ["impact", "冲击爆裂"], ["block", "罡气护盾"], ["heal", "回春灵光"], ["draw", "抽牌提示"]];
const visualSounds = [["none", "无声效"], ["sword_slash", "剑刃破空"], ["fireball", "火球爆燃"], ["block", "罡气共鸣"], ["heal", "回春铃音"], ["draw", "抽牌提示"]];

const commonFields: Record<CategoryKey, Array<[string, string, "text" | "number" | "textarea" | "boolean" | "select", string[]?]>> = {
  cards: [["id", "ID", "text"], ["name", "名称", "text"], ["type", "牌类", "select", ["basic", "spell", "sword", "body", "talisman", "skill", "gongfa", "curse"]], ["class", "职业", "select", ["", "sword", "danxiu", "fuxiu"]], ["rarity", "稀有度", "select", ["common", "uncommon", "rare"]], ["cost", "费用", "number"], ["description", "卡面描述", "textarea"], ["flavor", "风味文字", "textarea"], ["art", "图片路径", "text"], ["retain", "保留", "boolean"], ["exhaust", "消耗", "boolean"], ["counter_condition", "计数条件", "text"]],
  enemies: [["id", "ID", "text"], ["name", "名称", "text"], ["tier", "阶级", "select", ["normal", "elite", "boss"]], ["hp", "最大气血", "number"], ["story", "背景故事", "textarea"], ["ability_desc", "机制描述", "textarea"], ["art", "图片路径", "text"]],
  relics: [["id", "ID", "text"], ["name", "名称", "text"], ["rarity", "稀有度", "select", ["common", "uncommon", "rare"]], ["description", "描述", "textarea"], ["trigger", "触发器", "select", triggers], ["condition", "触发条件", "text"], ["counter", "计数阈值", "number"], ["modifier", "修正系数", "number"], ["art", "图片路径", "text"]],
  pills: [["id", "ID", "text"], ["name", "名称", "text"], ["description", "描述", "textarea"], ["toxicity", "丹毒", "number"], ["art", "图片路径", "text"]],
  events: [["id", "ID", "text"], ["title", "标题", "text"], ["text", "正文", "textarea"]],
  story: [["act_id", "章节 ID", "text"], ["title", "标题", "text"], ["start_beat", "起始节点 ID", "text"]],
};

function templateFor(kind: CategoryKey): Entity {
  const stamp = Date.now().toString().slice(-6);
  if (kind === "cards") return { id: `new_card_${stamp}`, name: "新卡牌", type: "skill", rarity: "common", cost: 1, description: "造成 {effect0} 点伤害", flavor: "", retain: false, exhaust: false, effects: [{ action: "damage", target: "enemy", value: 6, times: 1 }], visual: { animation: "slash", sound: "sword_slash", accent: "#EAF7FF", duration: 0.42, intensity: 4, count: 1 }, upgrade: { description: "造成 {effect0} 点伤害", effects: [{ action: "damage", target: "enemy", value: 9, times: 1 }] }, art: "Art/cards/strike.png" };
  if (kind === "enemies") return { id: `new_enemy_${stamp}`, name: "新敌人", tier: "normal", hp: 30, story: "", abilities: [], ability_desc: "", intents: [{ action: "attack", value: 6, weight: 3 }, { action: "defend", value: 5, weight: 1 }], art: "Art/enemies/mountain_imp.png" };
  if (kind === "relics") return { id: `new_relic_${stamp}`, name: "新法器", rarity: "common", description: "每回合开始时获得3点护体罡气", trigger: "turn_start", condition: "", counter: 0, effect: { action: "block", target: "self", value: 3 }, art: "Art/relics/soul_jade.png" };
  if (kind === "pills") return { id: `new_pill_${stamp}`, name: "新丹药", description: "恢复8点气血", toxicity: 1, effect: { action: "heal", target: "self", value: 8 } };
  if (kind === "events") return { id: `new_event_${stamp}`, title: "新奇遇", text: "你在山路上发现了一处异象。", choices: [{ text: "上前查看", effects: [{ action: "gold", value: 10 }], result: "你获得了10枚灵石。" }] };
  return { act_id: `new_story_${stamp}`, title: "新剧情章节", start_beat: `new_beat_${stamp}`, beats: [{ id: `new_beat_${stamp}`, narrator_text: "新的故事由此开始。", choices: [] }] };
}

function entityId(entity: Entity) { return entity.id || entity.act_id || ""; }
function displayName(entity: Entity) { return entity.name || entity.title || entityId(entity) || "未命名"; }
function llmKind(kind: CategoryKey) { return ({ cards: "card", enemies: "enemy", relics: "relic", pills: "pill", events: "event", story: "story" } as const)[kind]; }
function deepClone<T>(value: T): T { return JSON.parse(JSON.stringify(value)); }

function validCondition(value: string) {
  if (!value || value === "always") return true;
  const numeric = /^(self_hp_below|self_hp_above|counter_at_least|hand_size_at_least|draw_pile_at_most|discard_pile_at_least):\d+$/;
  const status = /^(self_has_status|self_missing_status|target_has_status|target_missing_status):([a-z_]+)$/;
  const statusMatch = value.match(status);
  return numeric.test(value) || !!(statusMatch && statuses.includes(statusMatch[2]));
}

function validScaleSource(value: string) {
  if (!value) return true;
  if (["counter", "self_block", "missing_hp", "hand_size", "draw_pile", "discard_pile", "cards_played_this_turn", "basic_gongfa_played"].includes(value)) return true;
  const match = value.match(/^(self_status|target_status):([a-z_]+)$/);
  return !!(match && statuses.includes(match[2]));
}

function validateEffect(effect: Entity, cardIds: Set<string>, label: string) {
  const issues: string[] = [];
  if (!actionCatalog.some(([id]) => id === effect.action)) issues.push(`${label}：未知动作 ${effect.action || "(空)"}`);
  if (effect.target && !targets.includes(effect.target)) issues.push(`${label}：未知目标 ${effect.target}`);
  if (effect.chance != null && (effect.chance < 0 || effect.chance > 1)) issues.push(`${label}：chance 必须在 0~1`);
  if (!validCondition(effect.condition || "")) issues.push(`${label}：condition 表达式未被引擎支持`);
  if (!validScaleSource(effect.scale_by || "")) issues.push(`${label}：scale_by 来源未被引擎支持`);
  if (effect.scale_by && (!(effect.scale_factor > 0) || !(effect.scale_divisor >= 1))) issues.push(`${label}：动态倍率需要 scale_factor>0 且 scale_divisor≥1`);
  if (statusActions.includes(effect.action) && !effect.status) issues.push(`${label}：${effect.action} 必须填写状态/功法ID`);
  if (effect.status && effect.action !== "power" && !statuses.includes(effect.status)) issues.push(`${label}：未知状态 ${effect.status}`);
  if (paramActions.includes(effect.action) && !effect.param) issues.push(`${label}：${effect.action} 必须填写卡牌ID参数`);
  if (paramActions.includes(effect.action) && effect.param && cardIds.size > 0 && !cardIds.has(effect.param)) issues.push(`${label}：引用的卡牌ID ${effect.param} 不存在`);
  if ((effect.times ?? 1) < 1) issues.push(`${label}：times 必须至少为1`);
  return issues;
}

function validateEntity(kind: CategoryKey, entity: Entity, peers: Entity[]) {
  const issues: string[] = [];
  if (!entity || typeof entity !== "object") return ["内容不是 JSON 对象"];
  const id = entityId(entity);
  if (!id) issues.push(kind === "story" ? "缺少 act_id" : "缺少 id");
  if (id && !/^[a-z0-9_]+$/.test(id)) issues.push("ID 只能使用小写字母、数字和下划线");
  if (id && peers.filter((item) => entityId(item) === id).length > 1) issues.push("ID 重复");
  if (kind !== "events" && kind !== "story" && !entity.name) issues.push("缺少 name");
  const cardIds = new Set(kind === "cards" ? peers.map(entityId).filter(Boolean) : []);
  const effects: Entity[] = kind === "cards" ? [...(entity.effects || []), ...(entity.upgrade?.effects || [])] : kind === "relics" || kind === "pills" ? (entity.effect ? [entity.effect] : []) : [];
  effects.forEach((effect, index) => issues.push(...validateEffect(effect, cardIds, `效果${index + 1}`)));
  if (kind === "cards" && !counterConditions.includes(entity.counter_condition || "")) issues.push("计数条件未被战斗引擎支持");
  if (kind === "cards" && !counterConditions.includes(entity.upgrade?.counter_condition || "")) issues.push("升级计数条件未被战斗引擎支持");
  if (kind === "enemies") {
    for (const intent of entity.intents || []) if (!intent.action) issues.push("存在未填写 action 的敌人意图");
  }
  if (kind === "cards" && entity.visual) {
    if (!visualAnimations.some(([id]) => id === entity.visual.animation)) issues.push(`未知动画: ${entity.visual.animation}`);
    if (!visualSounds.some(([id]) => id === entity.visual.sound)) issues.push(`未知音效: ${entity.visual.sound}`);
    if (entity.visual.duration != null && entity.visual.duration <= 0) issues.push("动画时长必须大于0");
    if (entity.visual.intensity != null && entity.visual.intensity < 0) issues.push("震动强度不能为负数");
  }
  return [...new Set(issues)];
}

async function nestedDirectory(root: any, parts: string[]) {
  let current = root;
  for (const part of parts) current = await current.getDirectoryHandle(part);
  return current;
}

async function locateDataDirectory(root: any) {
  try { return await nestedDirectory(root, ["Content", "Data"]); } catch { /* selected Data directly */ }
  try {
    await root.getFileHandle("cards.json");
    return root;
  } catch { throw new Error("请选择 AscendSpire 工程根目录，或 Content/Data 目录"); }
}

async function readDocument(dataDir: any, kind: CategoryKey): Promise<DataDocument> {
  const config = categories[kind];
  const handle = await dataDir.getFileHandle(config.file);
  const text = await (await handle.getFile()).text();
  const raw = JSON.parse(text);
  const items = Array.isArray(raw[config.root]) ? raw[config.root] : [];
  return { raw, items };
}

function EffectEditor({ value, onChange, onRemove }: { value: Entity; onChange: (next: Entity) => void; onRemove: () => void }) {
  const set = (key: string, next: any) => onChange({ ...value, [key]: next });
  return <div className="effect-card">
    <div className="effect-head">
      <select value={value.action || ""} onChange={(e) => set("action", e.target.value)} aria-label="效果动作">
        <option value="">选择动作</option>{actionCatalog.map(([id, label]) => <option value={id} key={id}>{id} · {label}</option>)}
      </select>
      <button className="icon-button danger" onClick={onRemove} title="删除效果">删除</button>
    </div>
    <div className="effect-grid">
      <label>目标<select value={value.target || "self"} onChange={(e) => set("target", e.target.value)}>{targets.map((item) => <option key={item}>{item}</option>)}</select></label>
      <label>基础数值<input type="number" value={value.value ?? 0} onChange={(e) => set("value", Number(e.target.value))} /></label>
      <label>次数<input type="number" min="1" value={value.times ?? 1} onChange={(e) => set("times", Number(e.target.value))} /></label>
      <label>状态<input list="status-options" value={value.status || ""} onChange={(e) => set("status", e.target.value)} /></label>
      <label>层数<input type="number" value={value.stacks ?? 0} onChange={(e) => set("stacks", Number(e.target.value))} /></label>
      <label>参数 / 卡牌ID<input value={value.param || ""} onChange={(e) => set("param", e.target.value)} /></label>
      <label>执行条件<input list="condition-options" value={value.condition || ""} onChange={(e) => set("condition", e.target.value)} /></label>
      <label>概率<input type="number" min="0" max="1" step="0.05" value={value.chance ?? 1} onChange={(e) => set("chance", Number(e.target.value))} /></label>
      <label>变量来源<input list="scale-options" value={value.scale_by || ""} onChange={(e) => set("scale_by", e.target.value)} /></label>
      <label>每层倍率<input type="number" value={value.scale_factor ?? 0} onChange={(e) => set("scale_factor", Number(e.target.value))} /></label>
      <label>换算除数<input type="number" min="1" value={value.scale_divisor ?? 1} onChange={(e) => set("scale_divisor", Number(e.target.value))} /></label>
    </div>
    <p className="field-help">条件决定“是否触发”；变量来源决定“触发时数值如何成长”。计数玩法请同时设置卡牌的计数事件，并在这里使用 <code>counter_at_least:N</code> 或 <code>scale_by=counter</code>。</p>
  </div>;
}

function EffectsSection({ title, effects, onChange }: { title: string; effects: Entity[]; onChange: (next: Entity[]) => void }) {
  return <section className="editor-section">
    <div className="section-heading"><div><span className="eyebrow">EFFECT STACK</span><h3>{title}</h3></div><button onClick={() => onChange([...effects, { action: "damage", target: "enemy", value: 5, times: 1 }])}>＋ 添加效果</button></div>
    {effects.length === 0 ? <p className="empty-note">尚无效果</p> : effects.map((effect, index) => <EffectEditor key={index} value={effect} onChange={(next) => { const copy = [...effects]; copy[index] = next; onChange(copy); }} onRemove={() => onChange(effects.filter((_, i) => i !== index))} />)}
  </section>;
}

function VisualEditor({ value, onChange }: { value: Entity; onChange: (next: Entity) => void }) {
  const set = (key: string, next: any) => onChange({ animation: "none", sound: "none", accent: "#FFFFFF", duration: 0.42, intensity: 4, count: 1, ...value, [key]: next });
  const animation = value.animation || "none";
  return <section className="editor-section visual-section">
    <div className="section-heading"><div><span className="eyebrow">PRESENTATION</span><h3>动画与音效</h3></div><span className="visual-chip">{animation}</span></div>
    <div className="field-grid">
      <label>生效动画<select value={animation} onChange={(e) => set("animation", e.target.value)}>{visualAnimations.map(([id, label]) => <option key={id} value={id}>{id} · {label}</option>)}</select></label>
      <label>音效<select value={value.sound || "none"} onChange={(e) => set("sound", e.target.value)}>{visualSounds.map(([id, label]) => <option key={id} value={id}>{id} · {label}</option>)}</select></label>
      <label>强调色<div className="color-field"><input type="color" value={/^#[0-9A-Fa-f]{6}$/.test(value.accent || "") ? value.accent : "#FFFFFF"} onChange={(e) => set("accent", e.target.value.toUpperCase())} /><input value={value.accent || "#FFFFFF"} onChange={(e) => set("accent", e.target.value)} /></div></label>
      <label>时长（秒）<input type="number" min="0.1" max="3" step="0.05" value={value.duration ?? 0.42} onChange={(e) => set("duration", Number(e.target.value))} /></label>
      <label>震动强度<input type="number" min="0" max="30" step="1" value={value.intensity ?? 4} onChange={(e) => set("intensity", Number(e.target.value))} /></label>
      <label>重复视觉次数<input type="number" min="1" max="8" step="1" value={value.count ?? 1} onChange={(e) => set("count", Number(e.target.value))} /></label>
    </div>
    <p className="field-help">动画只负责表现，伤害和状态仍由下方效果栈结算；未知动画会安全回退，不影响卡牌逻辑。</p>
  </section>;
}

function ArtPreview({ root, path }: { root: any; path?: string }) {
  const [url, setUrl] = useState("");
  useEffect(() => {
    let active = true; let objectUrl = "";
    (async () => {
      if (!root || !path) return setUrl("");
      try {
        const clean = path.replace(/^Content\//, "");
        const parts = clean.split("/");
        const fileName = parts.pop()!;
        const content = await nestedDirectory(root, path.startsWith("Content/") ? ["Content", ...parts] : ["Content", ...parts]);
        const file = await (await content.getFileHandle(fileName)).getFile();
        objectUrl = URL.createObjectURL(file);
        if (active) setUrl(objectUrl);
      } catch { if (active) setUrl(""); }
    })();
    return () => { active = false; if (objectUrl) URL.revokeObjectURL(objectUrl); };
  }, [root, path]);
  return url ? <img src={url} alt="内容图片预览" /> : <div className="art-fallback"><span>墨</span><small>{path || "未设置图片"}</small></div>;
}

function EntityPreview({ kind, entity, projectRoot }: { kind: CategoryKey; entity: Entity; projectRoot: any }) {
  const isCard = kind === "cards";
  const description = entity.description || entity.ability_desc || entity.text || "暂无描述";
  return <div className={`entity-preview preview-${kind}`}>
    <div className="preview-art"><ArtPreview root={projectRoot} path={entity.art} /><span className="rarity-chip">{entity.rarity || entity.tier || categories[kind].singular}</span></div>
    <div className="preview-title"><h2>{displayName(entity)}</h2>{isCard && <strong>{entity.cost ?? 0}</strong>}</div>
    <p>{description}</p>
    {isCard && entity.visual && entity.visual.animation && entity.visual.animation !== "none" && <span className="preview-visual-tag">✦ {entity.visual.animation} · {entity.visual.sound || "none"}</span>}
    {isCard && <div className="preview-flags">{entity.retain && <span>保留</span>}{entity.exhaust && <span>消耗</span>}<span>{entity.type || "未分类"}</span></div>}
    <code>{entityId(entity) || "missing_id"}</code>
  </div>;
}

export default function Home() {
  const [documents, setDocuments] = useState<Partial<Record<CategoryKey, DataDocument>>>({});
  const [kind, setKind] = useState<CategoryKey>("cards");
  const [selected, setSelected] = useState(0);
  const [query, setQuery] = useState("");
  const [projectRoot, setProjectRoot] = useState<any>(null);
  const [dataDirectory, setDataDirectory] = useState<any>(null);
  const [connectionName, setConnectionName] = useState("未连接工程");
  const [notice, setNotice] = useState("选择工程目录后即可读取和保存真实数据");
  const [rawDraft, setRawDraft] = useState("");
  const [rawError, setRawError] = useState("");
  const [llmDraft, setLlmDraft] = useState("");
  const [llmValidationIssues, setLlmValidationIssues] = useState<string[]>([]);
  const [showJson, setShowJson] = useState(false);
  const [showProtocol, setShowProtocol] = useState(false);
  const uploadRef = useRef<HTMLInputElement>(null);

  const doc = documents[kind];
  const items = doc?.items || [];
  const filtered = useMemo(() => items.map((entity, index) => ({ entity, index })).filter(({ entity }) => `${displayName(entity)} ${entityId(entity)} ${entity.description || ""}`.toLowerCase().includes(query.toLowerCase())), [items, query]);
  const current = items[selected] || null;
  const issues = current ? validateEntity(kind, current, items) : [];

  useEffect(() => { setRawDraft(current ? JSON.stringify(current, null, 2) : ""); setRawError(""); }, [current, kind]);
  useEffect(() => { setSelected(0); }, [kind]);

  const replaceItems = (nextItems: Entity[]) => setDocuments((previous) => {
    const previousDoc = previous[kind] || { raw: { [categories[kind].root]: [] }, items: [] };
    return { ...previous, [kind]: { raw: { ...previousDoc.raw, [categories[kind].root]: nextItems }, items: nextItems } };
  });
  const updateCurrent = (next: Entity) => { const copy = [...items]; copy[selected] = next; replaceItems(copy); };
  const setField = (field: string, value: any) => current && updateCurrent({ ...current, [field]: value });

  const connectProject = async () => {
    try {
      const picker = (window as any).showDirectoryPicker;
      if (!picker) throw new Error("当前浏览器不支持目录写入，请使用下方“导入 JSON 文件”模式");
      const root = await picker({ mode: "readwrite" });
      const dataDir = await locateDataDirectory(root);
      const loaded: Partial<Record<CategoryKey, DataDocument>> = {};
      for (const key of Object.keys(categories) as CategoryKey[]) {
        try { loaded[key] = await readDocument(dataDir, key); } catch { /* optional file */ }
      }
      setProjectRoot(dataDir === root ? null : root); setDataDirectory(dataDir); setDocuments(loaded); setConnectionName(root.name || "AscendSpire"); setNotice("已连接真实工程；保存会写回 Content/Data");
    } catch (error: any) { setNotice(error.message || "无法打开工程目录"); }
  };

  const saveKind = async (targetKind = kind) => {
    const targetDoc = documents[targetKind]; if (!targetDoc) return;
    if (!dataDirectory) return downloadKind(targetKind);
    try {
      const config = categories[targetKind];
      const fileHandle = await dataDirectory.getFileHandle(config.file, { create: true });
      const writable = await fileHandle.createWritable();
      await writable.write(JSON.stringify({ ...targetDoc.raw, [config.root]: targetDoc.items }, null, 2) + "\n");
      await writable.close(); setNotice(`已保存 ${config.file}`);
    } catch (error: any) { setNotice(`保存失败：${error.message}`); }
  };

  const saveAll = async () => {
    for (const key of Object.keys(documents) as CategoryKey[]) await saveKind(key);
    setNotice("所有已载入数据文件均已保存");
  };

  const downloadKind = (targetKind = kind) => {
    const targetDoc = documents[targetKind]; if (!targetDoc) return;
    const config = categories[targetKind];
    const blob = new Blob([JSON.stringify({ ...targetDoc.raw, [config.root]: targetDoc.items }, null, 2) + "\n"], { type: "application/json" });
    const url = URL.createObjectURL(blob); const link = document.createElement("a"); link.href = url; link.download = config.file; link.click(); URL.revokeObjectURL(url); setNotice(`已导出 ${config.file}`);
  };

  const importFiles = async (files: FileList | null) => {
    if (!files) return;
    const loaded = { ...documents };
    for (const file of Array.from(files)) {
      const target = (Object.keys(categories) as CategoryKey[]).find((key) => categories[key].file === file.name);
      if (!target) continue;
      try { const raw = JSON.parse(await file.text()); loaded[target] = { raw, items: raw[categories[target].root] || [] }; } catch { setNotice(`${file.name} 不是有效 JSON`); }
    }
    setDocuments(loaded); setNotice("JSON 已载入；当前为导入/导出模式，不会直接覆盖工程");
  };

  const applyRaw = () => {
    try { const next = JSON.parse(rawDraft); updateCurrent(next); setRawError(""); setNotice("原始 JSON 已应用到当前条目"); } catch (error: any) { setRawError(error.message); }
  };

  const importLlmJson = () => {
    try {
      const parsed = JSON.parse(llmDraft);
      let targetKind = kind; let payload: any = parsed;
      const kindMap: Record<string, CategoryKey> = { card: "cards", enemy: "enemies", relic: "relics", pill: "pills", item: "pills", event: "events", story: "story" };
      if (parsed.kind && kindMap[parsed.kind]) { targetKind = kindMap[parsed.kind]; payload = parsed.data ?? parsed.content ?? parsed; }
      const config = categories[targetKind];
      const incoming = Array.isArray(payload) ? payload : Array.isArray(payload[config.root]) ? payload[config.root] : [payload];
      const targetDoc = documents[targetKind] || { raw: { [config.root]: [] }, items: [] };
      const normalized = incoming.map((entity: Entity) => entity.kind ? (entity.data || entity.content || entity) : entity);
      const incomingIds = new Set(normalized.map(entityId).filter(Boolean));
      const validationPeers = [...targetDoc.items.filter((item) => !incomingIds.has(entityId(item))), ...normalized];
      const validationIssues = normalized.flatMap((entity: Entity, index: number) =>
        validateEntity(targetKind, entity, validationPeers).map((issue) => `第${index + 1}项：${issue}`));
      if (validationIssues.length > 0) {
        setLlmValidationIssues([...new Set(validationIssues)]);
        setNotice(`LLM 内容有 ${validationIssues.length} 个登记问题，尚未导入`);
        return;
      }
      const nextItems = [...targetDoc.items];
      for (const clean of normalized) {
        const cleanId = entityId(clean);
        const existing = nextItems.findIndex((item) => cleanId && entityId(item) === cleanId);
        if (existing >= 0) nextItems[existing] = clean; else nextItems.push(clean);
      }
      setDocuments((previous) => ({ ...previous, [targetKind]: { raw: { ...targetDoc.raw, [config.root]: nextItems }, items: nextItems } }));
      setKind(targetKind); setSelected(Math.max(0, nextItems.length - 1)); setLlmDraft(""); setLlmValidationIssues([]); setNotice(`已导入 ${incoming.length} 个${config.singular}条目`);
    } catch (error: any) { setNotice(`LLM JSON 导入失败：${error.message}`); }
  };

  const copyProtocol = async () => {
    const sample = templateFor(kind);
    const text = `你是《登仙路》的内容设计器。保留剧情主题，先在内部检查字段、引用、强度和卡面说明，只输出合法 JSON，不要 Markdown。\n格式：\n${JSON.stringify({ kind: llmKind(kind), data: sample }, null, 2)}\n可用 action：${actionCatalog.map(([id]) => id).join(", ")}\n可用 target：${targets.join(", ")}\n可用 status：${statuses.join(", ")}\n卡牌 counter_condition：${counterConditions.filter(Boolean).join(", ")}\ncondition 语法：self_hp_below:N | self_hp_above:N | self_has_status:S | self_missing_status:S | target_has_status:S | target_missing_status:S | counter_at_least:N | hand_size_at_least:N | draw_pile_at_most:N | discard_pile_at_least:N\nscale_by：counter | self_block | missing_hp | hand_size | draw_pile | discard_pile | cards_played_this_turn | basic_gongfa_played | self_status:S | target_status:S。使用时同时写 scale_factor>0 与 scale_divisor>=1。\ndescription 可使用 {effect0}、{effect1}、{counter}、{stacks:strength}、{hand_size}、{draw_pile}、{discard_pile}；不要用 chance 假装确定条件。卡牌 visual 可用 animation=${visualAnimations.map(([id]) => id).join("|")}，sound=${visualSounds.map(([id]) => id).join("|")}。`;
    await navigator.clipboard.writeText(text); setNotice("LLM 生成协议已复制");
  };

  const copyRepairPrompt = async () => {
    const text = `下面的《登仙路》JSON未通过登记校验：\n${llmValidationIssues.join("\n")}\n保持名称、主题、品阶、费用和核心玩法不变，只修复非法字段、引用或表达式，并同步修正description。只输出修订后的完整JSON，不要Markdown。\n\n原JSON：\n${llmDraft}`;
    await navigator.clipboard.writeText(text); setNotice("定向修订提示词已复制");
  };

  const addEntity = () => { const next = [...items, templateFor(kind)]; replaceItems(next); setSelected(next.length - 1); setNotice(`已创建${categories[kind].singular}`); };
  const duplicateEntity = () => { if (!current) return; const copy = deepClone(current); const idField = kind === "story" ? "act_id" : "id"; copy[idField] = `${entityId(copy) || "copy"}_copy`; if (copy.name) copy.name = `${displayName(copy)}·副本`; else copy.title = `${displayName(copy)}·副本`; const next = [...items, copy]; replaceItems(next); setSelected(next.length - 1); };
  const deleteEntity = () => { if (!current || !window.confirm(`确定删除“${displayName(current)}”？`)) return; const next = items.filter((_, index) => index !== selected); replaceItems(next); setSelected(Math.max(0, selected - 1)); };

  const totals = Object.values(documents).reduce((sum, value) => sum + (value?.items.length || 0), 0);

  return <main className="workbench-shell">
    <datalist id="status-options">{statuses.map((item) => <option key={item} value={item} />)}</datalist>
    <datalist id="condition-options">{conditions.map((item) => <option key={item} value={item} />)}</datalist>
    <datalist id="scale-options">{scaleSources.map((item) => <option key={item} value={item} />)}</datalist>

    <header className="topbar">
      <div className="brand-mark"><span>登</span></div>
      <div className="brand-copy"><span className="eyebrow">ASCEND SPIRE · CONTENT OS</span><h1>万象图鉴</h1><p>数据驱动内容工作台</p></div>
      <div className="connection-pill"><i className={dataDirectory ? "online" : ""} /><div><strong>{connectionName}</strong><span>{notice}</span></div></div>
      <div className="top-actions">
        <button className="ghost" onClick={() => uploadRef.current?.click()}>导入 JSON 文件</button>
        <input ref={uploadRef} hidden multiple type="file" accept="application/json,.json" onChange={(e) => importFiles(e.target.files)} />
        <button className="ghost" onClick={connectProject}>选择工程目录</button>
        <button className="primary" onClick={saveAll}>保存全部</button>
      </div>
    </header>

    <nav className="category-rail">
      {(Object.keys(categories) as CategoryKey[]).map((key) => <button key={key} className={kind === key ? "active" : ""} onClick={() => setKind(key)}><span>{categories[key].label}</span><b>{documents[key]?.items.length ?? 0}</b></button>)}
      <div className="rail-stat"><span>已载入</span><strong>{totals}</strong><small>个内容条目</small></div>
    </nav>

    <aside className="library-panel">
      <div className="panel-title"><div><span className="eyebrow">CATALOG</span><h2>{categories[kind].label}图鉴</h2></div><button className="square" onClick={addEntity}>＋</button></div>
      <label className="search-box"><span>⌕</span><input value={query} onChange={(e) => setQuery(e.target.value)} placeholder="搜索名称、ID、描述…" /></label>
      <div className="library-list">
        {filtered.map(({ entity, index }) => <button key={`${entityId(entity)}-${index}`} className={selected === index ? "active" : ""} onClick={() => setSelected(index)}>
          <span className="list-seal">{displayName(entity).slice(0, 1)}</span><span><strong>{displayName(entity)}</strong><code>{entityId(entity) || "missing_id"}</code></span>{validateEntity(kind, entity, items).length > 0 && <em>!</em>}
        </button>)}
        {filtered.length === 0 && <div className="empty-state"><strong>尚无内容</strong><p>连接工程或新建一个条目。</p></div>}
      </div>
    </aside>

    <section className="workspace-panel">
      {!current ? <div className="welcome-panel"><div className="mountain" /><span className="eyebrow">SCHEMA-DRIVEN CREATION</span><h2>从一个 JSON，生出一方世界</h2><p>选择 AscendSpire 工程目录即可载入全部图鉴；也可以直接粘贴 LLM 生成的结构化 JSON。</p><div><button className="primary" onClick={connectProject}>连接工程</button><button className="ghost" onClick={() => setShowProtocol(true)}>查看 LLM 协议</button></div></div> : <>
        <div className="workspace-head"><div><span className="eyebrow">{categories[kind].singular.toUpperCase()} RECORD</span><h2>{displayName(current)}</h2><code>{entityId(current)}</code></div><div className="record-actions"><button onClick={duplicateEntity}>复制</button><button onClick={() => setShowJson(!showJson)}>{showJson ? "关闭源码" : "JSON 源码"}</button><button className="danger" onClick={deleteEntity}>删除</button><button className="primary" onClick={() => saveKind()}>保存文件</button></div></div>
        {issues.length > 0 && <div className="issue-banner"><strong>需要检查</strong>{issues.map((issue) => <span key={issue}>{issue}</span>)}</div>}
        <div className="editor-grid-main">
          <div className="form-column">
            <section className="editor-section"><div className="section-heading"><div><span className="eyebrow">IDENTITY</span><h3>基础信息</h3></div></div><div className="field-grid">
              {commonFields[kind].map(([field, label, fieldType, options]) => <label key={field} className={fieldType === "textarea" ? "wide" : ""}>{label}
                {field === "counter_condition" ? <select value={current[field] ?? ""} onChange={(e) => setField(field, e.target.value)}>{counterConditions.map((item) => <option key={item} value={item}>{item || "不使用计数器"}</option>)}</select> : fieldType === "textarea" ? <textarea value={current[field] ?? ""} onChange={(e) => setField(field, e.target.value)} /> : fieldType === "boolean" ? <button className={`toggle ${current[field] ? "on" : ""}`} onClick={() => setField(field, !current[field])}><i />{current[field] ? "是" : "否"}</button> : fieldType === "select" ? <select value={current[field] ?? ""} onChange={(e) => setField(field, e.target.value)}>{options?.map((item) => <option key={item} value={item}>{item || "通用"}</option>)}</select> : <input type={fieldType} value={current[field] ?? (fieldType === "number" ? 0 : "")} onChange={(e) => setField(field, fieldType === "number" ? Number(e.target.value) : e.target.value)} />}
              </label>)}
            </div></section>

            {kind === "cards" && <><VisualEditor value={current.visual || {}} onChange={(next) => setField("visual", next)} /><EffectsSection title="基础效果" effects={current.effects || []} onChange={(next) => setField("effects", next)} /><section className="editor-section"><div className="section-heading"><div><span className="eyebrow">UPGRADE</span><h3>升级形态</h3></div></div><div className="field-grid"><label>升级费用<input type="number" value={current.upgrade?.cost ?? -1} onChange={(e) => setField("upgrade", { ...(current.upgrade || {}), cost: Number(e.target.value) })} /></label><label className="wide">升级描述<textarea value={current.upgrade?.description || ""} onChange={(e) => setField("upgrade", { ...(current.upgrade || {}), description: e.target.value })} /></label><label>升级计数条件<select value={current.upgrade?.counter_condition || ""} onChange={(e) => setField("upgrade", { ...(current.upgrade || {}), counter_condition: e.target.value })}>{counterConditions.map((item) => <option key={item} value={item}>{item || "沿用/不使用计数器"}</option>)}</select></label></div></section><EffectsSection title="升级效果" effects={current.upgrade?.effects || []} onChange={(next) => setField("upgrade", { ...(current.upgrade || {}), effects: next })} /></>}
            {(kind === "relics" || kind === "pills") && <EffectsSection title="触发效果" effects={current.effect ? [current.effect] : []} onChange={(next) => setField("effect", next[0] || null)} />}
            {kind === "enemies" && <section className="editor-section"><div className="section-heading"><div><span className="eyebrow">BEHAVIOR</span><h3>能力与行动意图</h3></div><button onClick={() => setField("intents", [...(current.intents || []), { action: "attack", value: 5, weight: 1 }])}>＋ 添加意图</button></div><label className="stack-field">特殊能力（每行一个，如 thorns:2）<textarea value={(current.abilities || []).join("\n")} onChange={(e) => setField("abilities", e.target.value.split("\n").map((line) => line.trim()).filter(Boolean))} /></label>{(current.intents || []).map((intent: Entity, index: number) => <div className="intent-row" key={index}><select value={intent.action || "attack"} onChange={(e) => { const list = [...current.intents]; list[index] = { ...intent, action: e.target.value }; setField("intents", list); }}><option>attack</option><option>attack_multi</option><option>defend</option><option>buff</option><option>debuff</option><option>heal_ally</option><option>buff_ally</option></select>{["value", "times", "weight", "stacks"].map((field) => <label key={field}>{field}<input type="number" value={intent[field] ?? (field === "times" || field === "weight" ? 1 : 0)} onChange={(e) => { const list = [...current.intents]; list[index] = { ...intent, [field]: Number(e.target.value) }; setField("intents", list); }} /></label>)}<input placeholder="status" value={intent.status || ""} onChange={(e) => { const list = [...current.intents]; list[index] = { ...intent, status: e.target.value }; setField("intents", list); }} /><button className="danger" onClick={() => setField("intents", current.intents.filter((_: any, i: number) => i !== index))}>删除</button></div>)}</section>}
          </div>
          <aside className="preview-column"><span className="eyebrow">LIVE PREVIEW</span><EntityPreview kind={kind} entity={current} projectRoot={projectRoot} /><div className="variable-card"><h3>动态变量</h3><p><code>{`{effect0}`}</code> 第一个效果的实时数值</p><p><code>{`{counter}`}</code> 当前计数器</p><p><code>{`{stacks:strength}`}</code> 指定状态层数</p><p><code>scale_by</code> 让效果随罡气、状态、牌堆或计数器成长</p></div><button className="protocol-button" onClick={() => setShowProtocol(true)}><span>LLM</span><div><strong>生成协议</strong><small>复制提示词或粘贴 JSON</small></div>→</button></aside>
        </div>
        {showJson && <section className="raw-editor"><div><span className="eyebrow">RAW JSON</span><h3>当前条目源码</h3></div><textarea spellCheck={false} value={rawDraft} onChange={(e) => setRawDraft(e.target.value)} /><footer>{rawError && <span>{rawError}</span>}<button onClick={() => setRawDraft(JSON.stringify(current, null, 2))}>重置</button><button className="primary" onClick={applyRaw}>应用 JSON</button></footer></section>}
      </>}
    </section>

    {showProtocol && <div className="modal-backdrop" onMouseDown={() => setShowProtocol(false)}><section className="protocol-modal" onMouseDown={(e) => e.stopPropagation()}><button className="modal-close" onClick={() => setShowProtocol(false)}>×</button><span className="eyebrow">LLM CONTENT PROTOCOL · V2</span><h2>让模型直接铸造内容</h2><p>粘贴单个实体、实体数组，或带 <code>kind</code> 与 <code>data</code> 的标准包。导入前会按战斗引擎规则严格检查；非法内容不会写入。</p><div className="protocol-actions"><button onClick={copyProtocol}>复制完整设计规则</button><button onClick={() => { setLlmDraft(JSON.stringify({ kind: llmKind(kind), data: templateFor(kind) }, null, 2)); setLlmValidationIssues([]); }}>载入模板</button></div><textarea spellCheck={false} value={llmDraft} onChange={(e) => { setLlmDraft(e.target.value); setLlmValidationIssues([]); }} placeholder='在此粘贴 { "kind": "card", "data": { ... } }' />{llmValidationIssues.length > 0 && <div className="llm-diagnostics"><strong>尚未登记 · 请让 LLM 定向修订</strong>{llmValidationIssues.map((issue) => <span key={issue}>{issue}</span>)}<button onClick={copyRepairPrompt}>复制修订提示词</button></div>}<div className="catalog-strip"><strong>常用动作</strong>{actionCatalog.slice(0, 15).map(([id]) => <code key={id}>{id}</code>)}</div><footer><span>条件、计数器、动态倍率、卡牌引用及动画都会在导入前检查。</span><button className="primary" onClick={importLlmJson}>严格校验并导入</button></footer></section></div>}
  </main>;
}
