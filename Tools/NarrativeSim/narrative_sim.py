#!/usr/bin/env python3
"""Headless writer + MVU replay against the configured OpenAI-compatible API.

The tool reads the live save and prompt assets, but never writes the save or launches
Unreal. Full samples are written under Saved/Diagnostics/NarrativeSim; stdout stays
compact so Codex sessions and terminal logs do not balloon.
"""

from __future__ import annotations

import argparse
import configparser
import datetime as dt
import json
import pathlib
import re
import sys
import time
import urllib.error
import urllib.request


PROJECT = pathlib.Path(__file__).resolve().parents[2]
USER_SAVED = pathlib.Path.home() / "Library/Application Support/Epic/AscendSpire/Saved"
CONFIG_PATH = USER_SAVED / "Config/InfiniteNarrative.ini"
SAVE_PATH = USER_SAVED / "SaveGames/run_save.json"
DATA = PROJECT / "Content/Data"
OUTPUT_DIR = PROJECT / "Saved/Diagnostics/NarrativeSim"
KNOWN_CARD_NAMES: set[str] = set()
KNOWN_RELIC_NAMES: set[str] = set()
KNOWN_ENEMY_NAMES: set[str] = set()
OWNED_CARD_NAMES: set[str] = set()
OWNED_RELIC_NAMES: set[str] = set()
AUTHORIZED_RESOURCE_TEXT = ""


def load_json(path: pathlib.Path):
    return json.loads(path.read_text(encoding="utf-8"))


def load_config() -> dict[str, str]:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    parser.read(CONFIG_PATH, encoding="utf-8")
    section = parser["AscendSpire.InfiniteNarrative"]
    return dict(section.items())


def load_save() -> dict:
    return json.loads(SAVE_PATH.read_text(encoding="utf-16"))


def endpoint_from(config: dict[str, str]) -> str:
    endpoint = config.get("Endpoint", "").strip().rstrip("/")
    if not endpoint.startswith(("http://", "https://")):
        use_https = config.get("EndpointUseHttps", "True").lower() in {"1", "true", "yes"}
        endpoint = ("https://" if use_https else "http://") + endpoint
    if not endpoint.lower().endswith("/chat/completions"):
        endpoint += "/chat/completions"
    return endpoint


def parse_model_json(text: str) -> dict:
    clean = text.strip()
    clean = re.sub(r"^```(?:json)?\s*", "", clean, flags=re.I)
    clean = re.sub(r"\s*```$", "", clean)
    start, end = clean.find("{"), clean.rfind("}")
    if start < 0 or end < start:
        raise ValueError("model output has no JSON object")
    return json.loads(clean[start : end + 1])


def call_model(config: dict[str, str], messages: list[dict], temperature: float,
               max_tokens: int) -> tuple[str, float]:
    payload = {
        "model": config.get("Model", "deepseek-v4-flash"),
        "messages": messages,
        "temperature": temperature,
        "top_p": 0.99,
        "frequency_penalty": 0.1 if temperature > 0.5 else 0,
        "presence_penalty": 0.15 if temperature > 0.5 else 0,
        "max_tokens": max_tokens,
        "response_format": {"type": "json_object"},
        "stream": False,
    }
    if payload["model"].lower().startswith("deepseek-v4"):
        payload["thinking"] = {"type": "disabled"}
    request = urllib.request.Request(
        endpoint_from(config),
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={
            "Authorization": "Bearer " + config.get("ApiKey", ""),
            "Content-Type": "application/json",
            "User-Agent": "AscendSpire-NarrativeSim/1.0",
        },
        method="POST",
    )
    started = time.monotonic()
    try:
        with urllib.request.urlopen(request, timeout=180) as response:
            transport = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8", errors="replace")[:600]
        raise RuntimeError(f"HTTP {error.code}: {body}") from error
    content = transport["choices"][0]["message"]["content"]
    return content, time.monotonic() - started


def worldbook_text(worldbook: dict) -> str:
    chunks = [worldbook.get("setting", "")]
    chunks.extend(entry.get("content", "") for entry in worldbook.get("entries", []))
    return "\n\n".join(chunk for chunk in chunks if chunk)


WRITER_CONTRACT = """
只输出JSON对象，不要Markdown：
{"schema_version":"2.0-writer","scene":{"title":"","narration":"","messages":[{"speaker":"","portrait_id":"","expression":"neutral","text":""}]},"state_patch":{},"memory":{"title":"","summary":"","participants":[],"facts":[],"unresolved":[],"keywords":[],"importance":1},"choices":[{"choice_id":"A","text":"","result_summary":"选择执行后立即揭晓的确定结果","consequence_intent":"主体、完成性、持续性与代价","resolved_impact":{"kind":"narrative|acquire_item|lose_item|learn_ability|improve_owned_content|heal|hurt|gain_gold|lose_gold|trade|deck_edit|rest|reward|combat","subject":"player","object":"","completed":true,"persistent":true},"next":"continue_rp|combat","encounter_hint":""},{"choice_id":"B"},{"choice_id":"C"}]}
choices必须正好三项。正文目标600至1200个中文字符。result_summary禁止“若成功”“可能”“试图”。combat只在冲突已经发生且无法由本轮对话取消时使用，并在开战前停住。
A偏激进破局、战斗或复合后果，不能只靠好感或环境充数；B偏搜刮与成长，必须明确取得可玩内容、学习/强化、资源交易、牌组功能或身体修炼之一，可有副作用，单纯情报/好感/警戒/环境或战斗不算B成长；C偏戏剧化、反常规、富想象力且让NPC真实回应，并必须当场结算一个确定的引擎事实；只有提议、邀请、秘密、口头威胁、将来可能或物品可能受损不算结算。上一轮机械结果不是本轮的模仿模板；三项应由各自新行动产生不同的直接引擎后果，不得统一降级成好感、警戒或环境变化。
若真正事件受时辰、路程或等待门槛阻挡，正文直接跳到有事发生的时刻，不得把空档拆成观察、读告示、摸路线、打听、确认或等待等填充轮。调查与交涉可以是手段，但result_summary后半句必须完成一次可编译后果；纯线索、地点、环境和任务进度不算结果。后果不等于奖励，损失、负面状态、敌对反制、战术劣势和战斗同样有效。
边界对照（只学习边界，禁止照抄）：错误A是“斩断弓弦、逼退敌人后被包围”，因为已结算交手；正确A停在“弓已拉满、六人包围、第一击将发”。错误B是“获得敌人人数情报并逃开”，因为没有成长；正确B明确玩家已取得丹药/藏宝图并写清其玩法概念，副作用另写追兵逼近。错误C是“提出荒诞交易但对方未回答”；正确C必须写NPC当场接受或拒绝，以及物品、态度、势力、身体或战斗中的确定变化。
""".strip()


MVU_CONTRACT = """
只输出JSON对象，不要改写scene、text、result_summary或consequence_intent：
{"schema_version":"3.1-engine-state","choices":[{"choice_id":"A","compile_status":"compiled|no_executable_effect","no_effect_reason":"","gm_judgement":"","settlement_key":"","resolved_impact":{"kind":"narrative|acquire_item|lose_item|learn_ability|improve_owned_content|heal|hurt|gain_gold|lose_gold|trade|deck_edit|rest|reward|combat","subject":"player","object":"","completed":true,"persistent":true},"next":"continue_rp|combat","state_patch":{},"variable_updates":[{"domain":"body|item|skill|relationship|environment|faction","target":"","field":"condition|ownership|knowledge|affinity|bond|location|trait|combat_edge|alert","op":"add|set|remove|gain|lose|learn|upgrade|forget","value":"","amount":0,"duration":0,"causality":"direct|incidental","valence":"positive|negative|mixed|neutral","magnitude":"minor|moderate|major"}],"requirements":[],"operations":[],"content_jobs":[{"kind":"card","source_fact":"已完成的获得事实","concept":"物品或能力意象","mechanic_intent":"至少两个题材特征与玩法方向","acquisition":"gain"}],"rewards":{"created_cards":[],"created_relics":[]},"encounter":{"template_name":"","faction_id":"","name":"","story":"","tier":"normal","hp_scale":1.0,"intent_scale":1.0,"abilities":[],"ability_desc":""}},{"choice_id":"B"},{"choice_id":"C"}]}
第一阶段剧情、选项与结果已经永久锁定；MVU任何格式、语义或工具错误都只能重做MVU，绝不能要求写手重写。每项独立裁决：先摘出已经完成的事实，识别主体、否定、条件、所有权、持续性与直接因果；排除尝试、提议、将来可能、仅被看见或由他人承受的事实；再对应能力清单并只输出最小差分，在gm_judgement说明原文证据。路线、暗号、消息与口头情报不是item，除非实体媒介真正转手。玩家实际获得一个或多个实体物品时，必须逐项登记item/ownership；能准确复用已有卡才grant_card，否则至少输出一个card content_job交给独立工坊，不判断物品是否有战斗用途，多个物品允许按共同场景、用途或意象合并。MVU禁止编写created_cards或effects。真实气血变化必须使用hp操作，不能写body/condition=hp_loss；body状态名称必须受原文支持。禁止为凑效果而添加环境、好感、警戒、资源、奖励或战斗。结果确实无法映射时返回no_executable_effect与原因，等待本地只重试MVU。
剧情中的灵石、铜钱、银钱等通用货币都映射为gold；锁定结果明确支付、花费或被夺走货币时，必须输出对应负数gold并按需添加gold_at_least，不能只登记买到的物品。
""".strip()


def build_context(save: dict, card_names_by_id: dict[str, str],
                  relic_names_by_id: dict[str, str]) -> tuple[dict, list[dict], int]:
    state = save["state"]
    turns = state.get("rPRecentTurns", [])
    streak = 0
    for turn in reversed(turns):
        if str(turn.get("next", "")).lower() == "combat":
            break
        streak += 1
    compact = {
        "hp": state.get("hP"),
        "max_hp": state.get("maxHP"),
        "gold": state.get("gold"),
        "deck_size": len(state.get("deck", [])),
        # Unique names only: enough to establish ownership without dumping duplicate deck entries.
        "owned_ability_names": sorted({
            card_names_by_id.get(entry.get("cardId"), entry.get("cardId"))
            for entry in state.get("deck", []) if isinstance(entry, dict) and entry.get("cardId")
        }),
        "owned_relic_names": sorted({
            relic_names_by_id.get(relic_id, relic_id) for relic_id in state.get("relicIds", [])
        }),
        "act": state.get("rPCurrentAct"),
        "typed_location": state.get("rPCurrentLocation"),
        "environment_traits": state.get("rPEnvironmentTraits", []),
        "narrative_items": state.get("rPNarrativeItems", []),
        "combat_edge": state.get("rPCombatEdge", 0),
        "body": state.get("rPBodyConditions", []),
        "skills": state.get("rPSkills", []),
        "relationships": state.get("rPRelationships", []),
        "faction_alerts": state.get("rPFactionAlerts", []),
        "narrative_state": json.loads(state.get("rPWorldStateJson") or "{}"),
    }
    return compact, turns[-6:], streak


def reward_has_effect(reward: dict) -> bool:
    if not isinstance(reward, dict):
        return False
    scalar_keys = ("hp", "gold", "hp_change", "gold_change")
    if any(reward.get(key, 0) not in (0, None, "") for key in scalar_keys):
        return True
    list_keys = ("cards", "relics", "created_cards", "created_relics",
                 "removed_cards", "removed_relics")
    return any(bool(reward.get(key)) for key in list_keys)


def reward_has_positive_growth(reward: dict) -> bool:
    if not isinstance(reward, dict):
        return False
    if any((reward.get(key, 0) or 0) > 0 for key in ("hp", "gold", "hp_change", "gold_change")):
        return True
    return any(bool(reward.get(key)) for key in
               ("cards", "relics", "created_cards", "created_relics"))


def active_update(update: dict) -> bool:
    domain, field = update.get("domain"), update.get("field")
    amount = update.get("amount", 0)
    if domain in {"body", "item", "skill"}:
        return True
    if domain == "relationship":
        return field == "bond" or amount not in (0, None)
    if domain == "faction" and field == "alert":
        return amount not in (0, None)
    return domain == "environment" and field == "combat_edge" and amount not in (0, None)


def normalize_operation(operation: dict) -> dict:
    arguments = operation.get("arguments") if isinstance(operation.get("arguments"), dict) else {}
    if not arguments and isinstance(operation.get("args"), dict):
        arguments = operation["args"]
    return {
        "op": operation.get("op") or operation.get("tool"),
        "action": operation.get("action"),
        "name": operation.get("name") or arguments.get("name"),
        "value": operation.get("value", arguments.get("value", 0)),
    }


def locked_text_names(locked_result: str, candidate: str) -> bool:
    candidate = candidate.strip()
    if not candidate:
        return False
    if candidate in locked_result:
        return True
    if len(candidate) >= 3 and any(
        candidate[start:start + 3] in locked_result
        for start in range(len(candidate) - 2)
    ):
        return True
    return any(noun in candidate and noun in locked_result for noun in
               ("腰牌", "地图", "铜牌", "木牌", "令牌", "纸卷", "卷轴", "玉简", "信件", "丹药",
                "血痕", "灼痕", "伤口", "划伤", "中毒", "眩晕", "昏沉"))


def validate_content_jobs(choice: dict, choice_id: str, locked_result: str,
                          errors: list[str]) -> list[dict]:
    jobs = choice.get("content_jobs") or []
    if not isinstance(jobs, list):
        errors.append(f"选项{choice_id}的content_jobs不是数组")
        return []
    valid = []
    for job_index, job in enumerate(jobs[:2]):
        label = f"选项{choice_id}内容任务#{job_index + 1}"
        if not isinstance(job, dict):
            errors.append(label + "不是对象")
            continue
        if job.get("kind", "card") != "card" or job.get("acquisition", "gain") != "gain":
            errors.append(label + "只能是kind=card且acquisition=gain")
            continue
        if not str(job.get("source_fact", "")).strip() or not str(job.get("concept", "")).strip():
            errors.append(label + "缺少source_fact或concept")
            continue
        if len(str(job.get("mechanic_intent", "")).strip()) < 8:
            errors.append(label + "的mechanic_intent没有说明题材特征与玩法方向")
            continue
        job_text = " ".join(str(job.get(key, "")) for key in
                            ("source_fact", "concept", "mechanic_intent"))
        intangible = any(term in job_text for term in
                         ("情报", "路线", "暗号", "规矩", "秘密", "线索", "约定"))
        physical = any(term in locked_result for term in
                       ("地图", "信件", "纸卷", "卷轴", "令牌", "木牌", "铜牌", "玉简",
                        "册页", "图纸", "画像", "画卷", "纸页", "残页", "拓片", "收进", "收入"))
        learned = (choice.get("resolved_impact") or {}).get("kind") == "learn_ability" and any(
            term in locked_result for term in ("学会", "领悟", "掌握", "习得"))
        if intangible and not physical and not learned:
            errors.append(label + "把纯口头情报/路线送进卡牌工坊")
            continue
        valid.append(job)
    return valid


def score_mvu(mvu: dict, writer: dict | None = None) -> dict:
    choices = mvu.get("choices") if isinstance(mvu, dict) else None
    errors: list[str] = []
    warnings: list[str] = []
    if not isinstance(choices, list) or len(choices) != 3:
        return {"valid": False, "errors": ["choices必须正好三项"], "active": 0,
                "pressure": 0, "environment_only": 0, "combat": 0}
    active_count = pressure_count = environment_only = combat_count = 0
    details = []
    for index, choice in enumerate(choices):
        writer_choice = ((writer or {}).get("choices") or [{}, {}, {}])[index]
        locked_result = (str(writer_choice.get("result_summary", "")) + " "
                         + str(writer_choice.get("consequence_intent", "")))
        updates = choice.get("variable_updates") or choice.get("engine_updates") or []
        # Typed effects must be explicitly stated by the locked current result; ordinary
        # cooperation, dialogue or scenery cannot be promoted into filler affinity/alert/edge.
        for update in updates:
            if not isinstance(update, dict):
                continue
            domain, field = update.get("domain"), update.get("field")
            target, value = str(update.get("target", "")), str(update.get("value", ""))
            named = locked_text_names(locked_result, target) or locked_text_names(locked_result, value)
            supported = True
            if domain in {"item", "skill", "body"}:
                supported = bool(named)
            elif domain == "relationship":
                supported = any(term in locked_result for term in
                                ("好感", "信任", "承认保护", "答应保护", "结盟", "同盟", "决裂",
                                 "亲近", "疏离", "敌视", "依赖", "道侣", "羁绊"))
            elif domain == "faction" and field == "alert":
                supported = any(term in locked_result for term in
                                ("警戒", "通缉", "追捕", "搜捕", "敌意", "起疑", "暴露", "列为目标", "优先目标"))
            elif domain == "environment" and field == "combat_edge":
                supported = any(term in locked_result for term in
                                ("优势", "劣势", "先机", "伏击", "地形", "遮蔽", "破绽", "受制", "包围", "退路", "视线", "偷袭"))
            if not supported:
                errors.append(f"选项{'ABC'[index]}的{domain}/{field}没有锁定结果的明确支持")
            if domain == "body" and any(token in value.lower() for token in ("hp_loss", "hp_gain")):
                errors.append(f"选项{'ABC'[index]}把气血变化伪装成body/condition；必须使用hp操作")
            if domain == "item" and update.get("op") in {"gain", "add"}:
                item_name = target + " " + value
                intangible = any(term in item_name for term in
                                 ("情报", "路线", "暗号", "消息", "秘密", "线索", "位置"))
                physical = any(term in locked_result for term in
                               ("地图", "信件", "纸卷", "卷轴", "令牌", "木牌", "铜牌",
                                "玉简", "册页", "图纸", "画像", "画卷", "纸页",
                                "抛给", "递给", "交给", "塞进", "收进"))
                if intangible and not physical:
                    errors.append(f"选项{'ABC'[index]}把无实体媒介的路线/情报误写成item")
        raw_operations = choice.get("operations") or []
        operations = [normalize_operation(operation) for operation in raw_operations if isinstance(operation, dict)]
        allowed_ops = {"hp", "gold", "grant_card", "grant_relic", "remove_card", "remove_relic",
                       "choose_remove_card", "choose_upgrade_card", "open_shop", "open_reward", "open_rest"}
        valid_operations = []
        for operation in operations:
            op, name = operation.get("op"), operation.get("name")
            if op not in allowed_ops:
                warnings.append(f"选项{'ABC'[index]}丢弃不支持的附加工具{op or operation.get('action')}")
                continue
            if op in {"grant_card", "remove_card"} and name not in KNOWN_CARD_NAMES:
                warnings.append(f"选项{'ABC'[index]}丢弃未知卡牌引用{name}")
                continue
            if op in {"grant_relic", "remove_relic"} and name not in KNOWN_RELIC_NAMES:
                warnings.append(f"选项{'ABC'[index]}丢弃未知法宝引用{name}")
                continue
            if op == "remove_card" and name not in OWNED_CARD_NAMES:
                warnings.append(f"选项{'ABC'[index]}丢弃未拥有卡牌移除{name}")
                continue
            if op == "remove_relic" and name not in OWNED_RELIC_NAMES:
                warnings.append(f"选项{'ABC'[index]}丢弃未拥有法宝移除{name}")
                continue
            valid_operations.append(operation)
        reward = choice.get("rewards") or {}
        content_jobs = validate_content_jobs(choice, "ABC"[index], locked_result, errors)
        if reward.get("created_cards"):
            errors.append(f"选项{'ABC'[index]}仍在MVU中编写created_cards；必须改用content_jobs")
        resolved_impact = choice.get("resolved_impact") or {}
        item_gain = any(
            isinstance(update, dict) and update.get("domain") == "item"
            and update.get("field") == "ownership" and update.get("op") in {"gain", "add"}
            for update in updates
        )
        claims_item_gain = item_gain or (
            resolved_impact.get("kind") == "acquire_item"
            and resolved_impact.get("completed") is True
            and resolved_impact.get("persistent") is True
        )
        direct_card_gain = bool(reward.get("cards")) or bool(content_jobs) or any(
            operation.get("op") == "grant_card" for operation in valid_operations
        )
        if claims_item_gain and not direct_card_gain:
            errors.append(
                f"选项{'ABC'[index]}只登记了物品变化；一个或多个实体物品必须至少合并或分别编译成一张卡"
            )
        combat = str(choice.get("next", "")).lower() == "combat"
        active = combat or bool(valid_operations) or bool(content_jobs) or reward_has_effect(reward) or any(
            active_update(update) for update in updates if isinstance(update, dict)
        )
        any_transition = (combat or bool(valid_operations) or bool(content_jobs)
                          or reward_has_effect(reward) or bool(updates))
        negative = any(
            update.get("causality") == "direct" and update.get("valence") in {"negative", "mixed"}
            for update in updates if isinstance(update, dict)
        )
        negative = negative or any(
            operation.get("op") in {"remove_card", "remove_relic", "choose_remove_card"}
            or (operation.get("op") in {"hp", "gold"} and operation.get("value", 0) < 0)
            for operation in valid_operations
        )
        paid_currency = bool(re.search(
            r"(支付|付出|花了|买下|卖给你).{0,12}(灵石|铜钱|铜板|银钱)|"
            r"(灵石|铜钱|铜板|银钱).{0,12}(支付|付出|花了|买下|卖给你)",
            locked_result))
        compiled_payment = any(
            operation.get("op") == "gold" and (operation.get("value", 0) or 0) < 0
            for operation in valid_operations
        ) or any((reward.get(key, 0) or 0) < 0 for key in ("gold", "gold_change"))
        if paid_currency and not compiled_payment:
            errors.append(f"选项{'ABC'[index]}明确支付了通用货币，却没有编译负数gold")
        compile_status = str(choice.get("compile_status", "compiled")).lower()
        if compile_status == "no_executable_effect" and any_transition:
            errors.append(f"选项{'ABC'[index]}同时声明无效果并输出了状态转移")
        if not any_transition:
            if compile_status == "no_executable_effect":
                reason = str(choice.get("no_effect_reason", "")).strip() or "没有说明原因"
                errors.append(f"选项{'ABC'[index]}无可编译效果：{reason}；锁定剧情不变，请MVU重新裁决")
            else:
                errors.append(f"选项{'ABC'[index]}没有状态转移且未声明no_executable_effect")
        if index == 1:
            growth_update = any(
                isinstance(update, dict) and (
                    (update.get("domain") == "skill" and update.get("op") in
                        {"learn", "upgrade", "gain", "add"})
                    or (update.get("domain") == "body" and
                        (update.get("valence") == "positive" or update.get("op") in {"learn", "upgrade"}))
                )
                for update in updates
            )
            growth_ops = {"grant_card", "grant_relic", "choose_upgrade_card", "choose_remove_card",
                          "remove_card", "open_shop", "open_reward", "open_rest"}
            growth_operation = any(
                operation.get("op") in growth_ops
                or (operation.get("op") in {"hp", "gold"} and (operation.get("value", 0) or 0) > 0)
                for operation in valid_operations
            )
            if (not reward_has_positive_growth(reward) and not content_jobs
                    and not growth_operation and not growth_update):
                errors.append("选项B没有形成正向搜刮或成长；锁定剧情不变，请MVU重新裁决并编译现有结果")
        template_name = (choice.get("encounter") or {}).get("template_name")
        if combat and template_name not in KNOWN_ENEMY_NAMES:
            errors.append(f"选项{'ABC'[index]} combat的template_name={template_name}不在现有敌人目录；必须逐字复制一个已有模板名，剧情称号只放name")
        negative_hp = any(operation.get("op") == "hp" and operation.get("value", 0) < 0
                          for operation in valid_operations)
        unsupported_negative_body = any(
            update.get("domain") == "body" and update.get("op") in {"add", "set"}
            and update.get("valence") in {"negative", "mixed"}
            and not locked_text_names(locked_result, str(update.get("value", "")))
            for update in updates if isinstance(update, dict)
        )
        harm_supported = bool(re.search(r"受伤|负伤|失血|划伤|血痕|流血|出血|中毒|灼伤|伤口|剧痛|眼前发黑|眩晕|昏沉|气血.{0,5}(损|降|失)|被.{0,8}(刺|砍|击中)", locked_result))
        if (negative_hp or unsupported_negative_body) and not harm_supported:
            errors.append(f"选项{'ABC'[index]}擅自增加了锁定剧情没有发生的玩家伤害")
        active_count += int(active)
        pressure_count += int(combat or negative)
        combat_count += int(combat)
        environment_only += int(any_transition and not active)
        details.append({
            "id": choice.get("choice_id", "ABC"[index]),
            "active": active,
            "pressure": combat or negative,
            "next": choice.get("next"),
            "updates": [f"{u.get('domain')}/{u.get('field')}:{u.get('amount', 0)}" for u in updates],
            "operations": [op.get("op") for op in valid_operations],
            "content_jobs": [job.get("concept") for job in content_jobs],
        })
    return {
        "valid": not errors,
        "errors": errors,
        "warnings": warnings,
        "active": active_count,
        "pressure": pressure_count,
        "environment_only": environment_only,
        "combat": combat_count,
        "details": details,
    }


def score_writer(writer: dict) -> dict:
    choices = writer.get("choices") if isinstance(writer, dict) else None
    if not isinstance(choices, list) or len(choices) != 3:
        return {"valid": False, "passive": 3, "combat": 0, "errors": ["choices数量错误"]}
    passive_words = ("观察", "等待", "确认", "试探", "打听", "询问", "不急", "再看")
    passive = sum(any(word in str(choice.get("text", "")) for word in passive_words) for choice in choices)
    combat = sum(str(choice.get("next", "")).lower() == "combat" for choice in choices)
    uncertain = 0
    for choice in choices:
        summary = str(choice.get("result_summary", ""))
        resolved_attempt = any(word in summary for word in ("但", "却", "随后", "失败", "成功", "确认"))
        uncertain += int("若成功" in summary or "可能" in summary
                         or ("试图" in summary and not resolved_attempt))
    combined = [str(choice.get("text", "")) + str(choice.get("result_summary", "")) for choice in choices]
    invented = 0
    for text in combined:
        explicit_hallucination = bool(re.search(
            r"早就料到|早已准备|早有准备|备下|脚下.{0,12}(藏|埋)|恰好.{0,12}(机关|道具)|"
            r"(沟底|淤泥|草丛|路边).{0,18}(摸到|探到|捡到|发现).{0,16}(牌|图|器|物|卷)", text))
        claimed_resource = re.search(r"(烟雾弹|传讯玉简|飞剑传讯|迷香|毒药|爆裂符|遁符)", text)
        unauthorized_claim = bool(claimed_resource and claimed_resource.group(1) not in AUTHORIZED_RESOURCE_TEXT)
        invented += int(explicit_hallucination or unauthorized_claim)
    combat_pre_resolved = sum(
        str(choice.get("next", "")).lower() == "combat"
        and bool(re.search(r"交手|避让|挡下|格挡|躲开|躲过|闪避|斩断|削断|断裂|擦破|横劈|还招|回剑|且战且退|已经突围|顺势翻出|战了.{0,4}招|[三两几]招|血口|灼痕|被.{0,12}(划|刺|击中)|逼退|击退|撞开|挡开|射空|脱手|成功脱离|夺路而逃|扣住.{0,8}巡吏|巡吏.{0,8}反臂|两人.{0,8}僵持", str(choice.get("result_summary", ""))))
        for choice in choices
    )
    growth_kinds = {
        "acquire_item", "learn_ability", "improve_owned_content", "heal", "gain_gold",
        "trade", "deck_edit", "rest", "reward",
    }
    b_choice = choices[1]
    b_kind = str((b_choice.get("resolved_impact") or {}).get("kind", "")).lower()
    b_summary = str(b_choice.get("result_summary", ""))
    b_completed = (b_choice.get("resolved_impact") or {}).get("completed") is True
    if b_kind == "acquire_item":
        b_growth = b_completed and bool(re.search(
            r"取得|到手|收下|带走|收入|收好|藏入|塞进|抽走|捡起|拿到|手里多|获得|学会|领悟|强化|升级|商店|休整",
            b_summary))
    else:
        b_growth = b_kind in growth_kinds and b_completed
    c_choice = choices[2]
    c_kind = str((c_choice.get("resolved_impact") or {}).get("kind", "")).lower()
    c_summary = str(c_choice.get("result_summary", ""))
    explicit_state_change = bool(re.search(
        r"信任|撤回戒备|明确拒绝|当场拒绝|当场接受|翻脸|敌意|通缉|搜捕|封锁|追杀|结盟|断绝|"
        r"受伤|中毒|失去|损坏|成交|支付|扣除|交出|收下|取得|学会|领悟|强化|升级|删去|"
        r"剔除|商店|休整|开战|拔刀|包围|封路|追到|战术", c_summary))
    c_effect = c_kind not in {"", "narrative"} or explicit_state_change
    distinct_b_c = not (b_kind == "acquire_item" and c_kind == "acquire_item")
    errors = []
    if uncertain:
        errors.append(f"{uncertain}项result仍使用假设/未来措辞")
    if invented:
        errors.append(f"{invented}项凭空追加玩家准备或万能机关")
    if combat_pre_resolved:
        errors.append(f"{combat_pre_resolved}项在combat前提前结算交手或伤害")
    if passive:
        errors.append(f"{passive}项仍以观察、等待或打听作为选择主体")
    if not b_growth:
        errors.append("选项B没有明确完成搜刮、成长、交易或牌组功能")
    if not c_effect:
        errors.append("选项C只有脱身、放行或叙事变化，没有当场可编译后果")
    if not distinct_b_c:
        errors.append("选项B和C都以获得物品为主要结果，没有形成不同玩法因果")
    technical = sum(bool(re.search(
        r"faction_alert|relationship/affinity|rp\.items|variable_updates|operations|hp_loss|hp_gain|好感[+-]|关系值[+-]|警戒[+-]",
        str(choice.get("result_summary", "")) + " " + str(choice.get("consequence_intent", "")),
        re.IGNORECASE)) for choice in choices)
    if technical:
        errors.append(f"{technical}项第一阶段结果泄漏内部字段或数值标签")
    return {"valid": not errors, "passive": passive, "combat": combat,
            "uncertain_results": uncertain, "invented_resources": invented,
            "combat_pre_resolved": combat_pre_resolved, "b_growth": b_growth,
            "c_effect": c_effect, "distinct_b_c": distinct_b_c, "errors": errors}


def run_sample(config: dict[str, str], writer_messages: list[dict], mvu_system: str,
               writer_temperature: float, index: int) -> dict:
    result: dict = {"sample": index, "requests": 0}
    writer = {}
    writer_seconds = 0.0
    active_writer_messages = list(writer_messages)
    for writer_attempt in range(1, 3):
        writer_raw, elapsed = call_model(config, active_writer_messages, writer_temperature, 8000)
        writer_seconds += elapsed
        result["requests"] += 1
        try:
            writer = parse_model_json(writer_raw)
            writer_score = score_writer(writer)
        except (ValueError, json.JSONDecodeError) as error:
            writer = {}
            writer_score = {
                "valid": False, "passive": 3, "combat": 0,
                "errors": [f"写手JSON无法解析：{error}"],
            }
        result.setdefault("writer_attempts", []).append({
            "attempt": writer_attempt, "seconds": round(elapsed, 2),
            "score": writer_score, "output": writer,
        })
        if writer_score["valid"] or writer_attempt == 2:
            break
        active_writer_messages.extend([
            {"role": "assistant", "content": writer_raw},
            {"role": "user", "content": "[本地作者定向修复]\n" + "；".join(writer_score["errors"]) +
             "。保持场景因果，纠正错误并重新输出完整场景与A/B/C。"},
        ])
    result["writer"] = writer
    result["writer_score"] = writer_score
    result["writer_seconds"] = round(writer_seconds, 2)
    if not writer.get("choices"):
        result["error"] = "写手在有限修复次数内仍未返回可解析choices"
        return result
    def current_mvu_messages(current_writer: dict, repair: str = "") -> list[dict]:
        compiler_view = {
            "schema_version": "2.0-writer-choices-only",
            "choices": current_writer.get("choices", []),
        }
        messages = [
            {"role": "system", "content": mvu_system},
            {"role": "user", "content": "[当前轮A/B/C；不含历史正文]\n" +
             json.dumps(compiler_view, ensure_ascii=False)},
        ]
        if repair:
            messages.append({"role": "system", "content": "[本地沙箱定向修复]\n" + repair})
        return messages

    mvu_messages = current_mvu_messages(writer)
    max_mvu_attempts = max(1, 4 - result["requests"])
    for attempt in range(1, max_mvu_attempts + 1):
        if result["requests"] >= 4:
            result["final_score"] = result["mvu_attempts"][-1]["score"]
            result["mvu_retries"] = len(result["mvu_attempts"]) - 1
            break
        mvu_raw, mvu_seconds = call_model(config, mvu_messages, 0.35, 10000)
        result["requests"] += 1
        try:
            mvu = parse_model_json(mvu_raw)
            score = score_mvu(mvu, writer)
        except (ValueError, json.JSONDecodeError) as error:
            mvu = {}
            score = {
                "valid": False, "errors": [f"MVU JSON无法解析：{error}"], "warnings": [],
                "active": 0, "pressure": 0, "environment_only": 0, "combat": 0,
                "details": [],
            }
        result.setdefault("mvu_attempts", []).append({
            "attempt": attempt, "seconds": round(mvu_seconds, 2), "score": score, "output": mvu,
        })
        if score["valid"]:
            result["final_score"] = score
            result["mvu_retries"] = attempt - 1
            break
        if attempt < max_mvu_attempts:
            mvu_messages = current_mvu_messages(
                writer,
                "；".join(score["errors"]) +
                "。第一阶段剧情永久锁定，只重做MVU并修正错误字段；获得多个实体物品时可合并为至少一张意象卡，不得只报物品变化。",
            )
    else:
        result["final_score"] = result["mvu_attempts"][-1]["score"]
        result["mvu_retries"] = max_mvu_attempts - 1
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples", type=int, default=3)
    args = parser.parse_args()
    sample_count = max(1, min(args.samples, 10))

    config = load_config()
    if not config.get("ApiKey"):
        raise RuntimeError("InfiniteNarrative.ini has no ApiKey")
    save = load_save()
    worldbook = load_json(DATA / "rp_worldbook.json")
    capability = load_json(DATA / "rp_capability_manifest.json")
    authoring_worldbook = load_json(DATA / "rp_authoring_worldbook.json")
    writer_preset = load_json(DATA / "rp_prompt_preset.json")
    writer_temperature = float(writer_preset.get("temperature", 0.9))
    mvu_preset = load_json(DATA / "rp_mvu_prompt_preset.json")
    global KNOWN_CARD_NAMES, KNOWN_RELIC_NAMES, KNOWN_ENEMY_NAMES
    global OWNED_CARD_NAMES, OWNED_RELIC_NAMES, AUTHORIZED_RESOURCE_TEXT
    cards = load_json(DATA / "cards.json").get("cards", [])
    relics = load_json(DATA / "relics.json").get("relics", [])
    enemies = load_json(DATA / "enemies.json").get("enemies", [])
    card_names_by_id = {entry["id"]: entry["name"] for entry in cards}
    relic_names_by_id = {entry["id"]: entry["name"] for entry in relics}
    KNOWN_CARD_NAMES = set(card_names_by_id.values())
    KNOWN_RELIC_NAMES = set(relic_names_by_id.values())
    KNOWN_ENEMY_NAMES = {entry["name"] for entry in enemies}
    compact, recent, streak = build_context(save, card_names_by_id, relic_names_by_id)
    # MVU receives only a provenance-free current snapshot. Narrative history and the
    # accumulated open MVU object remain exclusive to the writer.
    mvu_snapshot = {
        key: compact.get(key) for key in (
            "hp", "max_hp", "gold", "deck_size", "owned_ability_names",
            "owned_relic_names", "act", "typed_location", "narrative_items",
            "combat_edge", "body", "skills", "relationships", "faction_alerts",
        )
    }
    OWNED_CARD_NAMES = set(compact["owned_ability_names"])
    OWNED_RELIC_NAMES = set(compact["owned_relic_names"])
    AUTHORIZED_RESOURCE_TEXT = json.dumps({
        "owned_ability_names": compact["owned_ability_names"],
        "owned_relic_names": compact["owned_relic_names"],
        "narrative_state": compact["narrative_state"],
        "known_story_item_aliases": ["青冥阁令牌", "黑色令牌", "青冥阁地图", "疗伤丹药"],
    }, ensure_ascii=False)
    content_catalog = "已有卡牌：" + "、".join(sorted(KNOWN_CARD_NAMES))
    content_catalog += "\n已有法宝：" + "、".join(sorted(KNOWN_RELIC_NAMES))
    content_catalog += "\n已有敌人模板名：" + "、".join(sorted(KNOWN_ENEMY_NAMES))
    enemy_guides = []
    for entry in enemies:
        if str(entry.get("id", "")).startswith("proc_"):
            continue
        guide = f"{entry['name']}【原型：{entry.get('story', '')[:90]}"
        if entry.get("abilities"):
            guide += "；机制：" + ",".join(entry["abilities"])
        enemy_guides.append(guide + "】")
    content_catalog += "\n敌人模板原型指南：" + "；".join(sorted(enemy_guides))
    content_catalog += "\n剧情敌人不必与模板同名；按身份、战斗方式与危险程度选择语义最接近的模板名，剧情称号另写encounter.name。"
    card_examples = next((entry.get("content", "") for entry in authoring_worldbook.get("entries", [])
                          if entry.get("id") == "forge.card.examples"), "")
    judgement_examples = next((prompt.get("content", "") for prompt in mvu_preset.get("prompts", [])
                               if prompt.get("identifier") == "judgementExamples"), "")
    subject_examples = next((prompt.get("content", "") for prompt in mvu_preset.get("prompts", [])
                             if prompt.get("identifier") == "subjectExamples"), "")

    writer_system = "\n\n".join([
        writer_preset["prompts"][0]["content"],
        worldbook_text(worldbook),
        "[真实引擎能力]\n" + json.dumps(capability, ensure_ascii=False),
        "[现有内容名称目录]\n" + content_catalog,
        WRITER_CONTRACT,
        writer_preset["prompts"][-1]["content"],
    ])
    writer_user = "\n\n".join([
        f"[肉鸽节奏压力] 已连续{streak}轮没有进入战斗。",
        ("连续两轮以上没有战斗：本幕A/B/C至少一个必须是有历史来源且立即触发的combat分支，"
         "并让另一个分支产生真实代价或游戏功能；不能再用三项纯交谈延后。") if streak >= 2 else "保持当前节奏。",
        "[当前权威状态]\n" + json.dumps(compact, ensure_ascii=False),
        "[最近六轮]\n" + json.dumps(recent, ensure_ascii=False),
        "承接最后选择生成下一幕。不要复述，不要继续观察或确认；立即打破停滞并输出三个可执行分支。",
    ])
    mvu_system = "\n\n".join([
        mvu_preset["prompts"][0]["content"],
        "[无来源的当前权威快照]\n" + json.dumps(mvu_snapshot, ensure_ascii=False),
        "[真实引擎能力]\n" + json.dumps(capability, ensure_ascii=False),
        card_examples,
        judgement_examples,
        subject_examples,
        "[现有内容名称目录]\n" + content_catalog,
        MVU_CONTRACT,
        mvu_preset["prompts"][-1]["content"],
    ])

    report = {
        "created_at": dt.datetime.now().astimezone().isoformat(),
        "model": config.get("Model"),
        "writer_temperature": writer_temperature,
        "save_turn": save["state"].get("rPTurnSerial"),
        "narrative_only_streak": streak,
        "samples": [],
    }
    for index in range(1, sample_count + 1):
        try:
            report["samples"].append(run_sample(
                config,
                [{"role": "system", "content": writer_system},
                 {"role": "user", "content": writer_user}],
                mvu_system,
                writer_temperature,
                index,
            ))
        except Exception as error:  # Keep the remaining samples useful after one provider failure.
            report["samples"].append({"sample": index, "error": str(error), "requests": 0})

    successful = [sample for sample in report["samples"] if "final_score" in sample]
    summary = {
        "samples": sample_count,
        "completed": len(successful),
        "writer_first_pass": sum(
            bool(sample.get("writer_attempts"))
            and sample["writer_attempts"][0]["score"].get("valid", False)
            for sample in successful
        ),
        "valid_without_mvu_retry": sum(sample.get("mvu_retries") == 0 for sample in successful),
        "valid_final": sum(sample["final_score"]["valid"] for sample in successful),
        "writer_quality_pass": sum(sample["writer_score"].get("valid", False) for sample in successful),
        "writer_combat_branches": sum(sample["writer_score"].get("combat", 0) for sample in successful),
        "writer_passive_choices": sum(sample["writer_score"].get("passive", 0) for sample in successful),
        "active_gameplay_choices": sum(sample["final_score"]["active"] for sample in successful),
        "environment_only_choices": sum(sample["final_score"]["environment_only"] for sample in successful),
        "combat_choices": sum(sample["final_score"]["combat"] for sample in successful),
        "total_requests": sum(sample.get("requests", 0) for sample in report["samples"]),
    }
    report["summary"] = summary
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    output_path = OUTPUT_DIR / f"narrative_sim_{stamp}.json"
    output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    latest_path = OUTPUT_DIR / "latest.json"
    latest_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"summary": summary, "report": str(output_path)}, ensure_ascii=False))
    return 0 if (len(successful) == sample_count and summary["valid_final"] == sample_count
                 and summary["writer_quality_pass"] == sample_count) else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(json.dumps({"fatal": str(exc)}, ensure_ascii=False), file=sys.stderr)
        raise SystemExit(1)
