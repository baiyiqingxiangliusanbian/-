#include "AscendSpireGameMode.h"
#include "AscendPlayerController.h"

AAscendSpireGameMode::AAscendSpireGameMode()
{
	PlayerControllerClass = AAscendPlayerController::StaticClass();
}
