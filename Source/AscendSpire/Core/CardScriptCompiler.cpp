#include "CardScriptCompiler.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	FString CleanToken(FString Token)
	{
		Token.TrimStartAndEndInline();
		if (Token.Len() >= 2 && ((Token[0] == TEXT('"') && Token[Token.Len() - 1] == TEXT('"'))
			|| (Token[0] == TEXT('\'') && Token[Token.Len() - 1] == TEXT('\''))))
			Token = Token.Mid(1, Token.Len() - 2);
		while (Token.EndsWith(TEXT(",")) || Token.EndsWith(TEXT("，"))) Token.LeftChopInline(1);
		return Token;
	}

	FString CanonicalTrigger(FString Value)
	{
		Value = CleanToken(Value).ToLower();
		Value.ReplaceInline(TEXT("-"), TEXT("_"));
		static const TMap<FString, FString> Aliases = {
			{TEXT("play"), TEXT("on_play")}, {TEXT("打出"), TEXT("on_play")},
			{TEXT("turn_start"), TEXT("on_turn_start")}, {TEXT("回合开始"), TEXT("on_turn_start")},
			{TEXT("turn_end"), TEXT("on_turn_end")}, {TEXT("回合结束"), TEXT("on_turn_end")},
			{TEXT("card_played"), TEXT("on_card_played")}, {TEXT("打牌后"), TEXT("on_card_played")},
			{TEXT("damage_dealt"), TEXT("on_damage_dealt")}, {TEXT("造成伤害"), TEXT("on_damage_dealt")},
			{TEXT("damage_taken"), TEXT("on_damage_taken")}, {TEXT("受到伤害"), TEXT("on_damage_taken")},
			{TEXT("draw"), TEXT("on_draw")}, {TEXT("抽牌时"), TEXT("on_draw")},
			{TEXT("discard"), TEXT("on_discard")}, {TEXT("弃牌时"), TEXT("on_discard")},
			{TEXT("exhaust"), TEXT("on_exhaust")}, {TEXT("消耗时"), TEXT("on_exhaust")},
			{TEXT("reshuffle"), TEXT("on_reshuffle")}, {TEXT("洗牌时"), TEXT("on_reshuffle")},
			{TEXT("kill"), TEXT("on_kill")}, {TEXT("击杀时"), TEXT("on_kill")},
			{TEXT("spend_spirit"), TEXT("after_spend_spirit")}, {TEXT("耗灵后"), TEXT("after_spend_spirit")},
			{TEXT("deal_damage"), TEXT("before_deal_damage")}, {TEXT("伤害前"), TEXT("before_deal_damage")},
			{TEXT("take_damage"), TEXT("before_take_damage")}, {TEXT("受伤前"), TEXT("before_take_damage")},
			{TEXT("gain_block"), TEXT("before_gain_block")}, {TEXT("获得罡气前"), TEXT("before_gain_block")},
			{TEXT("gained_block"), TEXT("after_gain_block")}, {TEXT("获得罡气后"), TEXT("after_gain_block")},
			{TEXT("status_applied"), TEXT("on_status_applied")}, {TEXT("状态施加时"), TEXT("on_status_applied")}
		};
		if (const FString* Found = Aliases.Find(Value)) return *Found;
		if (Value.StartsWith(TEXT("on_")) || Value.StartsWith(TEXT("before_"))
			|| Value.StartsWith(TEXT("after_"))) return Value;
		return TEXT("on_play");
	}

	FString CanonicalCounter(FString Value)
	{
		Value = CleanToken(Value).ToLower();
		Value.ReplaceInline(TEXT("-"), TEXT("_"));
		static const TMap<FString, FString> Aliases = {
			{TEXT("basic"), TEXT("on_basic_play")}, {TEXT("基础牌"), TEXT("on_basic_play")},
			{TEXT("basic_gongfa"), TEXT("on_basic_gongfa_play")}, {TEXT("基础功法"), TEXT("on_basic_gongfa_play")},
			{TEXT("any"), TEXT("on_any_card_play")}, {TEXT("any_card"), TEXT("on_any_card_play")},
			{TEXT("任意牌"), TEXT("on_any_card_play")}, {TEXT("same_type"), TEXT("on_same_type_play")},
			{TEXT("同类牌"), TEXT("on_same_type_play")}, {TEXT("sword"), TEXT("on_sword_play")},
			{TEXT("剑诀"), TEXT("on_sword_play")}, {TEXT("spell"), TEXT("on_spell_play")},
			{TEXT("法术"), TEXT("on_spell_play")}, {TEXT("damage"), TEXT("on_damage_dealt")},
			{TEXT("伤害"), TEXT("on_damage_dealt")}, {TEXT("draw"), TEXT("on_draw")},
			{TEXT("抽牌"), TEXT("on_draw")}, {TEXT("turn"), TEXT("on_turn_start")},
			{TEXT("回合"), TEXT("on_turn_start")}
		};
		if (const FString* Found = Aliases.Find(Value)) return *Found;
		return Value.StartsWith(TEXT("on_")) ? Value : TEXT("");
	}

	FString CanonicalRarity(FString Value)
	{
		Value = CleanToken(Value).ToLower();
		static const TMap<FString, FString> Aliases = {
			{TEXT("普通"), TEXT("common")}, {TEXT("常见"), TEXT("common")},
			{TEXT("罕见"), TEXT("uncommon")}, {TEXT("精良"), TEXT("uncommon")},
			{TEXT("稀有"), TEXT("rare")}, {TEXT("史诗"), TEXT("rare")},
			{TEXT("传说"), TEXT("legendary")}, {TEXT("传奇"), TEXT("legendary")}
		};
		return Aliases.Contains(Value) ? Aliases.FindRef(Value) : Value;
	}

	FString CanonicalType(FString Value)
	{
		Value = CleanToken(Value).ToLower();
		static const TMap<FString, FString> Aliases = {
			{TEXT("基础"), TEXT("basic")}, {TEXT("法术"), TEXT("spell")},
			{TEXT("剑诀"), TEXT("sword")}, {TEXT("剑术"), TEXT("sword")},
			{TEXT("炼体"), TEXT("body")}, {TEXT("符箓"), TEXT("talisman")},
			{TEXT("技巧"), TEXT("skill")}, {TEXT("技能"), TEXT("skill")},
			{TEXT("功法"), TEXT("gongfa")}, {TEXT("诅咒"), TEXT("curse")}
		};
		return Aliases.Contains(Value) ? Aliases.FindRef(Value) : Value;
	}

	FString CanonicalStatus(FString Value)
	{
		Value = CleanToken(Value).ToLower();
		static const TMap<FString, FString> Aliases = {
			{TEXT("中毒"), TEXT("poison")}, {TEXT("燃烧"), TEXT("burn")}, {TEXT("虚弱"), TEXT("weak")},
			{TEXT("易伤"), TEXT("vulnerable")}, {TEXT("力量"), TEXT("strength")},
			{TEXT("敏捷"), TEXT("dexterity")}, {TEXT("梦魇"), TEXT("nightmare")},
			{TEXT("临时力量"), TEXT("temp_strength")}
		};
		return Aliases.Contains(Value) ? Aliases.FindRef(Value) : Value;
	}

	FString CanonicalAction(FString Value, FString& OutImplicitStatus)
	{
		Value = CleanToken(Value).ToLower();
		Value.ReplaceInline(TEXT("-"), TEXT("_"));
		static const TMap<FString, FString> Aliases = {
			{TEXT("hit"), TEXT("damage")}, {TEXT("hurt"), TEXT("damage")}, {TEXT("attack"), TEXT("damage")},
			{TEXT("伤害"), TEXT("damage")}, {TEXT("攻击"), TEXT("damage")},
			{TEXT("aoe"), TEXT("damage_all")}, {TEXT("群伤"), TEXT("damage_all")},
			{TEXT("random_damage"), TEXT("damage_random")}, {TEXT("随机伤害"), TEXT("damage_random")},
			{TEXT("shield"), TEXT("block")}, {TEXT("guard"), TEXT("block")},
			{TEXT("罡气"), TEXT("block")}, {TEXT("护甲"), TEXT("block")},
			{TEXT("治疗"), TEXT("heal")}, {TEXT("恢复"), TEXT("heal")},
			{TEXT("抽牌"), TEXT("draw")}, {TEXT("pick"), TEXT("discover_draw")},
			{TEXT("discover"), TEXT("discover_draw")}, {TEXT("发现"), TEXT("discover_draw")},
			{TEXT("energy"), TEXT("gain_spirit")}, {TEXT("mana"), TEXT("gain_spirit")},
			{TEXT("qi"), TEXT("gain_spirit")}, {TEXT("灵力"), TEXT("gain_spirit")},
			{TEXT("next_energy"), TEXT("spirit_next_turn")}, {TEXT("下回合灵力"), TEXT("spirit_next_turn")},
			{TEXT("lose_energy"), TEXT("lose_spirit")}, {TEXT("失去灵力"), TEXT("lose_spirit")},
			{TEXT("血祭"), TEXT("self_damage")}, {TEXT("自伤"), TEXT("self_damage")},
			{TEXT("discard"), TEXT("discard_random")}, {TEXT("弃牌"), TEXT("discard_random")},
			{TEXT("discard_all"), TEXT("discard_hand")}, {TEXT("弃手牌"), TEXT("discard_hand")},
			{TEXT("gold"), TEXT("gain_gold")}, {TEXT("灵石"), TEXT("gain_gold")},
			{TEXT("max_hp"), TEXT("gain_max_hp")}, {TEXT("最大气血"), TEXT("gain_max_hp")},
			{TEXT("cleanse"), TEXT("cleanse_toxicity")}, {TEXT("清丹毒"), TEXT("cleanse_toxicity")},
			{TEXT("move"), TEXT("move_cards")}, {TEXT("移动牌"), TEXT("move_cards")},
			{TEXT("copy"), TEXT("copy_cards")}, {TEXT("复制牌"), TEXT("copy_cards")},
			{TEXT("cost"), TEXT("modify_card_cost")}, {TEXT("改费"), TEXT("modify_card_cost")},
			{TEXT("upgrade"), TEXT("upgrade_cards")}, {TEXT("升级牌"), TEXT("upgrade_cards")},
			{TEXT("transform"), TEXT("transform_cards")}, {TEXT("变牌"), TEXT("transform_cards")},
			{TEXT("shuffle"), TEXT("shuffle_zone")}, {TEXT("洗牌"), TEXT("shuffle_zone")},
			{TEXT("convert"), TEXT("transfer")}, {TEXT("conversion"), TEXT("transfer")},
			{TEXT("转换"), TEXT("transfer")}, {TEXT("转化"), TEXT("transfer")}
		};
		if (const FString* Found = Aliases.Find(Value)) return *Found;
		static const TSet<FString> StatusWords = {
			TEXT("poison"), TEXT("burn"), TEXT("weak"), TEXT("vulnerable"), TEXT("strength"),
			TEXT("dexterity"), TEXT("nightmare"), TEXT("temp_strength"), TEXT("中毒"), TEXT("燃烧"),
			TEXT("虚弱"), TEXT("易伤"), TEXT("力量"), TEXT("敏捷"), TEXT("梦魇"), TEXT("临时力量")
		};
		if (StatusWords.Contains(Value))
		{
			OutImplicitStatus = CanonicalStatus(Value);
			return TEXT("apply_status");
		}
		return Value;
	}

	bool IsSupportedAction(const FString& Action)
	{
		static const TSet<FString> Values = {
			TEXT("damage"), TEXT("damage_all"), TEXT("damage_random"), TEXT("block"), TEXT("draw"),
			TEXT("discover_draw"), TEXT("gain_spirit"), TEXT("lose_spirit"), TEXT("spirit_next_turn"),
			TEXT("heal"), TEXT("apply_status"), TEXT("remove_status"), TEXT("set_status"),
			TEXT("apply_temp_strength"), TEXT("self_damage"), TEXT("discard_random"), TEXT("discard_hand"),
			TEXT("gain_gold"), TEXT("gain_max_hp"), TEXT("cleanse_toxicity"), TEXT("create_card"),
			TEXT("add_card_to_draw"), TEXT("add_card_to_discard"), TEXT("power"),
			TEXT("damage_per_block"), TEXT("damage_per_status"), TEXT("damage_all_per_status"),
			TEXT("block_per_status"), TEXT("amplify_status"), TEXT("damage_all_per_repeat"),
			TEXT("damage_all_per_basic_gongfa"), TEXT("cost_free_basic_gongfa"),
			TEXT("defense_to_strength"), TEXT("move_cards"), TEXT("copy_cards"),
			TEXT("modify_card_cost"), TEXT("upgrade_cards"), TEXT("transform_cards"),
			TEXT("shuffle_zone"), TEXT("transfer"), TEXT("none")
		};
		return Values.Contains(Action);
	}

	FString FindAction(const TArray<FString>& Tokens, FString& OutImplicitStatus)
	{
		for (const FString& Raw : Tokens)
		{
			FString CandidateStatus;
			const FString Candidate = CanonicalAction(Raw, CandidateStatus);
			if (!IsSupportedAction(Candidate)) continue;
			OutImplicitStatus = CandidateStatus;
			return Candidate;
		}
		return TEXT("");
	}

	FString CanonicalTarget(const TArray<FString>& Tokens, const FString& Action, const FString& ImplicitStatus)
	{
		for (const FString& Raw : Tokens)
		{
			const FString Token = CleanToken(Raw).ToLower();
			if (Token == TEXT("all") || Token == TEXT("aoe") || Token == TEXT("all_enemies")
				|| Token == TEXT("全体") || Token == TEXT("全敌人")) return TEXT("all_enemies");
			if (Token == TEXT("random") || Token == TEXT("random_enemy") || Token == TEXT("随机"))
				return TEXT("random_enemy");
			if (Token == TEXT("self") || Token == TEXT("me") || Token == TEXT("自身") || Token == TEXT("自己"))
				return TEXT("self");
		}
		static const TSet<FString> SelfActions = {
			TEXT("block"), TEXT("draw"), TEXT("discover_draw"), TEXT("gain_spirit"), TEXT("lose_spirit"),
			TEXT("spirit_next_turn"), TEXT("heal"), TEXT("self_damage"), TEXT("discard_random"),
			TEXT("discard_hand"), TEXT("gain_gold"), TEXT("gain_max_hp"), TEXT("cleanse_toxicity"),
			TEXT("move_cards"), TEXT("copy_cards"), TEXT("modify_card_cost"), TEXT("upgrade_cards"),
			TEXT("transform_cards"), TEXT("shuffle_zone"), TEXT("transfer")
		};
		if (Action == TEXT("damage_all")) return TEXT("all_enemies");
		if (Action == TEXT("damage_random")) return TEXT("random_enemy");
		if (Action == TEXT("apply_status")
			&& (ImplicitStatus == TEXT("strength") || ImplicitStatus == TEXT("dexterity")
				|| ImplicitStatus == TEXT("temp_strength"))) return TEXT("self");
		return SelfActions.Contains(Action) ? TEXT("self") : TEXT("enemy");
	}

	FString CanonicalScriptResource(FString Value, bool bDestination)
	{
		Value = CleanToken(Value).ToLower();
		Value.ReplaceInline(TEXT("current"), TEXT(""));
		Value.TrimStartAndEndInline();
		static const TMap<FString, FString> Aliases = {
			{TEXT("block"), TEXT("self_block")}, {TEXT("armor"), TEXT("self_block")},
			{TEXT("罡气"), TEXT("self_block")}, {TEXT("护甲"), TEXT("self_block")},
			{TEXT("hp"), TEXT("self_hp")}, {TEXT("health"), TEXT("self_hp")}, {TEXT("气血"), TEXT("self_hp")},
			{TEXT("spirit"), TEXT("self_spirit")}, {TEXT("energy"), TEXT("self_spirit")}, {TEXT("灵力"), TEXT("self_spirit")},
			{TEXT("strength"), TEXT("self_status:strength")}, {TEXT("力量"), TEXT("self_status:strength")},
			{TEXT("dexterity"), TEXT("self_status:dexterity")}, {TEXT("敏捷"), TEXT("self_status:dexterity")},
			{TEXT("poison"), TEXT("self_status:poison")}, {TEXT("中毒"), TEXT("self_status:poison")}
		};
		if (const FString* Found = Aliases.Find(Value)) return *Found;
		if (Value.StartsWith(TEXT("self_")) || Value.StartsWith(TEXT("target_")) || Value.StartsWith(TEXT("var:"))) return Value;
		return bDestination ? TEXT("") : Value;
	}

	bool ParseInteger(FString Token, int32& Out)
	{
		Token = CleanToken(Token);
		if (Token.StartsWith(TEXT("+"))) Token.RightChopInline(1);
		const int32 Start = Token.StartsWith(TEXT("-")) ? 1 : 0;
		if (Start >= Token.Len()) return false;
		for (int32 Index = Start; Index < Token.Len(); ++Index)
			if (!FChar::IsDigit(Token[Index])) return false;
		Out = FCString::Atoi(*Token);
		return true;
	}

	int32 FirstInteger(const TArray<FString>& Tokens, int32 Default)
	{
		for (const FString& Token : Tokens)
		{
			int32 Value = 0;
			if (ParseInteger(Token, Value)) return Value;
		}
		return Default;
	}

	FString ValueAfter(const TArray<FString>& Tokens, const TArray<FString>& Keys)
	{
		for (int32 Index = 0; Index < Tokens.Num(); ++Index)
		{
			const FString Token = CleanToken(Tokens[Index]);
			const FString Lower = Token.ToLower();
			for (const FString& Key : Keys)
			{
				if (Lower == Key && Tokens.IsValidIndex(Index + 1)) return CleanToken(Tokens[Index + 1]);
				if (Lower.StartsWith(Key + TEXT("="))) return CleanToken(Token.Mid(Key.Len() + 1));
			}
		}
		return TEXT("");
	}

	bool HasToken(const TArray<FString>& Tokens, const FString& Needle)
	{
		return Tokens.ContainsByPredicate([&Needle](const FString& Raw)
			{ return CleanToken(Raw).Equals(Needle, ESearchCase::IgnoreCase); });
	}

	void SetCommonOptions(const TArray<FString>& Tokens, const TSharedPtr<FJsonObject>& Effect)
	{
		FString Times = ValueAfter(Tokens, {TEXT("times"), TEXT("hits"), TEXT("次数"), TEXT("段")});
		for (const FString& Raw : Tokens)
		{
			const FString Token = CleanToken(Raw).ToLower();
			if ((Token.StartsWith(TEXT("x")) || Token.StartsWith(TEXT("*"))) && Token.Mid(1).IsNumeric())
				Times = Token.Mid(1);
		}
		if (!Times.IsEmpty()) Effect->SetNumberField(TEXT("times"), FMath::Max(1, FCString::Atoi(*Times)));

		FString Chance = ValueAfter(Tokens, {TEXT("chance"), TEXT("概率")});
		if (!Chance.IsEmpty())
		{
			const bool bPercent = Chance.RemoveFromEnd(TEXT("%"));
			float Number = FCString::Atof(*Chance);
			if (bPercent || Number > 1.f) Number /= 100.f;
			Effect->SetNumberField(TEXT("chance"), FMath::Clamp(Number, 0.f, 1.f));
		}
		const FString Limit = ValueAfter(Tokens, {TEXT("limit"), TEXT("max"), TEXT("上限")});
		if (!Limit.IsEmpty()) Effect->SetNumberField(TEXT("max_triggers"), FMath::Max(0, FCString::Atoi(*Limit)));
		const FString Condition = ValueAfter(Tokens, {TEXT("if"), TEXT("when"), TEXT("若")});
		if (!Condition.IsEmpty()) Effect->SetStringField(TEXT("condition"), Condition);
		const FString Scale = ValueAfter(Tokens, {TEXT("scale"), TEXT("by"), TEXT("按")});
		if (!Scale.IsEmpty())
		{
			Effect->SetStringField(TEXT("scale_by"), Scale.ToLower());
			const FString Factor = ValueAfter(Tokens, {TEXT("factor"), TEXT("每组")});
			const FString Divisor = ValueAfter(Tokens, {TEXT("divisor"), TEXT("每")});
			Effect->SetNumberField(TEXT("scale_factor"), Factor.IsEmpty() ? 1 : FCString::Atoi(*Factor));
			Effect->SetNumberField(TEXT("scale_divisor"), Divisor.IsEmpty() ? 1 : FCString::Atoi(*Divisor));
		}
	}

	bool ParseEffect(FString Body, const FString& Trigger, TSharedPtr<FJsonObject>& OutEffect, FString& OutError)
	{
		Body.TrimStartAndEndInline();
		Body.ReplaceInline(TEXT("，"), TEXT(" "));
		Body.ReplaceInline(TEXT(","), TEXT(" "));
		Body.ReplaceInline(TEXT("("), TEXT(" "));
		Body.ReplaceInline(TEXT(")"), TEXT(" "));
		TArray<FString> Tokens;
		Body.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() == 0) { OutError = TEXT("空效果行"); return false; }

		FString ImplicitStatus;
		const bool bLooksLikeTransfer = Body.Contains(TEXT("->"))
			|| Body.Contains(TEXT("convert"), ESearchCase::IgnoreCase)
			|| Body.Contains(TEXT("转化")) || Body.Contains(TEXT("转换"));
		const FString Action = bLooksLikeTransfer ? TEXT("transfer") : FindAction(Tokens, ImplicitStatus);
		if (Action.IsEmpty()) { OutError = TEXT("没有识别到受支持的动作"); return false; }
		OutEffect = MakeShared<FJsonObject>();
		OutEffect->SetStringField(TEXT("action"), Action);
		OutEffect->SetStringField(TEXT("trigger"), Trigger);
		OutEffect->SetStringField(TEXT("duration"), Trigger == TEXT("on_play") ? TEXT("instant") : TEXT("combat"));
		OutEffect->SetStringField(TEXT("target"), CanonicalTarget(Tokens, Action, ImplicitStatus));
		const int32 DefaultValue = Action == TEXT("none") || Action == TEXT("discard_hand")
			|| Action == TEXT("cleanse_toxicity") || Action == TEXT("transfer")
			|| Action == TEXT("shuffle_zone") ? 0 : 1;
		OutEffect->SetNumberField(TEXT("value"), FirstInteger(Tokens, DefaultValue));

		static const TSet<FString> StatusActions = {
			TEXT("apply_status"), TEXT("remove_status"), TEXT("set_status"), TEXT("amplify_status"),
			TEXT("damage_per_status"), TEXT("damage_all_per_status"), TEXT("block_per_status")
		};
		if (StatusActions.Contains(Action))
		{
			FString Status = ImplicitStatus;
			if (Status.IsEmpty()) Status = CanonicalStatus(ValueAfter(Tokens, {TEXT("status"), TEXT("状态")}));
			OutEffect->SetStringField(TEXT("status"), Status.IsEmpty() ? TEXT("poison") : Status);
			OutEffect->SetNumberField(TEXT("stacks"), FMath::Max(1, FirstInteger(Tokens, 1)));
			// Runtime status strength lives in stacks. Keep the generic value neutral so a
			// perfectly valid "poison 2" is not rejected as an overpowered value=2 action.
			if (Action == TEXT("apply_status") || Action == TEXT("remove_status") || Action == TEXT("set_status"))
				OutEffect->SetNumberField(TEXT("value"), 1);
		}

		if (Action == TEXT("transfer"))
		{
			const int32 Arrow = Body.Find(TEXT("->"));
			FString Source;
			FString Destination;
			if (Arrow != INDEX_NONE)
			{
				FString Left = Body.Left(Arrow); Left.ReplaceInline(TEXT("transfer"), TEXT(""), ESearchCase::IgnoreCase);
				Left.ReplaceInline(TEXT("convert"), TEXT(""), ESearchCase::IgnoreCase);
				Left.ReplaceInline(TEXT("转化"), TEXT("")); Left.ReplaceInline(TEXT("转换"), TEXT(""));
				FString Right = Body.Mid(Arrow + 2);
				TArray<FString> LeftTokens; Left.ParseIntoArrayWS(LeftTokens);
				TArray<FString> RightTokens; Right.ParseIntoArrayWS(RightTokens);
				if (LeftTokens.Num() > 0) Source = CanonicalScriptResource(LeftTokens.Last(), false);
				if (RightTokens.Num() > 0) Destination = CanonicalScriptResource(RightTokens[0], true);
			}
			else
			{
				bool bPastJoin = false;
				for (const FString& Raw : Tokens)
				{
					const FString Lower = CleanToken(Raw).ToLower();
					if (Lower == TEXT("to") || Lower == TEXT("into") || Lower == TEXT("as")
						|| Lower == TEXT("为") || Lower == TEXT("成")) { bPastJoin = true; continue; }
					const FString Resource = CanonicalScriptResource(Lower, bPastJoin);
					if (Resource.IsEmpty() || Resource == Lower && !Resource.StartsWith(TEXT("self_"))
						&& !Resource.StartsWith(TEXT("target_")) && !Resource.StartsWith(TEXT("var:"))) continue;
					if (!bPastJoin && Source.IsEmpty()) Source = Resource;
					else if ((bPastJoin || !Source.IsEmpty()) && Destination.IsEmpty() && Resource != Source) Destination = Resource;
				}
			}
			if (Source.IsEmpty() || Destination.IsEmpty()) { OutError = TEXT("transfer 需要来源和目标；可写 block -> strength 或 convert block into strength"); return false; }
			OutEffect->SetStringField(TEXT("source"), Source);
			OutEffect->SetStringField(TEXT("destination"), Destination);
			FString Mode = TEXT("add");
			for (const FString& Candidate : {TEXT("set"), TEXT("min"), TEXT("max"), TEXT("multiply"), TEXT("add")})
				if (HasToken(Tokens, Candidate)) { Mode = Candidate; break; }
			OutEffect->SetStringField(TEXT("write_mode"), Mode);
			OutEffect->SetBoolField(TEXT("consume_source"), HasToken(Tokens, TEXT("consume"))
				|| HasToken(Tokens, TEXT("耗尽")) || HasToken(Tokens, TEXT("全部转化")));
		}
		else if (Action == TEXT("move_cards") || Action == TEXT("copy_cards")
			|| Action == TEXT("modify_card_cost") || Action == TEXT("upgrade_cards")
			|| Action == TEXT("transform_cards") || Action == TEXT("shuffle_zone"))
		{
			static const TSet<FString> Zones = {TEXT("hand"), TEXT("draw"), TEXT("draw_top"), TEXT("draw_random"), TEXT("discard"), TEXT("exhaust"), TEXT("last_played")};
			TArray<FString> FoundZones;
			for (const FString& Raw : Tokens)
			{
				const FString Token = CleanToken(Raw).ToLower();
				if (Zones.Contains(Token)) FoundZones.Add(Token);
			}
			OutEffect->SetStringField(TEXT("source"), FoundZones.Num() > 0 ? FoundZones[0] : TEXT("hand"));
			if (Action == TEXT("move_cards") || Action == TEXT("copy_cards"))
				OutEffect->SetStringField(TEXT("destination"), FoundZones.Num() > 1 ? FoundZones[1] : TEXT("draw_top"));
			FString Selector = ValueAfter(Tokens, {TEXT("select"), TEXT("selector"), TEXT("选")});
			if (Selector.IsEmpty())
			{
				for (const FString& Candidate : {TEXT("any"), TEXT("random"), TEXT("highest_cost"), TEXT("lowest_cost"), TEXT("upgraded"), TEXT("non_upgraded"), TEXT("retained"), TEXT("exhausting")})
					if (HasToken(Tokens, Candidate)) { Selector = Candidate; break; }
			}
			OutEffect->SetStringField(TEXT("param"), Selector.IsEmpty() ? TEXT("random") : Selector);
			if (Action == TEXT("modify_card_cost"))
			{
				const FString Delta = ValueAfter(Tokens, {TEXT("delta"), TEXT("cost"), TEXT("费用")});
				OutEffect->SetNumberField(TEXT("stacks"), Delta.IsEmpty() ? -1 : FCString::Atoi(*Delta));
			}
		}
		SetCommonOptions(Tokens, OutEffect);
		return true;
	}

	bool SplitKeyValue(const FString& Line, FString& OutKey, FString& OutValue)
	{
		int32 Split = INDEX_NONE;
		if (!Line.FindChar(TEXT(':'), Split)) Line.FindChar(TEXT('='), Split);
		if (Split == INDEX_NONE) return false;
		OutKey = Line.Left(Split).TrimStartAndEnd().ToLower();
		OutValue = Line.Mid(Split + 1).TrimStartAndEnd();
		return true;
	}
}

bool FCardScriptCompiler::CompileToAuthoredObject(const FString& Source,
	TSharedPtr<FJsonObject>& OutCard, FString& OutError)
{
	OutCard.Reset();
	OutError.Reset();
	FString Clean = Source;
	Clean.ReplaceInline(TEXT("\r"), TEXT(""));
	Clean.ReplaceInline(TEXT("```cardscript"), TEXT(""), ESearchCase::IgnoreCase);
	Clean.ReplaceInline(TEXT("```card"), TEXT(""), ESearchCase::IgnoreCase);
	Clean.ReplaceInline(TEXT("```"), TEXT(""));
	Clean.ReplaceInline(TEXT(";"), TEXT("\n"));
	TArray<FString> Lines;
	Clean.ParseIntoArrayLines(Lines, true);

	OutCard = MakeShared<FJsonObject>();
	OutCard->SetStringField(TEXT("rarity"), TEXT("uncommon"));
	OutCard->SetStringField(TEXT("type"), TEXT("skill"));
	OutCard->SetStringField(TEXT("class"), TEXT(""));
	OutCard->SetNumberField(TEXT("cost"), 1);
	OutCard->SetBoolField(TEXT("exhaust"), false);
	OutCard->SetBoolField(TEXT("retain"), false);
	TArray<TSharedPtr<FJsonValue>> Effects;

	for (FString Line : Lines)
	{
		Line.TrimStartAndEndInline();
		while (Line.StartsWith(TEXT("- ")) || Line.StartsWith(TEXT("* "))) Line.RightChopInline(2);
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("//"))) continue;
		FString Key, Value;
		const bool bHasKeyValue = SplitKeyValue(Line, Key, Value);
		const FString Lower = Line.ToLower();
		TArray<FString> LooseTokens;
		Line.ParseIntoArrayWS(LooseTokens);
		const FString LooseKey = LooseTokens.Num() > 0 ? CleanToken(LooseTokens[0]).ToLower() : TEXT("");
		const FString LooseValue = LooseTokens.Num() > 1
			? Line.Mid(Line.Find(LooseTokens[1])).TrimStartAndEnd() : TEXT("");

		if (Lower.StartsWith(TEXT("card ")) || Lower.StartsWith(TEXT("name ")) || Lower.StartsWith(TEXT("卡名 ")))
		{
			OutCard->SetStringField(TEXT("name"), CleanToken(Line.Mid(Line.Find(TEXT(" ")) + 1)));
			continue;
		}
		if (bHasKeyValue && (Key == TEXT("card") || Key == TEXT("name") || Key == TEXT("卡名") || Key == TEXT("名称"))) { OutCard->SetStringField(TEXT("name"), CleanToken(Value)); continue; }
		if (bHasKeyValue && (Key == TEXT("rarity") || Key == TEXT("稀有度"))) { OutCard->SetStringField(TEXT("rarity"), CanonicalRarity(Value)); continue; }
		if (!bHasKeyValue && (LooseKey == TEXT("rarity") || LooseKey == TEXT("稀有度"))) { OutCard->SetStringField(TEXT("rarity"), CanonicalRarity(LooseValue)); continue; }
		if (bHasKeyValue && (Key == TEXT("type") || Key == TEXT("类型"))) { OutCard->SetStringField(TEXT("type"), CanonicalType(Value)); continue; }
		if (!bHasKeyValue && (LooseKey == TEXT("type") || LooseKey == TEXT("类型"))) { OutCard->SetStringField(TEXT("type"), CanonicalType(LooseValue)); continue; }
		if (bHasKeyValue && (Key == TEXT("class") || Key == TEXT("职业"))) { OutCard->SetStringField(TEXT("class"), Value.Equals(TEXT("none"), ESearchCase::IgnoreCase) ? TEXT("") : Value.ToLower()); continue; }
		if (!bHasKeyValue && (LooseKey == TEXT("class") || LooseKey == TEXT("职业"))) { OutCard->SetStringField(TEXT("class"), LooseValue.Equals(TEXT("none"), ESearchCase::IgnoreCase) ? TEXT("") : LooseValue.ToLower()); continue; }
		if (bHasKeyValue && (Key == TEXT("cost") || Key == TEXT("费用"))) { OutCard->SetNumberField(TEXT("cost"), FCString::Atoi(*Value)); continue; }
		if (!bHasKeyValue && (LooseKey == TEXT("cost") || LooseKey == TEXT("费用"))) { OutCard->SetNumberField(TEXT("cost"), FCString::Atoi(*LooseValue)); continue; }
		if (bHasKeyValue && (Key == TEXT("flavor") || Key == TEXT("风味"))) { OutCard->SetStringField(TEXT("flavor"), Value); continue; }
		if (!bHasKeyValue && (LooseKey == TEXT("flavor") || LooseKey == TEXT("风味"))) { OutCard->SetStringField(TEXT("flavor"), LooseValue); continue; }
		if (bHasKeyValue && (Key == TEXT("counter") || Key == TEXT("计数器"))) { OutCard->SetStringField(TEXT("counter_condition"), CanonicalCounter(Value)); continue; }
		if (!bHasKeyValue && (LooseKey == TEXT("counter") || LooseKey == TEXT("计数器"))) { OutCard->SetStringField(TEXT("counter_condition"), CanonicalCounter(LooseValue)); continue; }
		if (bHasKeyValue && (Key == TEXT("exhaust") || Key == TEXT("消耗"))) { OutCard->SetBoolField(TEXT("exhaust"), !Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) && Value != TEXT("0") && Value != TEXT("否")); continue; }
		if (bHasKeyValue && (Key == TEXT("retain") || Key == TEXT("保留"))) { OutCard->SetBoolField(TEXT("retain"), !Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) && Value != TEXT("0") && Value != TEXT("否")); continue; }
		if (Lower == TEXT("exhaust") || Lower == TEXT("消耗")) { OutCard->SetBoolField(TEXT("exhaust"), true); continue; }
		if (Lower == TEXT("retain") || Lower == TEXT("保留")) { OutCard->SetBoolField(TEXT("retain"), true); continue; }

		FString Trigger = TEXT("on_play");
		FString EffectBody = Line;
		if (bHasKeyValue)
		{
			const FString Candidate = CanonicalTrigger(Key.StartsWith(TEXT("on ")) ? Key.Mid(3) : Key);
			if (Key == TEXT("play") || Key == TEXT("打出") || Key == TEXT("effect") || Key == TEXT("效果")
				|| Key.StartsWith(TEXT("on_"))
				|| Key.StartsWith(TEXT("on ")) || Key.StartsWith(TEXT("before_")) || Key.StartsWith(TEXT("after_"))
				|| Candidate != TEXT("on_play"))
			{
				Trigger = Candidate;
				EffectBody = Value;
			}
		}
		TSharedPtr<FJsonObject> Effect;
		FString EffectError;
		if (ParseEffect(EffectBody, Trigger, Effect, EffectError))
		{
			Effects.Add(MakeShared<FJsonValueObject>(Effect));
			continue;
		}
		FString ExistingName;
		if (!OutCard->TryGetStringField(TEXT("name"), ExistingName) && Effects.Num() == 0)
		{
			OutCard->SetStringField(TEXT("name"), CleanToken(Line));
			continue;
		}
		OutError = FString::Printf(TEXT("无法理解卡牌脚本行：%s（%s）"), *Line, *EffectError);
		OutCard.Reset();
		return false;
	}

	FString Name;
	if (!OutCard->TryGetStringField(TEXT("name"), Name) || Name.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("卡牌脚本缺少名称；写 name: 卡名 或 card 卡名");
		OutCard.Reset();
		return false;
	}
	if (Effects.Num() == 0)
	{
		OutError = TEXT("卡牌脚本至少需要一行效果，例如 play: damage 8");
		OutCard.Reset();
		return false;
	}
	OutCard->SetArrayField(TEXT("effects"), Effects);
	return true;
}

FString FCardScriptCompiler::PromptReference()
{
	return TEXT(
		"只输出宽松卡牌短脚本，不输出JSON、Markdown、分析或解释。字段顺序自由，冒号或等号都行；"
		"可省略默认值（uncommon / skill / cost 1）。最小只需卡名和一行效果：\n"
		"name: 卡名\nplay: damage 8\n"
		"可选字段：rarity common|uncommon|rare|legendary；type basic|spell|sword|body|talisman|skill|gongfa；"
		"cost 0..3；class；exhaust；retain；counter；flavor。\n"
		"每个效果必须独占一行，格式固定为 trigger: action value [target] [x次数] [if 条件] [scale 来源 factor N divisor N] [chance 50%] [limit N]。"
		"也接受自然英文，如 play: deal 8 damage、play: gain 6 block、play: apply 2 poison。\n"
		"trigger可写 play/turn_start/turn_end/card_played/damage_dealt/damage_taken/draw/discard/exhaust/kill。\n"
		"action可写 damage/aoe/random_damage/block/heal/draw/discover/energy/next_energy/self_damage/discard/"
		"poison/burn/weak/vulnerable/strength/dexterity/move/copy/cost/upgrade/shuffle/transfer。\n"
		"任意资源转化：play: block -> strength consume；或 play: convert block into strength consume。\n"
		"牌区操作：play: move 1 highest_cost discard draw_top\n"
		"if条件只允许self_hp_below:50、self_hp_above:50、self_has_status:strength、self_missing_status:poison、"
		"target_has_status:poison、target_missing_status:weak、counter_at_least:3、hand_size_at_least:5、"
		"draw_pile_at_most:3、discard_pile_at_least:5；不要把月相、天气或剧情概念发明成条件名，改用retain、trigger和多条效果近似。\n"
		"绝不能写trigger: play、effect: 一段说明、choose/reveal路径或其他自创语法；若复杂概念无法完美实现，就用上述原语做最接近的可玩近似。\n"
		"模仿结构而非题材：\n"
		"name: 袖底青芒\nrarity: uncommon\ncost: 1\nexhaust\nplay: damage 7\nplay: poison 2\n\n"
		"name: 月痕藏宝图\ntype: skill\ncost: 1\nretain\nplay: discover 3\nplay: block 5\n\n"
		"name: 罡尽锋生\ntype: gongfa\nrarity: rare\ncost: 2\nexhaust\nplay: block -> strength consume\n"
		"不要为了像示例而使用抽牌；先从concept提炼材质、用途、变化规律、取得代价至少两个特征，再各自映射成效果行。"
		"提交前逐行检查：除字段行外，每行冒号左边只能是trigger，右边第一个可识别动作必须来自action清单。" );
}
