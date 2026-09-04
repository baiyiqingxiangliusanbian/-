#include "AscendOriginalCardTestCommandlet.h"

#include "Combat/CombatEngine.h"
#include "AscendPlayerController.h"
#include "CardScriptCompiler.h"
#include "Dom/JsonObject.h"
#include "InfiniteNarrativeService.h"
#include "PrivateReasoningPrefillLoader.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/StringConv.h"
#include "NarrativePromptManager.h"
#include "RunManager.h"
#include "GameDataLibrary.h"
#include "UI/CardVisualResolver.h"
#include "NarrativeGuidanceRegressionTests.h"
#include "OpeningRelicRandomRegressionTests.h"
#include "HAL/PlatformTime.h"
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

	FString MakeOpeningWordingContent(const TArray<FString>& RelicIds,
		const TArray<FString>& Texts, bool bAddUnauthorizedField = false)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Choices;
		for (int32 Index = 0; Index < FMath::Min(RelicIds.Num(), Texts.Num()); ++Index)
		{
			TSharedPtr<FJsonObject> Choice = MakeShared<FJsonObject>();
			Choice->SetStringField(TEXT("choice_id"), RelicIds[Index]);
			Choice->SetStringField(TEXT("text"), Texts[Index]);
			if (bAddUnauthorizedField && Index == 0) Choice->SetStringField(TEXT("next"), TEXT("combat"));
			Choices.Add(MakeShared<FJsonValueObject>(Choice));
		}
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
	FCardData BlockConversionCard;
	FString BlockConversionFeedback;
	Check(Service->TestCardForgeCandidateForAutomationTest(TEXT(
		"name: 罡尽开门\nrarity: uncommon\ntype: talisman\ncost: 1\n"
		"play: damage 1 enemy scale self_block factor 1\n"
		"play: self_block -> target_status:vulnerable consume\n"
		"upgrade_play: damage 3 enemy scale self_block factor 1\n"
		"upgrade_play: self_block -> target_status:vulnerable consume"),
		12, BlockConversionCard, BlockConversionFeedback)
		&& BlockConversionCard.Effects.Num() == 2
		&& BlockConversionCard.Effects[0].ScaleBy == TEXT("self_block")
		&& BlockConversionCard.Effects[1].Destination == TEXT("target_status:vulnerable")
		&& BlockConversionCard.Effects[1].bConsumeSource
		&& BlockConversionCard.Description.Contains(TEXT("易伤"))
		&& BlockConversionCard.Description.Contains(TEXT("消耗来源")),
		TEXT("CardScript faithfully implements damage first, then consumes all block into vulnerable"));
	FCardData MissingStatusCard;
	FString MissingStatusFeedback;
	Check(!Service->TestCardForgeCandidateForAutomationTest(TEXT(
		"name: 猜测毒性\nrarity: uncommon\ncost: 1\nplay: apply_status 1"),
		12, MissingStatusCard, MissingStatusFeedback)
		&& MissingStatusFeedback.Contains(TEXT("缺少明确 status"))
		&& !MissingStatusFeedback.Contains(TEXT("PASS")),
		TEXT("CardScript rejects an unspecified status instead of silently guessing poison"));
	FString AgentAction;
	FString AgentIntendedText;
	FString AgentScript;
	FString AgentImplementationCheck;
	FString AgentPowerCheck;
	FString AgentContent;
	FString AgentProtocolError;
	Check(Service->ParseCardForgeAgentStepForAutomationTest(MakeTransportResponse(TEXT(
		"{\"action\":\"try_card\",\"intended_text\":\"寒针刺破旧伤，使毒意沿霜痕蔓延\","
		"\"script\":\"name: 霜痕针\\nrarity: uncommon\\ncost: 1\\nplay: damage 6\\nplay: poison 1\"}")),
		AgentAction, AgentIntendedText, AgentScript, AgentImplementationCheck, AgentPowerCheck,
		AgentContent, AgentProtocolError)
		&& AgentAction == TEXT("try_card") && AgentScript.Contains(TEXT("霜痕针"))
		&& AgentScript.Contains(TEXT("poison 1")) && AgentIntendedText.Contains(TEXT("旧伤")),
		TEXT("card forge agent protocol extracts its story design target and executable script"));
	FString LegacyAgentAction;
	FString LegacyAgentIntendedText;
	FString LegacyAgentScript;
	FString LegacyImplementationCheck;
	FString LegacyPowerCheck;
	FString LegacyAgentContent;
	FString LegacyAgentError;
	Check(Service->ParseCardForgeAgentStepForAutomationTest(MakeTransportResponse(TEXT(
		"{\"action\":\"finish_card\",\"script\":\"name: 旧路兼容\\nplay: block 5\"}")),
		LegacyAgentAction, LegacyAgentIntendedText, LegacyAgentScript,
		LegacyImplementationCheck, LegacyPowerCheck, LegacyAgentContent, LegacyAgentError)
		&& LegacyAgentAction == TEXT("try_card") && LegacyAgentScript.Contains(TEXT("旧路兼容")),
		TEXT("legacy finish actions carrying a script remain compatible with try_card"));
	FString ReviewAction;
	FString ReviewIntendedText;
	FString ReviewScript;
	FString ReviewImplementationCheck;
	FString ReviewPowerCheck;
	FString ReviewContent;
	FString ReviewError;
	Check(Service->ParseCardForgeAgentStepForAutomationTest(MakeTransportResponse(TEXT(
		"{\"action\":\"accept_card\","
		"\"implementation_check\":\"真实卡面逐项对应预期，没有额外状态或代价\","
		"\"power_check\":\"低值：无罡气时无收益；常见值：六点罡气形成合理转换；上限值：高罡气爆发但会失去全部防御\"}")),
		ReviewAction, ReviewIntendedText, ReviewScript, ReviewImplementationCheck, ReviewPowerCheck,
		ReviewContent, ReviewError)
		&& ReviewAction == TEXT("accept_card")
		&& ReviewImplementationCheck.Contains(TEXT("没有额外"))
		&& ReviewPowerCheck.Contains(TEXT("低值")) && ReviewPowerCheck.Contains(TEXT("常见值"))
		&& ReviewPowerCheck.Contains(TEXT("上限值")),
		TEXT("card forge protocol carries explicit implementation fidelity and power review before acceptance"));
	FCardData ToolTestCard;
	FString ToolTestFeedback;
	const double DryRunStartedAt = FPlatformTime::Seconds();
	const bool bDryRunPassed = Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 霜痕针\nrarity: uncommon\ncost: 1\nexhaust\nplay: damage 6\nplay: poison 1"),
		12, ToolTestCard, ToolTestFeedback);
	UE_LOG(LogTemp, Display, TEXT("[CardForgePerf] local dry-run elapsed=%.3fms"),
		(FPlatformTime::Seconds() - DryRunStartedAt) * 1000.0);
	Check(bDryRunPassed
		&& ToolTestCard.Effects.Num() == 2 && ToolTestFeedback.StartsWith(TEXT("PASS："))
		&& ToolTestFeedback.Contains(TEXT("无头战斗 smoke test")),
		TEXT("try_card compiles and plays base plus upgrade through a lightweight headless combat smoke test"));
	FCardData DuplicateNameCard;
	FString DuplicateNameFeedback;
	Check(!Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 基础剑诀\nrarity: common\ncost: 1\nplay: damage 7"),
		12, DuplicateNameCard, DuplicateNameFeedback)
		&& DuplicateNameFeedback.Contains(TEXT("卡名"))
		&& DuplicateNameFeedback.Contains(TEXT("重复")),
		TEXT("try_card repairs duplicate names that would collide with an existing card definition"));
	FCardData DuplicateEffectCard;
	FString DuplicateEffectFeedback;
	Check(!Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 双重空转\nrarity: uncommon\ncost: 1\nplay: gain_spirit 1\nplay: gain_spirit 1"),
		12, DuplicateEffectCard, DuplicateEffectFeedback)
		&& DuplicateEffectFeedback.Contains(TEXT("完全重复")),
		TEXT("try_card repairs mechanically identical duplicate effects instead of accepting stitched output"));
	FCardData UnreachableCounterCard;
	FString UnreachableCounterFeedback;
	Check(!Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 无源刻度\nrarity: uncommon\ncost: 1\nplay: damage 5 if counter_at_least:2"),
		12, UnreachableCounterCard, UnreachableCounterFeedback)
		&& UnreachableCounterFeedback.Contains(TEXT("计数永远不会增长")),
		TEXT("try_card repairs counter gates that have no runtime counter source"));
	FCardData UnreachableVariableCard;
	FString UnreachableVariableFeedback;
	Check(!Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 空墨借势\nrarity: uncommon\ncost: 1\ncounter: on_sword_play\n"
			"play: damage 4 scale var:ink factor 2"),
		12, UnreachableVariableCard, UnreachableVariableFeedback)
		&& UnreachableVariableFeedback.Contains(TEXT("var:ink"))
		&& UnreachableVariableFeedback.Contains(TEXT("永远只会读到 0")),
		TEXT("try_card repairs custom variables that are read without any runtime write source"));
	FCardData ReachableVariableCard;
	FString ReachableVariableFeedback;
	Check(Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 墨潮回锋\nrarity: uncommon\ncost: 1\n"
			"card_played: var:void -> var:ink add value 1 if event_tag_is:sword limit 3\n"
			"play: damage 4 scale var:ink factor 2"),
		12, ReachableVariableCard, ReachableVariableFeedback)
		&& ReachableVariableFeedback.StartsWith(TEXT("PASS：")),
		TEXT("try_card accepts a custom variable when the card supplies an executable write source"));
	FCardData DenseCard;
	FString DenseCardFeedback;
	Check(!Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 万象冗卷\nrarity: rare\ncost: 2\nplay: damage 4\nplay: block 4\n"
			"play: poison 1\nplay: weak 1\nplay: draw 1"),
		12, DenseCard, DenseCardFeedback)
		&& DenseCardFeedback.Contains(TEXT("卡面过密"))
		&& DenseCardFeedback.Contains(TEXT("4 个效果")),
		TEXT("try_card repairs dense cards that cannot be scanned clearly on the card face"));
	FCardData ExplicitUpgradeCard;
	FString ExplicitUpgradeFeedback;
	Check(Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 霜痕进境\nrarity: uncommon\ncost: 1\nplay: damage 6\n"
			"upgrade_cost: 0\nupgrade_play: damage 9"),
		12, ExplicitUpgradeCard, ExplicitUpgradeFeedback)
		&& ExplicitUpgradeCard.UpgradedCost == 0
		&& ExplicitUpgradeCard.UpgradedEffects.Num() == 1
		&& ExplicitUpgradeCard.UpgradedEffects[0].Value == 9,
		TEXT("CardScript lets the agent implement an explicit executable upgrade"));
	FCardData UnchangedUpgradeCard;
	FString UnchangedUpgradeFeedback;
	Check(!Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 原地踏步\nrarity: uncommon\ncost: 1\nplay: damage 5\nupgrade_play: damage 5"),
		12, UnchangedUpgradeCard, UnchangedUpgradeFeedback)
		&& UnchangedUpgradeFeedback.Contains(TEXT("升级版与基础版完全相同")),
		TEXT("try_card repairs explicit upgrades that do not materially change the card"));
	FCardData FailedToolCard;
	FString FailedToolFeedback;
	Check(!Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 坏脚本\nplay: lunar_beam 99"), 12, FailedToolCard, FailedToolFeedback)
		&& FailedToolFeedback.StartsWith(TEXT("ERROR：")),
		TEXT("card forge test feedback returns a compact actionable error for unsupported mechanics"));
	FCardData RecursiveRuleCard;
	FString RecursiveRuleFeedback;
	Check(!Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 无尽回音\nrarity: rare\ncost: 2\ndamage_dealt: damage 3"),
		12, RecursiveRuleCard, RecursiveRuleFeedback)
		&& RecursiveRuleFeedback.Contains(TEXT("可能再次触发自身"))
		&& RecursiveRuleFeedback.Contains(TEXT("limit")),
		TEXT("try_card blocks only an immediate unbounded self-triggering combat rule"));
	FCardData BoundedRuleCard;
	FString BoundedRuleFeedback;
	Check(Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 一度回音\nrarity: rare\ncost: 2\ndamage_dealt: damage 3 limit 1"),
		12, BoundedRuleCard, BoundedRuleFeedback)
		&& BoundedRuleCard.Effects.Num() == 1,
		TEXT("the same expressive combat rule passes once it has a finite trigger limit"));
	FCardData SoftWarningCard;
	FString SoftWarningFeedback;
	Check(Service->TestCardForgeCandidateForAutomationTest(
		TEXT("name: 轻灵双式\nrarity: uncommon\ncost: 0\nplay: damage 4\nplay: block 4"),
		12, SoftWarningCard, SoftWarningFeedback)
		&& SoftWarningFeedback.StartsWith(TEXT("PASS："))
		&& SoftWarningFeedback.Contains(TEXT("WARN：")),
		TEXT("soft balance concerns remain warnings and never add another agent turn"));
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
	const FString BuiltInHistorySentinel = TEXT("synthetic post-history sentinel");
	BuiltInNarrativeContext.ChatHistory.Add({TEXT("assistant"), BuiltInHistorySentinel + TEXT(" A")});
	BuiltInNarrativeContext.ChatHistory.Add({TEXT("user"), BuiltInHistorySentinel + TEXT(" B")});
	FString BuiltInNarrativeDiagnostic;
	const TArray<FNarrativePromptMessage> BuiltInNarrativeMessages = bBuiltInNarrativeLoaded
		? FNarrativePromptManager::BuildMessages(BuiltInNarrativePreset, BuiltInNarrativeContext,
			BuiltInNarrativeDiagnostic) : TArray<FNarrativePromptMessage>();
	const int32 LastBuiltInHistoryIndex = BuiltInNarrativeMessages.FindLastByPredicate(
		[&BuiltInHistorySentinel](const FNarrativePromptMessage& Message)
		{
			return Message.Content.Contains(BuiltInHistorySentinel);
		});
	const int32 RouteExamplesIndex = BuiltInNarrativeMessages.IndexOfByPredicate(
		[](const FNarrativePromptMessage& Message)
		{
			return Message.Content.Contains(TEXT("抽到hp=-10"))
				&& Message.Content.Contains(TEXT("抽到card_forge"))
				&& Message.Content.Contains(TEXT("不得加冒号、解释、效果、费用、脚本"));
		});
	Check(bBuiltInNarrativeLoaded && BuiltInNarrativeMessages.Num() > 0
		&& RouteExamplesIndex != INDEX_NONE && LastBuiltInHistoryIndex != INDEX_NONE
		&& RouteExamplesIndex > LastBuiltInHistoryIndex,
		TEXT("single-pass director receives post-history examples for explaining pre-rolled engine routes"));

	FString RouteWorldBookJson;
	const bool bRouteWorldBookLoaded = FFileHelper::LoadFileToString(RouteWorldBookJson,
		*(FPaths::ProjectContentDir() / TEXT("Data/rp_route_worldbook.json")));
	FNarrativePromptBuildContext RouteWorldBookContext = BuiltInNarrativeContext;
	RouteWorldBookContext.WorldBookJson = RouteWorldBookJson;
	RouteWorldBookContext.GameState = TEXT("A=[[route.combat]]\nB=[[route.relic_reward]]\nC=[[route.upgrade]] [[route.gold_loss]]");
	const TArray<FNarrativePromptMessage> RouteWorldBookMessages = bBuiltInNarrativeLoaded && bRouteWorldBookLoaded
		? FNarrativePromptManager::BuildMessages(BuiltInNarrativePreset, RouteWorldBookContext,
			BuiltInNarrativeDiagnostic) : TArray<FNarrativePromptMessage>();
	const auto HasRouteRule = [&RouteWorldBookMessages](const FString& RuleName)
	{
		return RouteWorldBookMessages.ContainsByPredicate([&RuleName](const FNarrativePromptMessage& Message)
			{ return Message.Content.Contains(RuleName); });
	};
	Check(bRouteWorldBookLoaded && HasRouteRule(TEXT("引擎路由导演协议"))
		&& HasRouteRule(TEXT("战斗页映射")) && HasRouteRule(TEXT("固有法宝映射"))
		&& HasRouteRule(TEXT("升级页映射"))
		&& HasRouteRule(TEXT("灵石损失映射")) && !HasRouteRule(TEXT("商店页映射")),
		TEXT("pre-rolled engine route markers activate only the matching narrative worldbook entries"));

	FInfiniteNarrativeRequestContext RoutePlanContext;
	RoutePlanContext.Cycle = 3;
	RoutePlanContext.HP = 42;
	RoutePlanContext.MaxHP = 70;
	RoutePlanContext.Gold = 80;
	RoutePlanContext.DeckSize = 12;
	RoutePlanContext.UpgradeableCardCount = 8;
	RoutePlanContext.AvailableFixedRelicIds = {TEXT("soul_jade"), TEXT("spirit_pearl"), TEXT("qi_bell")};
	RoutePlanContext.FixedRelicIdToName.Add(TEXT("soul_jade"), TEXT("养魂玉"));
	RoutePlanContext.FixedRelicIdToName.Add(TEXT("spirit_pearl"), TEXT("聚灵珠"));
	RoutePlanContext.FixedRelicIdToName.Add(TEXT("qi_bell"), TEXT("罡气钟"));
	TArray<TMap<FString, int32>> SlotRouteCounts;
	SlotRouteCounts.SetNum(3);
	int32 BaseGoodRoutes = 0;
	int32 LuckyGoodRoutes = 0;
	bool bSawFixedRelicRouteWithPayload = false;
	bool bSawPureContinueRoute = false;
	bool bCombatPlansValid = true;
	TSet<int32> SeenCombatCounts;
	const TSet<FString> GoodRoutes = {
		TEXT("card_forge"), TEXT("relic_reward"), TEXT("reward"), TEXT("shop"),
		TEXT("rest"), TEXT("upgrade"), TEXT("heal"), TEXT("gain_gold")
	};
	for (int32 Seed = 0; Seed < 1200; ++Seed)
	{
		TArray<FString> Payloads;
		TArray<FInfiniteEnemySpec> CombatPlans;
		RoutePlanContext.RouteRewardBias = 0.f;
		const TArray<FString> BaseRoutes = Service->PlanRoutesForAutomationTest(
			RoutePlanContext, Seed, &Payloads, &CombatPlans);
		for (int32 Slot = 0; Slot < BaseRoutes.Num(); ++Slot)
		{
			++SlotRouteCounts[Slot].FindOrAdd(BaseRoutes[Slot]);
			if (BaseRoutes[Slot] == TEXT("continue_rp")) bSawPureContinueRoute = true;
			if (GoodRoutes.Contains(BaseRoutes[Slot])) ++BaseGoodRoutes;
			if (BaseRoutes[Slot] == TEXT("relic_reward") && Payloads.IsValidIndex(Slot)
				&& RoutePlanContext.AvailableFixedRelicIds.Contains(Payloads[Slot]))
				bSawFixedRelicRouteWithPayload = true;
			if (BaseRoutes[Slot] == TEXT("combat"))
			{
				if (!CombatPlans.IsValidIndex(Slot))
				{
					bCombatPlansValid = false;
					continue;
				}
				const FInfiniteEnemySpec& Plan = CombatPlans[Slot];
				SeenCombatCounts.Add(Plan.Count);
				const bool bScaleMatchesCount = (Plan.Count == 1
					&& Plan.HPScale >= 0.90f && Plan.HPScale <= 1.25f
					&& Plan.IntentScale >= 0.90f && Plan.IntentScale <= 1.15f)
					|| (Plan.Count == 2 && Plan.HPScale >= 0.65f && Plan.HPScale <= 0.90f
						&& Plan.IntentScale >= 0.70f && Plan.IntentScale <= 0.92f)
					|| (Plan.Count == 3 && Plan.HPScale >= 0.48f && Plan.HPScale <= 0.68f
						&& Plan.IntentScale >= 0.55f && Plan.IntentScale <= 0.78f);
				bCombatPlansValid &= !Plan.TemplateId.IsEmpty() && !Plan.Name.IsEmpty() && bScaleMatchesCount;
			}
		}
		RoutePlanContext.RouteRewardBias = 1.f;
		const TArray<FString> LuckyRoutes = Service->PlanRoutesForAutomationTest(RoutePlanContext, Seed);
		for (const FString& Route : LuckyRoutes) if (GoodRoutes.Contains(Route)) ++LuckyGoodRoutes;
	}
	const int32 ForgeSpread = FMath::Max3(
		SlotRouteCounts[0].FindRef(TEXT("card_forge")),
		SlotRouteCounts[1].FindRef(TEXT("card_forge")),
		SlotRouteCounts[2].FindRef(TEXT("card_forge")))
		- FMath::Min3(
			SlotRouteCounts[0].FindRef(TEXT("card_forge")),
			SlotRouteCounts[1].FindRef(TEXT("card_forge")),
			SlotRouteCounts[2].FindRef(TEXT("card_forge")));
	Check(!bSawPureContinueRoute && bSawFixedRelicRouteWithPayload
		&& ForgeSpread < 90 && LuckyGoodRoutes > BaseGoodRoutes,
		TEXT("A/B/C share one actionable weighted pool, fixed relic payloads are reachable, and relic luck shifts outcomes upward"));
	Check(bCombatPlansValid && SeenCombatCounts.Num() == 3,
		TEXT("combat routes pre-roll a named local enemy plus count-aware HP and intent scaling"));

	URunManager* PendingGroupRun = NewObject<URunManager>();
	const bool bPendingGroupStarted = PendingGroupRun->StartNewRun(20260815, true);
	FInfiniteEnemySpec PendingGroupSpec;
	PendingGroupSpec.TemplateId = TEXT("mountain_imp");
	PendingGroupSpec.Name = TEXT("山魈");
	PendingGroupSpec.Count = 3;
	PendingGroupSpec.HPScale = 0.60f;
	PendingGroupSpec.IntentScale = 0.65f;
	const FString PendingGroupEnemyId = bPendingGroupStarted
		? PendingGroupRun->RegisterInfiniteEnemy(PendingGroupSpec, 2) : FString();
	const FEnemyData* PendingGroupTemplate = PendingGroupRun->GetEnemyData(PendingGroupEnemyId);
	TArray<FEnemyData> PendingGroup;
	if (PendingGroupTemplate) PendingGroup.Init(*PendingGroupTemplate, 3);
	PendingGroupRun->SavePendingInfiniteCombat(EMapNodeType::Combat, PendingGroup, 2,
		TEXT("三只山魈封住山道。"), {}, {});
	FNodeEncounter RestoredGroup;
	FString RestoredGroupSummary;
	TArray<FDeckCard> RestoredGroupCards;
	TArray<FString> RestoredGroupRelics;
	const bool bPendingGroupRestored = PendingGroupRun->RestorePendingInfiniteCombat(RestoredGroup,
		RestoredGroupSummary, RestoredGroupCards, RestoredGroupRelics);
	Check(bPendingGroupRestored && RestoredGroup.EnemyIds.Num() == 3
		&& RestoredGroup.EnemyIds[0] == RestoredGroup.EnemyIds[1]
		&& RestoredGroupSummary.Contains(TEXT("三只山魈")),
		TEXT("pending infinite combat preserves the complete multi-enemy encounter for title-screen recovery"));
	UCombatEngine* PendingGroupCombat = NewObject<UCombatEngine>();
	if (PendingGroupTemplate) PendingGroupCombat->RegisterRuntimeEnemies({*PendingGroupTemplate});
	const bool bPendingGroupCombatStarted = PendingGroupCombat->StartCombat(PendingGroupRun->State.Deck,
		RestoredGroup.EnemyIds, PendingGroupRun->State.RelicIds, PendingGroupRun->State.MaxHP,
		PendingGroupRun->State.HP, RestoredGroup.EnemyHPBonus, 20260815, 0);
	Check(bPendingGroupCombatStarted && PendingGroupCombat->Enemies.Num() == 3
		&& PendingGroupCombat->Enemies[0].State.MaxHP == PendingGroupCombat->Enemies[2].State.MaxHP,
		TEXT("combat engine instantiates every member of a restored locally-scaled enemy group"));
	PendingGroupRun->ClearPendingInfiniteCombat();

	TArray<FDeckCard> DuplicateTargetDeck;
	for (int32 CardIndex = 0; CardIndex < 5; ++CardIndex)
	{
		FDeckCard StrikeCard;
		StrikeCard.CardId = TEXT("strike");
		DuplicateTargetDeck.Add(StrikeCard);
	}
	UCombatEngine* DuplicateTargetCombat = NewObject<UCombatEngine>();
	const bool bDuplicateTargetStarted = DuplicateTargetCombat->StartCombat(
		DuplicateTargetDeck, {TEXT("mountain_imp"), TEXT("mountain_imp")}, {}, 74, 74, 0, 20260831, 0);
	if (bDuplicateTargetStarted && DuplicateTargetCombat->Enemies.Num() == 2)
	{
		// Reproduce the presentation bug: the left copy is already dead while the
		// player explicitly attacks the live, identically named copy on the right.
		DuplicateTargetCombat->Enemies[0].State.HP = 0;
		const int32 RightEnemyHPBefore = DuplicateTargetCombat->Enemies[1].State.HP;
		const int32 StrikeIndex = DuplicateTargetCombat->Hand.IndexOfByPredicate([](const FCardInstance& Card)
			{ return Card.Data.Id == TEXT("strike"); });
		const bool bRightEnemyHit = StrikeIndex != INDEX_NONE
			&& DuplicateTargetCombat->PlayCard(StrikeIndex, 1);
		const FEnemyDamageEvent* LastDamageEvent = DuplicateTargetCombat->EnemyDamageEvents.Num() > 0
			? &DuplicateTargetCombat->EnemyDamageEvents.Last() : nullptr;
		Check(bRightEnemyHit && DuplicateTargetCombat->Enemies[0].State.HP == 0
			&& DuplicateTargetCombat->Enemies[1].State.HP < RightEnemyHPBefore
			&& LastDamageEvent && LastDamageEvent->EnemyIndex == 1 && LastDamageEvent->Damage > 0,
			TEXT("damage presentation keeps the selected enemy index when a dead left enemy has the same name"));
	}
	else
	{
		Check(false, TEXT("damage presentation keeps the selected enemy index when a dead left enemy has the same name"));
	}

	const FString MissingResultContent = TEXT(
		"{\"schema_version\":\"2.0-direct-route\",\"scene\":{\"title\":\"断桥\","
		"\"narration\":\"断桥另一端传来铁索声。\",\"dialogue\":\"\",\"messages\":[]},\"choices\":["
		"{\"text\":\"踏上断桥\",\"next\":\"combat\",\"encounter\":{\"template_id\":\"mountain_imp\"}},"
		"{\"text\":\"检查桥墩\",\"result_summary\":\"你找到桥下暗门。\",\"next\":\"shop\"},"
		"{\"text\":\"斩断铁索\",\"result_summary\":\"铁索坠入深谷。\",\"next\":\"reward\"}]}");
	FInfiniteNarrativeBeat MissingResultBeat;
	FString MissingResultError;
	Check(!Service->ParseResponseForAutomationTest(MakeTransportResponse(MissingResultContent),
		MissingResultBeat, MissingResultError) && MissingResultError.Contains(TEXT("result_summary")),
		TEXT("writer responses missing a choice result are rejected instead of receiving a local placeholder settlement"));

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
	Check(ParsedCard.Description.Contains(TEXT("当前计数至少为1"))
		&& ParsedCard.Description.Contains(TEXT("当前罡气"))
		&& !ParsedCard.Description.Contains(TEXT("self_block")),
		TEXT("card-face description localizes executable dynamic values without leaking engine field names"));
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
	const FCardData* OneSwordVisual = Run->GetCardData(TEXT("one_sword"));
	Check(OneSwordVisual && OneSwordVisual->Visual.Animation == TEXT("greatsword")
		&& OneSwordVisual->Visual.Sound == TEXT("sword_heavy")
		&& OneSwordVisual->Visual.Intensity >= 10.f,
		TEXT("One Sword registers the dedicated heavy greatsword impact family"));
	const FCardData* MyriadSwordVisual = Run->GetCardData(TEXT("wan_jian_gui_zong"));
	Check(MyriadSwordVisual && MyriadSwordVisual->Visual.Animation == TEXT("myriad_swords")
		&& MyriadSwordVisual->Visual.Sound == TEXT("sword_flurry")
		&& MyriadSwordVisual->Visual.Count >= 7,
		TEXT("Myriad Swords registers the staggered multi-hit visual and audio family"));
	TArray<FCardData> StaticVisualCards;
	FString StaticVisualLoadError;
	const bool bStaticVisualCardsLoaded = UGameDataLibrary::LoadCards(StaticVisualCards, StaticVisualLoadError);
	bool bAllStaticCardsHaveAnimation = bStaticVisualCardsLoaded && StaticVisualCards.Num() >= 55;
	bool bAllStaticCardsHaveSound = bAllStaticCardsHaveAnimation;
	TSet<FString> StaticAnimationFamilies;
	for (const FCardData& StaticCard : StaticVisualCards)
	{
		FCardInstance Instance;
		Instance.Data = StaticCard;
		const FString Animation = AscendCardVisual::ResolveAnimation(Instance);
		const FString Sound = AscendCardVisual::ResolveSound(Instance, Animation);
		bAllStaticCardsHaveAnimation &= !Animation.IsEmpty() && Animation != TEXT("none");
		bAllStaticCardsHaveSound &= !Sound.IsEmpty() && Sound != TEXT("none");
		StaticAnimationFamilies.Add(Animation);
	}
	Check(bAllStaticCardsHaveAnimation,
		TEXT("every static card routes to a non-placeholder animation family"));
	Check(bAllStaticCardsHaveSound,
		TEXT("every static card routes to an audible feedback family"));
	Check(StaticAnimationFamilies.Num() >= 12,
		TEXT("the static library spans at least twelve distinct visual families"));
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
	AffinityUpdate.Target = TEXT("test_companion");
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
	Check(Run->State.RPRelationships.Num() == 1
		&& Run->State.RPRelationships[0].CharacterId == TEXT("test_companion")
		&& Run->State.RPRelationships[0].Affinity == 22
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

	// ---------------------------------------------------------------------
	// Finite authored openings + first-combat narrative prefetch regression
	// ---------------------------------------------------------------------
	const TSet<FString> ExpectedOpeningDiagnostics = {
		TEXT("authored_opening:qinghe_spirit_stone"),
		TEXT("authored_opening:lingpan_last_chime"),
		TEXT("authored_opening:ancestral_house_lamp"),
		TEXT("authored_opening:third_furnace_watch"),
		TEXT("authored_opening:father_debt_box")
	};
	URunManager* OpeningRun = NewObject<URunManager>();
	OpeningRun->SetPersistentAuthoredContentEnabledForAutomationTest(false);
	const bool bOpeningRunStarted = OpeningRun->StartNewRun(20260903, true);
	const TArray<FString> OpeningRelicIds = OpeningRun->RollInitialRelicChoices(3);
	TArray<FRelicData> OpeningRelics;
	TSet<FString> OpeningRelicIdSet;
	for (const FString& RelicId : OpeningRelicIds)
	{
		if (const FRelicData* Relic = OpeningRun->GetRelicData(RelicId))
		{
			OpeningRelics.Add(*Relic);
			OpeningRelicIdSet.Add(RelicId);
		}
	}
	const bool bOpeningRelicsDistinctAndValid = bOpeningRunStarted
		&& OpeningRelicIds.Num() == 3 && OpeningRelics.Num() == 3
		&& OpeningRelicIdSet.Num() == 3;
	Check(bOpeningRelicsDistinctAndValid,
		TEXT("opening relic pool rolls three real, mutually distinct relic definitions"));
	if (OpeningRelicIds.Num() > 0)
	{
		OpeningRun->State.RelicIds.Add(OpeningRelicIds[0]);
		const TArray<FString> RerolledRelicIds = OpeningRun->RollInitialRelicChoices(3);
		TSet<FString> RerolledRelicIdSet;
		for (const FString& RelicId : RerolledRelicIds) RerolledRelicIdSet.Add(RelicId);
		Check(!RerolledRelicIds.Contains(OpeningRelicIds[0])
			&& RerolledRelicIdSet.Num() == RerolledRelicIds.Num(),
			TEXT("initial relic reroll excludes already-owned relics and still deduplicates its pool"));
	}
	TSet<FString> OpeningDiagnostics;
	TSet<FString> OpeningCombatTemplates;
	TSet<FString> OpeningFallbackLeads;
	int32 OpeningCombatChoiceCount = 0;
	int32 OpeningContinueChoiceCount = 0;
	bool bOpeningChoicesLeadOnly = true;
	bool bFiniteOpeningsValid = true;
	for (int32 OpeningIndex = 0; OpeningIndex < 5; ++OpeningIndex)
	{
		const FInfiniteNarrativeBeat Opening =
			AAscendPlayerController::BuildAuthoredOpeningForAutomationTest(OpeningIndex, OpeningRelics);
		OpeningDiagnostics.Add(Opening.Diagnostic);
		bFiniteOpeningsValid = bFiniteOpeningsValid
			&& !Opening.bError
			&& !Opening.Title.IsEmpty()
			&& !Opening.Narration.IsEmpty()
			&& Opening.Choices.Num() == 3
				&& ExpectedOpeningDiagnostics.Contains(Opening.Diagnostic);
		if (Opening.Choices.IsValidIndex(0)) OpeningFallbackLeads.Add(Opening.Choices[0].Text);
		TSet<FString> OpeningFallbackChoices;
		TSet<FString> OpeningChoiceRelicIds;
		FString SharedResultSummary;
		FString SharedConsequenceIntent;
		FString SharedResolvedImpactKind;
		FString SharedResolvedImpactObject;
		FString SharedSettlementKey;
		FString SharedStatePatchJson;
		FInfiniteEnemySpec SharedEnemy;
		bool bSharedRewardShape = false;
		bool bHasSharedOpeningShape = false;
		for (const FInfiniteNarrativeChoice& Choice : Opening.Choices)
		{
			const FString ChoiceRelicId = Choice.Reward.RelicIds.Num() == 1
				? Choice.Reward.RelicIds[0] : TEXT("");
			bFiniteOpeningsValid = bFiniteOpeningsValid
				&& !Choice.Text.IsEmpty()
				&& !Choice.ResultSummary.IsEmpty()
				&& !Choice.ConsequenceIntent.IsEmpty()
					&& Choice.ResolvedImpactKind == TEXT("acquire_relic")
				&& Choice.bResolvedImpactCompleted
				&& !Choice.bResolvedImpactPersistent
				&& Choice.SettlementKey.StartsWith(TEXT("authored_opening_relic:"))
				&& Choice.bGrantRewardBeforeCombat
					&& Choice.Reward.RelicIds.Num() == 1
					&& OpeningRelicIdSet.Contains(ChoiceRelicId);
			if (const FRelicData* Relic = OpeningRun->GetRelicData(ChoiceRelicId))
			{
				bFiniteOpeningsValid = bFiniteOpeningsValid
					&& !Choice.Text.Contains(Relic->Id)
					&& (Relic->Name.IsEmpty() || !Choice.Text.Contains(Relic->Name))
					&& (Relic->Description.IsEmpty() || !Choice.Text.Contains(Relic->Description.Left(180)))
					&& UInfiniteNarrativeService::IsOpeningChoiceWordingSafe(Choice.Text,
						Relic->Id, Relic->Name, Relic->Description, Relic->Rarity);
			}
			const int32 FallbackChoiceCountBefore = OpeningFallbackChoices.Num();
			OpeningFallbackChoices.Add(Choice.Text);
			if (OpeningFallbackChoices.Num() == FallbackChoiceCountBefore)
				bOpeningChoicesLeadOnly = false;
			if (!bHasSharedOpeningShape)
			{
				SharedResultSummary = Choice.ResultSummary;
				SharedConsequenceIntent = Choice.ConsequenceIntent;
				SharedResolvedImpactKind = Choice.ResolvedImpactKind;
				SharedResolvedImpactObject = Choice.ResolvedImpactObject;
				SharedSettlementKey = Choice.SettlementKey;
				SharedStatePatchJson = Choice.StatePatchJson;
				SharedEnemy = Choice.Enemy;
				bSharedRewardShape = Choice.Reward.Cards.Num() == 0
					&& Choice.Reward.CreatedCards.Num() == 0
					&& Choice.Reward.CreatedRelics.Num() == 0
					&& Choice.Reward.RemovedCardIds.Num() == 0
					&& Choice.Reward.RemovedRelicIds.Num() == 0
					&& Choice.Reward.GoldChange == 0 && Choice.Reward.HPChange == 0;
				bHasSharedOpeningShape = true;
			}
			else
			{
				bFiniteOpeningsValid = bFiniteOpeningsValid
					&& Choice.ResultSummary == SharedResultSummary
					&& Choice.ConsequenceIntent == SharedConsequenceIntent
					&& Choice.ResolvedImpactKind == SharedResolvedImpactKind
					&& Choice.ResolvedImpactObject == SharedResolvedImpactObject
					&& Choice.SettlementKey == SharedSettlementKey
					&& Choice.StatePatchJson == SharedStatePatchJson
					&& Choice.bResolvedImpactCompleted
					&& Choice.bResolvedImpactPersistent == false
					&& Choice.Reward.Cards.Num() == 0
					&& Choice.Reward.CreatedCards.Num() == 0
					&& Choice.Reward.CreatedRelics.Num() == 0
					&& Choice.Reward.RemovedCardIds.Num() == 0
					&& Choice.Reward.RemovedRelicIds.Num() == 0
					&& Choice.Reward.GoldChange == 0 && Choice.Reward.HPChange == 0
					&& bSharedRewardShape
					&& Choice.Enemy.TemplateId == SharedEnemy.TemplateId
					&& Choice.Enemy.Name == SharedEnemy.Name
					&& Choice.Enemy.FactionId == SharedEnemy.FactionId
					&& Choice.Enemy.Story == SharedEnemy.Story
					&& FMath::IsNearlyEqual(Choice.Enemy.HPScale, SharedEnemy.HPScale)
					&& FMath::IsNearlyEqual(Choice.Enemy.IntentScale, SharedEnemy.IntentScale)
					&& Choice.Enemy.Count == SharedEnemy.Count
					&& Choice.Enemy.Tier == SharedEnemy.Tier
					&& Choice.Enemy.Abilities == SharedEnemy.Abilities
					&& Choice.Enemy.AbilityDesc == SharedEnemy.AbilityDesc;
			}
			if (Choice.Next.Equals(TEXT("combat"), ESearchCase::IgnoreCase))
			{
				++OpeningCombatChoiceCount;
				OpeningCombatTemplates.Add(Choice.Enemy.TemplateId);
				OpeningChoiceRelicIds.Add(ChoiceRelicId);
				bFiniteOpeningsValid = bFiniteOpeningsValid
					&& !Choice.Enemy.TemplateId.IsEmpty()
					&& !Choice.Enemy.Name.IsEmpty()
					&& !Choice.Enemy.FactionId.IsEmpty()
					&& !Choice.Enemy.Story.IsEmpty()
					&& Choice.Enemy.Count == 1;
			}
			else
			{
				++OpeningContinueChoiceCount;
				bFiniteOpeningsValid = false;
			}
		}
		bFiniteOpeningsValid = bFiniteOpeningsValid && OpeningChoiceRelicIds.Num() == 3
			&& OpeningFallbackChoices.Num() == 3;
	}
	Check(bFiniteOpeningsValid && OpeningDiagnostics.Num() == 5
		&& OpeningDiagnostics.Num() == ExpectedOpeningDiagnostics.Num()
		&& OpeningCombatChoiceCount == 15 && OpeningCombatTemplates.Num() == 5
		&& OpeningFallbackLeads.Num() == 5
		&& bOpeningChoicesLeadOnly
		&& OpeningContinueChoiceCount == 0,
		TEXT("finite authored openings keep five distinct lead-only fallback sets, share one consequence/enemy, and route every choice to combat"));

	TArray<FString> OpeningWordingIds = {OpeningRelicIds[0], OpeningRelicIds[1], OpeningRelicIds[2]};
	TArray<FString> OpeningWordingTexts = {TEXT("先护住渡口的退路"), TEXT("先查清灵息的来处"), TEXT("先把法器藏入行囊")};
	TArray<FString> OpeningWordingNames;
	TArray<FString> OpeningWordingDescriptions;
	TArray<FString> OpeningWordingRarities;
	for (const FRelicData& Relic : OpeningRelics)
	{
		OpeningWordingNames.Add(Relic.Name);
		OpeningWordingDescriptions.Add(Relic.Description);
		OpeningWordingRarities.Add(Relic.Rarity);
	}
	TArray<FString> ReorderedIds = {OpeningWordingIds[2], OpeningWordingIds[0], OpeningWordingIds[1]};
	TArray<FString> ReorderedTexts = {OpeningWordingTexts[2], OpeningWordingTexts[0], OpeningWordingTexts[1]};
	TArray<FString> ParsedOpeningWording;
	FString OpeningWordingError;
	const bool bOpeningWordingBound = Service->ParseOpeningWordingForAutomationTest(
		MakeTransportResponse(MakeOpeningWordingContent(ReorderedIds, ReorderedTexts)),
		OpeningWordingIds, ParsedOpeningWording, OpeningWordingError);
	Check(bOpeningWordingBound && ParsedOpeningWording.Num() == 3
		&& ParsedOpeningWording[0] == OpeningWordingTexts[0]
		&& ParsedOpeningWording[1] == OpeningWordingTexts[1]
		&& ParsedOpeningWording[2] == OpeningWordingTexts[2],
		TEXT("opening wording parser restores the locked relic order even when model choices arrive reordered"));
	TArray<FString> MismatchedIds = ReorderedIds;
	MismatchedIds[1] = TEXT("relic_not_locked");
	Check(!Service->ParseOpeningWordingForAutomationTest(
		MakeTransportResponse(MakeOpeningWordingContent(MismatchedIds, ReorderedTexts)),
		OpeningWordingIds, ParsedOpeningWording, OpeningWordingError),
		TEXT("opening wording parser rejects a model attempt to replace a locked relic binding"));
	Check(!Service->ParseOpeningWordingForAutomationTest(
		MakeTransportResponse(MakeOpeningWordingContent(OpeningWordingIds, OpeningWordingTexts, true)),
		OpeningWordingIds, ParsedOpeningWording, OpeningWordingError),
		TEXT("opening wording parser rejects route/reward fields instead of letting copy mutate engine facts"));
	TArray<FString> LeakyOpeningTexts = {
		FString::Printf(TEXT("选择【%s】并查看它的效果"), *OpeningWordingNames[0]),
		TEXT("先观察现场的动静，再决定下一步"),
		TEXT("贴着墙根留出退路")
	};
	Check(!Service->ParseOpeningWordingForAutomationTest(
		MakeTransportResponse(MakeOpeningWordingContent(OpeningWordingIds, LeakyOpeningTexts)),
		OpeningWordingIds, OpeningWordingNames, OpeningWordingDescriptions, OpeningWordingRarities,
		ParsedOpeningWording, OpeningWordingError),
		TEXT("opening wording parser rejects exact relic identity/effect leakage and uses the local fallback path"));
	TArray<FString> CrossLeakyOpeningTexts = {
		TEXT("先观察现场的动静，再决定下一步"),
		FString::Printf(TEXT("顺手辨认%s的踪迹"), *OpeningWordingNames[0]),
		TEXT("贴着墙根留出退路")
	};
	Check(!Service->ParseOpeningWordingForAutomationTest(
		MakeTransportResponse(MakeOpeningWordingContent(OpeningWordingIds, CrossLeakyOpeningTexts)),
		OpeningWordingIds, OpeningWordingNames, OpeningWordingDescriptions, OpeningWordingRarities,
		ParsedOpeningWording, OpeningWordingError),
		TEXT("opening wording parser rejects a choice leaking another locked relic identity"));
	const TArray<FString> DuplicateOpeningTexts = {
		TEXT("先观察现场的动静，再决定下一步"), TEXT("先观察现场的动静，再决定下一步"),
		TEXT("先观察现场的动静，再决定下一步")
	};
	Check(!Service->ParseOpeningWordingForAutomationTest(
		MakeTransportResponse(MakeOpeningWordingContent(OpeningWordingIds, DuplicateOpeningTexts)),
		OpeningWordingIds, OpeningWordingNames, OpeningWordingDescriptions, OpeningWordingRarities,
		ParsedOpeningWording, OpeningWordingError),
		TEXT("opening wording parser rejects three identical model leads so local fallback remains distinguishable"));
	const FString FormerDisplay = FString::Printf(TEXT("%s\n法器【%s】\n%s"),
		*OpeningWordingTexts[0], *OpeningWordingNames[0], *OpeningWordingDescriptions[0]);
	const FString SafeRestoredLead = AAscendPlayerController::FilterAuthoredOpeningChoiceForAutomationTest(
		TEXT("third_furnace_watch"), 0, OpeningRelics, FormerDisplay);
	const FString UnsafeLegacyLead = FString::Printf(TEXT("你将获得%s"), *OpeningWordingNames[1]);
	const FString SafeFallbackLead = AAscendPlayerController::FilterAuthoredOpeningChoiceForAutomationTest(
		TEXT("third_furnace_watch"), 1, OpeningRelics, UnsafeLegacyLead);
	Check(SafeRestoredLead == OpeningWordingTexts[0]
		&& !SafeRestoredLead.Contains(OpeningWordingNames[0])
		&& !SafeRestoredLead.Contains(OpeningWordingDescriptions[0])
		&& !SafeFallbackLead.Contains(OpeningWordingNames[0])
		&& !SafeFallbackLead.Contains(OpeningWordingDescriptions[0])
		&& SafeFallbackLead != UnsafeLegacyLead,
		TEXT("restored opening text strips legacy relic details and replaces unsafe legacy leads without rerolling"));
	const FInfiniteNarrativeBeat RepeatOpening =
		AAscendPlayerController::BuildAuthoredOpeningForAutomationTest(0, OpeningRelics);
	bool bOpeningRetryKeepsBindings = RepeatOpening.Choices.Num() == 3;
	for (int32 Index = 0; Index < 3 && bOpeningRetryKeepsBindings; ++Index)
	{
		bOpeningRetryKeepsBindings = RepeatOpening.Choices[Index].Reward.RelicIds
			== AAscendPlayerController::BuildAuthoredOpeningForAutomationTest(0, OpeningRelics)
				.Choices[Index].Reward.RelicIds;
	}
	Check(bOpeningRetryKeepsBindings,
		TEXT("rebuilding wording for the same locked opening does not reroll or swap relic bindings"));

	FString SyntheticPrefill;
	int32 SyntheticPrefillBytes = 0;
	const bool bSyntheticPrefillLoaded = FPrivateReasoningPrefillLoader::ExtractJsonFieldForAutomationTest(
		TEXT("{\"preset\":{\"reasoning_chain_prefill\":\"synthetic opaque payload\",\"api_key\":\"must-not-read\"}}"),
		TEXT("preset.reasoning_chain_prefill"), SyntheticPrefill, SyntheticPrefillBytes);
	Check(bSyntheticPrefillLoaded && SyntheticPrefill == TEXT("synthetic opaque payload")
		&& SyntheticPrefillBytes == FTCHARToUTF8(*SyntheticPrefill).Length(),
		TEXT("hidden prefill loader extracts only the explicitly allow-listed synthetic field and reports opaque byte length"));
	Check(!FPrivateReasoningPrefillLoader::ExtractJsonFieldForAutomationTest(
		TEXT("{\"preset\":{\"api_key\":\"must-not-read\"}}"),
		TEXT("preset.api_key"), SyntheticPrefill, SyntheticPrefillBytes),
		TEXT("hidden prefill loader refuses sensitive fields and cannot collect API credentials"));

	FRunState PendingOpeningState;
	PendingOpeningState.bRunActive = true;
	PendingOpeningState.bInfiniteNarrativeMode = true;
	PendingOpeningState.bInfiniteOpeningPending = true;
	PendingOpeningState.PendingInfiniteOpeningIndex = 3;
	PendingOpeningState.PendingInfiniteOpeningId = TEXT("third_furnace_watch");
	PendingOpeningState.PendingInfiniteOpeningRelicIds = OpeningWordingIds;
	PendingOpeningState.PendingInfiniteOpeningChoiceTexts = OpeningWordingTexts;
	PendingOpeningState.bInfiniteOpeningWordingReady = true;
	const TSharedPtr<FJsonObject> PendingOpeningJson =
		FJsonObjectConverter::UStructToJsonObject(PendingOpeningState);
	FRunState PendingOpeningRoundtrip;
	const bool bPendingOpeningRoundtrip = PendingOpeningJson.IsValid()
		&& FJsonObjectConverter::JsonObjectToUStruct(PendingOpeningJson.ToSharedRef(), &PendingOpeningRoundtrip);
	Check(bPendingOpeningRoundtrip
		&& PendingOpeningRoundtrip.bInfiniteOpeningPending
		&& PendingOpeningRoundtrip.PendingInfiniteOpeningIndex == 3
		&& PendingOpeningRoundtrip.PendingInfiniteOpeningId == TEXT("third_furnace_watch")
		&& PendingOpeningRoundtrip.PendingInfiniteOpeningRelicIds == OpeningWordingIds
		&& PendingOpeningRoundtrip.PendingInfiniteOpeningChoiceTexts == OpeningWordingTexts
		&& PendingOpeningRoundtrip.bInfiniteOpeningWordingReady,
		TEXT("pending opening state round-trips through the actual FRunState JSON serializer"));
	TSharedPtr<FJsonObject> LegacyOpeningJson = MakeShared<FJsonObject>();
	LegacyOpeningJson->SetBoolField(TEXT("bRunActive"), true);
	LegacyOpeningJson->SetBoolField(TEXT("bInfiniteNarrativeMode"), true);
	FRunState LegacyOpeningState;
	const bool bLegacyOpeningParsed = FJsonObjectConverter::JsonObjectToUStruct(
		LegacyOpeningJson.ToSharedRef(), &LegacyOpeningState);
	Check(bLegacyOpeningParsed && !LegacyOpeningState.bInfiniteOpeningPending
		&& LegacyOpeningState.PendingInfiniteOpeningIndex == INDEX_NONE
		&& LegacyOpeningState.PendingInfiniteOpeningId.IsEmpty()
		&& LegacyOpeningState.PendingInfiniteOpeningRelicIds.Num() == 0
		&& LegacyOpeningState.PendingInfiniteOpeningChoiceTexts.Num() == 0,
		TEXT("legacy saves without pending-opening fields keep safe empty defaults"));
	OpeningRun->SavePendingInfiniteOpening(3, TEXT("third_furnace_watch"), OpeningRelicIds,
		OpeningWordingTexts, true);
	OpeningRun->ClearPendingInfiniteOpening();
	Check(!OpeningRun->HasPendingInfiniteOpening()
		&& !OpeningRun->State.bInfiniteOpeningPending
		&& OpeningRun->State.PendingInfiniteOpeningRelicIds.Num() == 0
		&& OpeningRun->State.PendingInfiniteOpeningChoiceTexts.Num() == 0,
		TEXT("clearing a selected opening removes its pending record and completed wording"));

	bool bOpeningRelicsAwardBeforeCombat = true;
	for (int32 RelicChoiceIndex = 0; RelicChoiceIndex < 3; ++RelicChoiceIndex)
	{
		URunManager* AwardRun = NewObject<URunManager>();
		AwardRun->SetPersistentAuthoredContentEnabledForAutomationTest(false);
		const bool bAwardRunStarted = AwardRun->StartNewRun(20260910 + RelicChoiceIndex, true);
		const FInfiniteNarrativeBeat AwardBeat =
			AAscendPlayerController::BuildAuthoredOpeningForAutomationTest(0, OpeningRelics);
		const FInfiniteNarrativeChoice& AwardChoice = AwardBeat.Choices[RelicChoiceIndex];
		const bool bCommitted = bAwardRunStarted
			&& AwardRun->TryCommitNarrativeSettlement(AwardChoice.SettlementKey);
		if (bCommitted) AwardRun->ApplyInfiniteNarrativeReward(AwardChoice.Reward);
		const int32 RelicCountAfterAward = AwardRun->State.RelicIds.Num();
		bOpeningRelicsAwardBeforeCombat = bOpeningRelicsAwardBeforeCombat
			&& bCommitted
			&& AwardChoice.Reward.RelicIds.Num() == 1
			&& AwardRun->State.RelicIds.Contains(AwardChoice.Reward.RelicIds[0])
			&& !AwardRun->TryCommitNarrativeSettlement(AwardChoice.SettlementKey)
			&& AwardRun->State.RelicIds.Num() == RelicCountAfterAward;
	}
	Check(bOpeningRelicsAwardBeforeCombat,
		TEXT("each opening relic choice uses the settlement gate and grants its selected relic before combat without duplicate claims"));

	// The production code uses these pure gates before touching UMG/HTTP state.
	// Simulate the two calls that can race at combat start/victory: setting the
	// in-flight bit on the first call must make the second call a no-op.
	bool bPrefetchRequestInFlight = false;
	bool bPrefetchReady = false;
	int32 PrefetchStartCount = 0;
	if (AAscendPlayerController::ShouldStartCombatNarrativePrefetch(
		true, true, bPrefetchRequestInFlight, bPrefetchReady))
	{
		++PrefetchStartCount;
		bPrefetchRequestInFlight = true;
	}
	if (AAscendPlayerController::ShouldStartCombatNarrativePrefetch(
		true, true, bPrefetchRequestInFlight, bPrefetchReady)) ++PrefetchStartCount;
	Check(PrefetchStartCount == 1
		&& !AAscendPlayerController::ShouldStartCombatNarrativePrefetch(true, true, true, false)
		&& !AAscendPlayerController::ShouldStartCombatNarrativePrefetch(true, true, false, true)
		&& !AAscendPlayerController::ShouldStartCombatNarrativePrefetch(false, true, false, false),
		TEXT("combat prefetch gate is one-shot while in flight or cached, and never runs outside active infinite RP"));
	Check(AAscendPlayerController::ShouldPrefetchAtCombatStart(true, true)
		&& !AAscendPlayerController::ShouldPrefetchAtCombatStart(false, true)
		&& AAscendPlayerController::ShouldPrefetchAtCombatStart(false, false),
		TEXT("first combat always selects mode B while later combats honor the A/B setting"));
	const int32 ForcedRPIndexA = AAscendPlayerController::ChooseFreeRPForcedChoiceIndexForAutomationTest(
		3, 20260904, 17);
	const int32 ForcedRPIndexB = AAscendPlayerController::ChooseFreeRPForcedChoiceIndexForAutomationTest(
		3, 20260904, 17);
	Check(AAscendPlayerController::IsFreeRPInputAvailableForAutomationTest(true, false, false, false)
		&& !AAscendPlayerController::IsFreeRPInputAvailableForAutomationTest(true, true, false, false)
		&& !AAscendPlayerController::IsFreeRPInputAvailableForAutomationTest(true, false, true, false)
		&& !AAscendPlayerController::IsFreeRPInputAvailableForAutomationTest(false, false, false, false)
		&& AAscendPlayerController::IsFreeRPForcedContinueAvailableForAutomationTest(true, true, false)
		&& !AAscendPlayerController::IsFreeRPForcedContinueAvailableForAutomationTest(true, true, true)
		&& !AAscendPlayerController::IsFreeRPForcedContinueAvailableForAutomationTest(true, false, false)
		&& ForcedRPIndexA == ForcedRPIndexB && ForcedRPIndexA >= 0 && ForcedRPIndexA < 3,
		TEXT("free RP opens only in its eligible phase, pauses after the response, and locks one replayable direction for the explicit continue"));
	FString RPDisplayDiagnostic;
	Check(AAscendPlayerController::ValidateRPNarrativeDisplayForAutomationTest(RPDisplayDiagnostic),
		TEXT("RP display keeps CRLF paragraph boundaries, two-CJK indentation and Unicode clusters intact"));
	FString RPStreamMerged;
	bool bRPStreamRebuilt = false;
	Check(AAscendPlayerController::MergeRPStreamTextForAutomationTest(
		TEXT("　　尾段已显示"), TEXT("　　尾段已显示，最终句。"), RPStreamMerged, bRPStreamRebuilt)
		&& RPStreamMerged == TEXT("　　尾段已显示，最终句。") && !bRPStreamRebuilt,
		TEXT("stream terminal flush appends an authoritative suffix without rebuilding a matching prefix"));
	Check(AAscendPlayerController::MergeRPStreamTextForAutomationTest(
		TEXT("旧的部分前缀"), TEXT("修正后的完整尾文"), RPStreamMerged, bRPStreamRebuilt)
		&& RPStreamMerged == TEXT("修正后的完整尾文") && bRPStreamRebuilt,
		TEXT("stream terminal rewrite marks only the mismatching segment for local rebuild"));
	const FString NormalizedRPDialogue = AAscendPlayerController::NormalizeRPDialogueForAutomationTest(
		TEXT("角色：\r\n“最终对白”"));
	const FString RPIdentityA = AAscendPlayerController::MakeRPDialogueIdentityKeyForAutomationTest(
		TEXT("角色"), TEXT("portrait_a"), TEXT("neutral"));
	const FString RPIdentityB = AAscendPlayerController::MakeRPDialogueIdentityKeyForAutomationTest(
		TEXT("角色"), TEXT("portrait_b"), TEXT("neutral"));
	Check(NormalizedRPDialogue == TEXT("角色：\n最终对白") && RPIdentityA != RPIdentityB,
		TEXT("stream dialogue normalization unifies CRLF/quotes and portrait identity changes cannot reuse the old row"));
	Check(AAscendPlayerController::ShouldAutoScrollRPStreamForAutomationTest(0.f, 100.f)
		&& !AAscendPlayerController::ShouldAutoScrollRPStreamForAutomationTest(0.f, 240.f),
		TEXT("terminal stream follows the end only when the reader was already within the bottom threshold"));

	// UI/HTTP callback order is intentionally not constructed in this headless
	// commandlet. Keep a small wiring sentinel for the production callbacks, while
	// the one-shot decisions above exercise the actual gate implementation.
	FString ControllerNarrativeSource;
	const bool bControllerSourceLoaded = FFileHelper::LoadFileToString(
		ControllerNarrativeSource,
		*(FPaths::ProjectDir() / TEXT("Source/AscendSpire/Core/AscendPlayerController_InfiniteNarrative.cpp")));
	Check(bControllerSourceLoaded
		&& ControllerNarrativeSource.Find(TEXT("StartCombatNarrativePrefetch(false)")) != INDEX_NONE
		&& ControllerNarrativeSource.Find(TEXT("ConsumeCombatNarrativePrefetch()")) != INDEX_NONE
		&& ControllerNarrativeSource.Find(TEXT("bDiscardCombatNarrativePrefetch")) != INDEX_NONE,
		TEXT("combat prefetch production wiring retains mode-B start, cached-beat consumption and defeat discard callback"));

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

	// ---------------------------------------------------------------------
	// Difficulty ledger + run-local cultivation regression
	// ---------------------------------------------------------------------
	bool bDifficultyMonotonic = true;
	bool bTemplateVariationPreserved = false;
	for (int32 Seed = 0; Seed < 12; ++Seed)
	{
		FCombatDifficultyProfile Previous;
		for (int32 Battle = 1; Battle <= 24; ++Battle)
		{
			FEnemyData Template;
			Template.Id = FString::Printf(TEXT("difficulty_fixture_%d_%d"), Seed, Battle);
			Template.Name = TEXT("难度测试敌");
			Template.MaxHP = 8 + ((Seed * 13 + Battle * 7) % 95);
			Template.Tier = (Battle % 11 == 0) ? TEXT("boss")
				: ((Battle % 5 == 0) ? TEXT("elite") : TEXT("normal"));
			FEnemyIntent Attack;
			Attack.Action = TEXT("attack");
			Attack.Value = 3 + ((Seed + Battle) % 10);
			Attack.Weight = 1;
			Template.Intents.Add(Attack);
			if ((Seed + Battle) % 3 == 0) Template.Abilities.Add(TEXT("thorns:2"));
			if ((Seed + Battle) % 4 == 0) Template.Abilities.Add(TEXT("regen:2"));
			const float TemplateThreat = UCombatEngine::ComputeTemplateThreat({Template});
			if (Battle == 1) bTemplateVariationPreserved = TemplateThreat > 0.f;
			const bool bElite = Template.Tier == TEXT("elite");
			const bool bBoss = Template.Tier == TEXT("boss");
			const FCombatDifficultyProfile Current = UCombatEngine::BuildDifficultyProfile(
				Battle, Previous.ThreatScore, Previous.HPScale, Previous.IntentScale,
				{Template}, bElite, bBoss);
			FString ProgressionError;
			bDifficultyMonotonic = bDifficultyMonotonic
				&& UCombatEngine::ValidateDifficultyProgression(Previous, Current, ProgressionError)
				&& (Battle == 1 || Current.HPScale >= Previous.HPScale * 1.08f)
				&& (Battle == 1 || Current.IntentScale >= Previous.IntentScale * 1.05f)
				&& (Battle == 1 || Current.ThreatScore >= Previous.ThreatScore * 1.08f);
			Previous = Current;
		}
	}
	Check(bDifficultyMonotonic && bTemplateVariationPreserved,
		TEXT("difficulty curve is strictly monotonic across varied templates, seeds, elite and Boss steps"));

	FRunState LegacyDefaults;
	FString LegacyDefaultsError;
	const bool bLegacyDefaultsValid = FCultivationSystem::Validate(
		LegacyDefaults.Cultivation, LegacyDefaultsError);
	Check(LegacyDefaults.BattleSerial == 0
		&& LegacyDefaults.PreviousBattleThreatScore == 0.f
		&& LegacyDefaults.PreviousBattleHPScale == 0.f
		&& LegacyDefaults.PreviousBattleIntentScale == 0.f
		&& LegacyDefaults.Cultivation.RealmIndex == FCultivationSystem::MinQiRealm
		&& LegacyDefaults.Cultivation.CultivationPoints == 0
		&& !LegacyDefaults.Cultivation.bAtBottleneck
		&& bLegacyDefaultsValid,
		TEXT("new difficulty and cultivation fields have safe defaults for old saves"));

	FCultivationState CultivationFixture;
	FCultivationSystem::Initialize(CultivationFixture);
	const bool bChoiceBlockedBeforeVictory = !FCultivationSystem::ApplyChoice(
		CultivationFixture, ECultivationChoice::Insight).bApplied;
	bool bVictoryCultivationApplied = true;
	for (int32 VictoryIndex = 0; VictoryIndex < 20 && !CultivationFixture.bAtBottleneck; ++VictoryIndex)
	{
		const FCultivationVictoryResult Victory = FCultivationSystem::GrantCultivationFromVictory(
			CultivationFixture, 120.f);
		bVictoryCultivationApplied = bVictoryCultivationApplied && Victory.bApplied
			&& Victory.CultivationGained > 0;
		while (CultivationFixture.PendingChoiceCount > 0)
		{
			const ECultivationChoice Choice = CultivationFixture.PendingChoiceCount % 3 == 0
				? ECultivationChoice::BodyTempering
				: (CultivationFixture.PendingChoiceCount % 3 == 1
					? ECultivationChoice::QiAbsorption : ECultivationChoice::Insight);
			bVictoryCultivationApplied = bVictoryCultivationApplied
				&& FCultivationSystem::ApplyChoice(CultivationFixture, Choice).bApplied;
		}
	}
	FString CultivationError;
	const bool bReachedBottleneck = CultivationFixture.RealmIndex == FCultivationSystem::MaxQiRealm
		&& CultivationFixture.bAtBottleneck
		&& CultivationFixture.CultivationPoints >= FCultivationSystem::GetRealmThreshold(
			FCultivationSystem::MaxQiRealm);
	const bool bPrematureBreakthroughRejected = !FCultivationSystem::TryBreakthroughAfterBoss(
		CultivationFixture, CultivationError);
	FString BossMessage;
	const bool bBossBreakthrough = FCultivationSystem::MarkBossDefeated(CultivationFixture, BossMessage)
		&& CultivationFixture.bFoundationEstablished
		&& CultivationFixture.RealmIndex == FCultivationSystem::FoundationRealm
		&& FCultivationSystem::IsFoundation(CultivationFixture);
	Check(bChoiceBlockedBeforeVictory && bVictoryCultivationApplied
		&& CultivationFixture.TotalCultivationFromVictories > 0
		&& bReachedBottleneck && bPrematureBreakthroughRejected
		&& bBossBreakthrough && FCultivationSystem::Validate(CultivationFixture, CultivationError),
		TEXT("cultivation choices cross Qi 1-9 thresholds, stop at bottleneck, and establish Foundation only after Boss"));

	URunManager* DifficultyRun = NewObject<URunManager>();
	const bool bDifficultyRunStarted = DifficultyRun->StartNewRun(20260902);
	const FCombatDifficultyProfile RunProfileA = bDifficultyRunStarted
		? DifficultyRun->BeginCombatDifficulty({TEXT("mountain_imp")}, EMapNodeType::Combat)
		: FCombatDifficultyProfile();
	const int32 SerialAfterFirst = DifficultyRun->State.BattleSerial;
	const float ThreatAfterFirst = DifficultyRun->State.PreviousBattleThreatScore;
	const FCombatDifficultyProfile RunProfileB = bDifficultyRunStarted
		? DifficultyRun->BeginCombatDifficulty({TEXT("mountain_imp")}, EMapNodeType::Elite)
		: FCombatDifficultyProfile();
	// Simulate a failed attempt without writing the commandlet's fixture to the
	// player's meta-save; the ledger itself must remain untouched by defeat.
	const int32 SerialBeforeFailure = DifficultyRun->State.BattleSerial;
	const float ThreatBeforeFailure = DifficultyRun->State.PreviousBattleThreatScore;
	DifficultyRun->State.bRunActive = false;
	UCombatEngine* ProfileCombat = NewObject<UCombatEngine>();
	const bool bProfileCombatStarted = bDifficultyRunStarted
		&& ProfileCombat->StartCombatWithDifficulty(DifficultyRun->State.Deck,
			{TEXT("mountain_imp")}, DifficultyRun->State.RelicIds,
			DifficultyRun->State.MaxHP, DifficultyRun->State.HP, 0, 20260903, RunProfileB);
	Check(bDifficultyRunStarted && RunProfileA.BattleSerial == 1
		&& RunProfileB.BattleSerial == SerialAfterFirst + 1
		&& RunProfileB.ThreatScore > ThreatAfterFirst
		&& SerialBeforeFailure == RunProfileB.BattleSerial
		&& ThreatBeforeFailure == RunProfileB.ThreatScore
		&& DifficultyRun->State.BattleSerial == SerialBeforeFailure
		&& DifficultyRun->State.PreviousBattleThreatScore == ThreatBeforeFailure
		&& bProfileCombatStarted
		&& ProfileCombat->DifficultyProfile.BattleSerial == RunProfileB.BattleSerial
		&& ProfileCombat->DifficultyProfile.HPScale == RunProfileB.HPScale
		&& ProfileCombat->Enemies.Num() == 1 && ProfileCombat->Enemies[0].State.MaxHP > 0,
		TEXT("run difficulty serial persists, rises through an elite step, and is not lowered by defeat"));

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

	// ---------------------------------------------------------------------
	// Authoritative combat damage regression
	//
	// These fixtures intentionally go through StartCombat -> StartPlayerTurn ->
	// PlayCard -> DealDamageToEnemy.  They do not call a parallel preview helper,
	// so a passing result proves the actual settlement path rather than only the
	// arithmetic in a test-side replica.  Strength is an additive per-hit value;
	// explicit card multipliers/repeats remain separate mechanics.
	// ---------------------------------------------------------------------
	{
		FEnemyData DamageRegressionEnemyA;
		DamageRegressionEnemyA.Id = TEXT("combat_regression_enemy_a");
		DamageRegressionEnemyA.Name = TEXT("伤害回归敌甲");
		DamageRegressionEnemyA.MaxHP = 200;
		DamageRegressionEnemyA.Tier = TEXT("normal");
		FEnemyData DamageRegressionEnemyB = DamageRegressionEnemyA;
		DamageRegressionEnemyB.Id = TEXT("combat_regression_enemy_b");
		DamageRegressionEnemyB.Name = TEXT("伤害回归敌乙");
		const TArray<FEnemyData> DamageRegressionEnemies = {
			DamageRegressionEnemyA, DamageRegressionEnemyB};

		auto StartDamageRegressionCombat =
			[&DamageRegressionEnemies](const TArray<FDeckCard>& Deck,
				const TArray<FCardData>& RuntimeCards, const TArray<FString>& EnemyIds,
				int32 Seed) -> UCombatEngine*
		{
			UCombatEngine* Fixture = NewObject<UCombatEngine>();
			Fixture->RegisterRuntimeEnemies(DamageRegressionEnemies);
			Fixture->RegisterRuntimePlayerContent(RuntimeCards, {});
			return Fixture->StartCombat(Deck, EnemyIds, {}, 100, 100, 0, Seed, 0)
				? Fixture : nullptr;
		};
		auto AddRegressionCard = [](const TCHAR* Id, const TCHAR* Name,
			const TCHAR* Action, int32 Value, const TCHAR* Target, int32 Times = 1)
		{
			FCardData Card;
			Card.Id = Id;
			Card.Name = Name;
			Card.Type = TEXT("zhaoshi");
			Card.Class = TEXT("sword");
			Card.Rarity = TEXT("common");
			Card.Cost = 0;
			FCardEffect Effect;
			Effect.Action = Action;
			Effect.Value = Value;
			Effect.Target = Target;
			Effect.Times = Times;
			Card.Effects.Add(Effect);
			return Card;
		};
		auto DeckFor = [](const FString& CardId)
		{
			FDeckCard DeckCard;
			DeckCard.CardId = CardId;
			return TArray<FDeckCard>{DeckCard};
		};
		auto FindAndPlay = [](UCombatEngine* Fixture, const FString& CardId,
			int32 TargetEnemyIndex = 0)
		{
			if (!Fixture) return false;
			const int32 HandIndex = Fixture->Hand.IndexOfByPredicate(
				[&CardId](const FCardInstance& Card) { return Card.Data.Id == CardId; });
			return HandIndex != INDEX_NONE && Fixture->PlayCard(HandIndex, TargetEnemyIndex);
		};

		const FCardData FlatDamageCard = AddRegressionCard(
			TEXT("combat_regression_flat_damage"), TEXT("平直伤害"),
			TEXT("damage"), 10, TEXT("enemy"));
		const FString EnemyAId = DamageRegressionEnemyA.Id;
		const FString EnemyBId = DamageRegressionEnemyB.Id;

		UCombatEngine* ZeroStrengthCombat = StartDamageRegressionCombat(
			DeckFor(FlatDamageCard.Id), {FlatDamageCard}, {EnemyAId}, 20260920);
		const int32 ZeroStrengthBefore = ZeroStrengthCombat && ZeroStrengthCombat->Enemies.Num() == 1
			? ZeroStrengthCombat->Enemies[0].State.HP : 0;
		const bool bZeroStrengthPlayed = FindAndPlay(ZeroStrengthCombat, FlatDamageCard.Id);
		Check(bZeroStrengthPlayed && ZeroStrengthCombat->Enemies[0].State.HP == ZeroStrengthBefore - 10
			&& ZeroStrengthCombat->EnemyDamageEvents.Num() == 1
			&& ZeroStrengthCombat->EnemyDamageEvents[0].Damage == 10,
			TEXT("combat damage baseline applies zero Strength as exactly the authored 10 damage"));

		UCombatEngine* FlatStrengthCombat = StartDamageRegressionCombat(
			DeckFor(FlatDamageCard.Id), {FlatDamageCard}, {EnemyAId}, 20260921);
		if (FlatStrengthCombat) FlatStrengthCombat->Player.AddStatus(TEXT("strength"), 3);
		const int32 FlatStrengthBefore = FlatStrengthCombat && FlatStrengthCombat->Enemies.Num() == 1
			? FlatStrengthCombat->Enemies[0].State.HP : 0;
		const bool bFlatStrengthPlayed = FindAndPlay(FlatStrengthCombat, FlatDamageCard.Id);
		Check(bFlatStrengthPlayed && FlatStrengthCombat->Enemies[0].State.HP == FlatStrengthBefore - 13
			&& FlatStrengthCombat->EnemyDamageEvents.Num() == 1
			&& FlatStrengthCombat->EnemyDamageEvents[0].Damage == 13,
			TEXT("one attack segment resolves 10 base plus 3 flat Strength exactly once (13)"));

		const FCardData MultiHitCard = AddRegressionCard(
			TEXT("combat_regression_multi_hit"), TEXT("三段伤害"),
			TEXT("damage"), 10, TEXT("enemy"), 3);
		UCombatEngine* MultiHitCombat = StartDamageRegressionCombat(
			DeckFor(MultiHitCard.Id), {MultiHitCard}, {EnemyAId}, 20260922);
		if (MultiHitCombat) MultiHitCombat->Player.AddStatus(TEXT("strength"), 3);
		const int32 MultiHitBefore = MultiHitCombat && MultiHitCombat->Enemies.Num() == 1
			? MultiHitCombat->Enemies[0].State.HP : 0;
		const bool bMultiHitPlayed = FindAndPlay(MultiHitCombat, MultiHitCard.Id);
		bool bEveryMultiHitIsFlat = bMultiHitPlayed && MultiHitCombat->EnemyDamageEvents.Num() == 3;
		if (bEveryMultiHitIsFlat)
		{
			for (const FEnemyDamageEvent& Event : MultiHitCombat->EnemyDamageEvents)
				bEveryMultiHitIsFlat = bEveryMultiHitIsFlat && Event.EnemyIndex == 0 && Event.Damage == 13;
		}
		Check(bEveryMultiHitIsFlat && MultiHitCombat->Enemies[0].State.HP == MultiHitBefore - 39,
			TEXT("three attack segments each add Strength once: (10+3) x 3 = 39"));

		const FCardData AreaDamageCard = AddRegressionCard(
			TEXT("combat_regression_area_damage"), TEXT("双目标伤害"),
			TEXT("damage_all"), 10, TEXT("all_enemies"));
		UCombatEngine* MultiTargetCombat = StartDamageRegressionCombat(
			DeckFor(AreaDamageCard.Id), {AreaDamageCard}, {EnemyAId, EnemyBId}, 20260923);
		if (MultiTargetCombat) MultiTargetCombat->Player.AddStatus(TEXT("strength"), 3);
		const int32 MultiTargetBeforeA = MultiTargetCombat && MultiTargetCombat->Enemies.Num() == 2
			? MultiTargetCombat->Enemies[0].State.HP : 0;
		const int32 MultiTargetBeforeB = MultiTargetCombat && MultiTargetCombat->Enemies.Num() == 2
			? MultiTargetCombat->Enemies[1].State.HP : 0;
		const bool bMultiTargetPlayed = FindAndPlay(MultiTargetCombat, AreaDamageCard.Id);
		bool bMultiTargetEvents = bMultiTargetPlayed && MultiTargetCombat->EnemyDamageEvents.Num() == 2;
		if (bMultiTargetEvents)
		{
			for (const FEnemyDamageEvent& Event : MultiTargetCombat->EnemyDamageEvents)
				bMultiTargetEvents = bMultiTargetEvents && Event.Damage == 13;
		}
		Check(bMultiTargetEvents && MultiTargetCombat->Enemies[0].State.HP == MultiTargetBeforeA - 13
			&& MultiTargetCombat->Enemies[1].State.HP == MultiTargetBeforeB - 13,
			TEXT("multi-target settlement applies the same flat Strength contribution independently to each enemy"));

		// A 30-point One Sword strike is intentionally synthetic: it isolates the
		// special strike action from its current card data while checking the exact
		// acceptance example 30 + Strength 3 = 33.
		const FCardData OneSwordThirtyCard = AddRegressionCard(
			TEXT("combat_regression_one_sword_thirty"), TEXT("一剑三十"),
			TEXT("one_sword_strike"), 30, TEXT("all_enemies"));
		UCombatEngine* OneSwordThirtyCombat = StartDamageRegressionCombat(
			DeckFor(OneSwordThirtyCard.Id), {OneSwordThirtyCard}, {EnemyAId}, 20260924);
		if (OneSwordThirtyCombat) OneSwordThirtyCombat->Player.AddStatus(TEXT("strength"), 3);
		const int32 OneSwordThirtyBefore = OneSwordThirtyCombat && OneSwordThirtyCombat->Enemies.Num() == 1
			? OneSwordThirtyCombat->Enemies[0].State.HP : 0;
		const bool bOneSwordThirtyPlayed = FindAndPlay(OneSwordThirtyCombat, OneSwordThirtyCard.Id);
		Check(bOneSwordThirtyPlayed && OneSwordThirtyCombat->Enemies[0].State.HP == OneSwordThirtyBefore - 33
			&& OneSwordThirtyCombat->EnemyDamageEvents.Num() == 1
			&& OneSwordThirtyCombat->EnemyDamageEvents[0].Damage == 33,
			TEXT("One Sword's already-computed 30 damage receives Strength +3 once, not as a multiplier (33)"));

		const FCardData OneSwordEnhancerCard = AddRegressionCard(
			TEXT("combat_regression_one_sword_enhancer"), TEXT("一剑强化测试"),
			TEXT("enhance_one_sword"), 2, TEXT("one_sword"));
		const FCardData OneSwordMultiplierCard = AddRegressionCard(
			TEXT("combat_regression_one_sword_multiplier"), TEXT("一剑重复测试"),
			TEXT("one_sword_multiply"), 0, TEXT("one_sword"));
		TArray<FDeckCard> OneSwordEnhanceDeck = DeckFor(OneSwordEnhancerCard.Id);
		FDeckCard OneSwordEnhanceSwordDeckCard;
		OneSwordEnhanceSwordDeckCard.CardId = TEXT("one_sword");
		OneSwordEnhanceDeck.Add(OneSwordEnhanceSwordDeckCard);
		UCombatEngine* OneSwordEnhanceCombat = StartDamageRegressionCombat(
			OneSwordEnhanceDeck, {OneSwordEnhancerCard}, {EnemyAId}, 20260925);
		if (OneSwordEnhanceCombat) OneSwordEnhanceCombat->Player.AddStatus(TEXT("strength"), 3);
		const bool bEnhancerPlayed = FindAndPlay(OneSwordEnhanceCombat, OneSwordEnhancerCard.Id);
		const int32 OneSwordEnhanceBefore = OneSwordEnhanceCombat && OneSwordEnhanceCombat->Enemies.Num() == 1
			? OneSwordEnhanceCombat->Enemies[0].State.HP : 0;
		const bool bOneSwordPlayed = bEnhancerPlayed && FindAndPlay(OneSwordEnhanceCombat, TEXT("one_sword"));
		Check(bOneSwordPlayed && OneSwordEnhanceCombat->GetOneSwordEnhance() == 2
			&& OneSwordEnhanceCombat->GetOneSwordDamage() == 14
			&& OneSwordEnhanceCombat->Enemies[0].State.HP == OneSwordEnhanceBefore - 17
			&& OneSwordEnhanceCombat->EnemyDamageEvents.Num() == 1
			&& OneSwordEnhanceCombat->EnemyDamageEvents[0].Damage == 17,
			TEXT("current One Sword contract uses base 8 + enhancement 2x3, then adds Strength 3 once (17)"));

		// Reusing an engine for a later encounter must not treat a prior combat's
		// enhancement ledger as a new multiplier or flat bonus.
		UCombatEngine* ReusedOneSwordCombat = OneSwordEnhanceCombat;
		const bool bReusedCombatStarted = ReusedOneSwordCombat
			&& ReusedOneSwordCombat->StartCombat(DeckFor(FString(TEXT("one_sword"))), {EnemyAId}, {},
				100, 100, 0, 20260927, 0);
		Check(bReusedCombatStarted && ReusedOneSwordCombat->GetOneSwordEnhance() == 0
			&& ReusedOneSwordCombat->GetOneSwordDamage() == 8,
			TEXT("starting a new combat clears the prior One Sword enhancement ledger"));

		TArray<FDeckCard> OneSwordRepeatDeck = DeckFor(OneSwordMultiplierCard.Id);
		FDeckCard OneSwordRepeatEnhanceDeckCard;
		OneSwordRepeatEnhanceDeckCard.CardId = OneSwordEnhancerCard.Id;
		OneSwordRepeatDeck.Add(OneSwordRepeatEnhanceDeckCard);
		FDeckCard OneSwordRepeatSwordDeckCard;
		OneSwordRepeatSwordDeckCard.CardId = TEXT("one_sword");
		OneSwordRepeatDeck.Add(OneSwordRepeatSwordDeckCard);
		UCombatEngine* OneSwordRepeatCombat = StartDamageRegressionCombat(
			OneSwordRepeatDeck, {OneSwordMultiplierCard, OneSwordEnhancerCard}, {EnemyAId}, 20260926);
		if (OneSwordRepeatCombat) OneSwordRepeatCombat->Player.AddStatus(TEXT("strength"), 3);
		const bool bMultiplierPlayed = FindAndPlay(OneSwordRepeatCombat, OneSwordMultiplierCard.Id);
		const bool bRepeatEnhancerPlayed = bMultiplierPlayed
			&& FindAndPlay(OneSwordRepeatCombat, OneSwordEnhancerCard.Id);
		const int32 OneSwordRepeatBefore = OneSwordRepeatCombat && OneSwordRepeatCombat->Enemies.Num() == 1
			? OneSwordRepeatCombat->Enemies[0].State.HP : 0;
		const bool bRepeatedOneSwordPlayed = bRepeatEnhancerPlayed
			&& FindAndPlay(OneSwordRepeatCombat, TEXT("one_sword"));
		bool bRepeatedEventsAreIndependent = bRepeatedOneSwordPlayed
			&& OneSwordRepeatCombat->EnemyDamageEvents.Num() == 2;
		if (bRepeatedEventsAreIndependent)
		{
			for (const FEnemyDamageEvent& Event : OneSwordRepeatCombat->EnemyDamageEvents)
				bRepeatedEventsAreIndependent = bRepeatedEventsAreIndependent && Event.Damage == 17;
		}
		Check(bRepeatedEventsAreIndependent && OneSwordRepeatCombat->Enemies[0].State.HP == OneSwordRepeatBefore - 34,
			TEXT("explicit One Sword repeat remains two independent 17-damage hits without multiplying Strength"));

		FStatusInstance NegativeStrength;
		NegativeStrength.Id = TEXT("strength");
		NegativeStrength.Stacks = -2;
		UCombatEngine* NegativeStrengthCombat = StartDamageRegressionCombat(
			DeckFor(FlatDamageCard.Id), {FlatDamageCard}, {EnemyAId}, 20260928);
		if (NegativeStrengthCombat) NegativeStrengthCombat->Player.Statuses.Add(NegativeStrength);
		const bool bNegativeStrengthPlayed = FindAndPlay(NegativeStrengthCombat, FlatDamageCard.Id);
		Check(bNegativeStrengthPlayed && NegativeStrengthCombat->EnemyDamageEvents.Num() == 1
			&& NegativeStrengthCombat->EnemyDamageEvents[0].Damage == 8,
			TEXT("negative Strength remains an additive -2 edge state (10 + -2 = 8)"));

		UCombatEngine* IndependentModifierCombat = StartDamageRegressionCombat(
			DeckFor(FlatDamageCard.Id), {FlatDamageCard}, {EnemyAId}, 20260929);
		if (IndependentModifierCombat)
		{
			IndependentModifierCombat->Player.Statuses.Add(NegativeStrength);
			FStatusInstance WeakStatus;
			WeakStatus.Id = TEXT("weak");
			WeakStatus.Stacks = 1;
			IndependentModifierCombat->Player.Statuses.Add(WeakStatus);
			FStatusInstance VulnerableStatus;
			VulnerableStatus.Id = TEXT("vulnerable");
			VulnerableStatus.Stacks = 1;
			if (IndependentModifierCombat->Enemies.Num() == 1)
				IndependentModifierCombat->Enemies[0].State.Statuses.Add(VulnerableStatus);
		}
		const bool bIndependentModifierPlayed = FindAndPlay(IndependentModifierCombat, FlatDamageCard.Id);
		Check(bIndependentModifierPlayed && IndependentModifierCombat->EnemyDamageEvents.Num() == 1
			&& IndependentModifierCombat->EnemyDamageEvents[0].Damage == 9,
			TEXT("negative Strength plus weak/vulnerable keeps independent modifier order: floor((10-2)*0.75*1.5) = 9"));
	}

	RunNarrativeGuidanceRegressionTests([&Check](bool bOk, const FString& Label)
	{
		Check(bOk, *Label);
	});
	RunOpeningRelicRandomRegressionTests([&Check](bool bOk, const FString& Label)
	{
		Check(bOk, *Label);
	});

	UE_LOG(LogTemp, Display, TEXT("========== RESULT: %d passed, %d failed =========="), Passed, Failed);
	return Failed == 0 ? 0 : 1;
}
