#include "Headers.h"
#include "InputListener.h"
#include "InputCodes.h"

using namespace D3D11Framework;
class MyInput : public InputListener
{
public:
	bool recenter = false;
	float scaleAmount = 1.0f;
	OVR::Vector3f translate = OVR::Vector3f(-2.0f,-0.0f,-0.0f); 
	bool KeyPressed(const KeyEvent &arg)
	{
		printf("key press %c\n", arg.wc);
		switch (arg.code)
		{
		case eKeyCodes::KEY_Z:
			scaleAmount *= 1.2f;
			break;
		case eKeyCodes::KEY_X:
			scaleAmount *= 0.8f;
		case eKeyCodes::KEY_UP:
			translate.z -= 0.1f;
			break;
		case eKeyCodes::KEY_DOWN:
			translate.z += 0.1f;
			break;
		case eKeyCodes::KEY_LEFT:
			translate.x -= 0.1f;
			break;
		case eKeyCodes::KEY_RIGHT:
			translate.x += 0.1f;
			break;
		case eKeyCodes::KEY_CONTROL:
			translate.y-= 0.1f;
			break; 
		case eKeyCodes::KEY_SHIFT:
				translate.y += 0.1f;
				break;
		case eKeyCodes::KEY_SPACE:
			recenter = true;
			break;

		default:
			break;
		}
		return false;
	}
	bool MouseMove(const MouseEvent &arg)
	{
		return false;
	}
	float getScale() {
		return scaleAmount;
	}
};
