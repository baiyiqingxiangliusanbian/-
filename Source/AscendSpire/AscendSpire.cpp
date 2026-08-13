#include "AscendSpire.h"
#include "Modules/ModuleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CoreDelegates.h"
#include "Rendering/SlateRenderer.h"

DEFINE_LOG_CATEGORY_STATIC(LogAscend, Log, All);

/**
 * Project-level shutdown guard for the UE 5.8 macOS Slate/Metal viewport race.
 *
 * FSlateApplication shuts the game window down after UGameEngine::PreExit.  If
 * a Slate draw is still in flight when that happens, the last shared reference
 * to FSceneViewport can be released on the render thread.  FSceneViewport's
 * destructor requires the game thread, so the engine's check then terminates
 * the process.  Clear our viewport widgets and drain Slate/RHI work while the
 * game thread is still in control, before the engine starts destroying the
 * native window.
 */
class FAscendSpireModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		EnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddStatic(&FAscendSpireModule::HandleEnginePreExit);
	}

	virtual void ShutdownModule() override
	{
		if (EnginePreExitHandle.IsValid())
		{
			FCoreDelegates::OnEnginePreExit.Remove(EnginePreExitHandle);
			EnginePreExitHandle.Reset();
		}
	}

private:
	static void HandleEnginePreExit()
	{
		if (!IsInGameThread())
		{
			return;
		}

		// Remove UMG overlays first so no project-owned Slate widgets can enqueue
		// another viewport draw while the renderer is being drained.
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->RemoveAllViewportWidgets();
		}

		if (FSlateApplication::IsInitialized())
		{
			if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
			{
				Renderer->FlushCommands();
			}
		}

		UE_LOG(LogAscend, Display, TEXT("[ShutdownGuard] Slate viewport widgets removed and render commands flushed before engine exit"));
	}

	FDelegateHandle EnginePreExitHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FAscendSpireModule, AscendSpire, "AscendSpire");
