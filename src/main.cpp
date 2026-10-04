#include "Engine.h"

int main() {
	Engine engine;

	if(engine.Init("Vulkan Engine", 1920, 1080))
	{
		engine.Run();
	}
	engine.Shutdown();
}