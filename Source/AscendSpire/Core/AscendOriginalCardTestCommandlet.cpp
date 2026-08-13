#include "AscendOriginalCardTestCommandlet.h"

#include "Combat/CombatEngine.h"
#include "CardScriptCompiler.h"
#include "Dom/JsonObject.h"
#include "InfiniteNarrativeService.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "NarrativePromptManager.h"
#include "RunManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString MakeTransportResponse(const FString& Content)
	{
		TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("content"), Content);
		TSharedPtr<FJsonObject> Choice = MakeShared<FJsonObject>();
		Choice->SetObjectField(TEXT("message"), Message);
		TArray<TSharedPtr<FJsonValue>> Choices;
		Choices.Add(MakeShared<FJsonValueObject>(Choice));
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("choices"), Choices);
		FString Result;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
		return Result;
	}
}

int32 UAscendOriginalCardTestCommandlet::Main(const FString& Params)
{
	int32 Passed = 0;
	int32 Failed = 0;
	auto Check = [&Passed, &Failed](bool bCondition, const TCHAR* Label)
	{
		if (bCondition)
		{
			++Passed;
			UE_LOG(LogTemp, Display, TEXT("[PASS] %s"), Label);
		}
		else
		{
			++Failed;
			UE_LOG(LogTemp, Error, TEXT("[FAIL] %s"), Label);
		}
	};

	UE_LOG(LogTemp, Display, TEXT("========== LLM Original Card E2E Test =========="));
	const FString InvalidContent = TEXT(
		"{\"schema_version\":\"1.2\","
		"\"scene\":{\"title\":\"古剑传承\",\"speaker\":\"古剑灵\",\"narration\":\"剑灵授下从未现世的剑诀。\",\"dialogue\":\"接住这一剑。\"},"
		"\"choices\":["
		"{\"text\":\"接受星痕剑诀\",\"result_summary\":\"剑诀化入识海。\",\"next\":\"continue_rp\","
		"\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0,\"created_cards\":[{"
		"\"name\":\"星痕剑诀\",\"rarity\":\"uncommon\",\"type\":\"attack\",\"cost\":1,"
		"\"description\":\"模型声称造成999点伤害\",\"design_note\":\"测试本地限幅与非法类型修复\","
		"\"art\":\"Art/cards/sword_qi.png\",\"exhaust\":false,\"retain\":false,"
		"\"effects\":[{\"action\":\"damage\",\"value\":999,\"target\":\"invalid_target\",\"times\":9,\"chance\":2.0}]}]}},"
		"{\"text\":\"暂且婉拒\",\"result_summary\":\"剑灵仍在等待。\",\"next\":\"continue_rp\",\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0}},"
		"{\"text\":\"询问剑诀来历\",\"result_summary\":\"剑灵讲起旧事。\",\"next\":\"continue_rp\",\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0}}"
		"],\"state_patch\":{}}"
	);

	UInfiniteNarrativeService* Service = NewObject<UInfiniteNarrativeService>();
	TSharedPtr<FJsonObject> ScriptCardObject;
	FString ScriptCompileError;
	const bool bShortScriptCompiled = FCardScriptCompiler::CompileToAuthoredObject(TEXT(
		"name: 罡尽锋生\nrarity: rare\ntype: gongfa\ncost: 2\nexhaust\n"
		"play: transfer self_block -> self_status:strength consume"), ScriptCardObject, ScriptCompileError);
	const TArray<TSharedPtr<FJsonValue>>* ScriptEffects = nullptr;
	const TSharedPtr<FJsonObject> ScriptEffect = bShortScriptCompiled
		&& ScriptCardObject->TryGetArrayField(TEXT("effects"), ScriptEffects) && ScriptEffects->Num() == 1
		? (*ScriptEffects)[0]->AsObject() : nullptr;
	Check(bShortScriptCompiled && ScriptEffect.IsValid()
		&& ScriptEffect->GetStringField(TEXT("action")) == TEXT("transfer")
		&& ScriptEffect->GetStringField(TEXT("source")) == TEXT("self_block")
		&& ScriptEffect->GetStringField(TEXT("destination")) == TEXT("self_status:strength")
		&& ScriptEffect->GetBoolField(TEXT("consume_source")),
		TEXT("lenient CardScript compiles a one-line all-block-to-strength transfer"));
	TSharedPtr<FJsonObject> ChineseScriptObject;
	FString ChineseScriptError;
	const bool bChineseScriptCompiled = FCardScriptCompiler::CompileToAuthoredObject(TEXT(
		"卡名=袖底青芒; 稀有度=uncommon; 费用=1; 消耗; 打出: 伤害 7; 打出: 中毒 2"),
		ChineseScriptObject, ChineseScriptError);
	const TArray<TSharedPtr<FJsonValue>>* ChineseEffects = nullptr;
	Check(bChineseScriptCompiled && ChineseScriptObject->TryGetArrayField(TEXT("effects"), ChineseEffects)
		&& ChineseEffects->Num() == 2
		&& (*ChineseEffects)[0]->AsObject()->GetStringField(TEXT("action")) == TEXT("damage")
		&& (*ChineseEffects)[1]->AsObject()->GetStringField(TEXT("status")) == TEXT("poison")
		&& (*ChineseEffects)[1]->AsObject()->GetIntegerField(TEXT("value")) == 1
		&& (*ChineseEffects)[1]->AsObject()->GetIntegerField(TEXT("stacks")) == 2,
		TEXT("CardScript accepts semicolons, equals signs and Chinese action aliases"));
	TSharedPtr<FJsonObject> NaturalScriptObject;
	FString NaturalScriptError;
	const bool bNaturalScriptCompiled = FCardScriptCompiler::CompileToAuthoredObject(TEXT(
		"name: Breathing Map\ncost 1\nplay: deal 8 damage\nplay: gain 6 block\n"
		"turn_end: convert block into strength consume"), NaturalScriptObject, NaturalScriptError);
	const TArray<TSharedPtr<FJsonValue>>* NaturalEffects = nullptr;
	Check(bNaturalScriptCompiled && NaturalScriptObject->TryGetArrayField(TEXT("effects"), NaturalEffects)
		&& NaturalEffects->Num() == 3
		&& (*NaturalEffects)[0]->AsObject()->GetStringField(TEXT("action")) == TEXT("damage")
		&& (*NaturalEffects)[1]->AsObject()->GetStringField(TEXT("action")) == TEXT("block")
		&& (*NaturalEffects)[2]->AsObject()->GetStringField(TEXT("action")) == TEXT("transfer")
		&& (*NaturalEffects)[2]->AsObject()->GetStringField(TEXT("source")) == TEXT("self_block")
		&& (*NaturalEffects)[2]->AsObject()->GetStringField(TEXT("destination")) == TEXT("self_status:strength"),
		TEXT("CardScript accepts natural English verbs and arrow-free resource conversion"));
	FCardData LenientConditionCard;
	FString LenientConditionError;
	Check(Service->ParseForgedCardForAutomationTest(MakeTransportResponse(TEXT(
		"name: 月相试图\nrarity: rare\ncost: 2\nretain\nplay: discover 3\n"
		"play: weak 1 all_enemies if moon_phase")), 10, LenientConditionCard, LenientConditionError)
		&& LenientConditionCard.Effects.Num() == 2
		&& LenientConditionCard.Effects[1].Condition.IsEmpty(),
		TEXT("unknown optional thematic conditions are dropped without discarding the executable card"));
	FString ExternalCardScriptPath;
	if (FParse::Value(*Params, TEXT("CardScriptFile="), ExternalCardScriptPath))
	{
		FString ExternalCardScript;
		FCardData ExternalForgedCard;
		FString ExternalForgeError;
		const bool bLoadedExternalScript = FFileHelper::LoadFileToString(
			ExternalCardScript, *ExternalCardScriptPath);
		Check(bLoadedExternalScript
			&& Service->ParseForgedCardForAutomationTest(MakeTransportResponse(ExternalCardScript), 11,
				ExternalForgedCard, ExternalForgeError)
			&& !ExternalForgedCard.Name.IsEmpty() && ExternalForgedCard.Effects.Num() > 0,
			TEXT("live-model CardScript compiles into a registered executable card"));
	}
	URunManager* ResolutionRun = NewObject<URunManager>();
	const bool bResolutionRunStarted = ResolutionRun->StartNewRun(20260809, true);
	TArray<FString> ResolutionEnemies = {TEXT("mountain_imp")};
	TArray<FString> ResolutionLog = {TEXT("山魈 被击杀!"), TEXT("====== 战斗胜利! ======")};
	if (bResolutionRunStarted)
		ResolutionRun->RecordInfiniteCombatDigest(ResolutionEnemies, 4, 74, 51, ResolutionLog);
	const FEnemyData* ResolutionEnemy = bResolutionRunStarted
		? ResolutionRun->GetEnemyData(TEXT("mountain_imp")) : nullptr;
	Check(ResolutionEnemy && ResolutionRun->State.LastResolvedEncounterFact.Contains(ResolutionEnemy->Name)
		&& ResolutionRun->State.LastResolvedEncounterFact.Contains(TEXT("均已被击倒或击杀"))
		&& ResolutionRun->State.LastResolvedEncounterFact.Contains(TEXT("同一敌人实体")),
		TEXT("combat victory stores an authoritative resolved-encounter continuity fact"));
	Check(ResolutionRun->State.LastCombatDigest.Contains(TEXT("被击杀"))
		&& ResolutionRun->State.LastCombatDigest.Contains(TEXT("本次遭遇已结束")),
		TEXT("combat digest recognizes kill wording and declares the encounter closed"));
	const FString GainRouting = Service->ResolveAuthoringKnowledgeForAutomationTest(TEXT(
		"{\"choices\":[{\"result_summary\":\"你接过羊皮地图，将它收入怀中。\","
		"\"consequence_intent\":\"玩家已经正式取得并持续持有地图。\"}]}"));
	Check(GainRouting.Contains(TEXT("[rules.ownership_gain]"))
		&& GainRouting.Contains(TEXT("[tools.content_lookup]"))
		&& GainRouting.Contains(TEXT("[forge.card]"))
		&& GainRouting.Contains(TEXT("[forge.relic_or_companion]")),
		TEXT("gain-language regex routes ownership, lookup and creation manuals before MVU generation"));
	const FString PhysicalClueRouting = Service->ResolveAuthoringKnowledgeForAutomationTest(TEXT(
		"{\"choices\":[{\"result_summary\":\"你接过止血草药，又把柳字木牌与信笺收入怀中。\","
		"\"consequence_intent\":\"玩家已经取得并持续持有草药、木牌和信笺。\"}]}"));
	Check(PhysicalClueRouting.Contains(TEXT("一个或多个实体物品"))
		&& PhysicalClueRouting.Contains(TEXT("多个物品可按共同场景"))
		&& PhysicalClueRouting.Contains(TEXT("mechanic_intent")),
		TEXT("every acquired physical item routes to mandatory card authoring and may be grouped"));
	const FString NegatedGainRouting = Service->ResolveAuthoringKnowledgeForAutomationTest(TEXT(
		"{\"choices\":[{\"result_summary\":\"你没有接过毒针，只看了一眼。\"}]}"));
	Check(NegatedGainRouting.Contains(TEXT("[rules.ownership_gain]"))
		&& NegatedGainRouting.Contains(TEXT("否定或假设")),
		TEXT("high-recall gain routing still gives the MVU model negation and ownership judgment rules"));
	const FString LossRouting = Service->ResolveAuthoringKnowledgeForAutomationTest(TEXT(
		"{\"choices\":[{\"result_summary\":\"你将旧术交出并从识海中遗忘。\"}]}"));
	Check(LossRouting.Contains(TEXT("[rules.ownership_loss]"))
		&& LossRouting.Contains(TEXT("[game.services]")),
		TEXT("loss-language regex routes removal semantics and executable deck services"));
	const FInfiniteNarrativeBeat StreamingPreview = Service->ParseStreamingPreviewForAutomationTest(TEXT(
		"{\"schema_version\":\"2.0-writer\",\"scene\":{\"title\":\"雨夜来客\","
		"\"narration\":\"雨线敲在残瓦上。\",\"messages\":["
		"{\"speaker\":\"沈照璃\",\"portrait_id\":\"shen_zhaoli\",\"expression\":\"concerned\",\"text\":\"你听，门外"
	));
	Check(StreamingPreview.Title == TEXT("雨夜来客")
		&& StreamingPreview.Narration == TEXT("雨线敲在残瓦上。")
		&& StreamingPreview.DialogueLines.Num() == 1
		&& StreamingPreview.DialogueLines[0].Speaker == TEXT("沈照璃")
		&& StreamingPreview.DialogueLines[0].Text == TEXT("你听，门外"),
		TEXT("SSE preview extracts complete narration and an in-progress chat bubble from partial JSON"));
	FInfiniteNarrativeBeat Beat;
	FString ParseError;
	const bool bInvalidParsed = Service->ParseResponseForAutomationTest(MakeTransportResponse(InvalidContent), Beat, ParseError);
	Check(bInvalidParsed && Beat.Choices.Num() == 3 && Beat.Choices[0].Reward.CreatedCards.Num() == 0,
		TEXT("illegal authored content is dropped locally without rejecting the narrative beat"));

	const FString MissingNarrativeRewardContent = TEXT(
		"{\"schema_version\":\"1.5\","
		"\"scene\":{\"title\":\"战后搜检\",\"narration\":\"敌人倒在雨中。\",\"dialogue\":\"\",\"messages\":[]},"
		"\"choices\":["
		"{\"text\":\"捡起尸体旁的三枚淬毒飞针并收好\",\"result_summary\":\"你取下三枚淬毒飞针，仔细端详后收入怀中。\",\"next\":\"continue_rp\",\"inventory_intent\":\"gain_card\",\"inventory_item\":\"淬毒飞针\",\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0}},"
		"{\"text\":\"查看足迹\",\"result_summary\":\"你辨认出敌人的去向。\",\"next\":\"continue_rp\",\"inventory_intent\":\"none\",\"inventory_item\":\"\",\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0}},"
		"{\"text\":\"立即离开\",\"result_summary\":\"你没有触碰尸体，转身离开。\",\"next\":\"continue_rp\",\"inventory_intent\":\"none\",\"inventory_item\":\"\",\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0}}"
		"],\"state_patch\":{}}"
	);
	Beat = FInfiniteNarrativeBeat();
	ParseError.Reset();
	const bool bMissingRewardParsed = Service->ParseResponseForAutomationTest(
		MakeTransportResponse(MissingNarrativeRewardContent), Beat, ParseError);
	Check(bMissingRewardParsed && Beat.Choices.Num() == 3 && Beat.Choices[0].Reward.CreatedCards.Num() == 0,
		TEXT("a missing item reward does not truncate the narrative beat"));

	const FString ThreeBranchContent = TEXT(
		"{\"schema_version\":\"1.6\","
		"\"scene\":{\"title\":\"雨后遗物\",\"narration\":\"三条道路各自通向不同的遗物。\",\"dialogue\":\"\",\"messages\":[]},"
		"\"choices\":["
		"{\"text\":\"捡起黑伞迎向追兵\",\"result_summary\":\"你将黑伞牢牢收进怀中；循着伞面残留的追魂印，流云剑宗的黑衣追兵从庙外跃入，拔剑封住了唯一退路。\",\"next\":\"combat\",\"reward_timing\":\"immediate\",\"inventory_intent\":\"gain_card\",\"inventory_item\":\"黑伞\","
		"\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0,\"created_cards\":[{\"name\":\"黑伞\",\"rarity\":\"common\",\"type\":\"skill\",\"class\":\"\",\"cost\":0,\"art\":\"\",\"effects\":[{\"action\":\"block\",\"value\":5,\"target\":\"self\"}]}]},"
		"\"encounter\":{\"template_id\":\"mountain_imp\",\"name\":\"流云剑宗黑衣追兵\",\"story\":\"他循着黑伞上的追魂印找到破庙，从雨幕中破门而入，并拔剑封住玩家与沈照璃的退路。\",\"tier\":\"normal\",\"hp_scale\":1.0,\"intent_scale\":1.0,\"abilities\":[]}},"
		"{\"text\":\"收下羊皮纸地图\",\"result_summary\":\"你将羊皮纸地图收入怀中。\",\"next\":\"continue_rp\",\"reward_timing\":\"immediate\",\"inventory_intent\":\"gain_card\",\"inventory_item\":\"羊皮纸地图\","
		"\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0,\"created_cards\":[{\"name\":\"羊皮纸地图\",\"rarity\":\"uncommon\",\"type\":\"skill\",\"class\":\"\",\"cost\":0,\"art\":\"\",\"effects\":[{\"action\":\"discover_draw\",\"value\":3,\"target\":\"self\"}]}]}},"
		"{\"text\":\"取走淬毒飞针\",\"result_summary\":\"你取走淬毒飞针并仔细收好。\",\"next\":\"continue_rp\",\"reward_timing\":\"immediate\",\"inventory_intent\":\"gain_card\",\"inventory_item\":\"淬毒飞针\","
		"\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0,\"created_cards\":[{\"name\":\"淬毒飞针\",\"rarity\":\"common\",\"type\":\"talisman\",\"class\":\"danxiu\",\"cost\":1,\"art\":\"\",\"effects\":[{\"action\":\"damage\",\"value\":5,\"target\":\"enemy\"}]}]}}"
		"],\"state_patch\":{},\"memory\":{}}"
	);
	Beat = FInfiniteNarrativeBeat();
	ParseError.Reset();
	const bool bThreeBranchesParsed = Service->ParseResponseForAutomationTest(
		MakeTransportResponse(ThreeBranchContent), Beat, ParseError);
	Check(bThreeBranchesParsed && Beat.Choices.Num() == 3
		&& Beat.Choices[0].Reward.CreatedCards.Num() == 1
		&& Beat.Choices[1].Reward.CreatedCards.Num() == 1
		&& Beat.Choices[2].Reward.CreatedCards.Num() == 1,
		TEXT("phase-two audit may pre-author independent items for all three future branches"));
	Check(bThreeBranchesParsed && Beat.Choices[0].bGrantRewardBeforeCombat,
		TEXT("an immediate item acquisition on a combat branch is committed before combat"));
	Check(bThreeBranchesParsed && Beat.Choices[1].Reward.CreatedCards[0].Effects[0].Action == TEXT("discover_draw")
		&& Beat.Choices[1].Reward.CreatedCards[0].Effects[0].Value == 3,
		TEXT("map-like authored cards can register a choose-one-from-three draw-pile effect"));
	FCardData ParsedMapCard;
	if (bThreeBranchesParsed && Beat.Choices[1].Reward.CreatedCards.Num() == 1)
		ParsedMapCard = Beat.Choices[1].Reward.CreatedCards[0];

	const FString WriterDraft = TEXT(
		"{\"schema_version\":\"2.0-writer\",\"scene\":{\"title\":\"锁定正文\",\"narration\":\"风雨将至。\",\"messages\":[]},\"choices\":["
		"{\"choice_id\":\"A\",\"text\":\"拾起毒针\",\"result_summary\":\"你将毒针收入袖中。\",\"consequence_intent\":\"玩家已经取得并持续持有毒针。\",\"next\":\"continue_rp\"},"
		"{\"choice_id\":\"B\",\"text\":\"迎战追兵\",\"result_summary\":\"门外追兵拔剑闯入。\",\"consequence_intent\":\"冲突不可避免，玩家没有提前受伤。\",\"next\":\"combat\"},"
		"{\"choice_id\":\"C\",\"text\":\"退回暗处\",\"result_summary\":\"你藏入阴影并辨出一条密道，却被旧钉划伤手掌，气血受损。\",\"consequence_intent\":\"玩家获得密道线索，没有取得物品，但手掌受伤。\",\"next\":\"continue_rp\"}]}" );
	const FString CompilerContent = TEXT(
		"{\"schema_version\":\"2.0-mvu\",\"state_patch\":{\"location\":\"破庙\"},\"memory\":{\"title\":\"毒针\"},\"choices\":["
		"{\"choice_id\":\"A\",\"text\":\"恶意改写\",\"consequence_intent\":\"恶意改写\",\"state_patch\":{\"facts\":{\"owns_poison_needles\":true}},\"reward_timing\":\"immediate\",\"inventory_intent\":\"gain_card\",\"inventory_item\":\"淬毒飞针\",\"rewards\":{\"cards\":[\"poison_needles\"],\"relics\":[],\"gold\":0,\"hp\":0}},"
		"{\"choice_id\":\"B\",\"reward_timing\":\"none\",\"inventory_intent\":\"none\",\"inventory_item\":\"\",\"rewards\":{},\"encounter\":{\"template_id\":\"mountain_imp\",\"name\":\"雨夜追兵\",\"story\":\"循足迹闯入破庙。\",\"tier\":\"normal\",\"hp_scale\":1.0,\"intent_scale\":1.0,\"abilities\":[]}},"
		"{\"choice_id\":\"C\",\"state_patch\":{\"clues\":{\"密道\":\"已发现\"}},\"reward_timing\":\"none\",\"inventory_intent\":\"none\",\"inventory_item\":\"\",\"operations\":[],\"rewards\":{}}]}" );
	const FString PreResolvedCombatDraft = TEXT(
		"{\"schema_version\":\"2.0-writer\",\"scene\":{\"title\":\"越界战斗\",\"narration\":\"敌人封住去路。\",\"messages\":[]},\"choices\":["
		"{\"choice_id\":\"A\",\"text\":\"拔剑\",\"result_summary\":\"敌人避让剑锋后横劈还招，你且战且退。\",\"next\":\"combat\"},"
		"{\"choice_id\":\"B\",\"text\":\"谈判\",\"result_summary\":\"敌人提出限时条件。\",\"next\":\"continue_rp\"},"
		"{\"choice_id\":\"C\",\"text\":\"撤离\",\"result_summary\":\"敌人堵住后门。\",\"next\":\"continue_rp\"}]}" );
	FInfiniteNarrativeBeat PreResolvedBeat;
	FString PreResolvedError;
	const bool bPreResolvedAccepted = Service->ParseResponseForAutomationTest(
		MakeTransportResponse(PreResolvedCombatDraft), PreResolvedBeat, PreResolvedError);
	Check(bPreResolvedAccepted && PreResolvedBeat.Choices.Num() == 3,
		TEXT("single-pass narrative parser does not reject or regenerate a readable scene on semantic style grounds"));

	const FString DirectRouteDraft = TEXT(
		"{\"schema_version\":\"4.0-direct-route\",\"scene\":{\"title\":\"星盘\",\"narration\":\"星光落在残盘上。\",\"messages\":[]},\"choices\":["
		"{\"choice_id\":\"A\",\"text\":\"迎战\",\"result_summary\":\"守印兽封住去路。\",\"next\":\"combat\",\"encounter\":{\"template_name\":\"山魈\",\"name\":\"守印兽\"}},"
		"{\"choice_id\":\"B\",\"text\":\"触碰星盘\",\"result_summary\":\"你当场取得月蚀星痕。\",\"next\":\"card_forge\",\"card_concept\":\"月蚀星痕：消耗气血并施加缠绕\"},"
		"{\"choice_id\":\"C\",\"text\":\"掷出玉片\",\"result_summary\":\"玉片反震令你受伤。\",\"next\":\"continue_rp\"}]}" );
	FInfiniteNarrativeBeat DirectRouteBeat;
	FString DirectRouteError;
	const bool bDirectRouteAccepted = Service->ParseResponseForAutomationTest(
		MakeTransportResponse(DirectRouteDraft), DirectRouteBeat, DirectRouteError);
	Check(bDirectRouteAccepted && DirectRouteBeat.Choices[1].Next == TEXT("card_forge")
		&& DirectRouteBeat.Choices[1].CardForgeJobs.Num() == 1
		&& DirectRouteBeat.Choices[1].CardForgeJobs[0].Concept == TEXT("月蚀星痕"),
		TEXT("direct-route parser keeps RP free of card mechanics and sends only a sanitized concept to the forge"));

	const FString UnmappedItemCompiler = TEXT(
		"{\"schema_version\":\"3.0-events\",\"state_patch\":{},\"memory\":{},\"choices\":["
		"{\"choice_id\":\"A\",\"next\":\"continue_rp\",\"variable_updates\":[{\"domain\":\"item\",\"target\":\"毒针\",\"field\":\"ownership\",\"op\":\"gain\",\"value\":\"已取得\",\"amount\":1,\"causality\":\"direct\",\"valence\":\"positive\",\"magnitude\":\"minor\"}],\"operations\":[{\"op\":\"hp\",\"value\":1}],\"rewards\":{}},"
		"{\"choice_id\":\"B\",\"next\":\"combat\",\"variable_updates\":[],\"operations\":[],\"rewards\":{},\"encounter\":{\"template_name\":\"山魈\",\"faction_id\":\"追兵\",\"name\":\"追兵\",\"story\":\"追兵拔剑闯入。\",\"tier\":\"normal\",\"hp_scale\":1.0,\"intent_scale\":1.0,\"abilities\":[]}},"
		"{\"choice_id\":\"C\",\"next\":\"continue_rp\",\"variable_updates\":[],\"operations\":[{\"op\":\"hp\",\"value\":-1}],\"rewards\":{}}]}" );
	FInfiniteNarrativeBeat UnmappedItemBeat;
	FString UnmappedItemError;
	const bool bUnmappedItemMerged = Service->MergeCompilerResponseForAutomationTest(
		WriterDraft, MakeTransportResponse(UnmappedItemCompiler), UnmappedItemBeat, UnmappedItemError);
	Check(bUnmappedItemMerged && UnmappedItemBeat.Choices[0].CardForgeJobs.Num() == 1,
		TEXT("an acquired physical item missing a card intent is routed locally to the independent forge"));
	const FString MappedItemCompiler = UnmappedItemCompiler.Replace(
		TEXT("\"operations\":[{\"op\":\"hp\",\"value\":1}],\"rewards\":{}"),
		TEXT("\"operations\":[{\"op\":\"hp\",\"value\":1},{\"op\":\"grant_card\",\"name\":\"淬毒飞针\"}],\"rewards\":{}"));
	FInfiniteNarrativeBeat MappedItemBeat;
	FString MappedItemError;
	const bool bMappedItemMerged = Service->MergeCompilerResponseForAutomationTest(
		WriterDraft, MakeTransportResponse(MappedItemCompiler), MappedItemBeat, MappedItemError);
	Check(bMappedItemMerged && MappedItemBeat.Choices[0].VariableUpdates.ContainsByPredicate(
		[](const FInfiniteVariableUpdate& Update) { return Update.Domain == TEXT("item") && Update.Target == TEXT("毒针"); })
		&& MappedItemBeat.Choices[0].Reward.Cards.Num() == 1,
		TEXT("an acquired physical item persists in story inventory and grants its corresponding card"));
	URunManager* StoryInventoryRun = NewObject<URunManager>();
	StoryInventoryRun->StartNewRun(20260814, true);
	TArray<FString> StoryInventoryReceipts;
	if (bMappedItemMerged)
		StoryInventoryRun->ApplyInfiniteVariableUpdates(
			MappedItemBeat.Choices[0].VariableUpdates, StoryInventoryReceipts);
	Check(StoryInventoryRun->State.RPNarrativeItems.Contains(TEXT("毒针"))
		&& StoryInventoryRun->BuildRPVariableContext().Contains(TEXT("叙事持有物=毒针")),
		TEXT("typed story inventory is saved into authoritative RP context for later ownership checks"));
	FInfiniteNarrativeBeat MergedBeat;
	FString MergeError;
	const bool bMerged = Service->MergeCompilerResponseForAutomationTest(
		WriterDraft, MakeTransportResponse(CompilerContent), MergedBeat, MergeError);
	Check(bMerged && MergedBeat.Choices[0].Text == TEXT("拾起毒针")
		&& MergedBeat.Choices[0].ConsequenceIntent == TEXT("玩家已经取得并持续持有毒针。")
		&& MergedBeat.Choices[0].Reward.Cards.Num() == 1
		&& MergedBeat.Choices[0].Reward.Cards[0].CardId == TEXT("poison_needles"),
		TEXT("mandatory MVU phase overlays inventory while writer prose remains locked"));
	Check(bMerged && MergedBeat.Choices[2].Operations.Num() == 0
		&& MergedBeat.Choices[2].StatePatchJson.Contains(TEXT("密道")),
		TEXT("a meaningful narrative branch may compile to a branch-only state patch without inventing a game operation"));
	Check(bMerged && MergedBeat.Choices[1].Next == TEXT("combat")
		&& MergedBeat.Choices[1].Enemy.Name == TEXT("雨夜追兵"),
		TEXT("MVU compiler supplies an executable enemy without changing writer branch type"));

	const FString RelicWriterDraft = TEXT(
		"{\"schema_version\":\"2.0-writer\",\"scene\":{\"title\":\"护符\",\"narration\":\"老人递来护符。\",\"messages\":[]},\"choices\":["
		"{\"choice_id\":\"A\",\"text\":\"收下护符\",\"result_summary\":\"你接过青木护符并炼化。\",\"consequence_intent\":\"玩家已经取得并持续持有青木护符。\",\"next\":\"continue_rp\"},"
		"{\"choice_id\":\"B\",\"text\":\"婉拒\",\"result_summary\":\"你没有接过护符。\",\"consequence_intent\":\"玩家没有取得物品。\",\"next\":\"continue_rp\"},"
		"{\"choice_id\":\"C\",\"text\":\"询问\",\"result_summary\":\"你询问护符来历。\",\"consequence_intent\":\"玩家只获得口头信息。\",\"next\":\"continue_rp\"}]}" );
	const FString RelicCompilerContent = TEXT(
		"{\"schema_version\":\"3.0-events\",\"state_patch\":{},\"memory\":{},\"choices\":["
		"{\"choice_id\":\"A\",\"operations\":[],\"rewards\":{\"created_relics\":[{\"name\":\"青木护符\",\"rarity\":\"common\",\"trigger\":\"none\",\"effect\":{\"action\":\"block\",\"value\":2,\"target\":\"self\"}}]}},"
		"{\"choice_id\":\"B\",\"operations\":[],\"rewards\":{}},"
		"{\"choice_id\":\"C\",\"operations\":[],\"rewards\":{}}]}" );
	FInfiniteNarrativeBeat RelicBeat;
	FString RelicError;
	const bool bRelicMerged = Service->MergeCompilerResponseForAutomationTest(
		RelicWriterDraft, MakeTransportResponse(RelicCompilerContent), RelicBeat, RelicError);
	Check(bRelicMerged && RelicBeat.Choices[0].Reward.CreatedRelics.Num() == 1
		&& RelicBeat.Choices[0].Reward.CreatedRelics[0].Trigger == TEXT("combat_start"),
		TEXT("unsupported authored relic trigger is normalized instead of dropping an acquired item"));

	const FString EventWriterDraft = TEXT(
		"{\"schema_version\":\"2.0-writer\",\"scene\":{\"title\":\"岔路机缘\",\"narration\":\"山门前有三条路。\",\"messages\":[]},\"choices\":["
		"{\"choice_id\":\"A\",\"text\":\"支付八十灵石进入仙品秘市\",\"result_summary\":\"守门人收下灵石，推开秘市石门。\",\"consequence_intent\":\"玩家完成支付并取得进入仙品秘市的交易资格。\",\"next\":\"continue_rp\"},"
		"{\"choice_id\":\"B\",\"text\":\"交出一门旧术作为代价\",\"result_summary\":\"你答应从识海中舍去一门旧术。\",\"consequence_intent\":\"玩家必须自行选择一张牌永久剔除。\",\"next\":\"continue_rp\"},"
		"{\"choice_id\":\"C\",\"text\":\"接下天剑诀并迎战山魈\",\"result_summary\":\"天剑诀入识海时，守山山魈破岩而出。\",\"consequence_intent\":\"玩家已学会天剑诀，山魈现身导致冲突不可避免。\",\"next\":\"combat\",\"encounter_hint\":\"守山山魈从石壁后出现\"}]}" );
	const FString EventCompilerContent = TEXT(
		"{\"schema_version\":\"3.1-engine-state\",\"state_patch\":{\"location\":\"山门秘市\"},\"memory\":{\"title\":\"山门岔路\"},\"choices\":["
		"{\"choice_id\":\"A\",\"requirements\":[{\"type\":\"gold_at_least\",\"value\":80}],\"operations\":[{\"op\":\"gold\",\"value\":-80},{\"op\":\"open_shop\",\"rarity\":\"legendary\",\"price_multiplier\":1.5}],\"rewards\":{}},"
		"{\"choice_id\":\"B\",\"requirements\":[{\"type\":\"deck_at_least\",\"value\":2}],\"operations\":[{\"op\":\"choose_remove_card\",\"count\":1,\"allow_starter\":true}],\"rewards\":{}},"
		"{\"choice_id\":\"C\",\"operations\":[{\"op\":\"grant_card\",\"name\":\"天剑诀\"}],\"rewards\":{},\"encounter\":{\"template_name\":\"山魈\",\"name\":\"守山山魈\",\"story\":\"它从山门石壁后破岩而出。\",\"tier\":\"normal\",\"hp_scale\":1.0,\"intent_scale\":1.0,\"abilities\":[]}}]}" );
	FInfiniteNarrativeBeat EventBeat;
	FString EventError;
	const bool bEventMerged = Service->MergeCompilerResponseForAutomationTest(
		EventWriterDraft, MakeTransportResponse(EventCompilerContent), EventBeat, EventError);
	Check(bEventMerged && EventBeat.Choices.Num() == 3
		&& EventBeat.Choices[0].Text == TEXT("支付八十灵石进入仙品秘市")
		&& EventBeat.Choices[0].Requirements.Num() == 1
		&& EventBeat.Choices[0].Reward.GoldChange == -80
		&& EventBeat.Choices[0].Operations.ContainsByPredicate([](const FInfiniteGameOperation& Operation)
		{
			return Operation.Op == TEXT("open_shop") && Operation.Rarity == TEXT("legendary")
				&& FMath::IsNearlyEqual(Operation.PriceMultiplier, 1.5f);
		}), TEXT("event interpreter preserves locked prose and compiles a gated 150% legendary shop"));
	Check(bEventMerged && EventBeat.Choices[1].Operations.ContainsByPredicate([](const FInfiniteGameOperation& Operation)
		{
			return Operation.Op == TEXT("choose_remove_card") && Operation.Count == 1;
		}), TEXT("event interpreter compiles a player-facing deck removal choice"));
	Check(bEventMerged && EventBeat.Choices[2].Next == TEXT("combat")
		&& EventBeat.Choices[2].Enemy.TemplateId == TEXT("mountain_imp")
		&& EventBeat.Choices[2].Reward.Cards.ContainsByPredicate([](const FInfiniteRewardCard& Card)
		{
			return Card.CardId == TEXT("heaven_sword_art");
		}), TEXT("existing card and enemy display names resolve to executable internal IDs"));
	const FString NoEffectCompilerContent = EventCompilerContent.Replace(
		TEXT("{\"choice_id\":\"A\",\"requirements\":[{\"type\":\"gold_at_least\",\"value\":80}],\"operations\":[{\"op\":\"gold\",\"value\":-80},{\"op\":\"open_shop\",\"rarity\":\"legendary\",\"price_multiplier\":1.5}],\"rewards\":{}}"),
		TEXT("{\"choice_id\":\"A\",\"compile_status\":\"no_executable_effect\",\"no_effect_reason\":\"只发生了口头询问\",\"operations\":[],\"rewards\":{}}"));
	FInfiniteNarrativeBeat NoEffectBeat;
	FString NoEffectError;
	const bool bNoEffectMerged = Service->MergeCompilerResponseForAutomationTest(
		EventWriterDraft, MakeTransportResponse(NoEffectCompilerContent), NoEffectBeat, NoEffectError);
	Check(bNoEffectMerged && NoEffectBeat.Choices.Num() == 3
		&& NoEffectBeat.Choices[0].Operations.Num() == 0
		&& NoEffectBeat.Choices[1].Operations.Num() == 1,
		TEXT("one narrative-only branch no longer discards valid operations from sibling branches"));

	const FString ItemWriterDraft = TEXT(
		"{\"schema_version\":\"2.0-writer\",\"scene\":{\"title\":\"水道遗物\",\"narration\":\"旧舟下压着三件遗物。\",\"messages\":[]},\"choices\":["
		"{\"choice_id\":\"A\",\"text\":\"取走水道图\",\"result_summary\":\"你将标有暗礁和潮汐的纸质水道图收入怀中。\",\"consequence_intent\":\"玩家已经获得并持有纸质水道图。\",\"next\":\"continue_rp\"},"
		"{\"choice_id\":\"B\",\"text\":\"取走药匣\",\"result_summary\":\"你取得药匣。\",\"consequence_intent\":\"玩家获得实体药匣。\",\"next\":\"continue_rp\"},"
		"{\"choice_id\":\"C\",\"text\":\"踢开旧舟\",\"result_summary\":\"伏兵拔刀围拢。\",\"consequence_intent\":\"战斗不可避免。\",\"next\":\"combat\"}]}" );
	const FString ItemCompilerContent = TEXT(
		"{\"schema_version\":\"3.2-direct\",\"choices\":["
		"{\"choice_id\":\"A\",\"settlement_key\":\"acquire:水道图:旧舟\",\"resolved_impact\":{\"kind\":\"acquire_item\",\"subject\":\"player\",\"object\":\"纸质水道图\",\"completed\":true,\"persistent\":true},\"variable_updates\":[{\"domain\":\"item\",\"target\":\"纸质水道图\",\"field\":\"ownership\",\"op\":\"gain\",\"value\":\"标有暗礁和潮汐\",\"causality\":\"direct\",\"valence\":\"positive\",\"magnitude\":\"moderate\"}],\"content_jobs\":[{\"kind\":\"card\",\"source_fact\":\"玩家取得标有暗礁与潮汐的纸质水道图\",\"concept\":\"暗礁潮汐水道图\",\"mechanic_intent\":\"利用路线预判回收弃牌，并以暗礁提供防护\",\"acquisition\":\"gain\"}],\"next\":\"continue_rp\",\"operations\":[],\"rewards\":{}},"
		"{\"choice_id\":\"B\",\"settlement_key\":\"acquire:药匣:旧舟\",\"resolved_impact\":{\"kind\":\"acquire_item\",\"subject\":\"player\",\"object\":\"药匣\",\"completed\":true,\"persistent\":true},\"variable_updates\":[{\"domain\":\"item\",\"target\":\"药匣\",\"field\":\"ownership\",\"op\":\"gain\",\"value\":\"旧舟遗物\",\"causality\":\"direct\",\"valence\":\"positive\",\"magnitude\":\"minor\"}],\"content_jobs\":[{\"kind\":\"card\",\"source_fact\":\"玩家取得实体药匣\",\"concept\":\"旧舟药匣\",\"mechanic_intent\":\"以有限药材治疗并承担消耗代价\",\"acquisition\":\"gain\"}],\"next\":\"continue_rp\",\"operations\":[],\"rewards\":{}},"
		"{\"choice_id\":\"C\",\"next\":\"combat\",\"operations\":[],\"rewards\":{},\"encounter\":{\"template_name\":\"不存在的模板拼写\",\"name\":\"旧舟伏兵\",\"story\":\"伏兵围拢。\",\"tier\":\"normal\"}}]}" );
	FInfiniteNarrativeBeat ItemBeat;
	FString ItemError;
	const bool bItemMerged = Service->MergeCompilerResponseForAutomationTest(
		ItemWriterDraft, MakeTransportResponse(ItemCompilerContent), ItemBeat, ItemError);
	Check(bItemMerged && ItemBeat.Choices[0].CardForgeJobs.Num() == 1
		&& ItemBeat.Choices[0].Reward.CreatedCards.Num() == 0,
		TEXT("direct narrative output emits card intent while independent forge owns executable card authoring"));
	Check(bItemMerged && ItemBeat.Choices[2].Next == TEXT("combat")
		&& ItemBeat.Choices[2].Enemy.TemplateId == TEXT("mountain_imp"),
		TEXT("bad encounter template is isolated and normalized without erasing sibling mechanics"));

	const FString ForgedCardContent = TEXT(
		"{\"card\":{\"name\":\"藏潮水图\",\"rarity\":\"uncommon\",\"type\":\"skill\",\"class\":\"\",\"cost\":1,\"flavor\":\"暗礁随潮声在纸上浮沉。\",\"exhaust\":false,\"retain\":true,\"effects\":["
		"{\"action\":\"move_cards\",\"value\":1,\"target\":\"self\",\"source\":\"discard\",\"destination\":\"draw_top\",\"param\":\"highest_cost\"},"
		"{\"action\":\"block\",\"value\":5,\"target\":\"self\"}]}}" );
	FCardData ForgedCard;
	FString ForgeError;
	Check(Service->ParseForgedCardForAutomationTest(MakeTransportResponse(ForgedCardContent), 7,
		ForgedCard, ForgeError) && ForgedCard.Name == TEXT("藏潮水图")
		&& ForgedCard.Effects.Num() == 2 && ForgedCard.Id.StartsWith(TEXT("llm_card_")),
		TEXT("independent card forge parser accepts a thematic multi-effect executable card"));
	const FString GenericDrawContent = TEXT(
		"{\"card\":{\"name\":\"又一张纸\",\"rarity\":\"common\",\"type\":\"skill\",\"class\":\"\",\"cost\":0,\"effects\":[{\"action\":\"draw\",\"value\":1,\"target\":\"self\"}]}}" );
	Check(Service->ParseForgedCardForAutomationTest(MakeTransportResponse(GenericDrawContent), 7,
		ForgedCard, ForgeError),
		TEXT("creative quality is taught by the forge prompt rather than enforced as a brittle local rejection"));
	const FString MissingNumericContent = TEXT(
		"{\"card\":{\"name\":\"残缺水路图\",\"rarity\":\"common\",\"type\":\"skill\",\"class\":\"\",\"cost\":1,\"effects\":[{\"action\":\"block\",\"value\":0,\"target\":\"self\"}]}}" );
	Check(Service->ParseForgedCardForAutomationTest(MakeTransportResponse(MissingNumericContent), 7,
		ForgedCard, ForgeError) && ForgedCard.Effects.Num() == 1 && ForgedCard.Effects[0].Value > 0,
		TEXT("minor numeric omissions are completed locally instead of regenerating the whole card"));
	FInfiniteNarrativeBeat WrappedEventBeat;
	FString WrappedEventError;
	const FString GatewayWrappedCompiler = TEXT("接口附加说明：{\"diagnostic\":\"generated\"}\n") + EventCompilerContent;
	const bool bWrappedEventMerged = Service->MergeCompilerResponseForAutomationTest(
		EventWriterDraft, MakeTransportResponse(GatewayWrappedCompiler), WrappedEventBeat, WrappedEventError);
	Check(bWrappedEventMerged && WrappedEventBeat.Choices.Num() == 3
		&& WrappedEventBeat.Choices[0].Operations.Num() >= 2,
		TEXT("parser extracts the executable event object when a gateway prepends another JSON object"));

	const FString PromptPresetJson = TEXT(
		"{\"name\":\"test\",\"prompts\":["
		"{\"identifier\":\"main\",\"role\":\"system\",\"content\":\"MAIN\"},"
		"{\"identifier\":\"worldInfoBefore\",\"role\":\"system\",\"marker\":true},"
		"{\"identifier\":\"chatHistory\",\"marker\":true},"
		"{\"identifier\":\"turn\",\"role\":\"user\",\"content\":\"{{currentTurn}}\"}],"
		"\"prompt_order\":[{\"order\":[{\"identifier\":\"main\"},{\"identifier\":\"worldInfoBefore\"},{\"identifier\":\"chatHistory\"},{\"identifier\":\"turn\"}]}]}" );
	FNarrativeGenerationPreset PromptPreset;
	FString PromptError;
	const bool bPresetLoaded = FNarrativePromptManager::LoadPreset(TEXT(""), TEXT(""), PromptPresetJson,
		PromptPreset, PromptError);
	FNarrativePromptBuildContext PromptContext;
	PromptContext.TokenBudget = 200000;
	PromptContext.WorldInfoScanDepth = 0;
	PromptContext.WorldBookJson = TEXT("{\"entries\":[{\"comment\":\"dragon\",\"keys\":[\"龙门\"],\"content\":\"龙门只在雨夜开启\",\"position\":\"before_char\"}]}");
	for (int32 HistoryIndex = 0; HistoryIndex < 25; ++HistoryIndex)
		PromptContext.ChatHistory.Add({HistoryIndex % 2 == 0 ? TEXT("assistant") : TEXT("user"),
			HistoryIndex == 24 ? TEXT("众人抵达龙门") : FString::Printf(TEXT("历史楼层%d"), HistoryIndex + 1)});
	PromptContext.Macros.Add(TEXT("currentTurn"), TEXT("继续"));
	PromptContext.Macros.Add(TEXT("userInput"), TEXT("继续"));
	FString PromptDiagnostic;
	const TArray<FNarrativePromptMessage> PromptMessages = bPresetLoaded
		? FNarrativePromptManager::BuildMessages(PromptPreset, PromptContext, PromptDiagnostic)
		: TArray<FNarrativePromptMessage>();
	Check(bPresetLoaded && PromptMessages.Num() >= 28
		&& PromptMessages.ContainsByPredicate([](const FNarrativePromptMessage& Message)
		{
			return Message.Content.Contains(TEXT("龙门只在雨夜开启"));
		}), TEXT("Prompt Manager keeps 25 history floors and activates keyed World Info"));
	PromptContext.ChatHistory.Reset();
	PromptContext.Macros.FindOrAdd(TEXT("userInput")) = TEXT("触发外层");
	PromptContext.WorldBookJson = TEXT(
		"{\"entries\":["
		"{\"comment\":\"outer\",\"keys\":[\"触发外层\"],\"content\":\"递归钥匙：内层秘闻\",\"position\":\"before_char\",\"recursive\":true},"
		"{\"comment\":\"inner\",\"keys\":[\"内层秘闻\"],\"content\":\"只有递归扫描才能注入的深层规则\",\"position\":\"before_char\"}]}" );
	const TArray<FNarrativePromptMessage> RecursivePromptMessages = FNarrativePromptManager::BuildMessages(
		PromptPreset, PromptContext, PromptDiagnostic);
	Check(RecursivePromptMessages.ContainsByPredicate([](const FNarrativePromptMessage& Message)
		{
			return Message.Content.Contains(TEXT("只有递归扫描才能注入的深层规则"));
		}), TEXT("World Info recursively activates a deeper keyed entry from injected lore content"));

	FNarrativeGenerationPreset BuiltInNarrativePreset;
	FString BuiltInNarrativeError;
	const bool bBuiltInNarrativeLoaded = FNarrativePromptManager::LoadPreset(
		TEXT("Data/rp_prompt_preset.json"), TEXT(""), TEXT(""), BuiltInNarrativePreset, BuiltInNarrativeError);
	FNarrativePromptBuildContext BuiltInNarrativeContext;
	BuiltInNarrativeContext.GenerationType = TEXT("normal");
	BuiltInNarrativeContext.TokenBudget = 200000;
	BuiltInNarrativeContext.OutputContract = TEXT("OUTPUT");
	BuiltInNarrativeContext.Macros.Add(TEXT("currentTurn"), TEXT("CURRENT"));
	BuiltInNarrativeContext.Macros.Add(TEXT("capabilityManifest"), TEXT("TOOLS"));
	FString BuiltInNarrativeDiagnostic;
	const TArray<FNarrativePromptMessage> BuiltInNarrativeMessages = bBuiltInNarrativeLoaded
		? FNarrativePromptManager::BuildMessages(BuiltInNarrativePreset, BuiltInNarrativeContext,
			BuiltInNarrativeDiagnostic) : TArray<FNarrativePromptMessage>();
	Check(bBuiltInNarrativeLoaded && BuiltInNarrativeMessages.Num() > 0
		&& BuiltInNarrativeMessages.Last().Content.Contains(TEXT("抽到hp=-10"))
		&& BuiltInNarrativeMessages.Last().Content.Contains(TEXT("抽到card_forge"))
		&& BuiltInNarrativeMessages.Last().Content.Contains(TEXT("不得加冒号、解释、效果、费用、脚本")),
		TEXT("single-pass director receives post-history examples for explaining pre-rolled engine routes"));

	FNarrativeGenerationPreset BuiltInMvuPreset;
	FString BuiltInMvuError;
	const bool bBuiltInMvuLoaded = FNarrativePromptManager::LoadPreset(
		TEXT("Data/rp_mvu_prompt_preset.json"), TEXT(""), TEXT(""), BuiltInMvuPreset, BuiltInMvuError);
	FNarrativePromptBuildContext BuiltInMvuContext;
	BuiltInMvuContext.GenerationType = TEXT("mvu");
	BuiltInMvuContext.TokenBudget = 200000;
	BuiltInMvuContext.OutputContract = TEXT("OUTPUT");
	BuiltInMvuContext.Macros.Add(TEXT("draft"), TEXT("DRAFT"));
	BuiltInMvuContext.Macros.Add(TEXT("authoringRules"), TEXT("RULES"));
	BuiltInMvuContext.Macros.Add(TEXT("contentCatalog"), TEXT("CATALOG"));
	BuiltInMvuContext.Macros.Add(TEXT("capabilityManifest"), TEXT("TOOLS"));
	FString BuiltInMvuDiagnostic;
	const TArray<FNarrativePromptMessage> BuiltInMvuMessages = bBuiltInMvuLoaded
		? FNarrativePromptManager::BuildMessages(BuiltInMvuPreset, BuiltInMvuContext,
			BuiltInMvuDiagnostic) : TArray<FNarrativePromptMessage>();
	Check(bBuiltInMvuLoaded && BuiltInMvuMessages.Num() > 0
		&& BuiltInMvuMessages.Last().Content.Contains(TEXT("最高优先级·当前轮裁决到工具"))
		&& BuiltInMvuMessages.Last().Content.Contains(TEXT("content_jobs"))
		&& BuiltInMvuMessages.Last().Content.Contains(TEXT("MVU禁止输出created_cards")),
		TEXT("historyless GM compiler delegates executable card authoring to the selected-branch forge"));

	UInfiniteNarrativeService* LoopGuardService = NewObject<UInfiniteNarrativeService>();
	LoopGuardService->SetRecentNarrativeContextForAutomationTest(TEXT(
		"[原文轮次 4｜雨夜破庙]\n火堆在破庙中央噼啪作响，将沈照璃苍白的脸映得忽明忽暗。"
		"你替她解开左肩的衣襟，一道深可见骨的剑伤横贯肩头。她咬着牙没有出声，只在你触碰伤口时微微皱眉。"
		"沈照璃说起流云剑宗追杀她的旧事，并指出庙外已有新的脚步声逼近。"));
	const FString RepeatedSceneContent = TEXT(
		"{\"schema_version\":\"1.6\",\"scene\":{\"title\":\"换名后的夜谈\","
		"\"narration\":\"火堆在破庙中央噼啪作响，将沈照璃苍白的脸映得忽明忽暗。你替她解开左肩的衣襟，一道深可见骨的剑伤横贯肩头。她咬着牙没有出声，只在你触碰伤口时微微皱眉。\","
		"\"dialogue\":\"\",\"messages\":[{\"speaker\":\"沈照璃\",\"text\":\"流云剑宗仍在追杀我，庙外又有脚步声逼近。\"}]},"
		"\"choices\":["
		"{\"text\":\"离开破庙\",\"result_summary\":\"你们开始收拾行装。\",\"next\":\"continue_rp\",\"rewards\":{}},"
		"{\"text\":\"查看门外\",\"result_summary\":\"你从门缝观察外面。\",\"next\":\"continue_rp\",\"rewards\":{}},"
		"{\"text\":\"继续交谈\",\"result_summary\":\"你示意她继续说。\",\"next\":\"continue_rp\",\"rewards\":{}}"
		"],\"state_patch\":{},\"memory\":{}}" );
	FInfiniteNarrativeBeat RepeatedBeat;
	FString RepeatedError;
	const bool bRepeatedParsed = LoopGuardService->ParseResponseForAutomationTest(
		MakeTransportResponse(RepeatedSceneContent), RepeatedBeat, RepeatedError);
	Check(bRepeatedParsed && RepeatedBeat.Choices.Num() == 3,
		TEXT("repeated narrative is accepted instead of being blocked by an anti-loop gate"));

	const FString Content = TEXT(
		"{\"schema_version\":\"1.4\","
		"\"scene\":{\"title\":\"古剑传承\",\"narration\":\"剑灵授下从未现世的剑诀。\",\"dialogue\":\"\",\"messages\":[{\"speaker\":\"古剑灵\",\"portrait_id\":\"sword_spirit\",\"expression\":\"angry\",\"text\":\"接住这一剑。\"}]},"
		"\"choices\":["
		"{\"text\":\"接受星痕剑诀\",\"result_summary\":\"剑诀化入识海。\",\"next\":\"continue_rp\","
		"\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0,\"created_cards\":[{"
		"\"name\":\"星痕剑诀\",\"rarity\":\"uncommon\",\"type\":\"sword\",\"class\":\"sword\",\"cost\":1,"
		"\"description\":\"获得2点罡气，再造成随罡气成长的伤害。\",\"flavor\":\"陆执事托付的剑谱残页，作为uncommon技能卡定位为攻防联动\",\"design_note\":\"二阶攻防联动剑诀\","
		"\"art\":\"Art/cards/model_invented_missing_art.png\",\"exhaust\":false,\"retain\":false,\"counter_condition\":\"on_any_card_play\","
		"\"effects\":[{\"action\":\"block\",\"value\":2,\"target\":\"self\",\"times\":1,\"chance\":1.0},"
		"{\"action\":\"damage\",\"value\":7,\"target\":\"enemy\",\"times\":1,\"chance\":1.0,\"condition\":\"counter_at_least:1\",\"scale_by\":\"self_block\",\"scale_factor\":1,\"scale_divisor\":2}],"
		"\"upgrade\":{\"cost\":1,\"counter_condition\":\"on_any_card_play\",\"description\":\"强化攻防\",\"effects\":[{\"action\":\"block\",\"value\":3,\"target\":\"self\"},{\"action\":\"damage\",\"value\":8,\"target\":\"enemy\",\"scale_by\":\"self_block\",\"scale_factor\":1,\"scale_divisor\":2}]},"
		"\"visual\":{\"animation\":\"slash\",\"sound\":\"sword_slash\",\"accent\":\"#EAF7FF\",\"duration\":0.42,\"intensity\":4,\"count\":1}}]}},"
		"{\"text\":\"暂且婉拒\",\"result_summary\":\"剑灵仍在等待。\",\"next\":\"continue_rp\",\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0}},"
		"{\"text\":\"询问剑诀来历\",\"result_summary\":\"剑灵讲起旧事。\",\"next\":\"continue_rp\",\"rewards\":{\"cards\":[],\"relics\":[],\"gold\":0,\"hp\":0}}"
		"],\"state_patch\":{}}"
	);
	Beat = FInfiniteNarrativeBeat();
	ParseError.Reset();
	const bool bParsed = Service->ParseResponseForAutomationTest(MakeTransportResponse(Content), Beat, ParseError);
	Check(bParsed, TEXT("schema 1.4 chat-message authored-card response passes production registration checks"));
	if (!bParsed)
	{
		UE_LOG(LogTemp, Error, TEXT("Parser diagnostic: %s"), *ParseError);
		return 1;
	}
	Check(Beat.Choices.Num() == 3, TEXT("exactly three narrative choices survive parsing"));
	Check(Beat.DialogueLines.Num() == 1 && Beat.DialogueLines[0].Speaker == TEXT("古剑灵")
		&& Beat.DialogueLines[0].PortraitId == TEXT("sword_spirit")
		&& Beat.DialogueLines[0].Expression == TEXT("angry"),
		TEXT("chat bubble retains independent speaker portrait and expression fields"));
	Check(Beat.Choices[0].Reward.CreatedCards.Num() == 1, TEXT("exactly one original card is created"));
	Check(Beat.Choices[1].Reward.CreatedCards.Num() == 0 && Beat.Choices[2].Reward.CreatedCards.Num() == 0,
		TEXT("unselected branches do not leak original rewards"));

	const FCardData& ParsedCard = Beat.Choices[0].Reward.CreatedCards[0];
	Check(ParsedCard.Id.StartsWith(TEXT("llm_card_")), TEXT("stable runtime card id is assigned"));
	Check(ParsedCard.Type == TEXT("sword") && ParsedCard.Class == TEXT("sword"), TEXT("legal card type and class are preserved"));
	Check(ParsedCard.CounterCondition == TEXT("on_any_card_play"), TEXT("generic card counter event is registered"));
	Check(ParsedCard.Effects.Num() == 2, TEXT("multi-effect authored card retains its effect stack"));
	Check(ParsedCard.Effects[1].ScaleBy == TEXT("self_block") && ParsedCard.Effects[1].ScaleFactor == 1
		&& ParsedCard.Effects[1].ScaleDivisor == 2, TEXT("dynamic scale source, factor and divisor are registered"));
	Check(ParsedCard.Effects[1].Condition == TEXT("counter_at_least:1"),
		TEXT("counter threshold condition is registered"));
	Check(ParsedCard.Description.Contains(TEXT("self_block")), TEXT("card-face description is rebuilt from executable dynamic values"));
	Check(ParsedCard.UpgradedEffects.Num() == 2 && ParsedCard.UpgradedEffects[0].Value == 3
		&& ParsedCard.UpgradedEffects[1].Value == 8, TEXT("LLM-authored upgrade effect stack is preserved"));
	Check(ParsedCard.Visual.Animation == TEXT("slash") && ParsedCard.Visual.Sound == TEXT("sword_slash"),
		TEXT("LLM-authored animation and sound are registered"));
	Check(ParsedCard.ArtPath == TEXT("Art/cards/jade_talisman.png"),
		TEXT("invalid presentation-only art falls back without rejecting a valid authored reward"));
	Check(ParsedCard.Flavor == TEXT("陆执事托付的剑谱残页"),
		TEXT("card-face flavor keeps only in-world lore and strips rarity/design commentary"));

	URunManager* Run = NewObject<URunManager>();
	Run->SetPersistentAuthoredContentEnabledForAutomationTest(false);
	Check(Run->StartNewRun(20260807, true), TEXT("isolated infinite-narrative run initializes"));
	Run->GenerateNarrativeShopStock(TEXT("legendary"), 1.5f, 6);
	const TArray<FShopItem> NarrativeShop = Run->GetShopStock();
	Check(NarrativeShop.Num() > 0 && NarrativeShop.ContainsByPredicate([Run](const FShopItem& Item)
		{
			const FCardData* Card = Run->GetCardData(Item.ItemId);
			return !Card || Card->Rarity != TEXT("legendary") || Item.Price != 180;
		}) == false, TEXT("special narrative shop contains only legendary cards priced at 150 percent"));
	const int32 DeckBeforeGatewayEdits = Run->State.Deck.Num();
	Check(Run->UpgradeDeckCardAt(0) && Run->State.Deck[0].bUpgraded,
		TEXT("event gateway upgrades the selected deck instance"));
	Check(Run->RemoveDeckCardAt(0, false) && Run->State.Deck.Num() == DeckBeforeGatewayEdits - 1,
		TEXT("event gateway removes exactly the selected deck instance"));
	FString StateError;
	const FString InitialWorldState = Run->State.RPWorldStateJson;
	Check(!Run->ApplyRPWorldStatePatchTransactional(TEXT("{\"hp\":1}"), StateError)
		&& StateError.Contains(TEXT("权威字段")) && Run->State.RPWorldStateJson == InitialWorldState,
		TEXT("transactional MVU rejects protected gameplay fields without mutating state"));
	StateError.Reset();
	Check(Run->ApplyRPWorldStatePatchTransactional(
		TEXT("{\"location\":\"试剑台\",\"relationships\":{\"古剑灵\":{\"trust\":2}}}"), StateError),
		TEXT("transactional MVU accepts a valid narrative-only patch"));
	Check(Run->State.RPStateSnapshots.Num() == 1 && Run->State.RPWorldStateJson.Contains(TEXT("试剑台"))
		&& Run->State.RPCurrentLocation == TEXT("试剑台"),
		TEXT("transactional MVU records a rollback snapshot and bridges legacy location into typed state"));
	TArray<FInfiniteVariableUpdate> TypedUpdates;
	FInfiniteVariableUpdate AffinityUpdate;
	AffinityUpdate.Domain = TEXT("relationship");
	AffinityUpdate.Target = TEXT("shen_zhaoli");
	AffinityUpdate.Field = TEXT("affinity");
	AffinityUpdate.Op = TEXT("add");
	AffinityUpdate.Amount = 22;
	TypedUpdates.Add(AffinityUpdate);
	FInfiniteVariableUpdate AlertUpdate;
	AlertUpdate.Domain = TEXT("faction");
	AlertUpdate.Target = TEXT("qingming_ge");
	AlertUpdate.Field = TEXT("alert");
	AlertUpdate.Op = TEXT("add");
	AlertUpdate.Amount = 2;
	AlertUpdate.Duration = 3;
	TypedUpdates.Add(AlertUpdate);
	FInfiniteVariableUpdate LocationUpdate;
	LocationUpdate.Domain = TEXT("environment");
	LocationUpdate.Field = TEXT("location");
	LocationUpdate.Op = TEXT("set");
	LocationUpdate.Value = TEXT("落雁峡密道");
	TypedUpdates.Add(LocationUpdate);
	TArray<FString> VariableReceipts;
	Run->ApplyInfiniteVariableUpdates(TypedUpdates, VariableReceipts);
	Check(Run->State.RPRelationships.Num() == 1 && Run->State.RPRelationships[0].Affinity == 27
		&& Run->State.RPCurrentLocation == TEXT("落雁峡密道") && VariableReceipts.Num() == 3,
		TEXT("typed RP variables apply relationship and environment transitions with receipts"));
	FInfiniteVariableUpdate FakeTaskUpdate;
	FakeTaskUpdate.Domain = TEXT("progress");
	FakeTaskUpdate.Field = TEXT("task");
	FakeTaskUpdate.Op = TEXT("set");
	FakeTaskUpdate.Value = TEXT("模型捏造的新任务");
	TArray<FString> FakeTaskReceipts;
	Run->ApplyInfiniteVariableUpdates({FakeTaskUpdate}, FakeTaskReceipts);
	Check(FakeTaskReceipts.Num() == 0
		&& !Run->BuildRPVariableContext().Contains(TEXT("任务=")),
		TEXT("nonexistent task-system updates are ignored and never injected into GM context"));
	Check(Run->GetFactionAlertLevel(TEXT("qingming_ge")) == 2
		&& Run->BuildRPVariableContext().Contains(TEXT("alert=2")),
		TEXT("faction-scoped temporary alert is visible to the next GM context"));
	FInfiniteVariableUpdate CombatEdgeUpdate;
	CombatEdgeUpdate.Domain = TEXT("environment");
	CombatEdgeUpdate.Field = TEXT("combat_edge");
	CombatEdgeUpdate.Op = TEXT("add");
	CombatEdgeUpdate.Amount = 2;
	CombatEdgeUpdate.Value = TEXT("掌握守卫换岗与侧门破绽");
	TArray<FString> EdgeReceipts;
	Run->ApplyInfiniteVariableUpdates({CombatEdgeUpdate}, EdgeReceipts);
	Check(Run->State.RPCombatEdge == 2 && Run->BuildRPVariableContext().Contains(TEXT("战术态势=+2"))
		&& EdgeReceipts.Num() == 1,
		TEXT("structured environment combat edge is visible and bounded before the next encounter"));
	const FEnemyData* EdgeTemplate = Run->GetEnemyData(TEXT("mountain_imp"));
	const int32 EdgeTemplateHP = EdgeTemplate ? EdgeTemplate->MaxHP : 0;
	FInfiniteEnemySpec EdgeSpec;
	EdgeSpec.TemplateId = TEXT("mountain_imp");
	EdgeSpec.Name = TEXT("战术态势测试山魈");
	EdgeSpec.HPScale = 1.f;
	EdgeSpec.IntentScale = 1.f;
	const FString EdgeEnemyId = Run->RegisterInfiniteEnemy(EdgeSpec, 0);
	const FEnemyData* EdgeEnemy = Run->GetEnemyData(EdgeEnemyId);
	Check(EdgeEnemy && EdgeTemplateHP > 0
		&& EdgeEnemy->MaxHP == FMath::RoundToInt(EdgeTemplateHP * 0.9f)
		&& Run->State.RPCombatEdge == 0 && Run->State.RPCombatEdgeSource.IsEmpty(),
		TEXT("positive combat edge weakens the next enemy and is consumed exactly once"));
	Run->AdvanceRPVariableDurations();
	Run->AdvanceRPVariableDurations();
	Run->AdvanceRPVariableDurations();
	Check(Run->GetFactionAlertLevel(TEXT("qingming_ge")) == 0,
		TEXT("temporary faction alert expires after its bounded RP duration"));
	Check(Run->TryCommitNarrativeSettlement(TEXT("acquire:雨庙木牌:序章"))
		&& !Run->TryCommitNarrativeSettlement(TEXT(" ACQUIRE:雨庙木牌:序章 ")),
		TEXT("the same normalized narrative fact settles only once"));
	for (int32 Turn = 1; Turn <= 7; ++Turn)
	{
		const FString Memory = FString::Printf(TEXT(
			"{\"title\":\"试炼%d\",\"summary\":\"玩家与古剑灵完成第%d次试炼\","
			"\"participants\":[\"古剑灵\"],\"facts\":[\"第%d次试炼完成\"],"
			"\"unresolved\":[\"剑冢之门仍未开启\"],\"keywords\":[\"古剑灵\",\"剑冢\"],\"importance\":0.8}"),
			Turn, Turn, Turn);
		Run->AddRPNarrativeTurn(FString::Printf(TEXT("试炼%d"), Turn), TEXT("古剑灵"),
			FString::Printf(TEXT("玩家进入第%d座剑阵并辨认其中剑意。"), Turn), TEXT("莫要分神。"),
			TEXT("继续试炼"), TEXT("剑阵暂时平息。"), TEXT("continue_rp"), Memory, 6, 3, 18000);
	}
	Check(Run->State.RPRecentTurns.Num() == 3 && Run->State.RPMemoryEvents.Num() == 4,
		TEXT("history threshold compresses old full turns while preserving the newest three"));
	const int32 RawBeforeUnlimited = Run->State.RPRecentTurns.Num();
	for (int32 Turn = 8; Turn <= 14; ++Turn)
	{
		Run->AddRPNarrativeTurn(FString::Printf(TEXT("长上下文%d"), Turn), TEXT("古剑灵"),
			TEXT("这一轮原文必须保留。"), TEXT("继续。"), TEXT("继续试炼"), TEXT("试炼推进。"),
			TEXT("continue_rp"), TEXT("{}"), 0, 0, 18000);
	}
	Check(Run->State.RPRecentTurns.Num() == RawBeforeUnlimited + 7,
		TEXT("zero history limits preserve every raw RP floor without destructive compression"));
	const FString RecalledMemory = Run->BuildRPMemoryContext(TEXT("古剑灵 剑冢"), 2000);
	Check(RecalledMemory.Contains(TEXT("古剑灵")) && RecalledMemory.Contains(TEXT("剑冢")),
		TEXT("entity and keyword recall returns relevant structured long-term memory"));
	const int32 DeckBefore = Run->State.Deck.Num();
	Run->ApplyInfiniteNarrativeReward(Beat.Choices[0].Reward);
	Check(Run->GetCardData(ParsedCard.Id) != nullptr, TEXT("original definition is registered in the run catalog"));
	Check(Run->State.DynamicCards.Num() == 1, TEXT("original definition is retained in dynamic run data"));
	Check(Run->State.Deck.Num() == DeckBefore + 1 && Run->State.Deck.Last().CardId == ParsedCard.Id,
		TEXT("original card is actually added to the player deck"));
	Check(Run->GetPersistentAuthoredCards().Num() == 1,
		TEXT("successful authored content is registered in the cross-save library"));
	Check(Run->DeletePersistentAuthoredCard(ParsedCard.Id)
		&& Run->GetPersistentAuthoredCards().Num() == 0 && Run->GetCardData(ParsedCard.Id) != nullptr,
		TEXT("manual library deletion affects future runs without corrupting the current run definition"));

	TSharedPtr<FJsonObject> SavedState = MakeShared<FJsonObject>();
	const bool bSerialized = FJsonObjectConverter::UStructToJsonObject(
		FRunState::StaticStruct(), &Run->State, SavedState.ToSharedRef(), 0, 0);
	FRunState ReloadedState;
	const bool bDeserialized = bSerialized && FJsonObjectConverter::JsonObjectToUStruct(
		SavedState.ToSharedRef(), FRunState::StaticStruct(), &ReloadedState, 0, 0);
	Check(bSerialized && bDeserialized, TEXT("run state serializes and deserializes in memory"));
	Check(ReloadedState.RPSettledFactKeys.Contains(TEXT("acquire:雨庙木牌:序章")),
		TEXT("settlement idempotency keys survive save round-trip"));
	Check(ReloadedState.DynamicCards.Num() == 1 && ReloadedState.DynamicCards[0].Id == ParsedCard.Id
		&& ReloadedState.DynamicCards[0].Effects.Num() == 2 && ReloadedState.DynamicCards[0].Effects[1].ScaleBy == TEXT("self_block"),
		TEXT("save round-trip preserves the original card definition and effect"));
	Check(ReloadedState.Deck.ContainsByPredicate([&ParsedCard](const FDeckCard& Card)
		{ return Card.CardId == ParsedCard.Id; }), TEXT("save round-trip preserves deck ownership"));

	UCombatEngine* Combat = NewObject<UCombatEngine>();
	Combat->RegisterRuntimePlayerContent(Run->GetDynamicCards(), Run->GetDynamicRelics());
	FDeckCard TestDeckCard;
	TestDeckCard.CardId = ParsedCard.Id;
	TArray<FDeckCard> TestDeck;
	TestDeck.Add(TestDeckCard);
	FDeckCard TriggerCard;
	TriggerCard.CardId = TEXT("strike");
	TestDeck.Add(TriggerCard);
	TArray<FString> TestEnemies;
	TestEnemies.Add(TEXT("mountain_imp"));
	const bool bCombatStarted = Combat->StartCombat(TestDeck, TestEnemies, {}, 74, 74, 0, 20260807, 0);
	Check(bCombatStarted, TEXT("combat starts with a deck containing only the original card"));
	const int32 InitialAuthoredIndex = Combat->Hand.IndexOfByPredicate([&ParsedCard](const FCardInstance& Card)
		{ return Card.Data.Id == ParsedCard.Id; });
	const int32 InitialTriggerIndex = Combat->Hand.IndexOfByPredicate([](const FCardInstance& Card)
		{ return Card.Data.Id == TEXT("strike"); });
	Check(InitialAuthoredIndex != INDEX_NONE && InitialTriggerIndex != INDEX_NONE,
		TEXT("original and trigger cards are instantiated into the opening hand"));
	if (bCombatStarted && InitialAuthoredIndex != INDEX_NONE && InitialTriggerIndex != INDEX_NONE && Combat->Enemies.Num() == 1)
	{
		Check(Combat->PlayCard(InitialTriggerIndex, 0), TEXT("a preceding card is played to drive the generic counter"));
		const int32 AuthoredIndex = Combat->Hand.IndexOfByPredicate([&ParsedCard](const FCardInstance& Card)
			{ return Card.Data.Id == ParsedCard.Id; });
		Check(AuthoredIndex != INDEX_NONE && Combat->Hand[AuthoredIndex].RepeatCount == 1,
			TEXT("on_any_card_play increments the original card instance counter"));
		const int32 HPBefore = Combat->Enemies[0].State.HP;
		const int32 SpiritBefore = Combat->Spirit;
		const bool bPlayed = AuthoredIndex != INDEX_NONE && Combat->PlayCard(AuthoredIndex, 0);
		Check(bPlayed, TEXT("original card can be played through the production combat engine"));
		Check(SpiritBefore - Combat->Spirit == 1, TEXT("original card spends its configured 1 spirit"));
		Check(Combat->Player.Block == 2, TEXT("first authored effect grants exactly 2 block"));
		Check(HPBefore - Combat->Enemies[0].State.HP == 8,
			TEXT("second authored effect resolves 7 base plus block scaling for exactly 8 damage"));
	}
	else
	{
		Check(false, TEXT("combat state is available for damage verification"));
	}

	UCombatEngine* DiscoverCombat = NewObject<UCombatEngine>();
	DiscoverCombat->RegisterRuntimePlayerContent({ParsedMapCard}, {});
	TArray<FDeckCard> DiscoverDeck;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		FDeckCard Card;
		Card.CardId = Index % 2 == 0 ? TEXT("strike") : TEXT("defend");
		DiscoverDeck.Add(Card);
	}
	const bool bDiscoverCombatStarted = !ParsedMapCard.Id.IsEmpty()
		&& DiscoverCombat->StartCombat(DiscoverDeck, TestEnemies, {}, 74, 74, 0, 20260808, 0);
	if (bDiscoverCombatStarted)
	{
		DiscoverCombat->Hand.Add(DiscoverCombat->MakeCard(ParsedMapCard.Id));
		const int32 MapIndex = DiscoverCombat->Hand.IndexOfByPredicate(
			[&ParsedMapCard](const FCardInstance& Card) { return Card.Data.Id == ParsedMapCard.Id; });
		const int32 DrawBeforeDiscover = DiscoverCombat->DrawPile.Num();
		const bool bMapPlayed = DiscoverCombat->PlayCard(MapIndex, 0);
		Check(bMapPlayed && DiscoverCombat->PendingDiscoverChoices.Num() == FMath::Min(3, DrawBeforeDiscover),
			TEXT("discover_draw pauses combat with up to three real draw-pile candidates"));
		const int32 HandBeforeChoice = DiscoverCombat->Hand.Num();
		const bool bChoiceResolved = DiscoverCombat->ResolveDiscoverChoice(0);
		Check(bChoiceResolved && DiscoverCombat->PendingDiscoverChoices.Num() == 0
			&& DiscoverCombat->Hand.Num() == HandBeforeChoice + 1
			&& DiscoverCombat->DrawPile.Num() == DrawBeforeDiscover - 1,
			TEXT("chosen discover candidate moves from draw pile to hand exactly once"));
	}
	else
	{
		Check(false, TEXT("discover_draw combat fixture starts"));
		Check(false, TEXT("chosen discover candidate moves from draw pile to hand exactly once"));
	}

	const FString TransferCompilerContent = TEXT(
		"{\"schema_version\":\"3.0-events\",\"scene\":{\"title\":\"罡气化锋\",\"narration\":\"道痕凝成一式。\",\"messages\":[]},\"choices\":["
		"{\"choice_id\":\"A\",\"text\":\"领悟化罡诀\",\"result_summary\":\"你领悟了化罡为力的法门。\",\"next\":\"continue_rp\",\"rewards\":{\"created_cards\":[{"
		"\"mechanic_intent\":\"牺牲当前全部罡气换取等量力量\",\"name\":\"化罡诀\",\"rarity\":\"uncommon\",\"type\":\"gongfa\",\"class\":\"\",\"cost\":1,"
		"\"effects\":[{\"action\":\"transfer\",\"trigger\":\"on_play\",\"duration\":\"instant\",\"source\":\"self_block\",\"destination\":\"self_status:strength\",\"consume_source\":true,\"scale_factor\":1,\"scale_divisor\":1}]}]}},"
		"{\"choice_id\":\"B\",\"text\":\"继续观察\",\"result_summary\":\"你暂不领悟。\",\"next\":\"continue_rp\",\"rewards\":{}},"
		"{\"choice_id\":\"C\",\"text\":\"离开\",\"result_summary\":\"你离开此地。\",\"next\":\"continue_rp\",\"rewards\":{}}]}" );
	FInfiniteNarrativeBeat TransferBeat;
	FString TransferParseError;
	const bool bTransferParsed = Service->ParseResponseForAutomationTest(
		MakeTransportResponse(TransferCompilerContent), TransferBeat, TransferParseError);
	Check(bTransferParsed && TransferBeat.Choices[0].Reward.CreatedCards.Num() == 1
		&& TransferBeat.Choices[0].Reward.CreatedCards[0].Effects[0].Action == TEXT("transfer")
		&& TransferBeat.Choices[0].Reward.CreatedCards[0].Effects[0].Source == TEXT("self_block")
		&& TransferBeat.Choices[0].Reward.CreatedCards[0].Effects[0].Destination == TEXT("self_status:strength"),
		TEXT("GM-authored natural-language conversion compiles into a sandboxed transfer effect"));
	if (bTransferParsed && TransferBeat.Choices[0].Reward.CreatedCards.Num() == 1)
	{
		const FCardData TransferCard = TransferBeat.Choices[0].Reward.CreatedCards[0];
		UCombatEngine* TransferCombat = NewObject<UCombatEngine>();
		TransferCombat->RegisterRuntimePlayerContent({TransferCard}, {});
		FDeckCard ConverterDeckCard;
		ConverterDeckCard.CardId = TransferCard.Id;
		FDeckCard DefendDeckCard;
		DefendDeckCard.CardId = TEXT("defend");
		const bool bTransferCombatStarted = TransferCombat->StartCombat(
			{ConverterDeckCard, DefendDeckCard}, TestEnemies, {}, 74, 74, 0, 20260810, 0);
		const int32 DefendIndex = TransferCombat->Hand.IndexOfByPredicate([](const FCardInstance& Card)
			{ return Card.Data.Id == TEXT("defend"); });
		const bool bDefended = bTransferCombatStarted && DefendIndex != INDEX_NONE
			&& TransferCombat->PlayCard(DefendIndex, 0);
		const int32 BlockBeforeTransfer = TransferCombat->Player.Block;
		const int32 ConverterIndex = TransferCombat->Hand.IndexOfByPredicate([&TransferCard](const FCardInstance& Card)
			{ return Card.Data.Id == TransferCard.Id; });
		const bool bConverted = bDefended && ConverterIndex != INDEX_NONE
			&& TransferCombat->PlayCard(ConverterIndex, 0);
		Check(bConverted && BlockBeforeTransfer > 0 && TransferCombat->Player.Block == 0
			&& TransferCombat->Player.GetStatusStacks(TEXT("strength")) == BlockBeforeTransfer,
			TEXT("transfer consumes all current block and grants equal strength in live combat"));

		FCardData FutureTransferCard = TransferCard;
		FutureTransferCard.Id = TEXT("test_future_block_to_strength");
		FutureTransferCard.Name = TEXT("逆罡常法");
		FutureTransferCard.Effects[0].Trigger = TEXT("before_gain_block");
		FutureTransferCard.Effects[0].Duration = TEXT("combat");
		FutureTransferCard.Effects[0].Source = TEXT("event_value");
		UCombatEngine* FutureCombat = NewObject<UCombatEngine>();
		FutureCombat->RegisterRuntimePlayerContent({FutureTransferCard}, {});
		FDeckCard FutureDeckCard;
		FutureDeckCard.CardId = FutureTransferCard.Id;
		const bool bFutureCombatStarted = FutureCombat->StartCombat(
			{FutureDeckCard, DefendDeckCard}, TestEnemies, {}, 74, 74, 0, 20260811, 0);
		const int32 FutureIndex = FutureCombat->Hand.IndexOfByPredicate([&FutureTransferCard](const FCardInstance& Card)
			{ return Card.Data.Id == FutureTransferCard.Id; });
		const bool bRuleRegistered = bFutureCombatStarted && FutureIndex != INDEX_NONE
			&& FutureCombat->PlayCard(FutureIndex, 0);
		const int32 FutureDefendIndex = FutureCombat->Hand.IndexOfByPredicate([](const FCardInstance& Card)
			{ return Card.Data.Id == TEXT("defend"); });
		const bool bFutureDefended = bRuleRegistered && FutureDefendIndex != INDEX_NONE
			&& FutureCombat->PlayCard(FutureDefendIndex, 0);
		Check(bFutureDefended && FutureCombat->Player.Block == 0
			&& FutureCombat->Player.GetStatusStacks(TEXT("strength")) > 0,
			TEXT("combat-duration before_gain_block hook converts future block gains without recursion"));
	}
	else
	{
		Check(false, TEXT("transfer consumes all current block and grants equal strength in live combat"));
		Check(false, TEXT("combat-duration before_gain_block hook converts future block gains without recursion"));
	}

	const FString CompositionalCompilerContent = TEXT(
		"{\"schema_version\":\"3.0-events\",\"scene\":{\"title\":\"三式异法\",\"narration\":\"三枚道印各循异理。\",\"messages\":[]},\"choices\":["
		"{\"choice_id\":\"A\",\"text\":\"领悟归墟手\",\"result_summary\":\"你学会从消耗区回收重法并减费。\",\"next\":\"continue_rp\",\"rewards\":{\"created_cards\":[{"
		"\"mechanic_intent\":\"从消耗区捞回最沉重的牌，并令它本场更易再用\",\"name\":\"归墟手\",\"rarity\":\"uncommon\",\"type\":\"skill\",\"class\":\"\",\"cost\":1,"
		"\"effects\":[{\"action\":\"move_cards\",\"source\":\"exhaust\",\"destination\":\"hand\",\"param\":\"highest_cost\",\"value\":1},{\"action\":\"modify_card_cost\",\"source\":\"hand\",\"param\":\"highest_cost\",\"value\":1,\"stacks\":-1}]}]}},"
		"{\"choice_id\":\"B\",\"text\":\"领悟墨契\",\"result_summary\":\"每三次施法，墨契复制刚才的法术。\",\"next\":\"continue_rp\",\"rewards\":{\"created_cards\":[{"
		"\"mechanic_intent\":\"按法术事件积累墨痕，达到三层后复制刚打出的牌并清零\",\"name\":\"三墨成契\",\"rarity\":\"rare\",\"type\":\"gongfa\",\"class\":\"fuxiu\",\"cost\":0,"
		"\"effects\":[{\"action\":\"transfer\",\"trigger\":\"on_card_played\",\"duration\":\"combat\",\"source\":\"var:void\",\"destination\":\"var:ink\",\"write_mode\":\"add\",\"value\":1,\"condition\":\"event_tag_is:spell\"},{\"action\":\"copy_cards\",\"trigger\":\"on_card_played\",\"duration\":\"combat\",\"source\":\"last_played\",\"destination\":\"hand\",\"param\":\"any\",\"value\":1,\"condition\":\"event_tag_is:spell&&source_at_least:var:ink=3\",\"max_triggers\":1},{\"action\":\"transfer\",\"trigger\":\"on_card_played\",\"duration\":\"combat\",\"source\":\"var:void\",\"destination\":\"var:ink\",\"write_mode\":\"set\",\"value\":0,\"condition\":\"source_at_least:var:ink=3\"}]}]}},"
		"{\"choice_id\":\"C\",\"text\":\"领悟换骨印\",\"result_summary\":\"击杀后弃牌中的旧招会换成基础剑诀。\",\"next\":\"continue_rp\",\"rewards\":{\"created_cards\":[{"
		"\"mechanic_intent\":\"击杀作为锚点，把弃牌区最高费牌改造成已有基础剑诀\",\"name\":\"换骨印\",\"rarity\":\"rare\",\"type\":\"gongfa\",\"class\":\"\",\"cost\":1,"
		"\"effects\":[{\"action\":\"transform_cards\",\"trigger\":\"on_kill\",\"duration\":\"combat\",\"source\":\"discard\",\"destination\":\"基础剑诀\",\"param\":\"highest_cost\",\"value\":1,\"max_triggers\":2}]}]}}]}" );
	FInfiniteNarrativeBeat CompositionalBeat;
	FString CompositionalError;
	const bool bCompositionalParsed = Service->ParseResponseForAutomationTest(
		MakeTransportResponse(CompositionalCompilerContent), CompositionalBeat, CompositionalError);
	Check(bCompositionalParsed && CompositionalBeat.Choices.Num() == 3
		&& CompositionalBeat.Choices[0].Reward.CreatedCards.Num() == 1
		&& CompositionalBeat.Choices[1].Reward.CreatedCards.Num() == 1
		&& CompositionalBeat.Choices[2].Reward.CreatedCards.Num() == 1,
		TEXT("GM compiler accepts three heterogeneous cards built from zones, variables, events and transformation"));
	Check(bCompositionalParsed
		&& CompositionalBeat.Choices[1].Reward.CreatedCards[0].Effects[1].Condition.Contains(TEXT("&&"))
		&& CompositionalBeat.Choices[2].Reward.CreatedCards[0].Effects[0].Destination == TEXT("strike"),
		TEXT("composite thresholds survive validation and display-name card references normalize to runtime IDs"));

	if (bCompositionalParsed && CompositionalBeat.Choices[0].Reward.CreatedCards.Num() == 1)
	{
		const FCardData RecoveryCard = CompositionalBeat.Choices[0].Reward.CreatedCards[0];
		UCombatEngine* RecoveryCombat = NewObject<UCombatEngine>();
		RecoveryCombat->RegisterRuntimePlayerContent({RecoveryCard}, {});
		FDeckCard RecoveryDeckCard; RecoveryDeckCard.CardId = RecoveryCard.Id;
		const bool bRecoveryStarted = RecoveryCombat->StartCombat(
			{RecoveryDeckCard}, TestEnemies, {}, 74, 74, 0, 20260812, 0);
		FCardInstance ExhaustedStrike = RecoveryCombat->MakeCard(TEXT("strike"));
		ExhaustedStrike.CostModifier = 2;
		RecoveryCombat->ExhaustPile.Add(ExhaustedStrike);
		const int32 RecoveryIndex = RecoveryCombat->Hand.IndexOfByPredicate([&RecoveryCard](const FCardInstance& Card)
			{ return Card.Data.Id == RecoveryCard.Id; });
		const bool bRecovered = bRecoveryStarted && RecoveryIndex != INDEX_NONE
			&& RecoveryCombat->PlayCard(RecoveryIndex, 0);
		const FCardInstance* RecoveredStrike = RecoveryCombat->Hand.FindByPredicate([](const FCardInstance& Card)
			{ return Card.Data.Id == TEXT("strike"); });
		Check(bRecovered && RecoveryCombat->ExhaustPile.Num() == 0 && RecoveredStrike
			&& RecoveredStrike->CostModifier == 1,
			TEXT("zone composition moves the highest-cost exhausted card to hand and modifies that live instance"));
	}
	else Check(false, TEXT("zone composition moves the highest-cost exhausted card to hand and modifies that live instance"));

	if (bCompositionalParsed && CompositionalBeat.Choices[1].Reward.CreatedCards.Num() == 1)
	{
		const FCardData InkRule = CompositionalBeat.Choices[1].Reward.CreatedCards[0];
		FCardData TestSpell;
		TestSpell.Id = TEXT("test_ink_spell"); TestSpell.Name = TEXT("试墨"); TestSpell.Type = TEXT("spell");
		TestSpell.Rarity = TEXT("common"); TestSpell.Cost = 0;
		FCardEffect Noop; Noop.Action = TEXT("none"); TestSpell.Effects.Add(Noop);
		UCombatEngine* InkCombat = NewObject<UCombatEngine>();
		InkCombat->RegisterRuntimePlayerContent({InkRule, TestSpell}, {});
		FDeckCard InkDeckCard; InkDeckCard.CardId = InkRule.Id;
		FDeckCard SpellDeckCard; SpellDeckCard.CardId = TestSpell.Id;
		const bool bInkStarted = InkCombat->StartCombat(
			{InkDeckCard, SpellDeckCard, SpellDeckCard, SpellDeckCard}, TestEnemies, {}, 74, 74, 0, 20260813, 0);
		int32 InkIndex = InkCombat->Hand.IndexOfByPredicate([&InkRule](const FCardInstance& Card)
			{ return Card.Data.Id == InkRule.Id; });
		bool bInkResolved = bInkStarted && InkIndex != INDEX_NONE && InkCombat->PlayCard(InkIndex, 0);
		for (int32 Play = 0; Play < 3 && bInkResolved; ++Play)
		{
			const int32 SpellIndex = InkCombat->Hand.IndexOfByPredicate([](const FCardInstance& Card)
				{ return Card.Data.Id == TEXT("test_ink_spell"); });
			bInkResolved = SpellIndex != INDEX_NONE && InkCombat->PlayCard(SpellIndex, 0);
		}
		int32 SpellCopiesInHand = 0;
		for (const FCardInstance& Card : InkCombat->Hand)
			if (Card.Data.Id == TEXT("test_ink_spell")) ++SpellCopiesInHand;
		Check(bInkResolved && SpellCopiesInHand == 1,
			TEXT("combat-local variable threshold fires once after three matching event tags and copies last played card"));
	}
	else Check(false, TEXT("combat-local variable threshold fires once after three matching event tags and copies last played card"));

	UE_LOG(LogTemp, Display, TEXT("========== RESULT: %d passed, %d failed =========="), Passed, Failed);
	return Failed == 0 ? 0 : 1;
}
